/**
  ******************************************************************************
  * @file    transport.c
  * @brief   Transport backend: W5500 Ethernet (TCP server) over SPI.
  *
  *          The W5500 is driven through the WIZnet ioLibrary (socket API) and
  *          the project's low-level SPI shim (Drivers/App_Drivers/W5500_if).
  *          This file:
  *            - registers the shim's CS / SPI byte+burst callbacks with the lib
  *            - brings the chip up (wizchip_init) and applies a STATIC net config
  *            - opens one listening TCP socket and accepts a client connection
  *            - drains received bytes into the RX ring (Transport_Read consumer)
  *            - sends queued bytes with W5500_send (Transport_Write)
  *
  *          The protocol layer above is unchanged: it still only calls
  *          Transport_Read/Write/Poll/RxAvailable.
  *
  *          Network values (MAC/IP/subnet/gateway, listen port, socket/port id)
  *          come from sys_config.h. Safe fallback defaults are provided with
  *          #ifndef so this compiles before that header is finalized.
  ******************************************************************************
  */

#include "transport.h"
#include <string.h>

/* HAL/CMSIS (GPIOx, IRQn_Type, SPI_HandleTypeDef). */
#include "main.h"

/* WIZnet ioLibrary + low-level SPI shim. */
#include "wizchip_conf.h"
#include "socket.h"
#include "W5500_if.h"

/* ------------------------------------------------------------------------- */
/* Configuration                                                             */
/*                                                                           */
/* Static network config + W5500 routing lives here (no sys_config.h).       */
/* Adjust these to match the deployment.                                     */
/* ------------------------------------------------------------------------- */

#define TP_W5500_PORT_ID    0U          /* index into the W5500_if port array */
#define TP_SOCKET           0U          /* WIZnet socket number used as server */
#define TP_LISTEN_PORT      5000U       /* TCP port the jig listens on */

#define TP_MAC_ADDR         { 0x00, 0x08, 0xDC, 0x01, 0x02, 0x03 }
#define TP_IP_ADDR          { 192, 168, 1, 50 }
#define TP_SUBNET           { 255, 255, 255, 0 }
#define TP_GATEWAY          { 192, 168, 1, 1 }

/* --- DUMMY hardware routing -------------------------------------------------
 * The W5500's SPI bus and CS/NRST GPIOs are NOT configured in CubeMX yet.
 * These placeholders let W5500_init() be called now; replace with the real
 * SPI handle and pins once the peripheral is added to the .ioc.            */
static SPI_HandleTypeDef tp_dummy_spi;          /* TODO: use the real W5500 SPI handle */
#define TP_DUMMY_GPIO_PORT   GPIOA              /* TODO: real CS/NRST/INT port */
#define TP_DUMMY_CS_PIN      GPIO_PIN_3         /* TODO: real CS pin  */
#define TP_DUMMY_NRST_PIN    GPIO_PIN_2         /* TODO: real NRST pin */
#define TP_DUMMY_INT_PIN     GPIO_PIN_1         /* TODO: real INT pin  */

/* ------------------------------------------------------------------------- */
/* Internal RX ring buffer (kept; the protocol layer consumes from here)     */
/* ------------------------------------------------------------------------- */

#define TP_RX_BUF_SIZE   256U           /* power of two for the mask */

static uint8_t  tp_rx_buf[TP_RX_BUF_SIZE];
static uint16_t tp_rx_head;             /* producer (poll) */
static uint16_t tp_rx_tail;             /* consumer (read) */

static uint16_t tp_rx_count(void)
{
    return (uint16_t)((tp_rx_head - tp_rx_tail) & (TP_RX_BUF_SIZE - 1U));
}

static uint16_t tp_rx_free(void)
{
    return (uint16_t)((TP_RX_BUF_SIZE - 1U) - tp_rx_count());
}

static void tp_rx_push(uint8_t b)
{
    uint16_t next = (uint16_t)((tp_rx_head + 1U) & (TP_RX_BUF_SIZE - 1U));
    if (next != tp_rx_tail)
    {
        tp_rx_buf[tp_rx_head] = b;
        tp_rx_head = next;
    }
}

/* ------------------------------------------------------------------------- */
/* WIZnet callback glue (delegates to the W5500_if SPI shim)                 */
/* ------------------------------------------------------------------------- */

/* Critical-section enter/exit: no RTOS, single-threaded poll loop -> no-ops.
 * If the W5500 is ever accessed from an ISR, disable/restore IRQs here. */
static void tp_cris_enter(void) { }
static void tp_cris_exit(void)  { }

static void    tp_cs_select(void)                        { W5500_select();        }
static void    tp_cs_deselect(void)                      { W5500_deselect();      }
static uint8_t tp_spi_readbyte(void)                     { return W5500_read_byte(); }
static void    tp_spi_writebyte(uint8_t b)               { W5500_write_byte(b);   }
static void    tp_spi_readburst(uint8_t *p, uint16_t n)  { W5500_read_brust(p, n);  }
static void    tp_spi_writeburst(uint8_t *p, uint16_t n) { W5500_write_brust(p, n); }

/* ------------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------- */

static bool tp_link_up;   /* W5500 brought up successfully */

void Transport_Init(void)
{
    tp_rx_head = 0U;
    tp_rx_tail = 0U;
    tp_link_up = false;

    /* Configure the W5500 port (SPI handle + CS/NRST/INT GPIOs) and pulse
     * reset. DUMMY values for now — see the placeholders above. */
    W5500_port_t port;
    port.w5500_spi_handler = &tp_dummy_spi;
    port.w5500_spi_cs_port = TP_DUMMY_GPIO_PORT;
    port.w5500_spi_cs_pin  = TP_DUMMY_CS_PIN;
    port.w5500_nrst_port   = TP_DUMMY_GPIO_PORT;
    port.w5500_nrst_pin    = TP_DUMMY_NRST_PIN;
    port.w5500_int_port    = TP_DUMMY_GPIO_PORT;
    port.w5500_int_pin     = TP_DUMMY_INT_PIN;
    port.w5500_irq         = EXTI0_IRQn;            /* TODO: real INT IRQ line */
    (void)W5500_init(TP_W5500_PORT_ID, &port);
    W5500_select_port(TP_W5500_PORT_ID);

    /* Register the chip-access callbacks with the ioLibrary. */
    reg_wizchip_cris_cbfunc(tp_cris_enter, tp_cris_exit);
    reg_wizchip_cs_cbfunc(tp_cs_select, tp_cs_deselect);
    reg_wizchip_spi_cbfunc(tp_spi_readbyte, tp_spi_writebyte);
    reg_wizchip_spiburst_cbfunc(tp_spi_readburst, tp_spi_writeburst);

    /* Bring up the chip with default 2KB/2KB per-socket buffers (8 sockets). */
    uint8_t txsize[8] = { 2, 2, 2, 2, 2, 2, 2, 2 };
    uint8_t rxsize[8] = { 2, 2, 2, 2, 2, 2, 2, 2 };
    if (wizchip_init(txsize, rxsize) != 0)
    {
        return;   /* leave link down; Transport_Poll will be inert */
    }

    /* Apply the static network configuration. */
    wiz_NetInfo netinfo;
    uint8_t mac[6] = TP_MAC_ADDR;
    uint8_t ip[4]  = TP_IP_ADDR;
    uint8_t sn[4]  = TP_SUBNET;
    uint8_t gw[4]  = TP_GATEWAY;
    memcpy(netinfo.mac, mac, sizeof(mac));
    memcpy(netinfo.ip,  ip,  sizeof(ip));
    memcpy(netinfo.sn,  sn,  sizeof(sn));
    memcpy(netinfo.gw,  gw,  sizeof(gw));
    memset(netinfo.dns, 0, sizeof(netinfo.dns));
    netinfo.dhcp = NETINFO_STATIC;
    wizchip_setnetinfo(&netinfo);

    tp_link_up = true;
}

/* ------------------------------------------------------------------------- */
/* Socket state machine + RX drain                                           */
/* ------------------------------------------------------------------------- */

void Transport_Poll(void)
{
    if (!tp_link_up) return;

    uint8_t st = getSn_SR(TP_SOCKET);

    switch (st)
    {
        case SOCK_CLOSED:
            /* (Re)open the socket as a TCP endpoint. */
            (void)W5500_socket(TP_SOCKET, Sn_MR_TCP, TP_LISTEN_PORT, 0x00);
            break;

        case SOCK_INIT:
            /* Opened but not yet listening: start listening for a client. */
            (void)W5500_listen(TP_SOCKET);
            break;

        case SOCK_ESTABLISHED:
        {
            /* Connected: pull any received bytes into the RX ring. */
            uint16_t rsr = getSn_RX_RSR(TP_SOCKET);
            while (rsr > 0U && tp_rx_free() > 0U)
            {
                uint8_t  chunk[64];
                uint16_t want = (rsr < sizeof(chunk)) ? rsr : (uint16_t)sizeof(chunk);
                if (want > tp_rx_free()) want = tp_rx_free();

                int32_t got = W5500_recv(TP_SOCKET, chunk, want);
                if (got <= 0) break;

                for (int32_t i = 0; i < got; i++) tp_rx_push(chunk[i]);
                rsr = getSn_RX_RSR(TP_SOCKET);
            }
            break;
        }

        case SOCK_CLOSE_WAIT:
            /* Peer closed: flush, then disconnect/close so we re-listen. */
            (void)W5500_disconnect(TP_SOCKET);
            (void)W5500_close(TP_SOCKET);
            break;

        default:
            /* SOCK_LISTEN / transient states: nothing to do this tick. */
            break;
    }
}

/* ------------------------------------------------------------------------- */
/* Public read/write                                                         */
/* ------------------------------------------------------------------------- */

uint16_t Transport_Read(uint8_t *buf, uint16_t max)
{
    uint16_t n = 0U;
    while (n < max && tp_rx_tail != tp_rx_head)
    {
        buf[n++] = tp_rx_buf[tp_rx_tail];
        tp_rx_tail = (uint16_t)((tp_rx_tail + 1U) & (TP_RX_BUF_SIZE - 1U));
    }
    return n;
}

bool Transport_Write(const uint8_t *buf, uint16_t len)
{
    if (!tp_link_up)                         return false;
    if (getSn_SR(TP_SOCKET) != SOCK_ESTABLISHED) return false;

    /* W5500_send takes a non-const pointer but only reads the buffer. */
    int32_t sent = W5500_send(TP_SOCKET, (uint8_t *)buf, len);
    return (sent == (int32_t)len);
}

bool Transport_RxAvailable(void)
{
    return (tp_rx_count() > 0U);
}
