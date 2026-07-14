/* Software timers: one-shot and periodic callbacks driven by the kernel tick
 * but EXECUTED IN A TASK -- the timer daemon -- never in the ISR.
 *
 * The tick handler only peeks at the head of the sorted active list; if
 * something is due it gives the daemon's semaphore (an ISR-safe operation)
 * and moves on. The daemon then pops every due timer, re-arms the periodic
 * ones, and runs the callbacks with interrupts enabled. Callbacks therefore
 * may take a while without wrecking tick latency -- but they run at
 * OS_TIMER_TASK_PRIO, so they can also be preempted like any other task. */

#include "os_internal.h"

static os_timer_t *g_active;    /* sorted by expiry_tick, soonest first */
static os_sem_t    g_timer_sem;

static void active_insert(os_timer_t *t)
{
    /* Sorted soonest-first: walk past timers expiring no later than us. */
    os_timer_t *prev = 0, *it = g_active;
    while (it && tick_reached(t->expiry_tick, it->expiry_tick)) {
        prev = it;
        it = it->next;
    }
    t->next = it;
    if (prev)
        prev->next = t;
    else
        g_active = t;
}

static void active_remove(os_timer_t *t)
{
    os_timer_t **pp = &g_active;
    while (*pp && *pp != t)
        pp = &(*pp)->next;
    if (*pp)
        *pp = t->next;
    t->next = 0;
}

/* Called from the tick ISR path with interrupts locked (overrides the weak
 * no-op in tick.c). Just a peek + a semaphore give. */
int os_swtimer_tick(void)
{
    if (g_active && tick_reached(g_tick, g_active->expiry_tick)) {
        int need = 0;
        os_sem_give_from_isr(&g_timer_sem, &need);
        return need;
    }
    return 0;
}

static void timer_daemon(void *arg)
{
    (void)arg;
    for (;;) {
        os_sem_take(&g_timer_sem, OS_WAIT_FOREVER);

        /* Drain everything currently due. */
        for (;;) {
            uint32_t key = irq_lock();
            os_timer_t *t = g_active;
            if (!t || !tick_reached(g_tick, t->expiry_tick)) {
                irq_unlock(key);
                break;
            }
            g_active = t->next;
            t->next = 0;
            if (t->periodic) {
                t->expiry_tick += t->period_ticks;
                active_insert(t);          /* stays active, re-armed */
            } else {
                t->active = 0;
            }
            irq_unlock(key);

            t->cb(t, t->arg);              /* task context, irqs enabled */
        }
    }
}

void os_timer_daemon_start(void)
{
    os_sem_init(&g_timer_sem, 0, 0 /* unbounded */);
    os_tcb_t *d = os_task_create(timer_daemon, 0, OS_TIMER_TASK_PRIO,
                                 "tmrsvc");
    OS_ASSERT(d);
}

void os_timer_init(os_timer_t *t, const char *name,
                   void (*cb)(os_timer_t *, void *), void *arg)
{
    t->cb = cb;
    t->arg = arg;
    t->name = name ? name : "?";
    t->period_ticks = 0;
    t->expiry_tick = 0;
    t->next = 0;
    t->active = 0;
    t->periodic = 0;
}

os_status_t os_timer_start(os_timer_t *t, uint32_t ticks, int periodic)
{
    if (!t || !t->cb || ticks == 0)
        return OS_ERR_PARAM;

    uint32_t key = irq_lock();
    if (t->active)
        active_remove(t);              /* restart re-arms cleanly */
    t->period_ticks = ticks;
    t->expiry_tick = g_tick + ticks;
    t->periodic = periodic ? 1 : 0;
    t->active = 1;
    active_insert(t);
    irq_unlock(key);
    return OS_OK;
}

os_status_t os_timer_stop(os_timer_t *t)
{
    if (!t)
        return OS_ERR_PARAM;

    uint32_t key = irq_lock();
    if (t->active) {
        active_remove(t);
        t->active = 0;
    }
    irq_unlock(key);
    return OS_OK;
}
