/**
  ******************************************************************************
  * @file    command.c
  * @brief   Table-driven command dispatcher + handlers.
  *
  *          One handler per opcode; each sends its own response via
  *          Protocol_SendResponse(). Stream sessions (WRITE_STREAM and READ)
  *          are tracked in a single session struct, lock-step with the client.
  ******************************************************************************
  */

#include "command.h"
#include "flash_wrapper/flash_wrapper.h"
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Stream session state (shared between START and DATA frames)               */
/* ------------------------------------------------------------------------- */

typedef enum
{
    SESS_NONE = 0,
    SESS_WRITE,    /* WRITE_STREAM in progress (client streams to us) */
    SESS_READ      /* READ in progress (we stream to client) */
} sess_kind_t;

typedef struct
{
    sess_kind_t kind;
    uint32_t    start_addr;
    uint32_t    total_len;
    uint32_t    bytes_done;
} stream_session_t;

static stream_session_t g_sess;

static void session_clear(void)
{
    g_sess.kind       = SESS_NONE;
    g_sess.start_addr = 0U;
    g_sess.total_len  = 0U;
    g_sess.bytes_done = 0U;
}

/* ------------------------------------------------------------------------- */
/* MSB-first field helpers                                                   */
/* ------------------------------------------------------------------------- */

static uint32_t rd_u24(const uint8_t *p)
{
    return ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | (uint32_t)p[2];
}

static void wr_u24(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 16);
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v & 0xFFU);
}

/* Map a flash-wrapper status onto a protocol STATUS. */
static proto_status_t fw_to_status(fw_status_t s)
{
    switch (s)
    {
        case FW_OK:           return ST_OK;
        case FW_ERR_RANGE:    return ST_ERR_ADDR;
        case FW_ERR_LEN:      return ST_ERR_LEN;
        case FW_ERR_SCOPE:    return ST_ERR_LEN;
        case FW_ERR_NOT_INIT: return ST_ERR_FLASH;
        default:              return ST_ERR_FLASH;
    }
}

/* ------------------------------------------------------------------------- */
/* Handlers                                                                  */
/* ------------------------------------------------------------------------- */

static void h_read_id(const proto_request_t *req)
{
    uint8_t id[FW_CHIP_ID_LEN];
    FW_ReadChipId(id);
    Protocol_SendResponse(req->chip, CMD_READ_ID, ST_OK, id, FW_CHIP_ID_LEN);
}

/* READ_START: open a read session, then immediately stream the first chunk.
 * Each subsequent client ACK (a CMD_READ_DATA request) pulls the next chunk. */
static void h_read_start(const proto_request_t *req)
{
    if (req->len != (PROTO_ADDR_LEN + PROTO_TOTAL_LEN))
    {
        Protocol_SendResponse(req->chip, CMD_READ_START, ST_ERR_LEN, NULL, 0U);
        return;
    }

    uint32_t start = rd_u24(&req->payload[0]);
    uint32_t total = rd_u24(&req->payload[PROTO_ADDR_LEN]);

    if (start >= FW_CapacityBytes() || total > (FW_CapacityBytes() - start))
    {
        Protocol_SendResponse(req->chip, CMD_READ_START, ST_ERR_ADDR, NULL, 0U);
        return;
    }

    session_clear();
    g_sess.kind       = SESS_READ;
    g_sess.start_addr = start;
    g_sess.total_len  = total;
    g_sess.bytes_done = 0U;

    /* ACK the START itself; the client then polls with CMD_READ_DATA. */
    Protocol_SendResponse(req->chip, CMD_READ_START, ST_OK, NULL, 0U);
}

/* READ_DATA: client is ACKing/requesting the next chunk in a read session. */
static void h_read_data(const proto_request_t *req)
{
    if (g_sess.kind != SESS_READ)
    {
        Protocol_SendResponse(req->chip, CMD_READ_DATA, ST_ERR_SEQ, NULL, 0U);
        return;
    }

    uint32_t remaining = g_sess.total_len - g_sess.bytes_done;
    if (remaining == 0U)
    {
        /* Nothing left: close session, report OK with empty payload. */
        session_clear();
        Protocol_SendResponse(req->chip, CMD_READ_DATA, ST_OK, NULL, 0U);
        return;
    }

    uint16_t chunk = (remaining < PROTO_MAX_DATA) ? (uint16_t)remaining
                                                  : (uint16_t)PROTO_MAX_DATA;
    uint8_t  data[PROTO_MAX_DATA];

    fw_status_t s = FW_ReadData(g_sess.start_addr + g_sess.bytes_done, data, chunk);
    if (s != FW_OK)
    {
        session_clear();
        Protocol_SendResponse(req->chip, CMD_READ_DATA, fw_to_status(s), NULL, 0U);
        return;
    }

    g_sess.bytes_done += chunk;
    Protocol_SendResponse(req->chip, CMD_READ_DATA, ST_OK, data, chunk);

    if (g_sess.bytes_done >= g_sess.total_len) session_clear();
}

static void h_erase(const proto_request_t *req)
{
    if (req->len != (1U + PROTO_ADDR_LEN))
    {
        Protocol_SendResponse(req->chip, CMD_ERASE, ST_ERR_LEN, NULL, 0U);
        return;
    }

    fw_erase_scope_t scope = (fw_erase_scope_t)req->payload[0];
    uint32_t         addr  = rd_u24(&req->payload[1]);

    fw_status_t s = FW_Erase(scope, addr);
    Protocol_SendResponse(req->chip, CMD_ERASE, fw_to_status(s), NULL, 0U);
}

/* WRITE_DATA: addr(3) + data(<=32). data length = payload len - 3. */
static void h_write_data(const proto_request_t *req)
{
    if (req->len < PROTO_ADDR_LEN || req->len > (PROTO_ADDR_LEN + PROTO_MAX_DATA))
    {
        Protocol_SendResponse(req->chip, CMD_WRITE_DATA, ST_ERR_LEN, NULL, 0U);
        return;
    }

    uint32_t addr     = rd_u24(&req->payload[0]);
    uint16_t data_len = (uint16_t)(req->len - PROTO_ADDR_LEN);

    fw_status_t s = FW_WriteData(addr, &req->payload[PROTO_ADDR_LEN], data_len);
    Protocol_SendResponse(req->chip, CMD_WRITE_DATA, fw_to_status(s), NULL, 0U);
}

static void h_write_stream_start(const proto_request_t *req)
{
    if (req->len != (PROTO_ADDR_LEN + PROTO_TOTAL_LEN))
    {
        Protocol_SendResponse(req->chip, CMD_WRITE_STREAM_START, ST_ERR_LEN, NULL, 0U);
        return;
    }

    uint32_t start = rd_u24(&req->payload[0]);
    uint32_t total = rd_u24(&req->payload[PROTO_ADDR_LEN]);

    if (start >= FW_CapacityBytes() || total > (FW_CapacityBytes() - start))
    {
        Protocol_SendResponse(req->chip, CMD_WRITE_STREAM_START, ST_ERR_ADDR, NULL, 0U);
        return;
    }

    session_clear();
    g_sess.kind       = SESS_WRITE;
    g_sess.start_addr = start;
    g_sess.total_len  = total;
    g_sess.bytes_done = 0U;

    Protocol_SendResponse(req->chip, CMD_WRITE_STREAM_START, ST_OK, NULL, 0U);
}

/* WRITE_STREAM_DATA: data only; length from frame LEN; addr is implicit.
 * ACK carries running progress (bytes_done, 3 bytes MSB-first). */
static void h_write_stream_data(const proto_request_t *req)
{
    if (g_sess.kind != SESS_WRITE)
    {
        Protocol_SendResponse(req->chip, CMD_WRITE_STREAM_DATA, ST_ERR_SEQ, NULL, 0U);
        return;
    }
    if (req->len == 0U || req->len > PROTO_MAX_DATA)
    {
        Protocol_SendResponse(req->chip, CMD_WRITE_STREAM_DATA, ST_ERR_LEN, NULL, 0U);
        return;
    }
    if ((uint32_t)req->len > (g_sess.total_len - g_sess.bytes_done))
    {
        /* Client sent more than it promised in START. */
        Protocol_SendResponse(req->chip, CMD_WRITE_STREAM_DATA, ST_ERR_LEN, NULL, 0U);
        return;
    }

    uint32_t addr = g_sess.start_addr + g_sess.bytes_done;
    fw_status_t s = FW_WriteData(addr, req->payload, req->len);
    if (s != FW_OK)
    {
        session_clear();
        Protocol_SendResponse(req->chip, CMD_WRITE_STREAM_DATA, fw_to_status(s), NULL, 0U);
        return;
    }

    g_sess.bytes_done += req->len;

    uint8_t progress[PROTO_TOTAL_LEN];
    wr_u24(progress, g_sess.bytes_done);
    Protocol_SendResponse(req->chip, CMD_WRITE_STREAM_DATA, ST_OK,
                          progress, PROTO_TOTAL_LEN);

    if (g_sess.bytes_done >= g_sess.total_len) session_clear();
}

/* VERIFY: expected_crc16(2). Whole-chip; server reads, computes, compares. */
static void h_verify(const proto_request_t *req)
{
    if (req->len != PROTO_CRC_LEN)
    {
        Protocol_SendResponse(req->chip, CMD_VERIFY, ST_ERR_LEN, NULL, 0U);
        return;
    }

    uint16_t expected = (uint16_t)(((uint16_t)req->payload[0] << 8) | req->payload[1]);
    uint16_t computed = FW_ComputeChipCrc16();

    proto_status_t st = (computed == expected) ? ST_OK : ST_ERR_VERIFY;
    Protocol_SendResponse(req->chip, CMD_VERIFY, st, NULL, 0U);
}

/* ------------------------------------------------------------------------- */
/* Dispatch table                                                            */
/* ------------------------------------------------------------------------- */

typedef void (*cmd_handler_t)(const proto_request_t *);

typedef struct
{
    proto_cmd_t   cmd;
    cmd_handler_t handler;
} cmd_entry_t;

static const cmd_entry_t k_cmd_table[] =
{
    { CMD_READ_ID,            h_read_id            },
    { CMD_READ_START,         h_read_start         },
    { CMD_READ_DATA,          h_read_data          },
    { CMD_ERASE,              h_erase              },
    { CMD_WRITE_DATA,         h_write_data         },
    { CMD_WRITE_STREAM_START, h_write_stream_start },
    { CMD_WRITE_STREAM_DATA,  h_write_stream_data  },
    { CMD_VERIFY,             h_verify             },
};

#define CMD_TABLE_LEN (sizeof(k_cmd_table) / sizeof(k_cmd_table[0]))

void Command_Dispatch(const proto_request_t *req)
{
    for (uint8_t i = 0U; i < CMD_TABLE_LEN; i++)
    {
        if (k_cmd_table[i].cmd == req->cmd)
        {
            k_cmd_table[i].handler(req);
            return;
        }
    }
    /* Unknown opcode. */
    Protocol_SendResponse(req->chip, req->cmd, ST_ERR_CMD, NULL, 0U);
}
