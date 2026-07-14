/* Test 7: priority inheritance -- the Mars Pathfinder scenario.
 *
 *   L (prio 20) takes the mutex, then does CPU-bound "work" in its critical
 *     section (spins, never yields).
 *   M (prio 10) is a compute hog that would normally starve L forever.
 *   H (prio 5)  needs the mutex.
 *
 * Without inheritance: H blocks on L, but L never runs again because M
 * outranks it -- H is effectively behind M. Deadlock-by-starvation; this
 * test would hang and the runner's timeout would fail it.
 *
 * With inheritance: the moment H blocks, L borrows priority 5, out-ranks M,
 * finishes its critical section, and hands the lock to H. We check the boost
 * is visible, the handoff happens, and L's priority is restored after. */

#include "os.h"
#include "board.h"
#include "kprintf.h"

static os_mutex_t mtx;
static volatile int l_in_cs;
static volatile int l_saw_boost;
static volatile int l_prio_after = -1;
static volatile int h_has_lock;
static volatile int stop_m;
static volatile uint32_t m_count;

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

static void low_fn(void *arg)
{
    (void)arg;
    os_mutex_lock(&mtx, OS_WAIT_FOREVER);
    l_in_cs = 1;

    /* CPU-bound critical section: run until we notice we've been boosted.
     * We only get CPU time past M's spinning if inheritance lifted us. */
    while (os_task_self()->curr_prio != 5)
        ;
    l_saw_boost = 1;

    os_mutex_unlock(&mtx);
    l_prio_after = os_task_self()->curr_prio;
    os_task_exit();
}

static void mid_fn(void *arg)
{
    (void)arg;
    while (!stop_m)
        m_count++;
    os_task_exit();
}

static void high_fn(void *arg)
{
    (void)arg;
    if (os_mutex_lock(&mtx, OS_WAIT_FOREVER) != OS_OK)
        fail("H lock failed");
    h_has_lock = 1;
    os_mutex_unlock(&mtx);
    os_task_exit();
}

static void main_task(void *arg)
{
    (void)arg;

    os_mutex_init(&mtx);

    os_tcb_t *ltcb = os_task_create(low_fn, 0, 20, "L");
    os_sleep_ticks(5);                 /* L locks and enters its spin */
    if (!l_in_cs)
        fail("L never entered its critical section");

    os_task_create(mid_fn, 0, 10, "M");   /* the starver */
    os_sleep_ticks(5);
    if (l_saw_boost)
        fail("L saw a boost before H even existed");

    os_task_create(high_fn, 0, 5, "H");   /* blocks on the mutex -> boosts L */
    os_sleep_ticks(20);

    if (!l_saw_boost)
        fail("L was never boosted (starved behind M): no inheritance");
    if (!h_has_lock)
        fail("H never obtained the mutex");
    /* L is starved behind M again right now (that's correct: it's back at
     * prio 20), so peek at its TCB rather than waiting for it to run. */
    if (!ltcb || ltcb->curr_prio != 20)
        fail("L's priority was not restored after unlock");

    /* Stop the hog so L can actually finish and report from its own context. */
    stop_m = 1;
    os_sleep_ticks(5);
    if (l_prio_after != 20)
        fail("L never resumed after de-boost");

    os_printf("L boosted 20->5, handed off, restored to %d; M spun %lu\n",
              l_prio_after, m_count);
    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
