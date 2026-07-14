/* Test 10: software timers -- one-shot fires exactly once at the right time,
 * periodic fires repeatedly until stopped, callbacks run in the timer
 * daemon's TASK context (not the ISR), and restart re-arms. */

#include <string.h>
#include "os.h"
#include "board.h"
#include "kprintf.h"

static os_timer_t t_once, t_perio;
static volatile uint32_t once_count, perio_count;
static volatile uint32_t once_fired_at;
static volatile int cb_in_isr = -1;
static char cb_taskname[8];

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

static void once_cb(os_timer_t *t, void *arg)
{
    (void)t; (void)arg;
    once_count++;
    once_fired_at = os_tick_count();

    /* Prove we're a task, not an ISR. */
    uint32_t ipsr;
    __asm__ volatile ("mrs %0, ipsr" : "=r"(ipsr));
    cb_in_isr = ipsr != 0;
    strncpy(cb_taskname, os_task_self()->name, sizeof cb_taskname - 1);
}

static void perio_cb(os_timer_t *t, void *arg)
{
    (void)t; (void)arg;
    perio_count++;
}

static void main_task(void *arg)
{
    (void)arg;

    os_timer_init(&t_once, "once", once_cb, 0);
    os_timer_init(&t_perio, "perio", perio_cb, 0);

    /* --- one-shot: fires once, on time, never again --- */
    uint32_t t0 = os_tick_count();
    if (os_timer_start(&t_once, 25, 0) != OS_OK)
        fail("one-shot start failed");
    os_sleep_ticks(80);
    if (once_count != 1)
        fail("one-shot fired wrong number of times");
    uint32_t dt = once_fired_at - t0;
    if (dt < 25 || dt > 28)
        fail("one-shot fired at the wrong time");
    os_printf("one-shot fired at +%lu ticks (asked 25)\n", dt);

    if (cb_in_isr != 0)
        fail("callback ran in interrupt context");
    if (strcmp(cb_taskname, "tmrsvc") != 0)
        fail("callback did not run in the timer daemon task");

    /* --- periodic: ~1 fire per 10 ticks --- */
    if (os_timer_start(&t_perio, 10, 1) != OS_OK)
        fail("periodic start failed");
    os_sleep_ticks(100);
    uint32_t fired = perio_count;
    if (fired < 8 || fired > 12)
        fail("periodic fire count out of range");
    os_printf("periodic fired %lu times in 100 ticks (period 10)\n", fired);

    /* --- stop freezes it --- */
    if (os_timer_stop(&t_perio) != OS_OK)
        fail("stop failed");
    uint32_t frozen = perio_count;
    os_sleep_ticks(50);
    if (perio_count != frozen)
        fail("stopped timer kept firing");

    /* --- restart re-arms a one-shot --- */
    once_count = 0;
    os_timer_start(&t_once, 100, 0);
    os_sleep_ticks(10);
    os_timer_start(&t_once, 15, 0);     /* restart with a shorter deadline */
    os_sleep_ticks(40);
    if (once_count != 1)
        fail("restarted one-shot misfired");

    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
