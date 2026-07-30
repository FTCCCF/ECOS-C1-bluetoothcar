# Project Knowledge — RC Car (StarrySky C1 + ST7735 LCD)

## Platform
- **SoC**: StarrySky C1 (RISC-V RV32IM)
- **Memory map**: FLASH @ 0x30000000, RAM @ 0x04000000, peripherals @ 0x03000000
- **Toolchain**: `riscv64-unknown-elf-` (picolibc, nostdlib)
- **SDK path**: `ECOS_SDK_HOME=/home/cf/.local/ecos-sdk`

## SDK Structure
- `ECOS_SDK_HOME/board/StarrySkyC1/` — C1 board files (board.h with register macros, start.s, sections.lds, Makefile)
- `ECOS_SDK_HOME/board/StarrySkyC2/` — C2 board (different memory map: peripherals @ 0x10000000, incompatible drivers)
- `ECOS_SDK_HOME/components/` — component-level drivers (qspi, gpio, timer, libc, libgcc, etc.)
- `ECOS_SDK_HOME/devices/` — device drivers (st7735, st7789, etc.)
- `ECOS_SDK_HOME/hal/` — HAL headers only (no .c implementations for C1)
- C1 has NO HAL implementations — must be provided locally under `driver/`

## Build Command
```bash
make
```
(ECOS_SDK_HOME fixed in Makefile at `/home/cf/embedded/embedded-sdk`; no env var needed)

## QSPI Protocol (ST7735)
The QSPI controller uses the **LEN+TXFIFO+STATUS** approach (NOT CLKDIV+ADR):

```
REG_QSPI_0_LEN     = 0x80000     // 1 byte for 8-bit transfers
REG_QSPI_0_TXFIFO  = data << 24  // data in upper byte
REG_QSPI_0_STATUS  = 258         // 0x102 = start transfer (bit1=START, bit8=?)
while ((REG_QSPI_0_STATUS & 0xFFFF) != 1);  // poll lower 16 bits for idle
```

Key details:
- Length encoding: LEN = num_bytes * 0x80000 (e.g., 32×32-bit = 128 bytes → 0x4000000)
- STATUS trigger value: **258 (0x102)**, NOT `cs|2`. CS is auto-managed, don't use explicit CS bits.
- Polling mask: **`& 0xFFFF`** (lower 16 bits only), not `& 0xFFFFFFFF`
- No `_cs` variant needed; use 258 for all transfers regardless of CS

## GPIO (DC pin control for ST7735)
- C1 GPIO at 0x03000000: DR (rw), DDR (rw), PUB, PDB
- DDR: 1 = input, 0 = output
- DR: read = pin level, write = set output level
- **Read-modify-write** required for both DR and DDR (lw → bit-op → sw)
- C1 has no PINMUX/FCFG registers — plain GPIO mode is default

## Timer
- TIM_0: CONFIG @ 0x0300005c, VALUE @ 0x03000060, DATA @ 0x03000064
- TIM_1: same pattern at +0xc offset
- Protocol: CONFIG=0x0100(stop) → DATA=count → CONFIG=0x0101(start) → poll DATA!=0

## Sys UART
- REG_UART_0_CLKDIV @ 0x03000010, REG_UART_0_DATA @ 0x03000014
- Baud = CPU_FREQ / CLKDIV

## Configuration
- `configs/.config` contains build options (CPU_FREQ_MHZ=72, UART_BAUD=115200, etc.)
- `configs/generated/autoconf.h` has `#define` equivalents
- Config is loaded by Makefile via `-include configs/.config` and C code via `#include "generated/autoconf.h"`

## Local Driver Files (`driver/`)
Place C1-specific HAL implementations here (since SDK has none):

| File | Contents |
|------|----------|
| `driver/gpio.c` | gpio_hal_set_level, gpio_hal_output_enable, etc. (simpler than C2's shadow-reg approach) |
| `driver/qspi.c` | hal_qspi_write_8_cs, hal_qspi_init, etc. (uses STATUS=258 protocol, NOT C2's CLKDIV+CMD+ADR) |

Note: sys_uart, timer, hp_uart use SDK's `board/StarrySkyC2/driver/` implementations (register macros resolve via C1's board.h).

## Struct for st7735_device_t (current SDK)
```c
st7735_device_t st7735 = {
    .dc_gpio_port = 0,
    .dc_gpio_pin = GPIO_NUM_2,
    .qspi_port = HAL_QSPI_PORT_0,
    .qspi_cs = HAL_QSPI_CS_0,
    .screen_width = 128,
    .screen_height = 128,
    .rotation = 0,
    .horizontal_offset = 2,
    .vertical_offset = 3,
};
```

Note: `.qspi_cs` is set but NOT used by the driver (all transfers use STATUS=258 regardless).

## Known SDK Bugs
1. `gpio.h` (components/gpio/include/gpio.h) line 43: missing semicolon after `gpio_get_level` prototype
2. `letter-shell` component fails to compile with picolibc (assert() macro mismatch) — excluded via Makefile `! -path "*/letter-shell/*"`
3. `sfud`, `fatfs`, `TimmoLog` excluded similarly if they cause issues

## SDK Component Filtering in Makefile
When compiling all SDK components, exclude problematic ones:
```makefile
SRC_PATH += $(shell find $(ECOS_SDK_HOME)/components -name "*.c" \
    ! -path "*/letter-shell/*" ! -path "*/sfud/*" \
    ! -path "*/fatfs/*" ! -path "*/TimmoLog/*")
SRC_PATH += $(shell find $(ECOS_SDK_HOME)/devices/st7735 -name "*.c")
```

## Memory
- Total FLASH: 16 MB (0x30000000, length 0x1000000)
- Total RAM: 8 MB (0x04000000, length 0x800000)
- Current firmware: ~48 KB FLASH + 32 KB RAM BSS (logo data 32 KB in FLASH as const uint32_t[8192])

## HP_UART (HC-05 Bluetooth)
- UART1 @ 0x03003000: LCR (config), DIV (baud), TRX (data), FCR (FIFO), LSR (status)
- **Actual HC-05 baud: 38400** (NOT default 9600 — this specific module is configured at 38400)
- Baud divisor: CPU_FREQ / baudrate (no /16 prescaler used in DIV), with `- 1` correction
- LCR = 0x1F for 8N1 (8 data bits, 1 stop, no parity)
- Send: wait LSR bit 8 == 0, then write TRX
- Recv: wait LSR bit 7 == 0, then read TRX
