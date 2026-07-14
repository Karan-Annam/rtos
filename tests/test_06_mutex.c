/* Test 6: mutex ownership rules -- only the owner unlocks, no recursion,
 * blocked lockers time out accurately, and unlock hands ownership directly
 * to the highest-priority waiter. */

#include "os.h"
#include "board.h"
#include "kprintf.h"

static os_mutex_t mtx;
static volatile int b_got_lock;
static volatile int helper_result = -1;
static volatile int timed_result = -1;
static volatile uint32_t timed_dt;

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

static void wrong_unlocker_fn(void *arg)
{
    (void)arg;
    helper_result = os_mutex_unlock(&mtx);   /* we don't own it */
}

static void blocked_locker_fn(void *arg)
{
    (void)arg;
    if (os_mutex_lock(&mtx, OS_WAIT_FOREVER) != OS_OK)
        fail("blocked locker failed");
    b_got_lock = 1;
    os_mutex_unlock(&mtx);
}

static void timed_locker_fn(void *arg)
{
    (void)arg;
    uint32_t t0 = os_tick_count();
    timed_result = os_mutex_lock(&mtx, 20);  /* main owns it: must expire */
    timed_dt = os_tick_count() - t0;
}

static void main_task(void *arg)
{
    (void)arg;

    os_mutex_init(&mtx);

    /* --- basics --- */
    if (os_mutex_lock(&mtx, OS_NO_WAIT) != OS_OK)
        fail("lock of free mutex failed");
    if (os_mutex_lock(&mtx, OS_NO_WAIT) != OS_ERR_STATE)
        fail("recursive lock should be rejected");

    /* --- unlock by non-owner rejected (prio 3 helper runs immediately) --- */
    os_task_create(wrong_unlocker_fn, 0, 3, "wrong");
    if (helper_result != OS_ERR_OWNER)
        fail("unlock by non-owner should return OS_ERR_OWNER");

    /* --- timed lock on a held mutex expires on schedule --- */
    os_task_create(timed_locker_fn, 0, 5, "timed");
    os_sleep_ticks(30);                 /* longer than its 20-tick patience */
    if (timed_result != OS_TIMEOUT)
        fail("timed lock should have expired with OS_TIMEOUT");
    if (timed_dt < 20 || timed_dt > 21)
        fail("lock timeout inaccurate");
    os_printf("timed lock expired after %lu ticks\n", timed_dt);

    /* --- unlock hands ownership to a waiter --- */
    os_task_create(blocked_locker_fn, 0, 5, "blocked");
    os_sleep_ticks(2);                  /* it blocks on the mutex */
    if (b_got_lock)
        fail("waiter acquired a held mutex");
    if (os_mutex_unlock(&mtx) != OS_OK)
        fail("owner unlock failed");
    os_sleep_ticks(2);                  /* waiter runs, unlocks, exits */
    if (!b_got_lock)
        fail("waiter did not receive ownership on unlock");

    /* --- unlock when nobody owns it --- */
    if (os_mutex_unlock(&mtx) != OS_ERR_OWNER)
        fail("unlock of unowned mutex should be rejected");

    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
