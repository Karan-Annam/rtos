/* Mutexes: semaphores with an owner, and the reason RTOSes are interesting --
 * PRIORITY INHERITANCE.
 *
 * The classic failure (Mars Pathfinder, 1997): low-prio task L holds a lock
 * that high-prio task H needs, and medium-prio task M -- which doesn't even
 * touch the lock -- preempts L indefinitely. H is now effectively below M:
 * priority inversion. The fix: while H waits, L *borrows* H's priority, so M
 * can't preempt it; L finishes its critical section and hands the lock (and
 * L's old priority back) over.
 *
 * Simplifications, deliberate + documented:
 *  - non-recursive: locking a mutex you own is an error, not a count
 *  - unlock restores base_prio directly (exact disinheritance with multiple
 *    held mutexes would need per-mutex ceiling tracking)
 *  - inheritance is single-level (no propagation through chains of mutexes)
 */

#include "os_internal.h"

void os_mutex_init(os_mutex_t *m)
{
    m->owner = 0;
    m->waiters = 0;
}

os_status_t os_mutex_lock(os_mutex_t *m, uint32_t timeout_ticks)
{
    if (arch_in_isr())
        return OS_ERR_ISR;      /* ISRs cannot own mutexes, ever */

    uint32_t key = irq_lock();

    if (!m->owner) {
        m->owner = g_curr;
        irq_unlock(key);
        return OS_OK;
    }
    if (m->owner == g_curr) {
        irq_unlock(key);
        return OS_ERR_STATE;    /* non-recursive by design */
    }
    if (timeout_ticks == OS_NO_WAIT) {
        irq_unlock(key);
        return OS_TIMEOUT;
    }

    /* Priority inheritance: if the owner is running below us, boost it so
     * no middle-priority task can keep the lock hostage. */
    if (m->owner->curr_prio > g_curr->curr_prio)
        os_task_change_prio(m->owner, g_curr->curr_prio);

    os_block_current(&m->waiters, m, timeout_ticks);
    irq_unlock(key);            /* switch happens here */

    /* OS_OK means unlock() made us the owner before waking us. */
    return (os_status_t)g_curr->wake_status;
}

os_status_t os_mutex_unlock(os_mutex_t *m)
{
    uint32_t key = irq_lock();

    if (m->owner != g_curr) {
        irq_unlock(key);
        return OS_ERR_OWNER;
    }

    /* Give back any priority we borrowed. */
    int deboosted = 0;
    if (g_curr->curr_prio != g_curr->base_prio) {
        os_task_change_prio(g_curr, g_curr->base_prio);
        deboosted = 1;
    }

    int need = 0;
    os_tcb_t *w = m->waiters;
    if (w) {
        m->owner = w;           /* ownership handoff: no thundering herd  */
        need = os_wake(w, OS_OK);
    } else {
        m->owner = 0;
    }

    /* After dropping our own priority, someone READY may now outrank us
     * even if we woke nobody. */
    if (deboosted && os_preempt_needed())
        need = 1;

    irq_unlock(key);
    if (need)
        os_pend_switch();
    return OS_OK;
}
