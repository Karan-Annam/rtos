/* Test 4: semaphores -- counting behavior, blocking take, timeout accuracy,
 * priority-ordered wakeup, and the direct-handoff guarantee (a give with
 * waiters transfers the token; it never lands in the counter where a third
 * party could steal it). */

#include "os.h"
#include "board.h"
#include "kprintf.h"

static os_sem_t sem;
static char wake_log[4];
static volatile int wake_pos;
static volatile int handoff_got;

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

static void waiter_fn(void *arg)
{
    if (os_sem_take(&sem, OS_WAIT_FOREVER) != OS_OK)
        fail("waiter take failed");
    wake_log[wake_pos++] = (char)(uintptr_t)arg;
}

static void handoff_fn(void *arg)
{
    (void)arg;
    if (os_sem_take(&sem, OS_WAIT_FOREVER) != OS_OK)
        fail("handoff waiter take failed");
    handoff_got = 1;
}

static void main_task(void *arg)
{
    (void)arg;

    /* --- counting basics, no blocking --- */
    os_sem_init(&sem, 2, 5);
    if (os_sem_take(&sem, OS_NO_WAIT) != OS_OK) fail("take 1 of 2");
    if (os_sem_take(&sem, OS_NO_WAIT) != OS_OK) fail("take 2 of 2");
    if (os_sem_take(&sem, OS_NO_WAIT) != OS_TIMEOUT)
        fail("take of empty sem with NO_WAIT should time out");

    /* --- max clamp (binary-style) --- */
    os_sem_init(&sem, 0, 1);
    if (os_sem_give(&sem) != OS_OK) fail("give to 1");
    if (os_sem_give(&sem) != OS_ERR_STATE)
        fail("give past max should report OS_ERR_STATE");

    /* --- timeout accuracy on a blocking take --- */
    os_sem_init(&sem, 0, 1);
    uint32_t t0 = os_tick_count();
    if (os_sem_take(&sem, 20) != OS_TIMEOUT)
        fail("timed take of empty sem should time out");
    uint32_t dt = os_tick_count() - t0;
    if (dt < 20 || dt > 21)
        fail("take timeout inaccurate");
    os_printf("timed take expired after %lu ticks\n", dt);

    /* --- blocking take, then give: priority order of wakeups ---
     * B (prio 6) blocks first, A (prio 5) second; gives must wake A then B
     * (priority beats FIFO). */
    os_sem_init(&sem, 0, 2);
    os_task_create(waiter_fn, (void *)(uintptr_t)'B', 6, "waitB");
    os_sleep_ticks(2);
    os_task_create(waiter_fn, (void *)(uintptr_t)'A', 5, "waitA");
    os_sleep_ticks(2);                 /* both blocked now */
    os_sem_give(&sem);
    os_sleep_ticks(2);
    os_sem_give(&sem);
    os_sleep_ticks(2);
    if (wake_pos != 2 || wake_log[0] != 'A' || wake_log[1] != 'B')
        fail("semaphore wakeups not in priority order");
    os_printf("wake order: %c then %c\n", wake_log[0], wake_log[1]);

    /* --- direct handoff: give with a waiter must NOT pass through count ---
     * The waiter is prio 5 (below us at 4), so after our give it is READY
     * but hasn't run. If give had incremented the counter, our NO_WAIT take
     * here would steal its token. */
    os_sem_init(&sem, 0, 1);
    os_task_create(handoff_fn, 0, 5, "handoff");
    os_sleep_ticks(2);                 /* let it block */
    os_sem_give(&sem);
    if (os_sem_take(&sem, OS_NO_WAIT) != OS_TIMEOUT)
        fail("token was stealable: no direct handoff");
    os_sleep_ticks(2);                 /* now the waiter runs */
    if (!handoff_got)
        fail("handoff waiter never got the token");

    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
