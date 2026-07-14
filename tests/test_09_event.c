/* Test 9: event flag groups -- ANY vs ALL semantics, consume-on-wake
 * (OS_EVT_CLEAR), priority-ordered consumption, and timeouts. */

#include "os.h"
#include "board.h"
#include "kprintf.h"

static os_event_t evt;
static volatile uint32_t any_got, all_got;
static volatile int any_woke, all_woke;
static char clear_log[4];
static volatile int clear_pos;

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

static void any_waiter_fn(void *arg)
{
    (void)arg;
    uint32_t got = 0;
    if (os_event_wait(&evt, 0x3, OS_EVT_ANY, &got, OS_WAIT_FOREVER) != OS_OK)
        fail("ANY wait failed");
    any_got = got;
    any_woke = 1;
    os_task_exit();
}

static void all_waiter_fn(void *arg)
{
    (void)arg;
    uint32_t got = 0;
    if (os_event_wait(&evt, 0x5, OS_EVT_ALL, &got, OS_WAIT_FOREVER) != OS_OK)
        fail("ALL wait failed");
    all_got = got;
    all_woke = 1;
    os_task_exit();
}

static void clear_waiter_fn(void *arg)
{
    if (os_event_wait(&evt, 0x100, OS_EVT_ANY | OS_EVT_CLEAR, 0,
                      OS_WAIT_FOREVER) != OS_OK)
        fail("CLEAR waiter failed");
    clear_log[clear_pos++] = (char)(uintptr_t)arg;
    os_task_exit();
}

static void main_task(void *arg)
{
    (void)arg;

    os_event_init(&evt);

    /* --- ANY: one bit of the mask suffices; got reports which --- */
    os_task_create(any_waiter_fn, 0, 5, "any");
    os_sleep_ticks(2);
    os_event_set(&evt, 0x2);
    os_sleep_ticks(2);
    if (!any_woke || any_got != 0x2)
        fail("ANY semantics broken");

    /* --- ALL: partial set must NOT wake --- */
    os_event_clear(&evt, 0xFFFFFFFF);
    os_task_create(all_waiter_fn, 0, 5, "all");
    os_sleep_ticks(2);
    os_event_set(&evt, 0x1);            /* half the mask */
    os_sleep_ticks(2);
    if (all_woke)
        fail("ALL woke on a partial mask");
    os_event_set(&evt, 0x4);            /* completes 0x5 */
    os_sleep_ticks(2);
    if (!all_woke || all_got != 0x5)
        fail("ALL semantics broken");

    /* --- without CLEAR, flags persist: an immediate wait succeeds --- */
    uint32_t got = 0;
    if (os_event_wait(&evt, 0x1, OS_EVT_ANY, &got, OS_NO_WAIT) != OS_OK)
        fail("persistent flag should satisfy immediately");

    /* --- CLEAR consumes, in priority order: two waiters on the same bit,
     * one set() -> only the higher-priority one wakes --- */
    os_event_clear(&evt, 0xFFFFFFFF);
    os_task_create(clear_waiter_fn, (void *)(uintptr_t)'L', 6, "clrL");
    os_task_create(clear_waiter_fn, (void *)(uintptr_t)'H', 5, "clrH");
    os_sleep_ticks(2);
    os_event_set(&evt, 0x100);
    os_sleep_ticks(2);
    if (clear_pos != 1 || clear_log[0] != 'H')
        fail("CLEAR should wake exactly the highest-priority waiter");
    if (evt.flags & 0x100)
        fail("CLEAR did not consume the bit");
    os_event_set(&evt, 0x100);          /* now the low one */
    os_sleep_ticks(2);
    if (clear_pos != 2 || clear_log[1] != 'L')
        fail("second CLEAR wake broken");

    /* --- timeout --- */
    uint32_t t0 = os_tick_count();
    if (os_event_wait(&evt, 0x8000, OS_EVT_ANY, 0, 20) != OS_TIMEOUT)
        fail("event wait should have timed out");
    uint32_t dt = os_tick_count() - t0;
    if (dt < 20 || dt > 21)
        fail("event timeout inaccurate");

    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
