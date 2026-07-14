/* The context switch -- the two exception handlers that make an RTOS an RTOS.
 *
 * How a switch works on ARMv7-M:
 *   On ANY exception entry the hardware automatically pushes r0-r3, r12, lr,
 *   pc, xPSR onto the *task's* stack (PSP). So by the time PendSV_Handler
 *   runs, half the context is already saved for free. We push the other half
 *   (r4-r11), record the resulting stack pointer in the task's TCB, ask the
 *   scheduler in C which task runs next, then do the exact mirror image:
 *   pop r4-r11 from the new task's stack, set PSP, and exception-return --
 *   which makes the hardware pop the other 8 registers and resume the new
 *   task as if nothing happened.
 *
 *   PendSV is configured as the LOWEST priority exception, so a switch
 *   requested from any ISR only happens after all nested ISRs finish.
 *
 * EXC_RETURN 0xFFFFFFFD = "return to thread mode, resume on PSP, no FPU
 * frame" -- tasks live on PSP, handlers on MSP, so a task stack only ever
 * needs room for its own frames, never for nested interrupts.
 */

    .syntax unified
    .cpu    cortex-m4
    .thumb

    .extern g_curr
    .extern os_sched_pick
    .extern _estack

/* ------------------------------------------------------------------ PendSV */
    .global PendSV_Handler
    .type   PendSV_Handler, %function
    .thumb_func
PendSV_Handler:
    cpsid   i                   /* the switch itself must be atomic        */

    mrs     r0, psp
    stmdb   r0!, {r4-r11}       /* save callee-saved half on task stack    */
    ldr     r1, =g_curr
    ldr     r2, [r1]
    str     r0, [r2]            /* g_curr->sp = final sp (sp is offset 0)  */

    push    {r3, lr}            /* r3 only to keep MSP 8-byte aligned      */
    bl      os_sched_pick       /* C picks next task, updates g_curr,      */
    pop     {r3, lr}            /*   checks stack canaries; returns TCB*   */

    ldr     r0, [r0]            /* r0 = next->sp                           */
    ldmia   r0!, {r4-r11}       /* restore its callee-saved half           */
    msr     psp, r0

    cpsie   i
    bx      lr                  /* hw pops r0-r3,r12,lr,pc,xPSR from PSP   */

/* -------------------------------------------------------------------- SVC
 * Used exactly once: os_start() executes 'svc 0' to launch the first task.
 * There is no previous context to save, so this is just the restore half of
 * PendSV, plus two one-time housekeeping steps. */
    .global SVC_Handler
    .type   SVC_Handler, %function
    .thumb_func
SVC_Handler:
    ldr     r0, =g_curr
    ldr     r0, [r0]
    ldr     r0, [r0]            /* r0 = first task's initial sp            */
    ldmia   r0!, {r4-r11}
    msr     psp, r0

    ldr     r0, =_estack        /* startup stack is dead now: hand ALL of  */
    msr     msp, r0             /*   it back to exception handlers         */

    ldr     lr, =0xFFFFFFFD     /* thread mode, PSP, no FPU frame          */
    bx      lr
