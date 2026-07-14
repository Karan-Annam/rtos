/* Kernel configuration. Every knob is #ifndef-guarded so a test or app can
 * override it from the command line (-DOS_MAX_TASKS=4) without editing this
 * file. All memory the kernel will ever use is sized by these constants at
 * compile time -- there is no heap. */

#ifndef OS_CONFIG_H
#define OS_CONFIG_H

#ifndef OS_MAX_TASKS
#define OS_MAX_TASKS        16      /* size of the TCB + stack pools        */
#endif

/* Fixed at 32 so the ready bitmap is a single word scanned with clz.
 * Priority 0 is the HIGHEST, 31 the lowest (reserved for the idle task). */
#define OS_MAX_PRIOS        32
#define OS_IDLE_PRIO        (OS_MAX_PRIOS - 1)

#ifndef OS_STACK_WORDS
#define OS_STACK_WORDS      256     /* per-task stack, in 32-bit words (1 KB) */
#endif

#ifndef OS_TICK_HZ
#define OS_TICK_HZ          1000    /* kernel tick rate                     */
#endif

#ifndef OS_TIME_SLICE_TICKS
#define OS_TIME_SLICE_TICKS 10      /* round-robin quantum; 0 disables slicing */
#endif

#ifndef OS_TIMER_TASK_PRIO
#define OS_TIMER_TASK_PRIO  1       /* software-timer daemon task priority  */
#endif

#ifndef OS_TIMER_TASK_STACK_WORDS
#define OS_TIMER_TASK_STACK_WORDS OS_STACK_WORDS
#endif

#define OS_STACK_FILL       0xA5A5A5A5u  /* canary / high-water-mark pattern */

#endif /* OS_CONFIG_H */
