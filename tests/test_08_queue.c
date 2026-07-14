/* Test 8: message queues -- FIFO integrity, blocking on full (producer) and
 * empty (consumer), direct handoff, and timeouts in both directions. */

#include "os.h"
#include "board.h"
#include "kprintf.h"

#define CAP    4
#define BURST  8

static os_queue_t q;
static uint32_t   q_storage[CAP];
static volatile int producer_done;
static volatile int handoff_val;

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

static void producer_fn(void *arg)
{
    (void)arg;
    /* Send BURST items into a CAP-4 queue: sends 5..8 must block until the
     * consumer drains. Every send must still report OS_OK. */
    for (uint32_t i = 1; i <= BURST; i++) {
        if (os_queue_send(&q, &i, OS_WAIT_FOREVER) != OS_OK)
            fail("producer send failed");
    }
    producer_done = 1;
    os_task_exit();
}

static void handoff_recv_fn(void *arg)
{
    (void)arg;
    uint32_t v = 0;
    if (os_queue_recv(&q, &v, OS_WAIT_FOREVER) != OS_OK)
        fail("handoff recv failed");
    handoff_val = (int)v;
    os_task_exit();
}

static void main_task(void *arg)
{
    (void)arg;

    os_queue_init(&q, q_storage, sizeof(uint32_t), CAP);

    /* --- non-blocking basics + FIFO order --- */
    for (uint32_t i = 10; i < 14; i++)
        if (os_queue_send(&q, &i, OS_NO_WAIT) != OS_OK)
            fail("send to non-full queue failed");
    uint32_t v;
    if (os_queue_send(&q, &v, OS_NO_WAIT) != OS_TIMEOUT)
        fail("send to full queue with NO_WAIT should time out");
    for (uint32_t i = 10; i < 14; i++) {
        if (os_queue_recv(&q, &v, OS_NO_WAIT) != OS_OK || v != i)
            fail("FIFO order broken");
    }
    if (os_queue_recv(&q, &v, OS_NO_WAIT) != OS_TIMEOUT)
        fail("recv from empty queue with NO_WAIT should time out");

    /* --- producer blocks on full; all data arrives exactly once, in order */
    os_task_create(producer_fn, 0, 5, "prod");
    os_sleep_ticks(5);              /* producer fills 4, blocks on the 5th */
    if (producer_done)
        fail("producer finished without a consumer (never blocked)");
    for (uint32_t i = 1; i <= BURST; i++) {
        if (os_queue_recv(&q, &v, 100) != OS_OK)
            fail("consumer recv failed");
        if (v != i)
            fail("data lost or reordered across the blocking boundary");
        os_sleep_ticks(1);          /* let the producer refill */
    }
    os_sleep_ticks(2);
    if (!producer_done)
        fail("producer still stuck after full drain");

    /* --- consumer blocks on empty; send hands off directly --- */
    os_task_create(handoff_recv_fn, 0, 5, "hrecv");
    os_sleep_ticks(2);              /* it blocks on the empty queue */
    uint32_t magic = 0xBEEF;
    if (os_queue_send(&q, &magic, OS_NO_WAIT) != OS_OK)
        fail("handoff send failed");
    os_sleep_ticks(2);
    if (handoff_val != 0xBEEF)
        fail("direct handoff to blocked receiver failed");
    if (q.count != 0)
        fail("handoff should bypass the ring buffer");

    /* --- recv timeout accuracy --- */
    uint32_t t0 = os_tick_count();
    if (os_queue_recv(&q, &v, 20) != OS_TIMEOUT)
        fail("timed recv on empty queue should expire");
    uint32_t dt = os_tick_count() - t0;
    if (dt < 20 || dt > 21)
        fail("recv timeout inaccurate");

    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
