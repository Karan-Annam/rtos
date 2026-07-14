/* The scheduler: ready bitmap + per-priority FIFO queues, wait lists, and
 * the pick function PendSV calls to decide who runs next.
 *
 * Data structure (from the design notes):
 *   - one 32-bit bitmap: bit set = "some task of that priority is ready".
 *     Priority p lives at bit (31 - p), so __builtin_clz(bitmap) IS the
 *     highest ready priority -- one instruction, O(1), no scanning.
 *   - per priority, a FIFO of TCBs (doubly-linked, intrusive) so equal
 *     priority tasks round-robin fairly.
 *   - the RUNNING task is not in any ready queue; it re-enters at the tail
 *     when it gets switched out while still runnable.
 */

#include "os_internal.h"
#include "kprintf.h"

uint32_t   g_ready_bitmap;
static os_tcb_t *g_readyq_head[OS_MAX_PRIOS];
static os_tcb_t *g_readyq_tail[OS_MAX_PRIOS];

os_tcb_t * volatile g_curr;
volatile uint32_t   g_tick;
int                 g_started;

static os_tcb_t *g_idle_tcb;

/* ------------------------------------------------------------ ready queue */

void readyq_insert(os_tcb_t *t)
{
    uint8_t p = t->curr_prio;

    t->qnext = 0;
    t->qprev = g_readyq_tail[p];
    if (g_readyq_tail[p])
        g_readyq_tail[p]->qnext = t;
    else
        g_readyq_head[p] = t;
    g_readyq_tail[p] = t;

    g_ready_bitmap |= 1u << (31 - p);
}

void readyq_remove(os_tcb_t *t)
{
    uint8_t p = t->curr_prio;

    if (t->qprev)
        t->qprev->qnext = t->qnext;
    else
        g_readyq_head[p] = t->qnext;
    if (t->qnext)
        t->qnext->qprev = t->qprev;
    else
        g_readyq_tail[p] = t->qprev;
    t->qnext = t->qprev = 0;

    if (!g_readyq_head[p])
        g_ready_bitmap &= ~(1u << (31 - p));
}

static inline uint8_t highest_ready_prio(void)
{
    return (uint8_t)__builtin_clz(g_ready_bitmap);
}

/* -------------------------------------------------------------- wait lists
 * Priority-ordered (highest first), FIFO among equals: insert walks past
 * everyone with prio <= ours. Wait lists reuse qnext/qprev -- a task can't
 * be both ready and waiting. */

void waitlist_insert(os_tcb_t **root, os_tcb_t *t)
{
    os_tcb_t *prev = 0, *it = *root;

    while (it && it->curr_prio <= t->curr_prio) {
        prev = it;
        it = it->qnext;
    }
    t->qnext = it;
    t->qprev = prev;
    if (it)
        it->qprev = t;
    if (prev)
        prev->qnext = t;
    else
        *root = t;
}

void waitlist_remove(os_tcb_t **root, os_tcb_t *t)
{
    if (t->qprev)
        t->qprev->qnext = t->qnext;
    else
        *root = t->qnext;
    if (t->qnext)
        t->qnext->qprev = t->qprev;
    t->qnext = t->qprev = 0;
}

os_tcb_t *waitlist_pop(os_tcb_t **root)
{
    os_tcb_t *t = *root;
    if (t)
        waitlist_remove(root, t);
    return t;
}

/* ------------------------------------------------------------ block / wake */

void os_block_current(os_tcb_t **wait_root, void *wait_obj,
                      uint32_t timeout_ticks)
{
    os_tcb_t *t = g_curr;

    OS_ASSERT(!arch_in_isr());
    OS_ASSERT(t != g_idle_tcb);          /* idle must never block */

    t->state = TS_BLOCKED;
    t->wake_status = OS_OK;
    t->wait_root = wait_root;
    t->wait_obj = wait_obj;
    if (wait_root)
        waitlist_insert(wait_root, t);
    if (timeout_ticks != OS_WAIT_FOREVER)
        timer_list_add(t, g_tick + timeout_ticks);

    os_pend_switch();
    /* Caller drops irq_lock next; PendSV fires there and switches away.
     * Execution resumes here once someone wakes us. */
}

int os_wake(os_tcb_t *t, uint8_t status)
{
    if (t->wait_root) {
        waitlist_remove(t->wait_root, t);
        t->wait_root = 0;
    }
    t->wait_obj = 0;
    if (t->on_timer_list)
        timer_list_remove(t);

    t->wake_status = status;
    t->state = TS_READY;
    readyq_insert(t);

    return t->curr_prio < g_curr->curr_prio;
}

/* Priority change (yield-safe): used by mutex priority inheritance. */
void os_task_change_prio(os_tcb_t *t, uint8_t new_prio)
{
    if (t->curr_prio == new_prio)
        return;

    switch (t->state) {
    case TS_READY:
        readyq_remove(t);
        t->curr_prio = new_prio;
        readyq_insert(t);
        break;
    case TS_BLOCKED:
        t->curr_prio = new_prio;
        if (t->wait_root) {            /* keep the wait list sorted */
            waitlist_remove(t->wait_root, t);
            waitlist_insert(t->wait_root, t);
        }
        break;
    default:                           /* RUNNING (or suspended): just set */
        t->curr_prio = new_prio;
        break;
    }
}

/* Does anyone in the ready queues outrank the current task? Used after an
 * operation that lowered g_curr's own priority (mutex de-boost). */
int os_preempt_needed(void)
{
    return g_ready_bitmap &&
           (uint8_t)__builtin_clz(g_ready_bitmap) < g_curr->curr_prio;
}

/* --------------------------------------------------------------- the pick
 * Called from PendSV_Handler (interrupts masked) after the outgoing task's
 * context is saved. Decides the incoming task, updates g_curr, returns it. */

os_tcb_t *os_sched_pick(void)
{
    os_tcb_t *out = g_curr;

    /* Stack canary: the deepest word of the stack must still hold the fill
     * pattern, and the saved sp must point inside the stack. Checked every
     * single switch, so an overflow is caught within one quantum. */
    if (out->stack_base[0] != OS_STACK_FILL ||
        out->sp < out->stack_base ||
        out->sp > out->stack_base + out->stack_words)
        os_panic("stack overflow in task '%s' (id %d)", out->name, out->id);

    if (out->state == TS_RUNNING) {          /* still runnable: requeue    */
        out->state = TS_READY;
        readyq_insert(out);
    } else if (out->state == TS_ZOMBIE) {    /* exited: recycle the slot   */
        out->state = TS_UNUSED;
        out->stack_base = 0;
    }
    /* BLOCKED and SUSPENDED tasks are already parked on their lists;
     * a task woken between blocking and switching is READY and queued. */

    OS_ASSERT(g_ready_bitmap != 0);          /* idle guarantees this       */

    os_tcb_t *in = g_readyq_head[highest_ready_prio()];
    readyq_remove(in);
    in->state = TS_RUNNING;
    in->nswitches++;
    in->slice_left = OS_TIME_SLICE_TICKS;

    g_curr = in;
    return in;
}

/* ------------------------------------------------------------- public API */

void os_yield(void)
{
    /* Requeue-at-tail happens naturally in os_sched_pick. */
    os_pend_switch();
}

void os_yield_from_isr(int need_yield)
{
    if (need_yield)
        os_pend_switch();
}

os_tcb_t *os_task_self(void)
{
    return g_curr;
}

uint32_t os_tick_count(void)
{
    return g_tick;
}

uint32_t os_idle_ticks(void)
{
    return g_idle_tcb ? g_idle_tcb->run_ticks : 0;
}

static void idle_fn(void *arg)
{
    (void)arg;
    for (;;)
        __asm__ volatile ("wfi");   /* sleep the CPU until the next interrupt */
}

void os_init(void)
{
    /* Pools live in .bss (already zero = TS_UNUSED); nothing else yet. */
}

void os_start(void)
{
    g_idle_tcb = os_task_create(idle_fn, 0, OS_IDLE_PRIO, "idle");
    OS_ASSERT(g_idle_tcb);
    os_timer_daemon_start();

    uint32_t key = irq_lock();
    os_tcb_t *first = g_readyq_head[highest_ready_prio()];
    readyq_remove(first);
    first->state = TS_RUNNING;
    first->nswitches = 1;
    first->slice_left = OS_TIME_SLICE_TICKS;
    g_curr = first;
    g_started = 1;
    irq_unlock(key);

    arch_start_first_task();
}
