/* ARMv7-M port: initial stack frames, exception priorities, SysTick setup,
 * first-task launch, and the fault handlers. */

#include "os_internal.h"
#include "armv7m.h"
#include "kprintf.h"
#include "board.h"

/* Task entry trampoline. The initial stack frame is forged so that the very
 * first exception-return into a task "returns" here, with the task function
 * in r0 and its argument in r1 (i.e. our two parameters). If the task
 * function ever returns, it falls into os_task_exit -- no task needs its own
 * exit boilerplate. */
static void task_trampoline(os_task_fn_t fn, void *arg)
{
    fn(arg);
    os_task_exit();
}

/* Build the initial stack frame exactly as PendSV expects to find it:
 *   [ r4..r11 | r0 r1 r2 r3 r12 lr pc xPSR ]   (low addr ... high addr)
 * so the first switch-in "restores" a context that never existed. */
uint32_t *arch_stack_init(uint32_t *stack_top, os_task_fn_t fn, void *arg)
{
    uint32_t *sp = stack_top;

    /* hardware-popped frame (reverse push order) */
    *--sp = 0x01000000u;              /* xPSR: just the Thumb bit          */
    *--sp = (uint32_t)task_trampoline;/* pc                                */
    *--sp = 0u;                       /* lr: trampoline never returns      */
    *--sp = 0x12121212u;              /* r12 (recognizable junk for debug) */
    *--sp = 0x03030303u;              /* r3                                */
    *--sp = 0x02020202u;              /* r2                                */
    *--sp = (uint32_t)arg;            /* r1 -> trampoline's 2nd parameter  */
    *--sp = (uint32_t)fn;             /* r0 -> trampoline's 1st parameter  */

    /* software-saved frame */
    for (int r = 11; r >= 4; r--)
        *--sp = 0x04040404u + 0x01010101u * (uint32_t)(r - 4);

    return sp;
}

void os_pend_switch(void)
{
    if (g_started) {
        SCB_ICSR = ICSR_PENDSVSET;
        arch_isb();
    }
}

void arch_systick_init(uint32_t tick_hz)
{
    uint32_t reload = board_sysclk_hz() / tick_hz;

    SYST_RVR = reload - 1;
    SYST_CVR = 0;
    SYST_CSR = SYST_CSR_CLKSOURCE | SYST_CSR_TICKINT | SYST_CSR_ENABLE;
}

void arch_start_first_task(void)
{
    /* PendSV must be the lowest priority in the system so context switches
     * requested from ISRs run only after every ISR has unwound. SysTick sits
     * one level above it; external IRQs default to 0 (highest) and may
     * preempt the tick -- all shared state is PRIMASK-protected. */
    SCB_SHPR3 = (0xFFu << 16)   /* PendSV  = lowest  */
              | (0xE0u << 24);  /* SysTick = just above PendSV */

    arch_systick_init(OS_TICK_HZ);

    __asm__ volatile ("cpsie i \n isb");
    __asm__ volatile ("svc 0");           /* never returns */
    for (;;)
        ;
}

/* ------------------------------------------------------------------ faults
 * A fault dump that names the running task and shows the stacked pc/lr turns
 * "it hangs" into "task shell dereferenced 0x0 at pc=0x1234". */

struct fault_frame {
    uint32_t r0, r1, r2, r3, r12, lr, pc, xpsr;
};

void hardfault_c(struct fault_frame *f, uint32_t exc_return)
{
    (void)exc_return;
    os_printf("\n*** HARD FAULT ***\n");
    if (g_curr)
        os_printf("task: %s (id %d)\n", g_curr->name, g_curr->id);
    os_printf("  pc=%08lx lr=%08lx xpsr=%08lx\n", f->pc, f->lr, f->xpsr);
    os_printf("  r0=%08lx r1=%08lx r2=%08lx r3=%08lx r12=%08lx\n",
              f->r0, f->r1, f->r2, f->r3, f->r12);
    os_printf("  HFSR=%08lx CFSR=%08lx MMFAR=%08lx BFAR=%08lx\n",
              SCB_HFSR, SCB_CFSR, SCB_MMFAR, SCB_BFAR);
    platform_exit(1);
}

__attribute__((naked)) void HardFault_Handler(void)
{
    /* Which stack holds the faulting frame? EXC_RETURN bit 2 says. */
    __asm__ volatile (
        "tst   lr, #4        \n"
        "ite   eq            \n"
        "mrseq r0, msp       \n"
        "mrsne r0, psp       \n"
        "mov   r1, lr        \n"
        "b     hardfault_c   \n");
}

/* Escalate the other faults into the same dump. */
void MemManage_Handler(void)  __attribute__((alias("HardFault_Handler")));
void BusFault_Handler(void)   __attribute__((alias("HardFault_Handler")));
void UsageFault_Handler(void) __attribute__((alias("HardFault_Handler")));
