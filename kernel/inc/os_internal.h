/* Kernel-internal API shared between sched.c, task.c, tick.c and the sync
 * primitives. Applications and tests must not include this. Everything here
 * assumes it is called with interrupts locked (irq_lock) unless noted. */

#ifndef OS_INTERNAL_H
#define OS_INTERNAL_H

#include "os.h"
#include "critical.h"

/* -------------------------------------------------------------- scheduler */
extern os_tcb_t * volatile g_curr;      /* the RUNNING task                */
extern volatile uint32_t   g_tick;
extern int                 g_started;

/* Ready structure ops. The RUNNING task is *not* in a ready queue. */
void      readyq_insert(os_tcb_t *t);   /* tail of its curr_prio FIFO      */
void      readyq_remove(os_tcb_t *t);
/* Move a READY task to a new priority slot; RUNNING/BLOCKED handled by
 * os_task_change_prio. */
void      os_task_change_prio(os_tcb_t *t, uint8_t new_prio);

/* Ask for a PendSV context switch (no-op before os_start). Safe anywhere. */
void      os_pend_switch(void);

/* Nonzero if a READY task outranks g_curr (used after self-deprioritizing). */
int       os_preempt_needed(void);

/* Software-timer daemon startup, called once from os_start (timer.c). */
void      os_timer_daemon_start(void);

/* Make t READY and return 1 if it outranks the current task (caller then
 * pends a switch). Clears any wait/timer state t was on. */
int       os_wake(os_tcb_t *t, uint8_t status);

/* Block the current task on a priority-ordered wait list (or on nothing,
 * for pure sleeps: wait_root == NULL) with an optional tick timeout, and
 * pend the switch. The switch happens when the caller drops irq_lock.
 * After the task resumes, g_curr->wake_status says why. */
void      os_block_current(os_tcb_t **wait_root, void *wait_obj,
                           uint32_t timeout_ticks);

/* Wait-list helpers (priority ordered, FIFO within a priority) */
void      waitlist_insert(os_tcb_t **root, os_tcb_t *t);
void      waitlist_remove(os_tcb_t **root, os_tcb_t *t);
os_tcb_t *waitlist_pop(os_tcb_t **root);      /* highest priority waiter   */

/* ------------------------------------------------------------- timer list */
void      timer_list_add(os_tcb_t *t, uint32_t wake_tick);
void      timer_list_remove(os_tcb_t *t);
void      os_tick_advance(void);        /* the tick ISR body               */

/* Software-timer tick hook (weak no-op until timer.c provides it); returns
 * nonzero if a context switch is warranted. Called with irqs locked. */
int       os_swtimer_tick(void);

/* Wrap-safe: has 'now' reached absolute tick 'when'? */
static inline int tick_reached(uint32_t now, uint32_t when)
{
    return (int32_t)(now - when) >= 0;
}

/* ------------------------------------------------------------------- arch */
uint32_t *arch_stack_init(uint32_t *stack_top, os_task_fn_t fn, void *arg);
void      arch_start_first_task(void) __attribute__((noreturn));
void      arch_systick_init(uint32_t tick_hz);

/* Provided by the board: CPU clock feeding SysTick. */
uint32_t  board_sysclk_hz(void);

#endif /* OS_INTERNAL_H */
