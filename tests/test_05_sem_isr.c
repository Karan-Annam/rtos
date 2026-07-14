/* Test 5: talking to the kernel from a REAL interrupt handler. We trigger
 * NVIC IRQ 0 by software (set-pending register), and its ISR gives a
 * semaphore with the _from_isr variant + need_yield protocol. A
 * higher-priority task blocked on that semaphore must be running before the
 * interrupted task reaches its next statement. Also checks that blocking
 * calls from ISR context are rejected. */

#include "os.h"
#include "board.h"
#include "kprintf.h"
#include "armv7m.h"

static os_sem_t sem;
static volatile int isr_entered;
static volatile int isr_take_status = -1;
static volatile int waiter_ran;

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

/* Strong override of the weak IRQ0 vector from startup.c */
void IRQ0_Handler(void)
{
    int need_yield = 0;

    isr_entered++;
    /* Blocking from an ISR must be refused, not attempted. */
    isr_take_status = os_sem_take(&sem, 10);

    os_sem_give_from_isr(&sem, &need_yield);
    os_yield_from_isr(need_yield);     /* classic ISR-tail yield */
}

static void hi_waiter_fn(void *arg)
{
    (void)arg;
    if (os_sem_take(&sem, OS_WAIT_FOREVER) != OS_OK)
        fail("waiter take failed");
    waiter_ran = 1;
    os_task_exit();
}

static void main_task(void *arg)
{
    (void)arg;

    os_sem_init(&sem, 0, 4);
    nvic_set_prio(0, 0x40);
    nvic_enable_irq(0);

    /* Higher-priority waiter (prio 3 beats our 4) parks on the sem. */
    os_task_create(hi_waiter_fn, 0, 3, "hiwait");
    os_sleep_ticks(2);
    if (waiter_ran)
        fail("waiter ran before the give");

    /* Fire the interrupt. Chain: pend -> IRQ0 now -> give_from_isr wakes
     * prio-3 waiter -> need_yield -> PendSV on ISR exit -> waiter runs...
     * all BEFORE we execute another line. */
    nvic_pend_irq(0);
    arch_dsb();
    arch_isb();

    if (!isr_entered)
        fail("IRQ0 never fired");
    if (!waiter_ran)
        fail("ISR give did not preempt into the higher-prio waiter");
    if (isr_take_status != OS_ERR_ISR)
        fail("blocking take from ISR was not rejected with OS_ERR_ISR");

    /* give_from_isr with nobody waiting must land in the counter. */
    nvic_pend_irq(0);
    arch_dsb();
    arch_isb();
    if (isr_entered != 2)
        fail("second IRQ0 never fired");
    if (os_sem_take(&sem, OS_NO_WAIT) != OS_OK)
        fail("ISR give without waiter didn't increment the count");

    os_printf("ISR fired %d times, preemption chain worked\n", isr_entered);
    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
