/* Test 13: round-robin time slicing. Three equal-priority CPU hogs that
 * never yield must each make comparable progress -- only the slice rotation
 * in the tick handler can make that happen. */

#include "os.h"
#include "board.h"
#include "kprintf.h"

static volatile uint32_t counts[3];

static void hog_fn(void *arg)
{
    volatile uint32_t *c = &counts[(uintptr_t)arg];
    for (;;)
        (*c)++;
}

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

static void main_task(void *arg)
{
    (void)arg;

    for (uintptr_t i = 0; i < 3; i++)
        os_task_create(hog_fn, (void *)i, 8, "hog");

    os_sleep_ms(300);   /* ~10 slice rotations each at 10-tick quantum */

    uint32_t lo = counts[0], hi = counts[0];
    for (int i = 1; i < 3; i++) {
        if (counts[i] < lo) lo = counts[i];
        if (counts[i] > hi) hi = counts[i];
    }
    os_printf("hog counts: %lu %lu %lu\n", counts[0], counts[1], counts[2]);

    if (lo == 0)
        fail("a hog was starved completely (slicing broken)");
    /* Perfect fairness would be 1.0; allow 2x for slice-boundary noise. */
    if (hi > 2 * lo)
        fail("slicing grossly unfair");

    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
