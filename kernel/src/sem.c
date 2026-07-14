/* Counting/binary semaphores: a counter with a priority-ordered waitlist.
 * No ownership -- anyone (including an ISR) may give.
 *
 * The key trick is DIRECT HANDOFF: when give() finds a waiter, the count is
 * never incremented -- the "token" moves straight to the woken task, whose
 * take() returns OS_OK without touching the counter. This means a woken
 * waiter can never lose its token to a task that sneaks in between the wake
 * and the reschedule, so there are no retry loops anywhere. */

#include "os_internal.h"

void os_sem_init(os_sem_t *s, uint32_t initial, uint32_t max)
{
    s->count = initial;
    s->max = max ? max : 0xFFFFFFFFu;
    s->waiters = 0;
}

os_status_t os_sem_take(os_sem_t *s, uint32_t timeout_ticks)
{
    if (arch_in_isr())
        return OS_ERR_ISR;

    uint32_t key = irq_lock();

    if (s->count > 0) {
        s->count--;
        irq_unlock(key);
        return OS_OK;
    }
    if (timeout_ticks == OS_NO_WAIT) {
        irq_unlock(key);
        return OS_TIMEOUT;
    }

    os_block_current(&s->waiters, s, timeout_ticks);
    irq_unlock(key);            /* PendSV switches us out right here */

    /* We're back: either give() handed us the token (OS_OK) or the tick
     * expired the wait (OS_TIMEOUT). wake_status was written before we were
     * made ready, so a plain read is safe. */
    return (os_status_t)g_curr->wake_status;
}

/* Shared core of give/give_from_isr; returns whether a higher-priority task
 * was woken. */
static os_status_t sem_give_internal(os_sem_t *s, int *need_switch)
{
    uint32_t key = irq_lock();
    os_status_t st = OS_OK;

    os_tcb_t *w = s->waiters;
    if (w) {
        *need_switch = os_wake(w, OS_OK);   /* token handed over, count untouched */
    } else if (s->count < s->max) {
        s->count++;
        *need_switch = 0;
    } else {
        st = OS_ERR_STATE;                  /* binary sem given twice etc. */
        *need_switch = 0;
    }

    irq_unlock(key);
    return st;
}

os_status_t os_sem_give(os_sem_t *s)
{
    int need;
    os_status_t st = sem_give_internal(s, &need);
    if (need)
        os_pend_switch();
    return st;
}

os_status_t os_sem_give_from_isr(os_sem_t *s, int *need_yield)
{
    int need;
    os_status_t st = sem_give_internal(s, &need);
    if (need_yield)
        *need_yield = need;
    return st;
}
