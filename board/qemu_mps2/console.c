/* QEMU board support: console + exit via ARM semihosting.
 *
 * Semihosting is a debugger trick: executing "bkpt 0xAB" traps to the host
 * (here: QEMU with -semihosting), which services a request described by
 * r0 = operation number, r1 = parameter block. It gives us host console I/O
 * and the ability to *end the simulation with an exit code* -- which is what
 * makes fully automated pass/fail tests possible with no hardware at all.
 */

#include <stdint.h>
#include "board.h"

#define SYS_WRITEC 0x03  /* write one char           */
#define SYS_WRITE0 0x04  /* write NUL-terminated str */
#define SYS_READC  0x07  /* blocking read one char   */
#define SYS_EXIT   0x18  /* report exception / exit  */

/* ADP "stopped" reason codes used by SYS_EXIT */
#define ADP_Stopped_ApplicationExit 0x20026  /* QEMU exits 0 */
#define ADP_Stopped_InternalError   0x20024  /* QEMU exits nonzero */

static inline uintptr_t semi_call(uintptr_t op, uintptr_t param)
{
    register uintptr_t r0 __asm__("r0") = op;
    register uintptr_t r1 __asm__("r1") = param;
    __asm__ volatile ("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
    return r0;
}

/* CMSDK APB UART0 (QEMU models it on mps2-an386). Semihosting input would
 * freeze the whole virtual CPU while waiting for a key; polling the UART
 * lets the kernel keep running -- essential for the interactive shell. With
 * -nographic, whatever you type lands on this UART's RX. */
#define UART0_BASE  0x40004000u
#define UART_DATA   (*(volatile uint32_t *)(UART0_BASE + 0x00))
#define UART_STATE  (*(volatile uint32_t *)(UART0_BASE + 0x04))
#define UART_CTRL   (*(volatile uint32_t *)(UART0_BASE + 0x08))
#define UART_BAUDDIV (*(volatile uint32_t *)(UART0_BASE + 0x10))
#define STATE_RXFULL (1u << 1)
#define CTRL_TXEN    (1u << 0)
#define CTRL_RXEN    (1u << 1)

void board_init(void)
{
    UART_BAUDDIV = 16;                 /* minimum QEMU accepts */
    UART_CTRL = CTRL_TXEN | CTRL_RXEN;
}

uint32_t board_sysclk_hz(void)
{
    return 25000000u;   /* MPS2 AN386 nominal SYSCLK */
}

int board_led_count(void) { return 0; }
void board_led_set(int idx, int on) { (void)idx; (void)on; }

void console_write(const char *s)
{
    semi_call(SYS_WRITE0, (uintptr_t)s);
}

void console_putc(char c)
{
    semi_call(SYS_WRITEC, (uintptr_t)&c);
}

int console_getc(void)
{
    return (int)semi_call(SYS_READC, 0);
}

int console_getc_nonblock(void)
{
    if (UART_STATE & STATE_RXFULL)
        return (int)(UART_DATA & 0xFF);
    return -1;
}

void platform_exit(int code)
{
    semi_call(SYS_EXIT, code == 0 ? ADP_Stopped_ApplicationExit
                                  : ADP_Stopped_InternalError);
    for (;;)
        ;
}
