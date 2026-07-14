/* Test 11: stack overflow detection. A task recurses to build a genuinely
 * deep stack (raising its high-water mark), then smashes the canary word at
 * the bottom of its own stack -- exactly the word a real overflow would hit
 * first. The kernel must catch it at the next context switch and panic,
 * naming the guilty task.
 *
 * NOTE: this test PASSES by panicking. The runner expects the string
 * "stack overflow in task" and a nonzero exit instead of "TEST PASS". */

#include "os.h"
#include "board.h"
#include "kprintf.h"

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

/* Non-tail recursion with a live buffer per frame; depth counted so we go
 * deep but stay inside the stack. */
static uint32_t recurse(volatile uint32_t depth)
{
    volatile uint32_t pad[8];
    pad[0] = depth;
    if (depth == 0)
        return pad[0];
    return recurse(depth - 1) + pad[0];
}

static void overflower_fn(void *arg)
{
    (void)arg;
    os_tcb_t *self = os_task_self();

    recurse(12);                        /* ~12 frames of real depth */
    uint32_t hwm = os_stack_high_water(self);
    os_printf("high-water mark after recursion: %lu/%lu words\n",
              hwm, self->stack_words);
    if (hwm < 40)
        fail("recursion did not register on the high-water mark");

    /* The fatal write a real overflow would make first: */
    ((volatile uint32_t *)self->stack_base)[0] = 0xDEADBEEF;

    os_printf("canary smashed, waiting for the kernel to notice...\n");
    for (;;)
        ;                               /* next tick's switch checks canaries */
}

static void main_task(void *arg)
{
    (void)arg;
    os_task_create(overflower_fn, 0, 5, "victim");
    os_sleep_ms(100);
    /* If we get here, the canary check never fired. */
    fail("kernel failed to detect the smashed canary");
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
