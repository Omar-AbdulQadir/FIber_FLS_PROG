# FIber_FLS_PROG — SPI Flash Programmer / Test Jig

Firmware for an STM32F401RCT6 that acts as an external SPI-flash programmer. The
MCU drives a Winbond W25Qxx serial flash over SPI1 and runs a **command server**:
a client sends framed requests over a serial transport; the server parses,
dispatches, acts on the flash, and replies with an ACK frame.

## Architecture overview

```
Transport (UART / USB CDC — deferred)
   v
Protocol  (framing, CRC, RX state machine, response builder)
   v
Command   (table-driven dispatcher + handlers + stream session)
   v
Flash wrapper (page-boundary split, range checks, chunking, chip-id, CRC)
   v
W25Qxx driver (untouchable — used as-is via its public API)
```

Application layers live under `_MiddleWare_/Services/<layer>/`, one folder per
layer. See `CLAUDE.md` for the full description, the wire frame format, and the
command set.

## Build

STM32CubeIDE project. Build/flash/debug via the IDE, or from a shell with the
GNU Arm toolchain on PATH:

```sh
cd Debug
make all
```

## Status

- Command server layers implemented (flash_wrapper, transport, protocol, command).
- **Deferred:** real CRC-16, real CHIP_ID, the transport peripheral (UART/USB),
  multi-chip support.

## Conventions

- **Every update is recorded in the changelog below.**
- The W25Qxx driver (`Drivers/App_Drivers/FLS/`) is untouchable; wrap it instead.
- New layers go under `_MiddleWare_/Services/`, one folder per layer.

## Changelog

- **2026-06-14** — Transport: dropped the `sys_config.h` dependency (net config
  now lives in `transport.c` as `TP_*` macros) and moved the W5500 bring-up
  into `Transport_Init` — it now builds a `W5500_port_t` and calls
  `W5500_init()` before `wizchip_init`. SPI handle and CS/NRST/INT GPIOs are
  **dummy placeholders** (`tp_dummy_spi`, `GPIOA` pins) pending the real
  peripheral in CubeMX; each is marked `TODO`.
- **2026-06-14** — Communication media: implemented the `transport` layer as a
  **W5500 Ethernet (TCP server) over SPI** backend. Registers the
  `Drivers/App_Drivers/W5500_if` SPI shim as the WIZnet ioLibrary callbacks,
  runs `wizchip_init`, applies a static net config, opens a listening TCP
  socket, drains `W5500_recv` into the RX ring, and sends via `W5500_send`.
  Net config (`TP_*` macros: MAC/IP/subnet/gateway/listen-port/socket/port-id)
  is sourced from `sys_config.h` with safe fallback defaults. Added include
  paths for `_MiddleWare_/Thirdparty/W5500` and `Drivers/App_Drivers/W5500_if`
  to `.cproject` (Debug + Release). The protocol layer is unchanged.
  **Pending (external):** `sys_config.h` + `W5500_port_t` and the W5500 SPI/CS/
  NRST pins in CubeMX.
- **2026-06-14** — Added the command-server middleware under
  `_MiddleWare_/Services/`: `flash_wrapper` (wraps the untouchable W25Qxx driver;
  page-boundary splitting, range checks, 32-byte chunking, placeholder CHIP_ID,
  dummy CRC-16), `transport` (deferred-peripheral stub with RX ring buffer),
  `protocol` (frame format `SOF·CHIP·CMD·LEN·PAYLOAD·CRC16`, MSB-first, req SOF
  `0xA5` / resp SOF `0x5A`, RX state machine, response builder), and `command`
  (table-driven dispatcher + handlers for READ_ID / READ stream / ERASE /
  WRITE_DATA / WRITE_STREAM / VERIFY, plus stream-session state). Wired the
  server loop (`Protocol_Process()`) into `main.c` and added
  `_MiddleWare_/Services` to the include path in `.cproject` (Debug + Release).
  Added the rule that every update is recorded in this changelog.
