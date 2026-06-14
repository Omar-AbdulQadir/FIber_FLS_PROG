# FIber_FLS_PROG — SPI Flash Programmer / Test Jig

Firmware for an STM32F401RCT6 that acts as an external SPI-flash programmer. The
MCU drives a Winbond W25Qxx serial flash over SPI1 and runs a **command server**:
a client sends framed requests over a serial transport; the server parses,
dispatches, acts on the flash, and replies with an ACK frame.

## Architecture overview

```
Transport (W5500 Ethernet / TCP server over SPI1)
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

- **2026-06-14** — Moved frame logging into the client itself: `protocol.py`'s
  `build_request` logs every outgoing request (`TX REQ`) and `read_response`
  logs every incoming response (`RX RESP`), decoded field-by-field, through a
  dedicated `frames` logger. It is silent by default (a `NullHandler`); the
  application (or tester) attaches a handler to see frames, and the `isEnabledFor`
  guard keeps it zero-cost when off (important for the ~65k-frame full image).
  Any code using `JigClient` now traces the real wire. The tester's mock-server
  trace was relabelled `SRV RX` / `SRV TX` so each transaction reads as a
  coherent four-line round-trip (client TX → server RX → server TX → client RX).
- **2026-06-14** — Added `Fiber_FLS_PROG_CL/tester.py`, a no-hardware component
  tester. It stands up `MockJigServer` — a real in-process TCP server that
  speaks the exact wire protocol and backs writes with a simulated W25Qxx flash
  array — and drives the real `JigClient` against it. 10 tests cover the happy
  cycle (read_id → erase → stream → verify with byte-for-byte flash checks),
  edge sizes (exact MAX_DATA multiple, empty image), chip-id reporting, and
  fault injection (ERASE error, VERIFY mismatch, progress desync, mid-stream
  link drop, connection refused, plus a 4-device parallel fleet run). Every
  request/response frame is decoded field-by-field (SOF/CHIP/CMD/LEN/PAYLOAD/CRC)
  through a dedicated `frames` logger and written to a fresh per-run log file
  (`tester_<timestamp>.log`, or `--log PATH`); the terminal stays a clean
  PASS/FAIL summary by default. `--frames` logs one short single-cycle transcript
  to the screen; `-v` echoes frames + client logs to the screen too. Added a
  `.gitignore` for the generated logs and `__pycache__`. All 10 pass.
- **2026-06-14** — Doc fix: the architecture-overview diagram top line said the
  transport was "UART / USB CDC — deferred"; corrected to "W5500 Ethernet / TCP
  server over SPI1" to match the implemented backend.
- **2026-06-14** — Added the Python host client under `Fiber_FLS_PROG_CL/` (Phase 1:
  connection layer). `protocol.py` mirrors the wire format (`A5`/`5A` framing,
  MSB-first, `Cmd`/`Status`/`Erase` enums, `MAX_DATA=32`) with `build_request`,
  `read_response`, and an isolated `crc16` stub that returns `0x0000` to match the
  firmware's dummy CRC. `jig_client.py` adds `JigClient` — one instance per jig,
  fully parameterized at construction — that owns a TCP socket and drives the
  program cycle (`read_id` → chip `erase` → `write_stream` via
  `WRITE_STREAM_START`/`DATA` in 32-byte frames with per-ACK progress-desync
  checks → `verify`). It carries full progress state (phase, bytes_written,
  chip_id, error, timing) and a non-raising `run()` that records the outcome.
  Verified offline against an in-process mock server (exact frame bytes,
  byte-for-byte stream integrity, status-error / desync / connection-refused
  paths). Image `I210_HEX/intel_fls.hex` is raw 2 MiB binary (not Intel HEX).
  **Pending:** Phase 2 — `FLS_PROG_CL.py` fleet orchestration (one thread per
  device across IPs `.100`–`.107`).
- **2026-06-14** — Non-blocking socket + connection-stall recovery. The W5500
  TCP socket is now opened in non-blocking IO mode (`CS_SET_IOMODE` /
  `SOCK_IO_NONBLOCK`), so `recv`/`send`/`listen` return `SOCK_BUSY` instead of
  spinning inside the library. Added `Transport_Reset()` (disconnect + close +
  flush RX ring → next poll re-listens). The protocol layer now runs a
  **per-frame mid-transfer timeout** (`PROTO_FRAME_TIMEOUT_MS`, via
  `HAL_GetTick`): if a frame starts arriving but stalls before completing, the
  connection is reset and the partial frame discarded. Idle time between frames
  does not arm the timeout. **Timeout value is a placeholder (1000 ms, TODO).**
- **2026-06-14** — Comment cleanup to match the current design (no logic
  changes): `transport.h`/`transport.c` no longer reference UART/USB or dummy
  pins; clarified that the W5500 INT pin is configured but unused (polled, no
  IRQ, no RTOS critical sections); `FW_Init` comment now states the flash is on
  SPI3 with no W5500 contention.
- **2026-06-14** — Resolved the SPI bus conflict by splitting onto two
  peripherals: **SPI1 now drives the W5500** (prescaler /2 = 40 MHz on the
  80 MHz APB2 — fast enough for Ethernet), and a new **SPI3 drives the W25Qxx
  flash** (prescaler /16 = 2.5 MHz). `FW_Init` passes `&hspi3` to `W25qxx_Init`;
  the transport keeps `&hspi1`. System clock retuned to HCLK 80 MHz (APB1 40 /
  APB2 80). No shared bus, so no interleaving/arbitration concern remains.
- **2026-06-14** — Wired the W5500 to real hardware pins: `.ioc`/`main.h`
  regenerated with `ETH_CS` (PA4), `ETH_RST`/`ETH_INT` (PC4/PC5,
  `EXTI9_5_IRQn`), and `IP_SEL_0..2` strap pins (PC0/1/2); flash CS moved to
  PA15. `Transport_Init` now builds the `W5500_port_t` from these real macros
  and uses `hspi1`. The IP host byte is derived at runtime from the strap pins
  (`TP_DEVICE_ID` → `192.168.1.{100+id}`), giving each board a distinct address.
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
