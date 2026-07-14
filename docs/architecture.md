# Architecture

How this kernel works, in the order the pieces come alive.

## 1. Boot

Cortex-M boot is refreshingly simple: the CPU reads word 0 (initial MSP) and
word 1 (reset vector) from address 0 and jumps. `board/*/startup.c` owns that
vector table. `Reset_Handler` copies `.data` from flash, zeroes `.bss`, calls
`board_init()` (clocks + console), then `main()`.

`main()` is *not a task*. It runs on the startup stack (MSP), creates the
initial tasks, and calls `os_start()`, which never returns.

## 2. Memory model: everything is static

```c
static os_tcb_t  g_tcbs[OS_MAX_TASKS];                    /* task.c */
static uint32_t  g_stacks[OS_MAX_TASKS][OS_STACK_WORDS];
```

Task id = index into both pools. "Allocating" a task is scanning for a
`TS_UNUSED` slot; deleting marks it `TS_UNUSED` again. Queues and timers use
caller-provided storage. There is no heap and therefore no fragmentation, no
allocation failure at 3am, and no `malloc` in the vector-table path — the
standard embedded discipline, enforced by not implementing an allocator at
all.

## 3. The scheduler: one bitmap, 32 FIFOs

```
g_ready_bitmap   bit (31-p) set  <=>  some task of priority p is ready
g_readyq_head[p] / tail[p]       FIFO of READY tasks at priority p
```

Priority 0 is highest, 31 is the idle task. The highest ready priority is
`__builtin_clz(g_ready_bitmap)` — one instruction, no scan, O(1) regardless
of task count. Each priority level is a doubly-linked FIFO, so equal-priority
tasks take turns (round-robin), rotated by the time-slicer every
`OS_TIME_SLICE_TICKS` ticks *only if* a peer is actually waiting.

The RUNNING task is not in any ready queue. At each switch, `os_sched_pick()`
re-queues the outgoing task at its FIFO's tail (if it's still runnable),
recycles it (if it exited — see §5), or leaves it alone (if it blocked), then
pops the head of the highest non-empty FIFO.

## 4. The context switch (`arch/armv7m/pendsv.S`)

The part everyone is afraid of, and it's 25 instructions.

On **any** exception entry, Cortex-M hardware pushes `r0-r3, r12, lr, pc,
xPSR` onto the *current* stack. Tasks run on PSP (process stack pointer),
handlers on MSP — so an exception from a task leaves half the task's context
already saved on the task's own stack, for free.

PendSV — pended by `os_pend_switch()`, configured as the *lowest* priority
exception — does the rest:

```
save:    mrs r0, psp             ; task's stack pointer
         stmdb r0!, {r4-r11}     ; the half hardware doesn't save
         str r0, [g_curr]        ; park sp in the TCB (offset 0)
pick:    bl os_sched_pick        ; C decides who's next, checks canaries
restore: ldr r0, [r0]            ; new task's parked sp
         ldmia r0!, {r4-r11}
         msr psp, r0
         bx lr                   ; magic EXC_RETURN value...
```

`lr` holds `0xFFFFFFFD`: "return to thread mode, restore from PSP". The
hardware pops the other 8 registers and the new task resumes as if its own
interrupt just ended. Because PendSV is lowest priority, a switch requested
from any ISR happens exactly once, after all ISRs unwind — this is why
`sem_give_from_isr` is safe from nested interrupts.

**Birth of a task**: `os_task_create` forges this exact frame on a virgin
stack, with `pc` = a trampoline and `r0/r1` = the task function and its
argument. The first switch-in "restores" a context that never existed; the
trampoline calls the function; if it returns, it falls into `os_task_exit()`.
No task needs exit boilerplate.

**The first task ever** has no one to switch *from*, so `os_start()` executes
`svc 0` and the SVC handler runs only the restore half, then donates the
whole startup stack back to the exception handlers (`msr msp, _estack`).

## 5. Blocking, waking, and the zombie trick

All state transitions happen inside PRIMASK critical sections
(`irq_lock/irq_unlock`), so an interrupt can never observe a half-linked
list — the "no lost wakeups" rule.

`os_block_current(wait_root, obj, timeout)` parks the caller on a
priority-ordered wait list, optionally also on the **timer list** (one sorted
list of absolute wake ticks; the tick handler looks only at its head), and
pends a switch that fires when the caller drops the lock.

`os_wake(t, status)` is the single wake path used by gives, sends, sets,
timeouts, aborts: unlink from wait list, unlink from timer list, write
`wake_status`, make READY. The woken task learns *why* it woke (OS_OK vs
OS_TIMEOUT) from that status — every timed wait works this way, so timeout
handling is uniform across all primitives.

A task woken between "I blocked" and "the switch actually happened" is
simply READY again when PendSV finally picks — the state machine makes the
race benign.

**Zombie reaping**: an exiting task can't free its own TCB — it's still
*running on the stack being freed* until PendSV switches away. So exit marks
the task `TS_ZOMBIE`, and `os_sched_pick()` recycles it at the exact moment
its context has been saved for the last time and can never be reused.

## 6. Direct handoff (why there are no retry loops)

Naive semaphore give: `count++`, wake a waiter, waiter re-checks the count
when it runs. But between the wake and the run, *anyone* can take the count
— so the waiter must loop, recompute its remaining timeout, and can starve.

This kernel transfers instead:

- **semaphore give** with waiters: token moves directly to the top waiter;
  the counter is never touched
- **mutex unlock** with waiters: `owner` is reassigned before the wake
- **queue send** to a blocked receiver: `memcpy` straight into the
  receiver's destination buffer (parked in its TCB)
- **queue recv** that frees a slot: pulls a blocked sender's element into
  the ring in the same critical section
- **event set**: writes the satisfying bits into each satisfied waiter's TCB
  and consumes (CLEAR) in priority order

When a woken task runs, its result is already in hand: `OS_OK` *means*
delivered. No loops, no re-checks, no starvation window.

## 7. Priority inheritance

`mutex_lock` on an owned mutex boosts the owner to the locker's priority if
it's higher (`os_task_change_prio` re-slots READY tasks and re-sorts wait
lists). `mutex_unlock` restores `base_prio` and — since dropping your own
priority can make someone else the rightful runner even when no one was
woken — explicitly checks `os_preempt_needed()`.

Deliberate simplifications (all documented in `mutex.c`, all standard for
kernels this size): non-recursive, single-level inheritance (no chained
propagation), restore-to-base on unlock (not per-mutex ceilings), and a
timed-out waiter leaves its boost in place until the owner unlocks.

## 8. Time

SysTick fires at `OS_TICK_HZ` (1 kHz). The handler: increments the tick,
credits the running task (`run_ticks` — a sampling profiler, in effect),
drains the due head of the timer list, kicks the software-timer daemon if
its head expired, and rotates the time slice. Tick comparisons use signed
wraparound-safe math (`(int32_t)(now - when) >= 0`), so the 49.7-day
rollover at 1 kHz is a non-event.

Software timers run their callbacks in the `tmrsvc` **task**, not the ISR:
the tick handler just gives a semaphore. Callbacks may block briefly, take
mutexes, print — things an ISR-context timer could never do — at the price
of running at `OS_TIMER_TASK_PRIO` like any other task.

## 9. When things go wrong

- Every context switch verifies the outgoing task's **stack canary** (the
  deepest word must still be `0xA5A5A5A5`) and that its sp points inside its
  stack. Overflow is caught within one time slice and panics with the task's
  name.
- The whole stack is canary-filled at creation, so `os_stack_high_water()`
  can report the deepest excursion ever — visible in the shell's `ps`.
- **HardFault** (and escalated Mem/Bus/Usage faults) dump the stacked
  `pc/lr/xPSR`, the fault status registers, and the running task's name,
  then end the simulation with a failure code — so a wild pointer in QEMU
  fails the test suite loudly instead of hanging it.
- `os_panic()` masks interrupts, prints, and exits (QEMU) or parks with the
  verdict on the LEDs (board).

## 10. Portability seams

Three interfaces, no `#ifdef` soup:

- `arch/armv7m/` — context frame layout, PendSV/SVC, SysTick, faults
- `board.h` — console in/out, `board_sysclk_hz`, LEDs, `platform_exit`
- the linker script + `startup.c` pair per board

QEMU's mps2-an386 and the STM32F303 Discovery differ *only* in the third
item plus one `board.c`. See [porting.md](porting.md).
