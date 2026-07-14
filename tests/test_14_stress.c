/* Test 14: stress -- everything at once, checked exactly.
 *
 * Five workers at mixed priorities each do 50 iterations of: pseudo-random
 * sleep, mutex-protected counter increment, queue send. A collector consumes
 * all 250 messages and signals completion via an event flag. Time slicing,
 * preemption, blocking sends/recvs, and lock contention all overlap here.
 *
 * The checks are exact, not statistical: every message accounted for, every
 * increment present -- any lost wakeup, double-delivery, or race corrupts a
 * total and fails the test. */

#include "os.h"
#include "board.h"
#include "kprintf.h"

#define WORKERS  5
#define ITERS    50
#define TOTAL    (WORKERS * ITERS)

static os_queue_t q;
static uint32_t   q_buf[8];
static os_mutex_t mtx;
static os_event_t done_evt;

static volatile uint32_t protected_count;
static uint32_t per_worker[WORKERS];
static volatile uint32_t seq_sum;
static volatile int collector_ok = -1;

static const uint8_t worker_prio[WORKERS] = { 5, 5, 6, 6, 7 };

static void fail(const char *why)
{
    os_printf("FAIL: %s\nTEST FAIL\n", why);
    platform_exit(1);
}

static void worker_fn(void *arg)
{
    uint32_t id = (uint32_t)(uintptr_t)arg;
    uint32_t rng = id * 12345u + 1u;

    for (uint32_t i = 0; i < ITERS; i++) {
        rng = rng * 1103515245u + 12345u;
        os_sleep_ticks(1 + ((rng >> 16) & 0x3));

        if (os_mutex_lock(&mtx, OS_WAIT_FOREVER) != OS_OK)
            fail("worker lock failed");
        protected_count++;              /* race-detector: must total exactly */
        os_mutex_unlock(&mtx);

        uint32_t msg = (id << 16) | i;
        if (os_queue_send(&q, &msg, OS_WAIT_FOREVER) != OS_OK)
            fail("worker send failed");
    }
    os_task_exit();
}

static void collector_fn(void *arg)
{
    (void)arg;
    for (int n = 0; n < TOTAL; n++) {
        uint32_t msg;
        if (os_queue_recv(&q, &msg, 2000) != OS_OK) {
            collector_ok = 0;
            os_event_set(&done_evt, 0x1);
            os_task_exit();
        }
        uint32_t id = msg >> 16;
        if (id < WORKERS)
            per_worker[id]++;
        seq_sum += msg & 0xFFFF;
    }
    collector_ok = 1;
    os_event_set(&done_evt, 0x1);
    os_task_exit();
}

static void main_task(void *arg)
{
    (void)arg;

    os_queue_init(&q, q_buf, sizeof(uint32_t), 8);
    os_mutex_init(&mtx);
    os_event_init(&done_evt);

    os_task_create(collector_fn, 0, 3, "collect");
    for (uintptr_t i = 0; i < WORKERS; i++)
        os_task_create(worker_fn, (void *)i, worker_prio[i], "worker");

    if (os_event_wait(&done_evt, 0x1, OS_EVT_ANY, 0, 10000) != OS_OK)
        fail("stress run did not complete in time");
    if (collector_ok != 1)
        fail("collector timed out mid-stream (lost messages)");

    if (protected_count != TOTAL)
        fail("mutex-protected counter wrong: increments lost to a race");
    for (int i = 0; i < WORKERS; i++)
        if (per_worker[i] != ITERS)
            fail("per-worker message count wrong");
    /* sum over workers of (0+1+...+49) */
    if (seq_sum != (uint32_t)WORKERS * (ITERS * (ITERS - 1) / 2))
        fail("sequence checksum wrong: duplicated or corrupted messages");

    os_printf("%d messages, %lu protected increments, checksum OK\n",
              TOTAL, protected_count);
    os_printf("TEST PASS\n");
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_task_create(main_task, 0, 4, "main");
    os_start();
}
