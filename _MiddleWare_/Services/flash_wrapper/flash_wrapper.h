/**
  ******************************************************************************
  * @file    flash_wrapper.h
  * @brief   Thin application wrapper around the (untouchable) W25Qxx driver.
  *
  *          All flash "rules" live here, NOT in w25qxx.c:
  *            - 32-byte max data chunking
  *            - 256-byte page-boundary splitting on writes
  *            - address/range validation against chip capacity
  *            - placeholder CHIP_ID
  *            - dummy CRC-16 (stub, replaced later)
  *
  *          Everything routes through the existing W25Qxx public API; the
  *          driver itself is never modified.
  ******************************************************************************
  */

#ifndef FLASH_WRAPPER_H
#define FLASH_WRAPPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Max data bytes per protocol frame (data field only). */
#define FW_MAX_DATA_PER_FRAME   32U

/* W25Qxx page size (program granularity). Mirrors _W25QXX_PAGE_SIZE. */
#define FW_PAGE_SIZE            256U

/* Placeholder CHIP_ID until the real JEDEC/UniqID read is wired in.
 * Length kept small and fixed so the protocol layer can size responses. */
#define FW_CHIP_ID_LEN          4U

/* Erase scope selector (matches the ERASE command's scope byte). */
typedef enum
{
    FW_ERASE_SECTOR = 0x00,   /* 4 KB  */
    FW_ERASE_BLOCK  = 0x01,   /* 64 KB */
    FW_ERASE_CHIP   = 0x02    /* whole chip */
} fw_erase_scope_t;

/* Result codes returned by wrapper calls. The protocol layer maps these
 * onto STATUS codes in the response frame. */
typedef enum
{
    FW_OK = 0,
    FW_ERR_RANGE,     /* address/length outside chip bounds */
    FW_ERR_LEN,       /* data length > FW_MAX_DATA_PER_FRAME */
    FW_ERR_NOT_INIT,  /* flash not initialized */
    FW_ERR_SCOPE      /* unknown erase scope */
} fw_status_t;

/* Bring the wrapper online; call after W25qxx_Init() in main(). */
void        FW_Init(void);

/* Total addressable capacity of the active chip, in bytes. */
uint32_t    FW_CapacityBytes(void);

/* Fill 'out' (>= FW_CHIP_ID_LEN bytes) with the placeholder chip id. */
void        FW_ReadChipId(uint8_t *out);

/* Write up to FW_MAX_DATA_PER_FRAME bytes at 'addr'.
 * Splits internally at 256-byte page boundaries; 'addr' need not be aligned. */
fw_status_t FW_WriteData(uint32_t addr, const uint8_t *data, uint16_t len);

/* Read up to FW_MAX_DATA_PER_FRAME bytes from 'addr' into 'out'. */
fw_status_t FW_ReadData(uint32_t addr, uint8_t *out, uint16_t len);

/* Erase by scope. 'addr' is ignored for FW_ERASE_CHIP. */
fw_status_t FW_Erase(fw_erase_scope_t scope, uint32_t addr);

/* Compute the (dummy) CRC-16 over the entire chip. Reads in chunks through
 * the driver. Replaced with the real CRC later. */
uint16_t    FW_ComputeChipCrc16(void);

/* Dummy CRC-16 over an arbitrary buffer (also used for frame CRC).
 * Stub for now: returns a fixed placeholder. */
uint16_t    FW_Crc16(const uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif /* FLASH_WRAPPER_H */
