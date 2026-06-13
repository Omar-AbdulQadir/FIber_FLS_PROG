/**
 * @file W5500_if.h
 * @author Omar-AbdulQadir (omar.abdulqadir@pylonump.net)
 * @brief Module handles physical wraping APIs for W5500 SPI communication
 * @version 1.1.0
 * @date 2025-01-23
 * @copyright Copyright (c) 2025
 */
#ifndef W5500_IF_H_
#define W5500_IF_H_
/* -------------------------------------------- Includes ------------------------------------------------------- */
/* ------------------------------------------------------------------------------------------------------------- */
/* STD libraries include */
#include <stdint.h>
#include <string.h>

/* HAL includes */
#include "gpio.h"
#include "spi.h"

/* ARCH includes */

/* ---------------------------- Configurations and global definitions ------------------------------------------ */
/* ------------------------------------------------------------------------------------------------------------- */
#define DEFAULT_INTERNAL_BUFFER_SIZE        32 // Size in kb
#define NUMBER_OF_PORTS                      2

/* W5500 phy port configurations */
typedef struct W5500_port_t
{
    GPIO_TypeDef* w5500_nrst_port;
    uint16_t w5500_nrst_pin;
    GPIO_TypeDef* w5500_int_port;
    uint16_t w5500_int_pin;
    IRQn_Type w5500_irq;
    GPIO_TypeDef* w5500_spi_cs_port;
    uint16_t w5500_spi_cs_pin;
    SPI_HandleTypeDef* w5500_spi_handler;
}W5500_port_t;

/* ----------------------------------------- Shared resources -------------------------------------------------- */
/* ------------------------------------------------------------------------------------------------------------- */


/* ----------------------------------------- Shared APIs ------------------------------------------------------- */
/* ------------------------------------------------------------------------------------------------------------- */


int8_t  W5500_init( uint8_t W5500_port_id, W5500_port_t *W5500_port_config );
void    W5500_select_port( uint8_t W5500_port_id );
void    W5500_select( void );
void    W5500_deselect( void );
uint8_t W5500_read_byte( void );
void    W5500_write_byte( uint8_t wb );
void    W5500_read_brust( uint8_t *buf, uint16_t len );
void    W5500_write_brust( uint8_t *buf, uint16_t len );
void    W5500_deinit( uint8_t W5500_port_id );

#endif /* W5500_IF_H_ */
