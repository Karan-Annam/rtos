/* Test 12: task lifecycle -- suspend freezes, resume thaws, suspending a
 * blocked task aborts its wait, delete recycles, and the state-machine
 * rejects nonsense transitions. */

#include "os.h"
#include "board.h"
#include "kprintf.h"

static volatile uint32_t spin_count;
static volatile uint32_t sleeper_wake_tick;
static volatile int self_susp_flag;

static void spinner_fn(void *arg)
{
    (void)arg;
    for (;;)
        spin_count++;
}

static void long_sleeper_fn(void *arg)
{
    (void)arg;
    os_sleep_ticks(5000);              /* way past the test's lifetime */
    sleeper_wake_tick = os_tick_count();
    os_task_exit();
}

static void self_susp_fn(void *arg)
{
    (void)arg;
    os_task_suspend(os_task_self());   /* park until someone resumes us */
    self_susp_flag = 1;
}

static void parked_fn(void *arg)
{
    (void)arg;
    for (;;)
        os_sleep_ticks(1000);
}

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

static void main_task(void *arg)
{
    (void)arg;

    /* --- suspend freezes a runnable task, resume revives it --- */
    os_tcb_t *sp = os_task_create(spinner_fn, 0, 10, "spin");
    os_sleep_ms(10);
    if (spin_count == 0)
        fail("spinner never ran");
    if (os_task_suspend(sp) != OS_OK)
        fail("suspend(READY) failed");
    uint32_t a = spin_count;
    os_sleep_ms(20);
    if (spin_count != a)
        fail("suspended task still running");
    if (os_task_resume(sp) != OS_OK)
        fail("resume failed");
    os_sleep_ms(10);
    if (spin_count == a)
        fail("resumed task not running");

    /* --- state machine sanity --- */
    if (os_task_resume(sp) != OS_ERR_STATE)
        fail("resume of non-suspended task should be rejected");

    /* --- suspending a BLOCKED task aborts the wait --- */
    uint32_t t0 = os_tick_count();
    os_tcb_t *sl = os_task_create(long_sleeper_fn, 0, 5, "sleeper");
    os_sleep_ticks(5);                 /* let it block */
    if (os_task_suspend(sl) != OS_OK)
        fail("suspend(BLOCKED) failed");
    if (os_task_resume(sl) != OS_OK)
        fail("resume of blocked-then-suspended failed");
    os_sleep_ticks(5);                 /* it's prio 5 > us? no: 5 beats 4? lower number wins; we are 4, it is 5: give it time */
    if (sleeper_wake_tick == 0)
        fail("aborted sleep did not return");
    if (sleeper_wake_tick - t0 > 100)
        fail("aborted sleep still slept the full duration");
    os_printf("5000-tick sleep aborted after %lu ticks\n",
              sleeper_wake_tick - t0);

    /* --- self-suspend + external resume --- */
    os_tcb_t *ss = os_task_create(self_susp_fn, 0, 3, "selfsusp");
    /* prio 3 beats us: it has already run and self-suspended by now */
    if (self_susp_flag)
        fail("self-suspend didn't stop the task");
    os_task_resume(ss);                /* prio 3 preempts us immediately */
    if (!self_susp_flag)
        fail("resumed self-suspender didn't continue");

    /* --- delete + slot recycling --- */
    os_tcb_t *victim = os_task_create(parked_fn, 0, 20, "victim");
    if (!victim)
        fail("create victim failed");
    int vid = victim->id;
    os_sleep_ticks(5);                 /* let it block in sleep */
    if (os_task_delete(victim) != OS_OK)
        fail("delete failed");
    if (os_task_by_index(vid) != 0)
        fail("deleted task still visible");
    if (!os_task_create(parked_fn, 0, 20, "recycled"))
        fail("could not reuse deleted slot");

    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
