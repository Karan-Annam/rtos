/* Test 2: preemption. A low-priority task that NEVER yields must still lose
 * the CPU, both when a higher-priority task is created and when one wakes
 * from sleep (tick preemption). */

#include "os.h"
#include "board.h"
#include "kprintf.h"

static volatile uint32_t spin_count;
static volatile int instant_flag;

static void spinner_fn(void *arg)
{
    (void)arg;
    for (;;)
        spin_count++;      /* no yield, no sleep: pure CPU hog */
}

static void instant_fn(void *arg)
{
    (void)arg;
    instant_flag = 1;      /* runs the moment it's created (higher prio) */
}

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

static void main_task(void *arg)
{
    (void)arg;

    os_task_create(spinner_fn, 0, 10, "spin");

    /* Creating a higher-priority task must preempt us before the next
     * statement executes. */
    os_task_create(instant_fn, 0, 2, "instant");
    if (!instant_flag)
        fail("create-time preemption didn't happen");

    /* While we sleep, the prio-10 spinner owns the CPU and never yields.
     * That we wake up AT ALL proves the tick preempts it. */
    os_sleep_ms(20);
    uint32_t c1 = spin_count;
    os_sleep_ms(20);
    uint32_t c2 = spin_count;

    if (c1 == 0)
        fail("spinner never ran");
    if (c2 <= c1)
        fail("spinner made no progress between sleeps");

    os_printf("spinner progressed %lu -> %lu without ever yielding\n", c1, c2);
    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
