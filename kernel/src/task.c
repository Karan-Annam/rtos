/* Task allocation and lifecycle.
 *
 * All memory is two static pools sized at compile time -- one of TCBs, one
 * of stacks -- bound 1:1 by index: task id = TCB index = stack index.
 * Nothing is ever malloc'd; "allocation" is finding an UNUSED slot, and
 * "freeing" is marking it UNUSED again. */

#include "os_internal.h"

static os_tcb_t g_tcbs[OS_MAX_TASKS];
__attribute__((aligned(8)))   /* AAPCS: stack tops must be 8-byte aligned */
static uint32_t g_stacks[OS_MAX_TASKS][OS_STACK_WORDS];

/* Scan the pool for a free TCB, bind it to its stack, return it claimed.
 * Runs under irq_lock so two tasks can't grab the same slot. */
static os_tcb_t *tcb_alloc(void)
{
    for (int i = 0; i < OS_MAX_TASKS; i++) {
        if (g_tcbs[i].state == TS_UNUSED) {
            os_tcb_t *t = &g_tcbs[i];
            t->id = (uint8_t)i;
            t->stack_base = g_stacks[i];
            t->stack_words = OS_STACK_WORDS;
            return t;
        }
    }
    return 0;
}

os_tcb_t *os_task_create(os_task_fn_t fn, void *arg, uint8_t prio,
                         const char *name)
{
    if (!fn || prio >= OS_MAX_PRIOS)
        return 0;

    uint32_t key = irq_lock();

    os_tcb_t *t = tcb_alloc();
    if (!t) {
        irq_unlock(key);
        return 0;
    }

    /* Fill the whole stack with the canary pattern: word [0] is the
     * overflow tripwire, and the high-water mark scan (how deep did this
     * task ever go?) looks for the first overwritten word. */
    for (uint32_t i = 0; i < t->stack_words; i++)
        t->stack_base[i] = OS_STACK_FILL;

    t->fn = fn;
    t->arg = arg;
    t->name = name ? name : "?";
    t->base_prio = t->curr_prio = prio;
    t->qnext = t->qprev = 0;
    t->tnext = t->tprev = 0;
    t->wait_root = 0;
    t->wait_obj = 0;
    t->wait_data = 0;
    t->on_timer_list = 0;
    t->run_ticks = 0;
    t->nswitches = 0;

    t->sp = arch_stack_init(t->stack_base + t->stack_words, fn, arg);

    t->state = TS_READY;
    readyq_insert(t);

    /* A newborn that outranks the current task runs immediately. */
    int preempt = g_started && prio < g_curr->curr_prio;
    irq_unlock(key);
    if (preempt)
        os_pend_switch();

    return t;
}

void os_task_exit(void)
{
    irq_lock();
    /* Can't recycle the TCB yet: we are still running on this task's stack
     * until PendSV switches away. ZOMBIE defers the recycle to the exact
     * moment in os_sched_pick where the context save has already happened. */
    g_curr->state = TS_ZOMBIE;
    os_pend_switch();
    __asm__ volatile ("cpsie i");
    for (;;)
        ;                       /* PendSV takes us; never reached */
}

os_status_t os_task_delete(os_tcb_t *t)
{
    if (!t)
        return OS_ERR_PARAM;
    if (t == g_curr)
        os_task_exit();         /* self-delete: no return */

    uint32_t key = irq_lock();
    switch (t->state) {
    case TS_READY:
        readyq_remove(t);
        break;
    case TS_BLOCKED:
        if (t->wait_root)
            waitlist_remove(t->wait_root, t);
        if (t->on_timer_list)
            timer_list_remove(t);
        break;
    case TS_SUSPENDED:
        break;
    default:
        irq_unlock(key);
        return OS_ERR_STATE;
    }
    t->state = TS_UNUSED;
    t->stack_base = 0;
    irq_unlock(key);
    return OS_OK;
}

os_status_t os_task_suspend(os_tcb_t *t)
{
    if (!t)
        return OS_ERR_PARAM;

    uint32_t key = irq_lock();
    int self = (t == g_curr);

    switch (t->state) {
    case TS_RUNNING:            /* suspending yourself */
        t->state = TS_SUSPENDED;
        os_pend_switch();
        break;
    case TS_READY:
        readyq_remove(t);
        t->state = TS_SUSPENDED;
        break;
    case TS_BLOCKED:
        /* Suspending a waiting task aborts the wait: it leaves the object's
         * wait list now and its blocking call returns OS_TIMEOUT after the
         * eventual resume. (Documented behavior.) */
        if (t->wait_root) {
            waitlist_remove(t->wait_root, t);
            t->wait_root = 0;
        }
        if (t->on_timer_list)
            timer_list_remove(t);
        t->wake_status = OS_TIMEOUT;
        t->state = TS_SUSPENDED;
        break;
    default:
        irq_unlock(key);
        return OS_ERR_STATE;
    }

    irq_unlock(key);
    (void)self;
    return OS_OK;
}

os_status_t os_task_resume(os_tcb_t *t)
{
    if (!t)
        return OS_ERR_PARAM;

    uint32_t key = irq_lock();
    if (t->state != TS_SUSPENDED) {
        irq_unlock(key);
        return OS_ERR_STATE;
    }
    t->state = TS_READY;
    readyq_insert(t);
    int preempt = g_started && t->curr_prio < g_curr->curr_prio;
    irq_unlock(key);

    if (preempt)
        os_pend_switch();
    return OS_OK;
}

/* ---------------------------------------------------------- introspection */

os_tcb_t *os_task_by_index(int idx)
{
    if (idx < 0 || idx >= OS_MAX_TASKS)
        return 0;
    return g_tcbs[idx].state != TS_UNUSED ? &g_tcbs[idx] : 0;
}

uint32_t os_stack_high_water(os_tcb_t *t)
{
    if (!t || !t->stack_base)
        return 0;
    uint32_t i = 0;
    while (i < t->stack_words && t->stack_base[i] == OS_STACK_FILL)
        i++;
    return t->stack_words - i;   /* words ever used */
}
