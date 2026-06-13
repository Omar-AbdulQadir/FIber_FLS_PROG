/**
 * @file W5500_if.c
 * @author Omar-AbdulQadir (omar.abdulqadir@pylonump.net)
 * @brief Module handles physical wraping APIs for W5500 SPI communication
 * @version 1.2.0
 * @date 2025-01-23
 * @copyright Copyright (c) 2025
 */

#include "W5500_if.h"


static W5500_port_t W5500_if_port_array[NUMBER_OF_PORTS];
static uint8_t      W5500_active_port_id = 0;

/* Convenience macro — resolves the currently selected port entry */
#define ACTIVE_PORT  (W5500_if_port_array[W5500_active_port_id])


int8_t W5500_init( uint8_t W5500_port_id, W5500_port_t* W5500_port_config )
{
	if (W5500_port_id >= NUMBER_OF_PORTS || W5500_port_config == NULL)
		return -1;

	memcpy(&W5500_if_port_array[W5500_port_id], W5500_port_config, sizeof(W5500_port_t));
	W5500_active_port_id = W5500_port_id;

	HAL_GPIO_WritePin(ACTIVE_PORT.w5500_nrst_port, ACTIVE_PORT.w5500_nrst_pin, GPIO_PIN_RESET);
	HAL_Delay(5);
	HAL_GPIO_WritePin(ACTIVE_PORT.w5500_nrst_port, ACTIVE_PORT.w5500_nrst_pin, GPIO_PIN_SET);

	return 0;
}

void W5500_select_port( uint8_t W5500_port_id )
{
	if (W5500_port_id < NUMBER_OF_PORTS)
		W5500_active_port_id = W5500_port_id;
}

void W5500_select( void )
{
	HAL_GPIO_WritePin(ACTIVE_PORT.w5500_spi_cs_port, ACTIVE_PORT.w5500_spi_cs_pin, GPIO_PIN_RESET);
}

void W5500_deselect( void )
{
	HAL_GPIO_WritePin(ACTIVE_PORT.w5500_spi_cs_port, ACTIVE_PORT.w5500_spi_cs_pin, GPIO_PIN_SET);
}

uint8_t W5500_read_byte( void )
{
	uint8_t rb = 0;
	HAL_SPI_Receive(ACTIVE_PORT.w5500_spi_handler, &rb, 1, 1000);
	return rb;
}

void W5500_write_byte( uint8_t wb )
{
	HAL_SPI_Transmit(ACTIVE_PORT.w5500_spi_handler, &wb, 1, 1000);
}

void W5500_read_brust( uint8_t* buf, uint16_t len )
{
	HAL_SPI_Receive(ACTIVE_PORT.w5500_spi_handler, buf, len, 1000);
}

void W5500_write_brust( uint8_t* buf, uint16_t len )
{
	HAL_SPI_Transmit(ACTIVE_PORT.w5500_spi_handler, buf, len, 1000);
}

void W5500_deinit( uint8_t W5500_port_id )
{
	if (W5500_port_id >= NUMBER_OF_PORTS)
		return;

	HAL_GPIO_WritePin(W5500_if_port_array[W5500_port_id].w5500_nrst_port,
	                  W5500_if_port_array[W5500_port_id].w5500_nrst_pin,
	                  GPIO_PIN_RESET);
	memset(&W5500_if_port_array[W5500_port_id], 0, sizeof(W5500_port_t));
}
