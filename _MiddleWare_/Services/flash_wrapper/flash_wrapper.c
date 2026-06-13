/**
  ******************************************************************************
  * @file    flash_wrapper.c
  * @brief   Application wrapper around the (untouchable) W25Qxx driver.
  *
  *          Implements the flash "rules" the driver does not enforce:
  *          page-boundary splitting on writes, range validation, chunking,
  *          a placeholder CHIP_ID, and a dummy CRC-16.
  ******************************************************************************
  */

#include "flash_wrapper.h"
#include "FLS/w25qxx.h"
#include "spi.h"        /* for SPI handle */
#include "main.h"       /* for GPIO definitions */

/* The driver exposes its state through this global, filled by W25qxx_Init(). */
extern w25qxx_t w25qxx;

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

void FW_Init(void)
{
    /* The W25Qxx driver is initialized in main() via W25qxx_Init().
     * Nothing extra to do here yet; kept as a seam for future setup. */
    W25qxx_Init(&hspi3, FLS_CS_GPIO_Port, FLS_CS_Pin);
}

uint32_t FW_CapacityBytes(void)
{
    /* CapacityInKiloByte is set by the driver's init from chip geometry. */
    return (uint32_t)w25qxx.CapacityInKiloByte * 1024U;
}

/* ------------------------------------------------------------------------- */
/* CHIP_ID (placeholder)                                                     */
/* ------------------------------------------------------------------------- */

void FW_ReadChipId(uint8_t *out)
{
    /* Placeholder pattern until the real JEDEC/UniqID read is wired in.
     * Distinct, recognizable bytes so the client can confirm the path works. */
    static const uint8_t placeholder[FW_CHIP_ID_LEN] = { 0xDE, 0xAD, 0xBE, 0xEF };
    for (uint8_t i = 0U; i < FW_CHIP_ID_LEN; i++)
    {
        out[i] = placeholder[i];
    }
}

/* ------------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ------------------------------------------------------------------------- */

/* True if [addr, addr+len) fits within the chip. */
static bool fw_range_ok(uint32_t addr, uint32_t len)
{
    uint32_t cap = FW_CapacityBytes();
    if (cap == 0U)            return false;       /* not initialized */
    if (len == 0U)            return true;        /* nothing to do */
    if (addr >= cap)          return false;
    if (len > (cap - addr))   return false;       /* overflow-safe */
    return true;
}

/* ------------------------------------------------------------------------- */
/* Write (page-boundary aware)                                               */
/* ------------------------------------------------------------------------- */

fw_status_t FW_WriteData(uint32_t addr, const uint8_t *data, uint16_t len)
{
    if (w25qxx.Initialized == 0U)        return FW_ERR_NOT_INIT;
    if (len > FW_MAX_DATA_PER_FRAME)     return FW_ERR_LEN;
    if (!fw_range_ok(addr, len))         return FW_ERR_RANGE;
    if (len == 0U)                       return FW_OK;

    /* Split the write so no single page-program crosses a 256-byte page
     * boundary, which the driver's W25qxx_WriteBytes does not guard against.
     * 'addr' may be unaligned; the client is not responsible for this. */
    uint16_t written = 0U;
    while (written < len)
    {
        uint32_t cur_addr   = addr + written;
        uint32_t page_off   = cur_addr % FW_PAGE_SIZE;
        uint32_t page_space = FW_PAGE_SIZE - page_off;       /* room left in page */
        uint16_t remaining  = (uint16_t)(len - written);
        uint16_t chunk      = (remaining < page_space) ? remaining
                                                       : (uint16_t)page_space;

        /* Cast away const: the driver takes a non-const pointer but only reads. */
        W25qxx_WriteBytes((uint8_t *)(data + written), cur_addr, chunk);
        written = (uint16_t)(written + chunk);
    }

    return FW_OK;
}

/* ------------------------------------------------------------------------- */
/* Read                                                                      */
/* ------------------------------------------------------------------------- */

fw_status_t FW_ReadData(uint32_t addr, uint8_t *out, uint16_t len)
{
    if (w25qxx.Initialized == 0U)        return FW_ERR_NOT_INIT;
    if (len > FW_MAX_DATA_PER_FRAME)     return FW_ERR_LEN;
    if (!fw_range_ok(addr, len))         return FW_ERR_RANGE;
    if (len == 0U)                       return FW_OK;

    W25qxx_ReadBytes(out, addr, len);
    return FW_OK;
}

/* ------------------------------------------------------------------------- */
/* Erase                                                                     */
/* ------------------------------------------------------------------------- */

fw_status_t FW_Erase(fw_erase_scope_t scope, uint32_t addr)
{
    if (w25qxx.Initialized == 0U) return FW_ERR_NOT_INIT;

    switch (scope)
    {
        case FW_ERASE_SECTOR:
            if (!fw_range_ok(addr, 1U)) return FW_ERR_RANGE;
            W25qxx_EraseSector(addr);
            return FW_OK;

        case FW_ERASE_BLOCK:
            if (!fw_range_ok(addr, 1U)) return FW_ERR_RANGE;
            W25qxx_EraseBlock(addr, block_64KB);
            return FW_OK;

        case FW_ERASE_CHIP:
            W25qxx_EraseChip();
            return FW_OK;

        default:
            return FW_ERR_SCOPE;
    }
}

/* ------------------------------------------------------------------------- */
/* CRC (dummy)                                                               */
/* ------------------------------------------------------------------------- */

uint16_t FW_Crc16(const uint8_t *data, uint32_t len)
{
    /* DUMMY: real CRC-16 wired in later. For now return a fixed placeholder
     * so the protocol path can be exercised end-to-end. */
    (void)data;
    (void)len;
    return 0x0000U;
}

uint16_t FW_ComputeChipCrc16(void)
{
    /* DUMMY: walk the whole chip in 32-byte chunks through the driver so the
     * read path is exercised, but feed a stub CRC. Replaced later. */
    uint32_t cap = FW_CapacityBytes();
    uint8_t  buf[FW_MAX_DATA_PER_FRAME];
    uint32_t addr = 0U;

    while (addr < cap)
    {
        uint32_t remaining = cap - addr;
        uint16_t chunk = (remaining < FW_MAX_DATA_PER_FRAME)
                             ? (uint16_t)remaining
                             : (uint16_t)FW_MAX_DATA_PER_FRAME;
        W25qxx_ReadBytes(buf, addr, chunk);
        /* (dummy CRC accumulation would go here) */
        addr += chunk;
    }

    return FW_Crc16((const uint8_t *)0, 0U);
}
