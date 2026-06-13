/**
  ******************************************************************************
  * @file    protocol.h
  * @brief   Wire protocol for the flash-programmer command server.
  *
  *  Framing (all multi-byte fields MSB-first / big-endian):
  *
  *    Request : SOF(0xA5) CHIP(1) CMD(1) LEN(2) PAYLOAD(LEN) CRC16(2)
  *    Response: SOF(0x5A) CHIP(1) CMD(1) STATUS(1) LEN(2) RESP(LEN) CRC16(2)
  *
  *  Lock-step: the client sends one request and waits for its response (ACK)
  *  before sending the next. Stream commands keep a small session state.
  *
  *  CRC-16 is a DUMMY for now (see flash_wrapper FW_Crc16); the field exists on
  *  the wire and is validated through the stub so the real CRC drops in later.
  ******************************************************************************
  */

#ifndef PROTOCOL_H
#define PROTOCOL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Start-of-frame markers. */
#define PROTO_SOF_REQ      0xA5U
#define PROTO_SOF_RESP     0x5AU   /* reversed: instantly tells the direction */

/* Fixed field sizes. */
#define PROTO_ADDR_LEN     3U      /* 24-bit address, MSB-first */
#define PROTO_TOTAL_LEN    3U      /* 24-bit total length, MSB-first */
#define PROTO_CRC_LEN      2U      /* CRC-16, MSB-first */
#define PROTO_MAX_DATA     32U     /* max DATA bytes in one frame */

/* Request header = SOF + CHIP + CMD + LEN(2).  Response adds STATUS. */
#define PROTO_REQ_HDR_LEN  5U
#define PROTO_RESP_HDR_LEN 6U

/* Largest payload we ever carry: ADDR(3) + DATA(32) for WRITE_DATA. */
#define PROTO_MAX_PAYLOAD  (PROTO_ADDR_LEN + PROTO_MAX_DATA)

/* Whole-frame buffer bound (request side). */
#define PROTO_MAX_FRAME    (PROTO_REQ_HDR_LEN + PROTO_MAX_PAYLOAD + PROTO_CRC_LEN)

/* Command opcodes. Expandable: add a row here and a handler in the table. */
typedef enum
{
    CMD_READ_ID            = 0x01,
    CMD_READ_START         = 0x02,   /* client: start_addr(3)+total_len(3) */
    CMD_READ_DATA          = 0x03,   /* server->client: data chunk; client ACKs */
    CMD_ERASE              = 0x04,   /* scope(1)+addr(3) */
    CMD_WRITE_DATA         = 0x05,   /* addr(3)+data(<=32) */
    CMD_WRITE_STREAM_START = 0x06,   /* start_addr(3)+total_len(3) */
    CMD_WRITE_STREAM_DATA  = 0x07,   /* data(<=32); ACK reports progress(3) */
    CMD_VERIFY             = 0x08    /* expected_crc16(2); whole-chip */
} proto_cmd_t;

/* Response STATUS codes. */
typedef enum
{
    ST_OK         = 0x00,
    ST_ERR_CRC    = 0x01,
    ST_ERR_CMD    = 0x02,
    ST_ERR_LEN    = 0x03,
    ST_ERR_ADDR   = 0x04,
    ST_ERR_SEQ    = 0x05,
    ST_ERR_BUSY   = 0x06,
    ST_ERR_FLASH  = 0x07,
    ST_ERR_VERIFY = 0x08
} proto_status_t;

/* Parsed request handed to a command handler. */
typedef struct
{
    uint8_t        chip;
    proto_cmd_t    cmd;
    const uint8_t *payload;   /* points into the RX assembly buffer */
    uint16_t       len;       /* payload length */
} proto_request_t;

/* Bring the protocol/server online (after Transport_Init + FW_Init). */
void Protocol_Init(void);

/* Drive the server: poll transport, assemble frames, dispatch, respond.
 * Call once per main-loop iteration. */
void Protocol_Process(void);

/* Build + send a response frame. Used by handlers and by the server for
 * streamed READ_DATA frames. 'resp'/'resp_len' may be NULL/0 for a bare ACK. */
void Protocol_SendResponse(uint8_t chip, proto_cmd_t cmd, proto_status_t status,
                           const uint8_t *resp, uint16_t resp_len);

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_H */
