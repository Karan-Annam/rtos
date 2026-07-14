/* Demo: the interactive shell with some background life to inspect.
 *
 *   make run-demo_shell        (Ctrl-A then X to leave QEMU)
 *
 * Try: ps / stats / suspend <id> / resume <id> / echo hi
 * The two couriers pass a token through a queue forever; the naptaker
 * sleeps in bursts -- so ps shows a live mix of states. */

#include "os.h"
#include "board.h"
#include "kprintf.h"
#include "shell.h"

static os_queue_t token_q;
static uint32_t   token_buf[2];

static void courier_fn(void *arg)
{
    uint32_t hops = 0;
    for (;;) {
        uint32_t tok;
        os_queue_recv(&token_q, &tok, OS_WAIT_FOREVER);
        hops++;
        os_sleep_ms(200 + 100 * (uint32_t)(uintptr_t)arg);
        os_queue_send(&token_q, &tok, OS_WAIT_FOREVER);
    }
}

static void naptaker_fn(void *arg)
{
    (void)arg;
    for (;;) {
        os_sleep_ms(50);
        for (volatile int i = 0; i < 20000; i++)
            ;                       /* pretend to work */
        os_sleep_ms(700);
    }
}

int main(void)
{
    os_init();

    os_queue_init(&token_q, token_buf, sizeof(uint32_t), 2);
    uint32_t token = 42;
    os_queue_send(&token_q, &token, OS_NO_WAIT);

    os_task_create(courier_fn, (void *)0, 6, "courier0");
    os_task_create(courier_fn, (void *)1, 6, "courier1");
    os_task_create(naptaker_fn, 0, 7, "naptaker");
    os_task_create(shell_task, 0, 8, "shell");

    os_start();
}
