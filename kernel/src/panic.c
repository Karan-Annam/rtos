/* Kernel panic: mask interrupts, say what died as loudly as possible,
 * then end the simulation (QEMU) or hang for the debugger (board). */

#include <stdarg.h>
#include "os_internal.h"
#include "kprintf.h"
#include "board.h"

void os_panic(const char *fmt, ...)
{
    static char buf[OS_PRINTF_BUF];
    va_list ap;

    irq_lock();                 /* never unlocked: the system is done */

    va_start(ap, fmt);
    os_vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    console_write("\n*** KERNEL PANIC: ");
    console_write(buf);
    console_write(" ***\n");

    if (g_curr) {
        os_snprintf(buf, sizeof buf,
                    "current task: '%s' id=%d prio=%d sp=%p base=%p\n",
                    g_curr->name, g_curr->id, g_curr->curr_prio,
                    (void *)g_curr->sp, (void *)g_curr->stack_base);
        console_write(buf);
    }

    platform_exit(1);
}
