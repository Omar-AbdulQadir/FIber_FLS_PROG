# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for an STM32F401RCT6 (Cortex-M4, LQFP64) that acts as an **external-SPI-flash programmer / test jig**. The MCU drives a Winbond W25Qxx serial flash over SPI1 and is intended to program a firmware image (`I210_HEX/intel_fls.bin`) into that flash. It is a STM32CubeMX/CubeIDE-generated project (`.ioc` is the source of truth for peripheral config).

## Build / flash

This is an STM32CubeIDE project — there is no command-line build script in the repo other than the IDE-generated makefile under `Debug/`. To build from a shell you need the **GNU Arm toolchain (`arm-none-eabi-gcc`, 13.3.rel1)** on PATH plus `make`:

```sh
# from the Debug/ directory (the makefile uses relative includes and hardcoded D:\ paths)
cd Debug
make all          # produces FIber_FLS_PROG.elf, .map, .list, and prints size
make clean
```

Normally you build/flash/debug via STM32CubeIDE (project files: `.project`, `.cproject`, launch config `FIber_FLS_PROG Debug.launch`). There are **no tests and no linter** — this is bare-metal firmware. Linker script: `STM32F401RCTX_FLASH.ld`.

When editing peripheral setup, prefer changing it in CubeMX (`FIber_FLS_PROG.ioc`) and regenerating, rather than hand-editing `Core/Src/{gpio,spi}.c` — those are generated files. The regenerator preserves only code inside `/* USER CODE BEGIN */ ... /* USER CODE END */` guards; anything outside those markers is overwritten.

## Architecture

- **`Core/Src/main.c`** — entry point. Boot sequence: `HAL_Init()` → `SystemClock_Config()` (HSI → PLL, **SYSCLK 64 MHz but HCLK divided to 16 MHz** via AHB /4) → `MX_GPIO_Init()` → `MX_SPI1_Init()` → `W25qxx_Init(...)` → `FW_Init()` → `Transport_Init()` → `Protocol_Init()`. The `while(1)` calls `Protocol_Process()` each iteration — that is the command server loop.
- **`Drivers/App_Drivers/FLS/`** — the W25Qxx SPI flash driver (third-party, Nima Askari / NimaLTD, customized). This is the low-level flash driver. **Treat it as untouchable** — the application layers wrap it via its existing public API and never modify it (see middleware below). Everything under `Drivers/STM32F4xx_HAL_Driver/` and `Drivers/CMSIS/` is vendor HAL — do not edit.
- **`_MiddleWare_/Services/`** — the application layers (the command server). One folder per layer, folder named after the layer. Registered on the include path as `_MiddleWare_/Services`, so includes use `<layer>/<file>.h` (e.g. `protocol/protocol.h`). See the next section.
- **Hardware mapping** (from `.ioc` / `main.h`): SPI1 is full-duplex master, mode 0 (CPOL=0/CPHA=0), 8-bit, MSB-first, prescaler /2. Chip-select is **software-controlled GPIO on PA4** (`FLS_CS`, idle high). Pins PA5/PA6/PA7 = SCK/MISO/MOSI. **No client-comms peripheral is configured yet** (the `.ioc` enables only NVIC/RCC/SPI1/SYS); the transport layer is a stub until UART or USB is chosen.

### Command server middleware (`_MiddleWare_/Services/`)

The jig is a **command server**: a client sends framed requests over a serial transport, the server parses, dispatches, acts on the flash, and replies with an ACK frame. Layers, bottom-up:

- **`flash_wrapper/`** — wraps the untouchable W25Qxx driver. Enforces the rules the driver does *not*: 256-byte page-boundary splitting on writes (so `addr` need not be aligned), 32-byte-per-frame cap, and address/range validation against chip capacity. Also holds the **placeholder CHIP_ID** (`DE AD BE EF`) and the **dummy CRC-16** (`FW_Crc16` / `FW_ComputeChipCrc16` are stubs — real CRC TBD).
- **`transport/`** — transport-agnostic byte I/O (`Transport_Read/Write/Poll`). Backend is **W5500 Ethernet (TCP server) over SPI**: it registers the W5500_if SPI shim as the WIZnet ioLibrary callbacks (`reg_wizchip_cs/spi/spiburst/cris_cbfunc`), runs `wizchip_init`, applies a **static** net config, opens one listening TCP socket, drains `W5500_recv` into a RX ring, and `Transport_Write` → `W5500_send`. Net values (MAC/IP/subnet/gw, listen port, socket/port id) come from `sys_config.h` (overridable `TP_*` macros with safe fallbacks). The protocol layer above is unchanged.
- **`protocol/`** — frame format + a byte-at-a-time RX state machine + response builder. **Request frame:** `SOF(0xA5) CHIP(1) CMD(1) LEN(2) PAYLOAD CRC16(2)`. **Response frame:** `SOF(0x5A) CHIP(1) CMD(1) STATUS(1) LEN(2) RESP CRC16(2)`. All multi-byte fields **MSB-first**. CRC is validated through the dummy stub for now.
- **`command/`** — table-driven dispatcher (`{cmd, handler}` table — add a row + a function to extend) plus the handlers and the **stream-session state** shared between START/DATA frames.

Commands: `READ_ID`, `READ_START`/`READ_DATA` (server streams, client ACKs each), `ERASE` (sector/block/chip), `WRITE_DATA` (`addr`+data, len = `LEN`−3), `WRITE_STREAM_START`/`WRITE_STREAM_DATA` (implicit advancing address; ACK carries running `bytes_done`), `VERIFY` (whole-chip; server reads all, computes CRC, compares, replies OK/NOK). Protocol is **lock-step**: client waits for each ACK before sending the next frame.

The W5500 stack lives in two places: the WIZnet **ioLibrary** (vendor, untouched) under `_MiddleWare_/Thirdparty/W5500/` (`socket.c/.h`, `wizchip_conf.c/.h`, `W5500/w5500.c/.h` — socket API is prefixed `W5500_`, e.g. `W5500_socket/listen/send/recv`), and the project's low-level **SPI shim** under `Drivers/App_Drivers/W5500_if/` (`W5500_select/deselect`, `W5500_read/write_byte`, `W5500_read/write_brust`). The shim depends on `sys_config.h` and a `W5500_port_t` (SPI handle + CS/NRST GPIOs) that are **provided externally** (board/app init), not in this repo yet — until they land, anything pulling in `W5500_if.h` won't compile. The W5500's own SPI bus + CS + NRST pins are **not in the `.ioc` yet** (to be wired in CubeMX).

Deferred (intentionally): real CRC-16, real CHIP_ID (JEDEC/UniqID), `sys_config.h`/`W5500_port_t` + W5500 SPI peripheral in CubeMX, multi-chip de-globalization, and the driver bugs below.

### W25Qxx driver notes (the part that actually matters)

- All SPI traffic goes through one helper, `W25qxx_Spi(Data, Length)` in `w25qxx.c`: it asserts CS low, does a single `HAL_SPI_TransmitReceive` into a local `Rx_Data` buffer, raises CS, then `memcpy`s the RX back over the caller's `Data` buffer. This is **full-duplex in place** — the command/address bytes you send and the bytes you read back share the same buffer, offset by the command+address length (4 bytes for read/program). That is why `W25qxx_ReadBytes` copies from `&RxTx_Data[4]`.
- **Device type is compile-time fixed** in `w25qxxConf.h` via `#define DEVICE_TYPE W25Q16`. `W25qxx_Init` only knows geometry for W25Q80/W25Q16/W25Q32 and returns `false` for anything else — it does **not** auto-detect from JEDEC ID. Changing the physical chip means changing this define.
- `W25qxx_Init` writes status registers to a **block-protected configuration** (SR1=0x00, SR2=0x42, SR3=0x60) on every boot. Note the apparent bug: init calls `W25qxx_WriteStatusRegister(rd_status_register_*, ...)` (the *read* enum values), and the write `switch` has no cases for those — so these calls are effectively no-ops. The real write-protect/region helpers (`W25qxx_protect_region`) are commented out.
- Many higher-level helpers (`IsEmptyPage/Sector/Block`, security-register read/write, `protect_region`) are **commented out / incomplete** — assume they don't work until ported. The `FU_info_exflash_t` struct and the `upper_2M`/`lower_2M`/`exStartAddr_*` constants in `w25qxx.h` hint at an intended firmware-update flow (split image into halves, program upper/lower 2M regions) that is **not yet implemented**.
- Address math: write/read-page/sector apply `PAGE_FILTER`/`SECTOR_FILTER` masks before issuing commands. `W25qxx_WriteBytes` does a single page-program of up to 256 B and does not handle page-boundary wraparound — callers must align.

## Conventions

- **Every update must be recorded in `README.md`.** After any change, add an entry to the changelog section of `README.md` describing what changed.
- Generated HAL/CubeMX files carry the `/* USER CODE BEGIN/END */` guard convention; keep additions inside those blocks so CubeMX regeneration is non-destructive.
- The **W25Qxx driver is untouchable** — do not edit `Drivers/App_Drivers/FLS/`. Application code wraps it through its existing public API in `_MiddleWare_/Services/flash_wrapper/`. (This supersedes earlier guidance that the driver was freely editable.)
- New application layers go under `_MiddleWare_/Services/`, **one folder per layer named after the layer**, with includes referenced as `<layer>/<file>.h`.
- `_W25QXX_USE_FREERTOS` is 0, so `W25qxx_Delay` maps to blocking `HAL_Delay`. There is no RTOS.
