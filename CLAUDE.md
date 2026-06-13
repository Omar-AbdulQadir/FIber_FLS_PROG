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

- **`Core/Src/main.c`** — entry point. Boot sequence: `HAL_Init()` → `SystemClock_Config()` (HSI → PLL, **SYSCLK 64 MHz but HCLK divided to 16 MHz** via AHB /4) → `MX_GPIO_Init()` → `MX_SPI1_Init()` → `W25qxx_Init(&hspi1, FLS_CS_GPIO_Port, FLS_CS_Pin)`, then an empty `while(1)`. The actual programming logic is not yet wired into the main loop — `main()` only initializes the flash.
- **`Drivers/App_Drivers/FLS/`** — the W25Qxx SPI flash driver (third-party, Nima Askari / NimaLTD, customized). This is the only application-specific driver and where flash read/write/erase work lives. Everything under `Drivers/STM32F4xx_HAL_Driver/` and `Drivers/CMSIS/` is vendor HAL — do not edit.
- **Hardware mapping** (from `.ioc` / `main.h`): SPI1 is full-duplex master, mode 0 (CPOL=0/CPHA=0), 8-bit, MSB-first, prescaler /2. Chip-select is **software-controlled GPIO on PA4** (`FLS_CS`, idle high). Pins PA5/PA6/PA7 = SCK/MISO/MOSI.

### W25Qxx driver notes (the part that actually matters)

- All SPI traffic goes through one helper, `W25qxx_Spi(Data, Length)` in `w25qxx.c`: it asserts CS low, does a single `HAL_SPI_TransmitReceive` into a local `Rx_Data` buffer, raises CS, then `memcpy`s the RX back over the caller's `Data` buffer. This is **full-duplex in place** — the command/address bytes you send and the bytes you read back share the same buffer, offset by the command+address length (4 bytes for read/program). That is why `W25qxx_ReadBytes` copies from `&RxTx_Data[4]`.
- **Device type is compile-time fixed** in `w25qxxConf.h` via `#define DEVICE_TYPE W25Q16`. `W25qxx_Init` only knows geometry for W25Q80/W25Q16/W25Q32 and returns `false` for anything else — it does **not** auto-detect from JEDEC ID. Changing the physical chip means changing this define.
- `W25qxx_Init` writes status registers to a **block-protected configuration** (SR1=0x00, SR2=0x42, SR3=0x60) on every boot. Note the apparent bug: init calls `W25qxx_WriteStatusRegister(rd_status_register_*, ...)` (the *read* enum values), and the write `switch` has no cases for those — so these calls are effectively no-ops. The real write-protect/region helpers (`W25qxx_protect_region`) are commented out.
- Many higher-level helpers (`IsEmptyPage/Sector/Block`, security-register read/write, `protect_region`) are **commented out / incomplete** — assume they don't work until ported. The `FU_info_exflash_t` struct and the `upper_2M`/`lower_2M`/`exStartAddr_*` constants in `w25qxx.h` hint at an intended firmware-update flow (split image into halves, program upper/lower 2M regions) that is **not yet implemented**.
- Address math: write/read-page/sector apply `PAGE_FILTER`/`SECTOR_FILTER` masks before issuing commands. `W25qxx_WriteBytes` does a single page-program of up to 256 B and does not handle page-boundary wraparound — callers must align.

## Conventions

- Generated HAL/CubeMX files carry the `/* USER CODE BEGIN/END */` guard convention; keep additions inside those blocks so CubeMX regeneration is non-destructive.
- The W25Qxx driver files are the exception — they are hand-maintained application drivers, edit freely.
- `_W25QXX_USE_FREERTOS` is 0, so `W25qxx_Delay` maps to blocking `HAL_Delay`. There is no RTOS.
