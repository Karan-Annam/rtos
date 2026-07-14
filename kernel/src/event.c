/* Event flag groups: 32 bits; tasks wait for ANY or ALL of a mask, with
 * optional consume-on-wake (OS_EVT_CLEAR).
 *
 * A waiter parks its request in its own TCB (mask in wait_flags, mode in
 * wait_data). set() walks the priority-ordered waitlist, and for each
 * satisfied waiter writes the satisfying bits *back into wait_flags* before
 * waking -- so the waiter reads its result with zero re-checking, and CLEAR
 * consumption happens in priority order. */

#include "os_internal.h"

void os_event_init(os_event_t *e)
{
    e->flags = 0;
    e->waiters = 0;
}

static uint32_t evt_satisfied(uint32_t flags, uint32_t mask, uint32_t mode)
{
    uint32_t hit = flags & mask;
    if (mode & OS_EVT_ALL)
        return hit == mask ? hit : 0;
    return hit;                        /* ANY: nonzero == satisfied */
}

os_status_t os_event_wait(os_event_t *e, uint32_t mask, uint32_t mode,
                          uint32_t *got, uint32_t timeout_ticks)
{
    if (arch_in_isr())
        return OS_ERR_ISR;
    if (!mask)
        return OS_ERR_PARAM;

    uint32_t key = irq_lock();

    uint32_t hit = evt_satisfied(e->flags, mask, mode);
    if (hit) {
        if (mode & OS_EVT_CLEAR)
            e->flags &= ~hit;
        irq_unlock(key);
        if (got)
            *got = hit;
        return OS_OK;
    }
    if (timeout_ticks == OS_NO_WAIT) {
        irq_unlock(key);
        return OS_TIMEOUT;
    }

    g_curr->wait_flags = mask;
    g_curr->wait_data = (void *)mode;
    os_block_current(&e->waiters, e, timeout_ticks);
    irq_unlock(key);                   /* switch */

    if (g_curr->wake_status != OS_OK)
        return OS_TIMEOUT;
    if (got)
        *got = g_curr->wait_flags;     /* setter stored the satisfying bits */
    return OS_OK;
}

static int event_set_internal(os_event_t *e, uint32_t mask)
{
    int need = 0;

    e->flags |= mask;

    os_tcb_t *t = e->waiters;
    while (t) {
        os_tcb_t *next = t->qnext;     /* os_wake unlinks t; grab next now */
        uint32_t want = t->wait_flags;
        uint32_t mode = (uint32_t)t->wait_data;
        uint32_t hit = evt_satisfied(e->flags, want, mode);
        if (hit) {
            t->wait_flags = hit;       /* result for the waiter            */
            if (mode & OS_EVT_CLEAR)
                e->flags &= ~hit;      /* consumed in priority order       */
            need |= os_wake(t, OS_OK);
        }
        t = next;
    }
    return need;
}

void os_event_set(os_event_t *e, uint32_t mask)
{
    uint32_t key = irq_lock();
    int need = event_set_internal(e, mask);
    irq_unlock(key);
    if (need)
        os_pend_switch();
}

void os_event_set_from_isr(os_event_t *e, uint32_t mask, int *need_yield)
{
    uint32_t key = irq_lock();
    int need = event_set_internal(e, mask);
    irq_unlock(key);
    if (need_yield)
        *need_yield = need;
}

void os_event_clear(os_event_t *e, uint32_t mask)
{
    uint32_t key = irq_lock();
    e->flags &= ~mask;
    irq_unlock(key);
}
