# rtos — a from-scratch ARM Cortex-M RTOS

A preemptive real-time kernel written from scratch in C and ARM assembly —
no vendor HAL, no CMSIS, no borrowed scheduler. Every register write and
every context switch is in this repo, on purpose: the point is to understand
how an RTOS actually works, down to the exception-return value.

Developed sim-first on QEMU (Cortex-M4, fully automated test suite, no
hardware needed) and portable to a real STM32F3 Discovery board where eight
tasks blink the LED compass ring at eight different rates.

```
$ ./run_all.sh
PASS  test_01_create_rr
PASS  test_02_preempt
...
15/15 tests passed
```

## Features

- **O(1) priority scheduler** — 32 priority levels, one-instruction
  highest-ready lookup (`clz` on a ready bitmap), FIFO round-robin with time
  slicing within a priority
- **PendSV context switching** the canonical Cortex-M way: hardware stacks
  half the registers, `pendsv.S` stacks the other half, tasks live on PSP,
  handlers on MSP
- **Fully static** — no heap, ever; fixed TCB + stack pools, caller-provided
  queue/timer storage
- **Semaphores** (counting/binary) with ISR-safe give and *direct handoff* —
  a released token goes straight to the highest-priority waiter, never
  through the counter where it could be stolen
- **Mutexes with priority inheritance** — the Mars Pathfinder bug, solved
  and unit-tested
- **Message queues** with blocking send/recv, timeouts, and direct
  buffer-to-buffer handoff to blocked peers
- **Event flag groups** — wait ANY/ALL of 32 bits, optional consume-on-wake
- **Software timers** — one-shot + periodic callbacks in a daemon task
- **Robustness** — stack canaries checked at every context switch,
  high-water-mark tracking, register-dumping hard-fault handler, kernel
  panic with task attribution
- **Interactive shell** — `ps`, `stats`, `suspend/resume/kill`, over QEMU's
  UART or the board's USART

## Quick start (no hardware needed)

Requires `arm-none-eabi-gcc`, `make`, and `qemu-system-arm`.

```sh
./run_all.sh                  # build + run the 15-test suite on QEMU
make run-demo_prodcons        # producer/consumer with live throughput
make run-demo_shell           # interactive shell (type 'ps'; Ctrl-A X quits)
make run-demo_ring            # the LED ring, rendered in ASCII on QEMU
```

## On the STM32F3 Discovery

```sh
make TARGET=f3disco
scripts/flash_f3.sh build/f3disco/demo_ring.elf
```

Serial console: 115200 8N1 on PC4 (TX) / PC5 (RX) — the ST-LINK virtual COM
port on newer board revisions, or any 3.3 V USB-serial adapter on older
ones. `demo_ring` runs the shell on serial while the ring blinks.

## The test suite

| # | proves |
|---|--------|
| 01 | task creation, PendSV switch, yield round-robin, pool exhaustion/recycling |
| 02 | preemption: on higher-prio create and on tick wakeup, no cooperation needed |
| 03 | sleep accuracy (±1 tick) and deadline-ordered wakeups |
| 04 | semaphore counting, blocking, timeout accuracy, priority-ordered wake, direct handoff |
| 05 | give from a real NVIC interrupt, need_yield protocol, ISR blocking rejected |
| 06 | mutex ownership rules, non-recursion, timed lock expiry, ownership handoff |
| 07 | priority inheritance (Mars Pathfinder scenario: L boosted past M, restored after) |
| 08 | queue FIFO integrity across blocking full/empty boundaries, handoff, timeouts |
| 09 | event ANY/ALL semantics, consume-on-wake in priority order, timeouts |
| 10 | software timers: one-shot punctuality, periodic rate, stop, restart, task context |
| 11 | stack-overflow canary detection (passes by panicking) |
| 12 | suspend freezes, resume thaws, suspending a blocked task aborts its wait, delete recycles |
| 13 | time-slice fairness among non-cooperating equal-priority hogs |
| 14 | stress: 250 messages, 5 workers, exact accounting of every increment and byte |

## Layout

```
kernel/        portable C: scheduler, tasks, tick, sem/mutex/queue/event/timer
arch/armv7m/   pendsv.S (the context switch), port.c, register defs
board/         qemu_mps2 (semihosting + CMSDK UART) and f3disco (bare-metal F303)
shell/         the CLI task
apps/          demos          tests/  the suite          docs/  how it works
```

**[docs/architecture.md](docs/architecture.md)** explains how every piece
works — the boot path, the context switch frame by frame, why direct handoff
avoids retry loops, and the deliberate simplifications.

Built as a learning project; the design notes it was rebuilt from fit on
three pages, the interesting parts didn't.
