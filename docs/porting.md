# Porting to a new board

The kernel only touches hardware through three seams. For any ARMv7-M part
(Cortex-M3/M4/M7), `arch/armv7m/` works as-is; a new board needs:

## 1. Linker script (`board/<name>/<chip>.ld`)

Copy `stm32f303vc.ld`, fix the `MEMORY` origins/lengths for your part's
flash and RAM. The section layout is generic. `_estack` (top of RAM) becomes
the startup/handler stack.

## 2. Startup (`board/<name>/startup.c`)

Copy an existing one. The vector table needs the 16 core entries (the kernel
provides `SVC_Handler`, `PendSV_Handler`, `SysTick_Handler`, and the fault
handlers — just reference them) plus however many external IRQ slots your
chip has. If you don't use vendor IRQs, point them all at `Default_Handler`.

## 3. Board support (`board/<name>/board.c`) — implement `board.h`

| function | job |
|---|---|
| `board_init()` | clocks up, console hardware ready (called pre-main) |
| `board_sysclk_hz()` | the frequency feeding SysTick |
| `console_write/putc` | debug + shell output (UART, semihosting, RTT...) |
| `console_getc_nonblock` | shell input; return -1 for "nothing" |
| `console_getc` | blocking variant (can just spin on the above) |
| `board_led_count/led_set` | optional; return 0/no-op if you have none |
| `platform_exit(code)` | end simulation, or park the CPU visibly |

## 4. Makefile

Add a `TARGET` branch selecting your `BOARD_DIR` and `LDSCRIPT`, and adjust
`CPUFLAGS` if your core differs (e.g. `-mcpu=cortex-m7`). If your part has
an FPU and you want hard-float, you'll also need FPU context save in
`pendsv.S` (lazy stacking) — currently everything is soft-float on purpose.

## Gotchas that bite

- Stack tops must be 8-byte aligned (AAPCS): keep `OS_STACK_WORDS` even.
- PendSV must stay the lowest-priority exception in the system. If you add
  vendor ISRs, give them numerically lower (= more urgent) priorities and
  only call `*_from_isr` kernel functions from them.
- SysTick reload = `sysclk / OS_TICK_HZ - 1` must fit 24 bits: at 1 kHz
  that's fine up to 16.7 GHz, at 10 kHz up to 1.67 GHz — you're fine.
- `console_write` may be called from fault handlers with interrupts dead:
  keep it polling, never interrupt-driven.
