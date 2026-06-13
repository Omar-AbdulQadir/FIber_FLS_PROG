/**
  ******************************************************************************
  * @file    transport.h
  * @brief   Transport-agnostic byte I/O interface for the command server.
  *
  *          The physical peripheral (UART / USB CDC) is DEFERRED. The protocol
  *          layer talks only to this interface, so the concrete peripheral can
  *          be dropped in later (transport.c) without touching anything above.
  *
  *          Contract:
  *            - non-blocking; the server polls in the main loop
  *            - byte stream in/out; framing is the protocol layer's job
  ******************************************************************************
  */

#ifndef TRANSPORT_H
#define TRANSPORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

/* Initialize the transport peripheral. No-op until a peripheral is chosen. */
void     Transport_Init(void);

/* Service the transport (push RX into the internal buffer, drive TX, etc.).
 * Call once per main-loop iteration. */
void     Transport_Poll(void);

/* Read up to 'max' received bytes into 'buf'. Returns the count actually
 * copied (0 if nothing available). Non-blocking. */
uint16_t Transport_Read(uint8_t *buf, uint16_t max);

/* Queue 'len' bytes for transmission. Returns true if accepted. */
bool     Transport_Write(const uint8_t *buf, uint16_t len);

/* True if at least one received byte is waiting. */
bool     Transport_RxAvailable(void);

#ifdef __cplusplus
}
#endif

#endif /* TRANSPORT_H */
