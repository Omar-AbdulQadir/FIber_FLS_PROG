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

#ifndef _W25QXX_H
#define _W25QXX_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stdbool.h"
#include "stm32f4xx_hal.h"

#define security_registerAddr		0x001000

#define last_half_page				32767
#define upper_2M_startpageAddr		16384
#define lower_2M_startpageAddr		0
#define FU_fristHalf				0
#define FU_secondHalf				1
#define exStartAddr_upper			0x200000
#define exStartAddr_lower			0

typedef enum
{
	W25Q10=1,
	W25Q20,
	W25Q40,
	W25Q80,
	W25Q16,
	W25Q32,
	W25Q64,
	W25Q128,
	W25Q256,
	W25Q512,
}W25QXX_ID_t;

typedef struct
{
	W25QXX_ID_t	ID;
	uint8_t		UniqID[8];
	uint16_t	PageSize;
	uint32_t	PageCount;
	uint32_t	SectorSize;
	uint32_t	SectorCount;
	uint32_t	BlockSize;
	uint32_t	BlockCount;
	uint32_t	CapacityInKiloByte;
	uint8_t		StatusRegister1;
	uint8_t		StatusRegister2;
	uint8_t		StatusRegister3;
	uint8_t		Lock;
	uint8_t		Initialized;
}w25qxx_t;

typedef struct  
{
	uint32_t start_page_address;
	//entered by user
	uint32_t Image_size;
	uint32_t Image_size_InPage; 
	uint8_t which_half;
	uint32_t page_counter;
	uint32_t start_address;
}FU_info_exflash_t;

typedef enum
{
	non,
	upper_2M,
	lower_2M,
	all
}erase_region;

typedef enum{
    rd_status_register_1 = 0,
    wr_status_register_1 = 1,
    rd_status_register_2 = 2,
    wr_status_register_2 = 3,
    rd_status_register_3 = 4,
    wr_status_register_3 = 5,
}StstusRegister_t;

typedef enum
{
	block_32KB,
	block_64KB
}block_size_t;

extern w25qxx_t	w25qxx;
//############################################################################
// in Page,Sector and block read/write functions, can put 0 to read maximum bytes 
//############################################################################


bool		W25qxx_Init(SPI_HandleTypeDef* SPI_Handle, GPIO_TypeDef* GPIO_Port, uint16_t GPIO_Pin);
void		W25qxx_ReadUniqID(void);

void		W25qxx_WriteEnable( void );
void		W25qxx_WriteDisable( void );

void		W25qxx_ReadStatusRegister( StstusRegister_t , uint8_t* );
void 		W25qxx_WriteStatusRegister(StstusRegister_t , uint8_t );

void 		W25qxx_EraseSector(uint32_t );
void		W25qxx_EraseBlock(uint32_t , block_size_t );
void 		W25qxx_EraseChip(void);

void 		W25qxx_ReadBytes(uint8_t* , uint32_t , uint16_t );
void		W25qxx_WriteBytes(uint8_t* , uint32_t , uint16_t );

void 		W25qxx_ReadPage(uint8_t* , uint32_t );
void		W25qxx_WritePage(uint8_t* , uint32_t );

void 		W25qxx_ReadSector(uint8_t* , uint32_t );
void 		W25qxx_WriteSector(uint8_t* , uint32_t );


//############################################################################
#ifdef __cplusplus
}
#endif

#endif

