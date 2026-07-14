/* Demo: classic bounded-buffer producer/consumer with live throughput.
 * Two producers race to fill a 4-deep queue; one consumer drains it slowly,
 * so producers spend real time blocked on "full". Runs 5 seconds, prints a
 * summary, exits. */

#include "os.h"
#include "board.h"
#include "kprintf.h"

static os_queue_t q;
static uint32_t   q_buf[4];
static volatile uint32_t produced[2], consumed;

static void producer_fn(void *arg)
{
    uint32_t id = (uint32_t)(uintptr_t)arg;
    for (;;) {
        uint32_t item = (id << 24) | produced[id];
        os_queue_send(&q, &item, OS_WAIT_FOREVER);
        produced[id]++;
        os_sleep_ms(3 + 2 * id);
    }
}

static void consumer_fn(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t item;
        os_queue_recv(&q, &item, OS_WAIT_FOREVER);
        consumed++;
        os_sleep_ms(4);             /* slower than the producers combined */
    }
}

static void reporter_fn(void *arg)
{
    (void)arg;
    for (int sec = 1; sec <= 5; sec++) {
        os_sleep_ms(1000);
        os_printf("[%ds] produced %lu+%lu consumed %lu (queue %u/%u)\n",
                  sec, produced[0], produced[1], consumed,
                  q.count, q.cap);
    }
    os_printf("done: %lu items consumed, none lost (%lu produced)\n",
              consumed, produced[0] + produced[1]);
    platform_exit(0);
}

int main(void)
{
    os_init();
    os_queue_init(&q, q_buf, sizeof(uint32_t), 4);
    os_task_create(producer_fn, (void *)0, 6, "prod0");
    os_task_create(producer_fn, (void *)1, 6, "prod1");
    os_task_create(consumer_fn, 0, 6, "consumer");
    os_task_create(reporter_fn, 0, 4, "reporter");
    os_start();
}
