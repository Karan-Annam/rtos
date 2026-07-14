/* M0 smoke test: no kernel yet -- just proves the whole toolchain works:
 * vector table -> Reset_Handler -> .data/.bss init -> main -> semihosting
 * console out -> clean QEMU exit with code 0. */

#include "board.h"
#include "kprintf.h"

static volatile int g_data_test = 42; /* .data -- checks the copy loop */
static volatile int g_bss_test;       /* .bss  -- checks the zero loop */

int main(void)
{
    os_printf("hello from bare metal (data=%d bss=%d hex=%08x)\n",
              g_data_test, g_bss_test, 0xC0FFEEu);

    if (g_data_test == 42 && g_bss_test == 0) {
        os_printf("TEST PASS\n");
        platform_exit(0);
    }
    os_printf("TEST FAIL\n");
    platform_exit(1);
}
