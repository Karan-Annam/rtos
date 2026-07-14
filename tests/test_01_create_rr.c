/* Test 1: task creation + context switch fundamentals.
 *  - two equal-priority tasks yield back and forth -> strict A/B alternation
 *    proves PendSV save/restore and FIFO round-robin work
 *  - trampoline: a task function that plain returns must be reaped
 *  - TCB pool: allocate to exhaustion, verify the count, verify NULL after
 */

#include "os.h"
#include "board.h"
#include "kprintf.h"

#define PINGS 8

static char log_buf[2 * PINGS + 1];
static volatile int log_pos;

static void ping_fn(void *arg)
{
    char tag = (char)(uintptr_t)arg;
    for (int i = 0; i < PINGS; i++) {
        log_buf[log_pos++] = tag;
        os_yield();
    }
    /* return -> trampoline -> os_task_exit */
}

static void dummy_fn(void *arg)
{
    (void)arg;   /* exit immediately via return */
}

static void fail(const char *why)
{
    os_printf("FAIL: %s\n", why);
    os_printf("TEST FAIL\n");
    platform_exit(1);
}

static void main_task(void *arg)
{
    (void)arg;

    os_tcb_t *a = os_task_create(ping_fn, (void *)(uintptr_t)'A', 5, "pingA");
    os_tcb_t *b = os_task_create(ping_fn, (void *)(uintptr_t)'B', 5, "pingB");
    if (!a || !b)
        fail("task_create returned NULL");
    if (a->id == b->id)
        fail("two tasks share an id");

    /* We are prio 4 (higher). Sleep so A/B (prio 5) can ping-pong. */
    os_sleep_ms(50);

    if (log_pos != 2 * PINGS)
        fail("wrong number of log entries");
    for (int i = 0; i < 2 * PINGS; i++) {
        char want = (i % 2) ? 'B' : 'A';
        if (log_buf[i] != want)
            fail("round-robin order broken");
    }
    os_printf("interleave: %s\n", log_buf);

    /* Pool exhaustion: used now = main + idle + timer daemon (A/B already
     * exited and were reaped while we slept... unless reaping is broken,
     * which the count will expose). */
    int created = 0;
    os_tcb_t *extra;
    while ((extra = os_task_create(dummy_fn, 0, 30, "dummy")) != 0)
        created++;
    os_printf("filled pool with %d extra tasks\n", created);
    if (created != OS_MAX_TASKS - 3)
        fail("pool exhaustion count wrong (A/B not reaped?)");

    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
