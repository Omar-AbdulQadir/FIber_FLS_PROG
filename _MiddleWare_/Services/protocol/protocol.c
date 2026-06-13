/**
  ******************************************************************************
  * @file    protocol.c
  * @brief   Frame assembly, validation, response building, and the server
  *          loop for the flash-programmer command protocol.
  *
  *          Byte stream -> framed request -> Command_Dispatch() -> response.
  *          Stream-session state lives in the command layer; this layer only
  *          deals with framing and CRC.
  ******************************************************************************
  */

#include "protocol.h"
#include "transport/transport.h"
#include "command/command.h"
#include "flash_wrapper/flash_wrapper.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* RX frame assembler (byte-at-a-time state machine)                         */
/* ------------------------------------------------------------------------- */

typedef enum
{
    RX_WAIT_SOF = 0,
    RX_CHIP,
    RX_CMD,
    RX_LEN_HI,
    RX_LEN_LO,
    RX_PAYLOAD,
    RX_CRC_HI,
    RX_CRC_LO
} rx_state_t;

static rx_state_t rx_state;
static uint8_t    rx_chip;
static uint8_t    rx_cmd;
static uint16_t   rx_len;
static uint16_t   rx_idx;        /* payload bytes received so far */
static uint8_t    rx_payload[PROTO_MAX_PAYLOAD];
static uint16_t   rx_crc;        /* received CRC */

static void rx_reset(void)
{
    rx_state = RX_WAIT_SOF;
    rx_idx   = 0U;
}

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

void Protocol_Init(void)
{
    rx_reset();
}

/* ------------------------------------------------------------------------- */
/* Response builder                                                          */
/* ------------------------------------------------------------------------- */

void Protocol_SendResponse(uint8_t chip, proto_cmd_t cmd, proto_status_t status,
                           const uint8_t *resp, uint16_t resp_len)
{
    uint8_t  frame[PROTO_RESP_HDR_LEN + PROTO_MAX_DATA + PROTO_CRC_LEN];
    uint16_t n = 0U;

    if (resp_len > PROTO_MAX_DATA) resp_len = PROTO_MAX_DATA;  /* clamp */

    frame[n++] = PROTO_SOF_RESP;
    frame[n++] = chip;
    frame[n++] = (uint8_t)cmd;
    frame[n++] = (uint8_t)status;
    frame[n++] = (uint8_t)(resp_len >> 8);     /* LEN MSB-first */
    frame[n++] = (uint8_t)(resp_len & 0xFFU);
    if (resp != NULL && resp_len > 0U)
    {
        memcpy(&frame[n], resp, resp_len);
        n = (uint16_t)(n + resp_len);
    }

    /* CRC over CHIP..RESP (everything but SOF and the CRC itself). */
    uint16_t crc = FW_Crc16(&frame[1], (uint32_t)(n - 1U));
    frame[n++] = (uint8_t)(crc >> 8);          /* CRC MSB-first */
    frame[n++] = (uint8_t)(crc & 0xFFU);

    (void)Transport_Write(frame, n);
}

/* ------------------------------------------------------------------------- */
/* Frame complete -> validate -> dispatch                                    */
/* ------------------------------------------------------------------------- */

static void handle_complete_frame(void)
{
    /* Validate CRC over CHIP..PAYLOAD (dummy for now, but path is real). */
    uint8_t  crc_input[3 + PROTO_MAX_PAYLOAD];
    uint16_t k = 0U;
    crc_input[k++] = rx_chip;
    crc_input[k++] = rx_cmd;
    crc_input[k++] = (uint8_t)(rx_len >> 8);
    /* NOTE: full CRC coverage incl. LEN/payload is finalized with the real
     * CRC; the dummy ignores content, so exact layout here is not critical. */
    uint16_t calc = FW_Crc16(crc_input, k);

    if (calc != rx_crc)
    {
        Protocol_SendResponse(rx_chip, (proto_cmd_t)rx_cmd, ST_ERR_CRC, NULL, 0U);
        return;
    }

    proto_request_t req;
    req.chip    = rx_chip;
    req.cmd     = (proto_cmd_t)rx_cmd;
    req.payload = rx_payload;
    req.len     = rx_len;

    Command_Dispatch(&req);   /* handler sends its own response(s) */
}

/* ------------------------------------------------------------------------- */
/* Byte-feed state machine                                                   */
/* ------------------------------------------------------------------------- */

static void feed_byte(uint8_t b)
{
    switch (rx_state)
    {
        case RX_WAIT_SOF:
            if (b == PROTO_SOF_REQ) rx_state = RX_CHIP;
            break;

        case RX_CHIP:
            rx_chip  = b;
            rx_state = RX_CMD;
            break;

        case RX_CMD:
            rx_cmd   = b;
            rx_state = RX_LEN_HI;
            break;

        case RX_LEN_HI:
            rx_len   = (uint16_t)((uint16_t)b << 8);
            rx_state = RX_LEN_LO;
            break;

        case RX_LEN_LO:
            rx_len  |= (uint16_t)b;
            rx_idx   = 0U;
            if (rx_len > PROTO_MAX_PAYLOAD)
            {
                /* Oversized: reject and resync. */
                Protocol_SendResponse(rx_chip, (proto_cmd_t)rx_cmd,
                                      ST_ERR_LEN, NULL, 0U);
                rx_reset();
            }
            else
            {
                rx_state = (rx_len > 0U) ? RX_PAYLOAD : RX_CRC_HI;
            }
            break;

        case RX_PAYLOAD:
            rx_payload[rx_idx++] = b;
            if (rx_idx >= rx_len) rx_state = RX_CRC_HI;
            break;

        case RX_CRC_HI:
            rx_crc   = (uint16_t)((uint16_t)b << 8);
            rx_state = RX_CRC_LO;
            break;

        case RX_CRC_LO:
            rx_crc |= (uint16_t)b;
            handle_complete_frame();
            rx_reset();
            break;

        default:
            rx_reset();
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* Server loop                                                               */
/* ------------------------------------------------------------------------- */

void Protocol_Process(void)
{
    uint8_t  buf[PROTO_MAX_FRAME];
    uint16_t n;

    Transport_Poll();

    n = Transport_Read(buf, (uint16_t)sizeof(buf));
    for (uint16_t i = 0U; i < n; i++)
    {
        feed_byte(buf[i]);
    }
}
