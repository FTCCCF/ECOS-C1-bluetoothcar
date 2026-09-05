# Project Knowledge — RC Car (StarrySky C1 + TB6612 + HC-05 + ST7735)

## Platform
- **SoC**: StarrySky C1 (RISC-V RV32IM, 72 MHz, NO hardware float — float literals pull in soft-float libcalls and fail to link; use integer fractions only)
- **Memory map**: FLASH @ 0x30000000, RAM @ 0x04000000, peripherals @ 0x03000000
- **Toolchain**: `riscv64-unknown-elf-` (picolibc, nostdlib)
- **SDK**: fixed in Makefile at `/home/cf/embedded/embedded-sdk` (no env var needed)

## SDK Structure
- `SDK/board/StarrySkyC1/` — C1 board files (board.h, start.s, sections.lds)
- `SDK/board/StarrySkyC2/` — C2 board (peripherals @ 0x10000000, INCOMPATIBLE register map, but its driver/*.c implementations work on C1 because register macros resolve via C1's board.h)
- `SDK/components/` — qspi, gpio, timer, libc, libgcc... (exclude letter-shell/sfud/fatfs/TimmoLog in Makefile)
- `SDK/devices/st7735/` — LCD driver; `SDK/hal/` — headers only, C1 has NO HAL impl → provide locally in `driver/`

## Build
```bash
make    # → build/retrosoc_fw.bin (+ .hex/.map); ~80 KB FLASH, 32 KB BSS
```

## ⚠️ CRITICAL — Motor PWM (empirically confirmed, do NOT "fix")
1. **Channel mapping**: `PWM_CH2`(CR2) = physical LEFT wheel, `PWM_CH1`(CR1) = physical RIGHT wheel (verified by single-wheel test: W→left, A→right).
2. **BOTH channels are INVERTED (active-low)**: CR=0 → 100% duty (full speed), CR=20000 → 0% duty (stopped). Proven by: CH2=0→left full speed / CH2=20000→left stopped, plus contradiction analysis for CH1 (right spun at CH1=2708 but not at 7916 → real duty was 86.5% then 60.4%).
3. **Compensation is mandatory**:
```c
pwm_hal_set_compare(NULL, 0, PWM_CH2, MOTOR_PWM_MAX - l_pwm);  // left
pwm_hal_set_compare(NULL, 0, PWM_CH1, MOTOR_PWM_MAX - r_pwm);  // right
```
   Init: write `MOTOR_PWM_MAX` to BOTH channels (0% duty).
4. **Speed→PWM**: `pwm = abs(spd) * MOTOR_PWM_MAX / STM32_SPEED_MAX` (MOTOR_PWM_MAX=20000, STM32_SPEED_MAX=4800).
5. **PWM frequency**: pscr=0, cmp=20000 → **3.6 kHz**. Was pscr=71 → 50 Hz (too low: motor pulses/surges). Duty scale unchanged by pscr.

## Motor Characteristics (empirically calibrated)
- **Left motor is physically faster** (~3-4x at same duty). Direction-dependent too:
  - Forward straight: `LEFT_ADJUST_PCT = 845` (permille = 84.5%; 0.1% step; use permille/denominator-1000, never float literals)
  - Reverse straight: `REVERSE_ADJUST_PCT = 1000` (no reduction!)
  - Apply in `motor_apply()`: `adj = (left_speed < 0) ? REVERSE_ADJUST_PCT : LEFT_ADJUST_PCT; l_pwm = spd * adj / 1000`
- **Stall thresholds**: left motor ≈40% duty, right motor ≈60-86% duty (high static friction). `LEFT_PWM_MIN` clamp exists (0 = disabled).
- **Left motor HARDWARE FAULT** (confirmed by motor swap): periodic fast-slow in FORWARD only, smooth in reverse → gearbox/commutator issue. Not fixable in software; only mitigate with higher duty.
- Direction pins: `>0` → DIR1=HIGH,DIR2=LOW; `<0` → LOW,HIGH; else both LOW (three-branch, never `>=0`).
- Wiring: left motor output = TB6612 **BO1** (channel B), right = AO1 (channel A). DIR: GPIO 0/1 = left, GPIO 6/7 = right (code labels match physical wheels).

## Defaults (main.c)
- `Std_Speed = 4000` (~83% duty — right motor's high stall threshold requires high duty); +/- steps of 200, clamp 200..6000
- Left wheel trim: `LEFT_ADJUST_PCT = 952` (forward) / `REVERSE_ADJUST_PCT = 1000` (reverse); J/I keys trim ∓/±1% live
- Turn inner wheel factor: `turn_slow_num/den = 5/10` (=0.5); `turn_rate` unused (kept 1/1)

## Commands (HC-05 → native UART1 @ 9600, branch `native-uart`)
W/w=forward both, S/s=stop, A/a+L/l=left turn (0.5S/S), D/d=right turn (S/0.5S), X/x=reverse, +/-=speed, J/I=left trim -/+1%, P/p=toggle debug-page/slideshow mode (3s auto-page).
Receive path: poll `hp_uart_data_ready()` (LSR bit7==0) → `hal_hp_uart_recv()`; GPIO5 software UART removed on this branch. Debug echo buffered: bytes queued in `dbg_buf`, flushed to sys UART after 20ms line-idle. LCD debug page refreshes only top 56 rows (`lcd_flush_rows`) to cut QSPI blocking.

## Interfaces
- **PWM driver** (SDK C2): `pwm_hal_init(NULL,0,&{pscr,cmp})`, `pwm_hal_set_compare(NULL,0,PWM_CHx,val)` (CR0..CR3), `pwm_hal_enable(NULL,0)` (CTRL=3). Regs @ 0x03004000.
- **GPIO** (local driver/gpio.c): DR/DDR/PUB/PDB @ 0x03000000; DDR 1=input 0=output; **read-modify-write required** for DR and DDR; no PINMUX/FCFG on C1.
- **QSPI** (local driver/qspi.c): LEN=n*0x80000 → TXFIFO=data<<24 → STATUS=258 → poll `STATUS & 0xFFFF == 1`. CS auto-managed; never use CS bits.
- **HP_UART (HC-05)**: @ 0x03003000 LCR/DIV/TRX/FCR/LSR; **baud 9600** (DIV = CPU_FREQ/9600 - 1; HC-05 data-mode baud, confirmed by GPIO sw-uart era); LCR=0x1F (8N1); FCR FIFO enabled by `hal_hp_uart_init`; send: wait LSR bit8==0 then TRX; recv: poll LSR bit7==0 (data ready) then read TRX. HC-05 TX → board CUST_UART_RX.
- **Sys UART**: CLKDIV @ 0x03000010, DATA @ 0x03000014; baud = CPU_FREQ/CLKDIV (115200).
- **Timer**: TIM_0 CONFIG 0x0300005c, VALUE 0x03000060, DATA 0x03000064 (TIM_1 +0xc); CONFIG=0x0100 stop → DATA=count → 0x0101 start → poll DATA!=0.
- **ST7735**: st7735_device_t with dc=GPIO_NUM_2, port=HAL_QSPI_PORT_0; `.qspi_cs` set but unused (all transfers use STATUS=258). fb = uint32_t[8192], 2× RGB565 per word (hi=even x).
- **Config**: `configs/.config` + `configs/generated/autoconf.h` (CPU_FREQ_MHZ=72, UART_BAUD=115200).

## SDK Bugs / Gotchas
1. `components/gpio/include/gpio.h` line 43: missing semicolon after `gpio_get_level` prototype
2. `letter-shell` fails with picolibc (assert macro) — excluded in Makefile; also sfud/fatfs/TimmoLog
3. No floats on RV32IM (see Platform)
4. C1 has no HAL implementations — local `driver/gpio.c` + `driver/qspi.c`

## History / Troubleshooting Notes (why things are the way they are)
- LEFT_ADJUST_PCT journey: 100→90→50→30→120→200→…→80→84.5 (forward; float literal folded to 84 at compile time). Now permille 845 (=84.5%, /1000). 80 chosen because left motor physically faster; reverse needed 100.
- LEFT_PWM_MIN experiment (20/30/40/50%) revealed left stall ≈40%; then disabled (0) for equal-PWM test which revealed CH1 inversion.
- Green/red screen replaced old LCD debug text; 4s move timer and BT timeout were implemented then REMOVED (no STATE pin wired; user rejected timeouts).
- Git: working branch `main` (GPIO sw-uart 定版) / `native-uart` (native UART1 RX); remote `origin` = `git@github.com:FTCCCF/ECOS-C1-.git`. Pull remote main before pushing.
- native-uart branch (2026-09-05): RX moved back to native UART1 @9600 at user request (onboard RX wiring confirmed working); GPIO5 sw-uart + TIM_1 bit-bang helpers deleted. Earlier "板载RX疑似损坏" conclusion superseded — fd0ae9f was the对照测试 that led to the sw-uart era.
