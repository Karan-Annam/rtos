/* Demo: the LED ring -- eight independent tasks, each blinking one LED of
 * the F3 Discovery's compass ring at its own rhythm. The most literal
 * possible visualization of a preemptive scheduler: eight things happening
 * at eight rates, one CPU.
 *
 * On QEMU (no LEDs) the reporter task draws the ring as ASCII instead.
 * On the board it ALSO runs the shell on the UART: ps over serial while
 * the ring spins. */

#include "os.h"
#include "board.h"
#include "kprintf.h"
#include "shell.h"

#define NLEDS 8

static volatile uint8_t ring_state;   /* bit i = LED i on */

static void blinker_fn(void *arg)
{
    int i = (int)(uintptr_t)arg;
    uint32_t half_period_ms = 80 + 45u * (uint32_t)i;

    for (;;) {
        ring_state ^= (uint8_t)(1u << i);
        board_led_set(i, (ring_state >> i) & 1);
        os_sleep_ms(half_period_ms);
    }
}

static void reporter_fn(void *arg)
{
    (void)arg;
    for (;;) {
        char pic[NLEDS + 3];
        pic[0] = '[';
        for (int i = 0; i < NLEDS; i++)
            pic[1 + i] = (ring_state >> i) & 1 ? '*' : '.';
        pic[NLEDS + 1] = ']';
        pic[NLEDS + 2] = '\0';
        os_printf("%s\n", pic);
        os_sleep_ms(500);
    }
}

int main(void)
{
    os_init();

    for (uintptr_t i = 0; i < NLEDS; i++)
        os_task_create(blinker_fn, (void *)i, 6, "blink");

    if (board_led_count() == 0)
        os_task_create(reporter_fn, 0, 7, "report");  /* QEMU: draw it */

    os_task_create(shell_task, 0, 8, "shell");

    os_start();
}
