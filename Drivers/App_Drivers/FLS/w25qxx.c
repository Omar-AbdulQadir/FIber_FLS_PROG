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

#include "main.h"
#include "w25qxx.h"
#include "w25qxxConf.h"

#if (_W25QXX_DEBUG==1)
#include <stdio.h>
#endif

w25qxx_t w25qxx;
FU_info_exflash_t FU_info_exflash;

static SPI_HandleTypeDef* _W25QXX_SPI_PER= NULL;
static GPIO_TypeDef* _W25QXX_CS_GPIO= NULL;
static uint16_t _W25QXX_CS_PIN= 0;

#if (_W25QXX_USE_FREERTOS==1)
#define	W25qxx_Delay(delay)		osDelay(delay)
#include "cmsis_os.h"
#else
#define	W25qxx_Delay(delay)		HAL_Delay(delay)
#endif

/**
 * @brief SPI send and receive buffer
 * 
 * @param Data 
 * @param Length 
 */
static void W25qxx_Spi(uint8_t* Data, uint16_t Length)
{
	if(w25qxx.Initialized == 0) return;
	/* Declaration of Rx data buffer */
	uint8_t Rx_Data[_W25QXX_BUFFER_SIZE];
	/* Enable SPI Chip select */
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO, _W25QXX_CS_PIN, GPIO_PIN_RESET);
	/* Send and receive SPI data */
	HAL_SPI_TransmitReceive(_W25QXX_SPI_PER, Data, Rx_Data, Length, _W25QXX_TIMEOUT);
	/* Disable SPI Chip select */
	HAL_GPIO_WritePin(_W25QXX_CS_GPIO, _W25QXX_CS_PIN, GPIO_PIN_SET);

	/* Copy received data to user buffer */
	memcpy(Data, Rx_Data, Length);
}

/**
 * @brief Initialize the flash module
 * @param SPI_Handle SPI handle pointer
 * @param GPIO_Port GPIO port pointer for the flash chip select pin
 * @param GPIO_Pin GPIO pin number for the flash chip select pin
 *  
 * @return Initialization status (true - false) 
 */
bool W25qxx_Init(SPI_HandleTypeDef* SPI_Handle, GPIO_TypeDef* GPIO_Port, uint16_t GPIO_Pin)
{
	/* Assign SPI handle to global variable */
	_W25QXX_SPI_PER = SPI_Handle;
	/* Assign GPIO port and pin to global variable */
	_W25QXX_CS_GPIO = GPIO_Port;
	_W25QXX_CS_PIN = GPIO_Pin;

	w25qxx.Lock = 1;
	w25qxx.Initialized = 1;
	w25qxx.ID = DEVICE_TYPE;
	
	if(w25qxx.ID == W25Q32) w25qxx.BlockCount = 64;
	else if(w25qxx.ID == W25Q16) w25qxx.BlockCount = 32;
	else if(w25qxx.ID == W25Q80) w25qxx.BlockCount = 16;
	else
	{
		w25qxx.Initialized = 0;
		return false;
	}

	w25qxx.PageSize = _W25QXX_PAGE_SIZE;
	w25qxx.SectorSize = _W25QXX_SECTOR_SIZE;
	w25qxx.SectorCount = w25qxx.BlockCount * _W25QXX_SECTOR_PER_BLOCK;
	w25qxx.PageCount = (w25qxx.SectorCount * w25qxx.SectorSize) / w25qxx.PageSize;
	w25qxx.BlockSize = w25qxx.SectorSize * _W25QXX_SECTOR_PER_BLOCK;
	w25qxx.CapacityInKiloByte = (w25qxx.SectorCount * w25qxx.SectorSize)/1024;
	
	W25qxx_ReadUniqID();
	W25qxx_WriteStatusRegister(rd_status_register_1, 0x0);
	W25qxx_WriteStatusRegister(rd_status_register_2, 0x42);
	W25qxx_WriteStatusRegister(rd_status_register_3, 0x60);

	w25qxx.Lock = 0;	
	return true;
}

/**
 * @brief Read Chip unique ID
 * 
 */
void W25qxx_ReadUniqID( void )
{
	/* Initialize of data buffer */
	uint8_t TxRx_Data[13] = {read_unique_id};
	/* SPI get Unique ID */
	W25qxx_Spi(TxRx_Data, 13);
	/* Copy Unique ID to the chip object */
	memcpy(&w25qxx.UniqID[0], &TxRx_Data[5], 8);
}

/**
 * @brief Enable writing to the flash
 * 
 */
void W25qxx_WriteEnable( void )
{
	/* Initialize of data buffer */
	uint8_t TxRx_Data = write_enable;
	/* Initialize write enable flag */
	uint8_t write_enable_flag = false;
	do
	{
		/* SPI send write enable command */
		W25qxx_Spi(&TxRx_Data, 1);
		/* SPI read register data */
		W25qxx_ReadStatusRegister(rd_status_register_1, &write_enable_flag);
		/* Check the write enable flag */
	} while ( (write_enable_flag & 0x02) == 0x00 );
}

/**
 * @brief Disable writing to the flash
 * 
 */
void W25qxx_WriteDisable( void )
{ 
	/* Initialize of data buffer */
	uint8_t TxRx_Data = write_disable;
	/* Initialize write enable flag */
	uint8_t write_enable_flag = true;
	do
	{
		/* SPI send write disable command */
		W25qxx_Spi(&TxRx_Data, 1);
		/* SPI read register data */
		W25qxx_ReadStatusRegister(rd_status_register_1, &write_enable_flag);
		/* Check the write enable flag */
	} while ( (write_enable_flag & 0x02) == 0x02 );
}

/**
 * @brief Wait for the flash module to finish the current write cycle
 * 
 */
void W25qxx_WaitForWriteEnd(void)
{
	/* Initialize busy flag */
	uint8_t busy_flag = true;
	do
	{
		/* Delay for the cycle to finish */
		W25qxx_Delay(1);
		/* SPI read register data */
		W25qxx_ReadStatusRegister(rd_status_register_1, &busy_flag);
	} while ( (busy_flag & 0x01) == 0x01 );
}

/**
 * @brief Read Status register
 * 
 * @param SelectStatusRegister_1_2_3 
 * @return uint8_t 
 */
void W25qxx_ReadStatusRegister( StstusRegister_t SelectStatusRegister_1_2_3 , uint8_t* pStatus )
{
	/* Initialize of data buffer */
	uint8_t TxRx_Data[2];

	/* Check which register to read */
	switch( SelectStatusRegister_1_2_3 )
	{
		/* wrong Input */
		default: *pStatus = STATUS_INVALID; break;
		/* Case of register 1 */
		case rd_status_register_1:
		{
			/* Set read register 1 command */
			TxRx_Data[0] = read_status_register_1;
			/* Get the register 1 data */
			W25qxx_Spi( TxRx_Data, 2 );
			/* Assign the register to the data pointer */
			*pStatus = TxRx_Data[1];
			/* Save the status data */
			w25qxx.StatusRegister1 = *pStatus;
		}
		break;
		/* Case of register 2 */
		case rd_status_register_2:
		{
			/* Set read register 2 command */
			TxRx_Data[0] = read_status_register_2;
			/* Get the register 2 data */
			W25qxx_Spi( TxRx_Data, 2 );
			/* Assign the register to the data pointer */
			*pStatus = TxRx_Data[1];
			/* Save the status data */
			w25qxx.StatusRegister2 = *pStatus;
		}
		break;
		/* Case of register 3 */
		case rd_status_register_3:
		{
			/* Set read register 3 command */
			TxRx_Data[0] = read_status_register_3;
			/* Get the register 3 data */
			W25qxx_Spi( TxRx_Data, 2 );
			/* Assign the register to the data pointer */
			*pStatus = TxRx_Data[1];
			/* Save the status data */
			w25qxx.StatusRegister3 = *pStatus;
		}
		break;
	}
}

/**
 * @brief Write Status register
 * 
 * @param SelectStatusRegister_1_2_3 
 * @param RegisterData 
 */
void W25qxx_WriteStatusRegister( StstusRegister_t SelectStatusRegister_1_2_3, uint8_t RegisterData )
{
	/* Initialize of data buffer */
	uint8_t TxRx_Data[2];

	/* Check which register to write */
	switch(SelectStatusRegister_1_2_3)
	{
		/* wrong Input */
		default: break;
		/* Case of register 1 */
		case wr_status_register_1:
		{
			/* Set write register 1 command */
			TxRx_Data[0] = write_status_register_1;
			/* Assign the register data */
			TxRx_Data[1] = RegisterData;
			/* Set the register 1 data */
			W25qxx_Spi( TxRx_Data, 2 );
			/* Save the status data */
			w25qxx.StatusRegister1 = RegisterData;
		}
		break;
		case wr_status_register_2:
		{
			/* Set write register 2 command */
			TxRx_Data[0] = write_status_register_2;
			/* Assign the register data */
			TxRx_Data[1] = RegisterData;
			/* Set the register 2 data */
			W25qxx_Spi( TxRx_Data, 2 );
			/* Save the status data */
			w25qxx.StatusRegister2 = RegisterData;
		}
		break;
		case wr_status_register_3:
		{
			/* Set write register 3 command */
			TxRx_Data[0] = write_status_register_3;
			/* Assign the register data */
			TxRx_Data[1] = RegisterData;
			/* Set the register 3 data */
			W25qxx_Spi( TxRx_Data, 2 );
			/* Save the status data */
			w25qxx.StatusRegister3 = RegisterData;
		}
		break;
	}
	W25qxx_WaitForWriteEnd();
}

/**
 * @brief Erase sector
 * 
 * @param sector_address 
 */
void W25qxx_EraseSector(uint32_t sector_address)
{
	/* Wait for the last operation to finish */
	W25qxx_WaitForWriteEnd();
	/* Enable writing to flash */
	W25qxx_WriteEnable();

	/* Initialize of data buffer */
	uint8_t RxTx_Data[4] = {sector_erase_4kb};
	/* Compute block address */
	sector_address &= SECTOR_FILTER;
	/* Fill the address bytes */
	for(uint8_t i= 1; i <= 3; i++)
	{
		RxTx_Data[i] = (uint8_t)(sector_address >> ((3 - i) * 8));
	}
	/* SPI erase block in the flash */
	W25qxx_Spi(RxTx_Data, 4);

	/* Wait till erase process ends */
	W25qxx_WaitForWriteEnd();
	/* Disable flash writing */
	W25qxx_WriteDisable();
}

/**
 * @brief Erase Block
 * 
 * @param block_address 
 * @param Block_size 
 */
void W25qxx_EraseBlock(uint32_t block_address, block_size_t Block_size)
{
	/* Wait for the last operation to finish */
	W25qxx_WaitForWriteEnd();
	/* Enable writing to flash */
	W25qxx_WriteEnable();

	/* Initialize of data buffer */
	uint8_t RxTx_Data[4] = {block_erase_64kb};
	/* Compute block address */
	block_address &= BLOCK_FILTER;
	/* Fill the address bytes */
	for(uint8_t i= 1; i <= 3; i++)
	{
		RxTx_Data[i] = (uint8_t)(block_address >> ((3 - i) * 8));
	}
	/* SPI erase block in the flash */
	W25qxx_Spi(RxTx_Data, 4);

	/* Wait till erase process ends */
	W25qxx_WaitForWriteEnd();
	/* Disable flash writing */
	W25qxx_WriteDisable();
}


/**
 * @brief Erase Entire chip
 * 
 */
void W25qxx_EraseChip(void)
{
	/* Wait for the last operation to finish */
	W25qxx_WaitForWriteEnd();
	/* Enable writing to flash */
	W25qxx_WriteEnable();

	/* Initialize of data buffer */
	uint8_t RxTx_Data = chip_erase_2;
	/* SPI erase block in the flash */
	W25qxx_Spi(&RxTx_Data, 1);

	/* Wait till erase process ends */
	W25qxx_WaitForWriteEnd();
	/* Disable flash writing */
	W25qxx_WriteDisable();
}


// bool W25qxx_IsEmptyPage(uint32_t Page_Address,uint32_t OffsetInByte,uint32_t NumByteToCheck_up_to_PageSize)
// {
// 	while(w25qxx.Lock==1)
// 	W25qxx_Delay(1);
// 	w25qxx.Lock=1;	
// 	if(((NumByteToCheck_up_to_PageSize+OffsetInByte)>w25qxx.PageSize)||(NumByteToCheck_up_to_PageSize==0))
// 		NumByteToCheck_up_to_PageSize=w25qxx.PageSize-OffsetInByte;	
// 	uint8_t	pBuffer[32];
// 	uint32_t	WorkAddress;
// 	uint32_t	i;
// 	i=OffsetInByte;
	
// 	while( i+sizeof(pBuffer) < w25qxx.PageSize)
// 	{
// 		WorkAddress=(i+Page_Address*w25qxx.PageSize);
// 		//W25qxx_Spi(0x0B ,send);
// 		if(w25qxx.ID>=W25Q256)
// 			//W25qxx_Spi((WorkAddress & 0xFF000000) >> 24 ,send);
// 		//W25qxx_Spi((WorkAddress & 0xFF0000) >> 16 ,send);
// 		//W25qxx_Spi((WorkAddress & 0xFF00) >> 8 ,send);
// 		//W25qxx_Spi(WorkAddress & 0xFF ,send);
// 		//W25qxx_Spi(0 ,send);
// //		 exflash_spi_PDC_receive();

// 		for(uint8_t x=0;x<sizeof(pBuffer);x++)
// 		{
// 			if(pBuffer[x]!=0xFF)
// 				goto NOT_EMPTY;		
// 		}	
// 		 i+=sizeof(pBuffer);		
// 	}	
// 	if((w25qxx.PageSize+OffsetInByte)%sizeof(pBuffer)!=0)
// 	{
// 		for( ; i<w25qxx.PageSize; i++)
// 		{
// 			WorkAddress=(i+Page_Address*w25qxx.PageSize);
// 			//W25qxx_Spi(0x0B ,send);
// 			if(w25qxx.ID>=W25Q256)
// 				//W25qxx_Spi((WorkAddress & 0xFF000000) >> 24 ,send);
// 			//W25qxx_Spi((WorkAddress & 0xFF0000) >> 16 ,send);
// 			//W25qxx_Spi((WorkAddress & 0xFF00) >> 8 ,send);
// 			//W25qxx_Spi(WorkAddress & 0xFF ,send);
// 			//W25qxx_Spi(0 ,send);
// //			 exflash_spi_PDC_receive();

// 			if(pBuffer[0]!=0xFF)
// 				goto NOT_EMPTY;
// 		}
// 	}	
// 	w25qxx.Lock=0;
// 	return true;	
// 	NOT_EMPTY:	
// 	w25qxx.Lock=0;
// 	return false;
// }


//###################################################################################################################


// bool 	W25qxx_IsEmptySector(uint32_t Sector_Address,uint32_t OffsetInByte,uint32_t NumByteToCheck_up_to_SectorSize)
// {
// 	while(w25qxx.Lock==1)
// 	W25qxx_Delay(1);
// 	w25qxx.Lock=1;	
// 	if((NumByteToCheck_up_to_SectorSize>w25qxx.SectorSize)||(NumByteToCheck_up_to_SectorSize==0))
// 		NumByteToCheck_up_to_SectorSize=w25qxx.SectorSize;
// 	#if (_W25QXX_DEBUG==1)
// 	printf("w25qxx CheckSector:%d, Offset:%d, Bytes:%d begin...\r\n",Sector_Address,OffsetInByte,NumByteToCheck_up_to_SectorSize);
// 	W25qxx_Delay(100);
// 	uint32_t	StartTime=HAL_GetTick();
// 	#endif		
// 	uint8_t	pBuffer[32];
// 	uint32_t	WorkAddress;
// 	uint32_t	i;
	
// 	i=OffsetInByte;
// 	while( i+sizeof(pBuffer) < w25qxx.SectorSize)
// 	{
// 		WorkAddress=(i+Sector_Address*w25qxx.SectorSize);
// 		//W25qxx_Spi(0x0B ,send);
// 		if(w25qxx.ID>=W25Q256)
// 			//W25qxx_Spi((WorkAddress & 0xFF000000) >> 24 ,send);
// 		//W25qxx_Spi((WorkAddress & 0xFF0000) >> 16 ,send);
// 		//W25qxx_Spi((WorkAddress & 0xFF00) >> 8 ,send);
// 		//W25qxx_Spi(WorkAddress & 0xFF ,send);
// 		//W25qxx_Spi(0 ,send);
// //		 exflash_spi_PDC_receive();

// 		for(uint8_t x=0;x<sizeof(pBuffer);x++)
// 		{
// 			if(pBuffer[x]!=0xFF)
// 				goto NOT_EMPTY;		
// 		}
// 		i+=sizeof(pBuffer);				
// 	}	
// 	if((w25qxx.SectorSize+OffsetInByte)%sizeof(pBuffer)!=0)
// 	{
// 		for( ; i<w25qxx.SectorSize; i++)
// 		{
// 			WorkAddress=(i+Sector_Address*w25qxx.SectorSize);
// 			//W25qxx_Spi(0x0B ,send);
// 			if(w25qxx.ID>=W25Q256)
// 				//W25qxx_Spi((WorkAddress & 0xFF000000) >> 24 ,send);
// 			//W25qxx_Spi((WorkAddress & 0xFF0000) >> 16 ,send);
// 			//W25qxx_Spi((WorkAddress & 0xFF00) >> 8 ,send);
// 			//W25qxx_Spi(WorkAddress & 0xFF ,send);
// 			//W25qxx_Spi(0 ,send);
// //			 exflash_spi_PDC_receive();

// 			if(pBuffer[0]!=0xFF)
// 				goto NOT_EMPTY;
// 		}
// 	}	
// 	#if (_W25QXX_DEBUG==1)
// 	printf("w25qxx CheckSector is Empty in %d ms\r\n",HAL_GetTick()-StartTime);
// 	W25qxx_Delay(100);
// 	#endif	
// 	w25qxx.Lock=0;
// 	return true;	
// 	NOT_EMPTY:
// 	#if (_W25QXX_DEBUG==1)
// 	printf("w25qxx CheckSector is Not Empty in %d ms\r\n",HAL_GetTick()-StartTime);
// 	W25qxx_Delay(100);
// 	#endif	
// 	w25qxx.Lock=0;
// 	return false;
// }


//###################################################################################################################


// bool 	W25qxx_IsEmptyBlock(uint32_t Block_Address,uint32_t OffsetInByte,uint32_t NumByteToCheck_up_to_BlockSize)
// {
// 	while(w25qxx.Lock==1)
// 	W25qxx_Delay(1);
// 	w25qxx.Lock=1;	
// 	if((NumByteToCheck_up_to_BlockSize>w25qxx.BlockSize)||(NumByteToCheck_up_to_BlockSize==0))
// 		NumByteToCheck_up_to_BlockSize=w25qxx.BlockSize;
// 	#if (_W25QXX_DEBUG==1)
// 	printf("w25qxx CheckBlock:%d, Offset:%d, Bytes:%d begin...\r\n",Block_Address,OffsetInByte,NumByteToCheck_up_to_BlockSize);
// 	W25qxx_Delay(100);
// 	uint32_t	StartTime=HAL_GetTick();
// 	#endif		
// 	uint8_t	pBuffer[32];
// 	uint32_t	WorkAddress;
// 	uint32_t	i;
// 	i=OffsetInByte;
// 	while( i+sizeof(pBuffer) < w25qxx.BlockSize)
// 	{
// 		WorkAddress=(i+Block_Address*w25qxx.BlockSize);
// 		//W25qxx_Spi(0x0B ,send);
// 		if(w25qxx.ID>=W25Q256)
// 			//W25qxx_Spi((WorkAddress & 0xFF000000) >> 24 ,send);
// 		//W25qxx_Spi((WorkAddress & 0xFF0000) >> 16 ,send);
// 		//W25qxx_Spi((WorkAddress & 0xFF00) >> 8 ,send);
// 		//W25qxx_Spi(WorkAddress & 0xFF ,send);
// 		//W25qxx_Spi(0 ,send);
// //		 exflash_spi_PDC_receive();

// 		for(uint8_t x=0;x<sizeof(pBuffer);x++)
// 		{
// 			if(pBuffer[x]!=0xFF)
// 				goto NOT_EMPTY;		
// 		}		
// 		i+=sizeof(pBuffer);		
// 	}	
// 	if((w25qxx.BlockSize+OffsetInByte)%sizeof(pBuffer)!=0)
// 	{
// 		for( ; i<w25qxx.BlockSize; i++)
// 		{
// 			WorkAddress=(i+Block_Address*w25qxx.BlockSize);
// 			//W25qxx_Spi(0x0B ,send);
// 			if(w25qxx.ID>=W25Q256)
// 				//W25qxx_Spi((WorkAddress & 0xFF000000) >> 24 ,send);
// 			//W25qxx_Spi((WorkAddress & 0xFF0000) >> 16 ,send);
// 			//W25qxx_Spi((WorkAddress & 0xFF00) >> 8 ,send);
// 			//W25qxx_Spi(WorkAddress & 0xFF ,send);
// 			//W25qxx_Spi(0 ,send);
// //			 exflash_spi_PDC_receive();

// 			if(pBuffer[0]!=0xFF)
// 				goto NOT_EMPTY;
// 		}
// 	}	
// 	#if (_W25QXX_DEBUG==1)
// 	printf("w25qxx CheckBlock is Empty in %d ms\r\n",HAL_GetTick()-StartTime);
// 	W25qxx_Delay(100);
// 	#endif	
// 	w25qxx.Lock=0;
// 	return true;	
// 	NOT_EMPTY:
// 	#if (_W25QXX_DEBUG==1)
// 	printf("w25qxx CheckBlock is Not Empty in %d ms\r\n",HAL_GetTick()-StartTime);
// 	W25qxx_Delay(100);
// 	#endif	
// 	w25qxx.Lock=0;
// 	return false;
// }


//###################################################################################################################

/**
 * @brief Write data buffer to the flash
 * 
 * @param pBuffer 
 * @param WriteAddr_inBytes 
 * @param size 
 */
void W25qxx_WriteBytes(uint8_t* pBuffer, uint32_t WriteAddr_inBytes, uint16_t size)
{
	/* Wait for the last operation to finish */
	W25qxx_WaitForWriteEnd();
	/* Enable writing to flash */
	W25qxx_WriteEnable();
	
	/* Initialize of data buffer */
	uint8_t RxTx_Data[_W25QXX_PAGE_SIZE + 4] = {page_program};
	/* Assign address */
	for(uint8_t i= 1; i <= 3; i++)
	{
		RxTx_Data[i] = (uint8_t)(WriteAddr_inBytes >> ((3 - i) * 8));
	}
	/* Copy buffer data */
	memcpy(&RxTx_Data[4], pBuffer, size);
	/* Write data to flash */
	W25qxx_Spi(RxTx_Data, 4 + size);

	/* Wait till the write operation is finished */
	W25qxx_WaitForWriteEnd();
	/* Disable writing to flash */
	W25qxx_WriteDisable();
}

/**
 * @brief Write an entire page(256B) to the flash
 * 
 * @param page_address 
 */
void W25qxx_WritePage(uint8_t* pbuffer, uint32_t page_address)
{
	/* Get start address of the page */
	page_address &= PAGE_FILTER;
	/* Write all bytes of given page */
	W25qxx_WriteBytes(pbuffer, page_address, _W25QXX_PAGE_SIZE);
}

/**
 * @brief Write an entire sector(4KB) to the flash
 * 
 * @param pBuffer 
 * @param sector_address 
 */
void W25qxx_WriteSector(uint8_t* pBuffer, uint32_t sector_address)
{
	/* Get start address of the sector */
	sector_address &= SECTOR_FILTER;
	/* Write every page in a given sector */
	for(uint16_t page_index = 0; page_index < _W25QXX_SECTOR_SIZE; page_index += _W25QXX_PAGE_SIZE)
	{
		/* Write all bytes of given page */
		W25qxx_WritePage( ( pBuffer + page_index ), ( sector_address + page_index ) );
	}
}

/**
 * @brief Read number of bytes
 * 
 * @param pBuffer 
 * @param Bytes_Address 
 */
void W25qxx_ReadBytes(uint8_t* pBuffer, uint32_t Bytes_Address, uint16_t size)
{
	/* Wait for the last operation to finish */
	W25qxx_WaitForWriteEnd();

	/* Initialize of data buffer */
	uint8_t RxTx_Data[_W25QXX_BUFFER_SIZE] = {read_data};
	/* Fill the address bytes */
	for(uint8_t i= 1; i <= 3; i++)
	{
		RxTx_Data[i] = (uint8_t)(Bytes_Address >> ((3 - i) * 8));
	}
	/* Read the data from flash */
	W25qxx_Spi(RxTx_Data, 4 + size);

	/* Copy read data to the user buffer */
	memcpy(pBuffer, &RxTx_Data[4], size);
}

/**
 * @brief 
 * 
 * @param pBuffer 
 * @param Page_address 
 */
void W25qxx_ReadPage(uint8_t* pBuffer, uint32_t page_address)
{
	/* Get start address of the page */
	page_address &= PAGE_FILTER;
	/* Read all bytes of given page */
	W25qxx_ReadBytes(pBuffer, page_address, _W25QXX_PAGE_SIZE);
}

/**
 * @brief 
 * 
 * @param pBuffer 
 * @param Sector_address 
 */
void W25qxx_ReadSector(uint8_t* pBuffer, uint32_t sector_address)
{
	/* Get start address of the sector */
	sector_address &= SECTOR_FILTER;
	/* Read every page in a given sector */
	for(uint16_t page_index = 0; page_index < _W25QXX_SECTOR_SIZE; page_index += _W25QXX_PAGE_SIZE)
	{
		/* Read all bytes of given page */
		W25qxx_ReadPage( ( pBuffer + page_index ), ( sector_address + page_index ) );
	}
}


//###################################################################################################################


// void W25qxx_protect_region(erase_region region)
// {
// 	switch (region)
// 	{
// 	case all:
// 		W25qxx_WriteStatusRegister(wr_status_register_1, 0x0);
// 		W25qxx_WriteStatusRegister(wr_status_register_2, 0x42);
// 		W25qxx_WriteStatusRegister(wr_status_register_3, 0x60);
// 		break;
// 	case non:
// 		W25qxx_WriteStatusRegister(wr_status_register_1, 0x0);
// 		W25qxx_WriteStatusRegister(wr_status_register_2, 0x02);
// 		W25qxx_WriteStatusRegister(wr_status_register_3, 0x60);
// 		break;
// 	case upper_2M:
// 		W25qxx_WriteStatusRegister(wr_status_register_1, 0x18);
// 		W25qxx_WriteStatusRegister(wr_status_register_2, 0x02);
// 		W25qxx_WriteStatusRegister(wr_status_register_3, 0x60);
// 		break;
// 	case lower_2M:
// 		W25qxx_WriteStatusRegister(wr_status_register_1, 0x18);
// 		W25qxx_WriteStatusRegister(wr_status_register_2, 0x42);
// 		W25qxx_WriteStatusRegister(wr_status_register_3, 0x60);
// 		break;		
// 	}
// }


//###################################################################################################################


// void W25qxx_EraseSecurity_reg1(void)
// {
// 	while(w25qxx.Lock==1)
// 	W25qxx_Delay(1);
// 	w25qxx.Lock=1;

// 	W25qxx_WaitForWriteEnd();
// 	W25qxx_WriteEnable();
// 	//W25qxx_Spi(0x44 ,send);
// 	if(w25qxx.ID>=W25Q256)
// 	//W25qxx_Spi((security_registerAddr & 0xFF000000) >> 24 ,send);
// 	//W25qxx_Spi((security_registerAddr & 0xFF0000) >> 16 ,send);
// 	//W25qxx_Spi((security_registerAddr & 0xFF00) >> 8 ,send);
// 	//W25qxx_Spi(security_registerAddr & 0xFF ,send);

// 	W25qxx_WaitForWriteEnd();
	
// 	W25qxx_Delay(1);
// 	w25qxx.Lock=0;
// }


//###################################################################################################################


// void 	W25qxx_WriteSecurity_reg1(uint8_t buffer)
// {
// 	while(w25qxx.Lock==1)
// 	W25qxx_Delay(1);
// 	w25qxx.Lock=1;
// 	W25qxx_WaitForWriteEnd();
// 	W25qxx_WriteEnable();
// 	//W25qxx_Spi(0x42 ,send);
// 	if(w25qxx.ID>=W25Q256)
// 	//W25qxx_Spi((security_registerAddr & 0xFF000000) >> 24 ,send);
// 	//W25qxx_Spi((security_registerAddr & 0xFF0000) >> 16 ,send);
// 	//W25qxx_Spi((security_registerAddr & 0xFF00) >> 8 ,send);
// 	//W25qxx_Spi(security_registerAddr  &0xFF ,send);
// 	//W25qxx_Spi(buffer,send);

// 	W25qxx_WaitForWriteEnd();

// 	W25qxx_Delay(1);
// 	w25qxx.Lock=0;
// }


//###################################################################################################################


// void 	W25qxx_ReadSecurity_reg1(uint8_t *pBuffer)
// {
// 	while(w25qxx.Lock==1)
// 	W25qxx_Delay(1);
// 	w25qxx.Lock=1;
	
// 	//W25qxx_Spi(0x48 ,send);
// 	if(w25qxx.ID>=W25Q256)
// 	//W25qxx_Spi((security_registerAddr & 0xFF000000) >> 24 ,send);
// 	//W25qxx_Spi((security_registerAddr & 0xFF0000) >> 16 ,send);
// 	//W25qxx_Spi((security_registerAddr & 0xFF00) >> 8 ,send);
// 	//W25qxx_Spi(security_registerAddr & 0xFF ,send);
// 	//W25qxx_Spi(0 ,send);
// 	//*pBuffer = W25qxx_Spi(W25QXX_DUMMY_BYTE ,receive);

// 	W25qxx_Delay(1);
// 	w25qxx.Lock=0;
// }


//###################################################################################################################
