/* The kernel tick: SysTick handler, the sleep/timeout list, and time slicing.
 *
 * Every blocked-with-timeout or sleeping task sits on ONE sorted list,
 * ordered by absolute wake tick. The tick handler only ever looks at the
 * head, so the per-tick cost is O(1) unless things actually expire.
 * Comparisons use signed wrap-safe math, so a 32-bit tick rolling over
 * (49.7 days at 1 kHz) is harmless. */

#include "os_internal.h"

static os_tcb_t *g_timerq;      /* sorted by wake_tick, soonest first */

void timer_list_add(os_tcb_t *t, uint32_t wake_tick)
{
    t->wake_tick = wake_tick;
    t->on_timer_list = 1;

    /* Walk past everyone who wakes no later than us, so the list stays
     * sorted soonest-first (and FIFO among equal deadlines). */
    os_tcb_t *prev = 0, *it = g_timerq;
    while (it && tick_reached(t->wake_tick, it->wake_tick)) {
        prev = it;
        it = it->tnext;
    }
    t->tnext = it;
    t->tprev = prev;
    if (it)
        it->tprev = t;
    if (prev)
        prev->tnext = t;
    else
        g_timerq = t;
}

void timer_list_remove(os_tcb_t *t)
{
    if (t->tprev)
        t->tprev->tnext = t->tnext;
    else
        g_timerq = t->tnext;
    if (t->tnext)
        t->tnext->tprev = t->tprev;
    t->tnext = t->tprev = 0;
    t->on_timer_list = 0;
}

/* Software-timer subsystem hook; timer.c overrides this. Weak so the kernel
 * links (and the linker can GC it) before M7 exists. */
__attribute__((weak)) int os_swtimer_tick(void) { return 0; }

void os_tick_advance(void)
{
    int need_switch = 0;
    uint32_t key = irq_lock();

    g_tick++;
    g_curr->run_ticks++;

    /* Wake everyone whose time has come. os_wake pulls them off wait lists
     * too, delivering OS_TIMEOUT to timed-out waits (sleeps ignore it). */
    while (g_timerq && tick_reached(g_tick, g_timerq->wake_tick)) {
        os_tcb_t *t = g_timerq;
        timer_list_remove(t);
        need_switch |= os_wake(t, OS_TIMEOUT);
    }

    need_switch |= os_swtimer_tick();

    /* Round-robin time slicing: when the quantum runs out and a peer of the
     * same priority is waiting, rotate. */
#if OS_TIME_SLICE_TICKS > 0
    if (g_curr->state == TS_RUNNING && g_curr->slice_left &&
        --g_curr->slice_left == 0) {
        extern uint32_t g_ready_bitmap;
        if (g_ready_bitmap & (1u << (31 - g_curr->curr_prio)))
            need_switch = 1;                 /* peers exist: switch out    */
        else
            g_curr->slice_left = OS_TIME_SLICE_TICKS;  /* alone: recharge  */
    }
#endif

    irq_unlock(key);

    if (need_switch)
        os_pend_switch();
}

void SysTick_Handler(void)
{
    os_tick_advance();
}

void os_sleep_ticks(uint32_t ticks)
{
    if (ticks == 0) {
        os_yield();
        return;
    }
    uint32_t key = irq_lock();
    os_block_current(0, 0, ticks);   /* no wait list, just the clock */
    irq_unlock(key);                 /* PendSV switches us out here  */
}

void os_sleep_ms(uint32_t ms)
{
    /* Round up so a nonzero ms never becomes a zero-tick (busy) sleep. */
    uint32_t ticks = (ms * OS_TICK_HZ + 999u) / 1000u;
    os_sleep_ticks(ticks ? ticks : 1);
}
