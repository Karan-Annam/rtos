/* Test 3: sleep timing. Durations must be accurate to within one tick of
 * quantization, and multiple sleepers must wake in deadline order. */

#include "os.h"
#include "board.h"
#include "kprintf.h"

static char wake_log[4];
static volatile int wake_pos;

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

static void sleeper_fn(void *arg)
{
    /* arg packs tag and duration: tag in low byte, ticks in high bits */
    uint32_t v = (uint32_t)(uintptr_t)arg;
    os_sleep_ticks(v >> 8);
    wake_log[wake_pos++] = (char)(v & 0xFF);
}

static void main_task(void *arg)
{
    (void)arg;

    /* Duration accuracy: dt must be in [N, N+1] ticks. */
    uint32_t t0 = os_tick_count();
    os_sleep_ticks(25);
    uint32_t dt = os_tick_count() - t0;
    if (dt < 25 || dt > 26)
        fail("sleep_ticks(25) inaccurate");
    os_printf("sleep_ticks(25) took %lu ticks\n", dt);

    /* ms conversion (1 ms per tick at OS_TICK_HZ=1000) */
    t0 = os_tick_count();
    os_sleep_ms(50);
    dt = os_tick_count() - t0;
    if (dt < 50 || dt > 51)
        fail("sleep_ms(50) inaccurate");
    os_printf("sleep_ms(50) took %lu ticks\n", dt);

    /* Wake ordering: A sleeps 30, B 20, C 10 -> wake order must be C B A,
     * regardless of creation order. */
    os_task_create(sleeper_fn, (void *)(uintptr_t)((30u << 8) | 'A'), 5, "slA");
    os_task_create(sleeper_fn, (void *)(uintptr_t)((20u << 8) | 'B'), 5, "slB");
    os_task_create(sleeper_fn, (void *)(uintptr_t)((10u << 8) | 'C'), 5, "slC");
    os_sleep_ticks(50);

    if (wake_pos != 3)
        fail("not all sleepers woke");
    if (wake_log[0] != 'C' || wake_log[1] != 'B' || wake_log[2] != 'A')
        fail("sleepers woke in wrong order");
    os_printf("wake order: %c%c%c\n", wake_log[0], wake_log[1], wake_log[2]);

    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
