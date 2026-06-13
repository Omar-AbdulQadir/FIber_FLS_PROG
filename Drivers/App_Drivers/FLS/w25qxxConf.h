/*
  Author:     Nima Askari
  WebSite:    http://www.github.com/NimaLTD
  Instagram:  http://instagram.com/github.NimaLTD
  Youtube:    https://www.youtube.com/channel/UCUhY7qY1klJm1d2kulr9ckw
  
  Version:    1.1.1
  
  
  Reversion History:
  
  (1.1.1)
  Fix some errors.
  
  (1.1.0)
  Fix some errors.
  
  (1.0.0)
  First release.
*/

#ifndef _W25QXXCONFIG_H
#define _W25QXXCONFIG_H

#define DEVICE_TYPE           W25Q16

#define _W25QXX_TIMEOUT			  0x400UL
#define _W25QXX_BUFFER_SIZE   0x800UL + 0x4UL
#define _W25QXX_PAGE_SIZE     0x100UL
#define _W25QXX_SECTOR_SIZE   0x1000UL

#define _W25QXX_SECTOR_PER_BLOCK  16

#define _W25QXX_USE_FREERTOS  0
#define _W25QXX_DEBUG         0

#define STATUS_INVALID        0xFF

#define W25QXX_DUMMY_BYTE     0xA5

#define BLOCK_FILTER          0x00FF0000
#define SECTOR_FILTER         0x00FFF000
#define PAGE_FILTER           0x00FFFF00

typedef enum
{
    write_status_register_1   = 0x01,
    page_program              = 0x02,
    read_data                 = 0x03,
    write_disable             = 0x04,
    read_status_register_1    = 0x05,
    write_enable              = 0x06,
    fast_read                 = 0x0B,
    write_status_register_3   = 0x11,
    read_status_register_3    = 0x15,
    sector_erase_4kb          = 0x20,
    write_status_register_2   = 0x31,
    read_status_register_2    = 0x35,
    individual_block_lock     = 0x36,
    individual_block_unlock   = 0x39,
    read_block_lock           = 0x3D,
    program_security_register = 0x42,
    erase_security_register   = 0x44,
    read_security_register    = 0x48,
    read_unique_id            = 0x4B,
    volatile_sr_write_enable  = 0x50,
    block_erase_32kb          = 0x52,
    read_SFDP_register        = 0x5A,
    chip_erase                = 0x60,
    enable_reset              = 0x66,
    erase_program_suspend     = 0x75,
    erase_program_resume      = 0x7A,
    global_block_lock         = 0x7E,
    manufacture_device_id     = 0x90,
    global_block_unlock       = 0x98,
    reset_device              = 0x99,
    jedec_id                  = 0x9F,
    release_power_down_id     = 0xAB,
    power_down                = 0xB9,
    chip_erase_2              = 0xC7,
    block_erase_64kb          = 0xD8
}_W25QXX_cmd_t;

#endif
