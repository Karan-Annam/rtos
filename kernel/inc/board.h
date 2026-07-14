/* Board interface: the small set of functions every board port must provide.
 * The kernel itself only ever talks to hardware through this + the armv7m
 * core peripherals (SysTick/NVIC/SCB), so porting = implementing this file
 * plus a startup/linker script pair. */

#ifndef BOARD_H
#define BOARD_H

/* Called from Reset_Handler before main(): clocks, console hardware. */
void board_init(void);

/* Console (semihosting on QEMU, USART on real boards) */
void console_write(const char *s);   /* NUL-terminated string */
void console_putc(char c);
int  console_getc(void);             /* blocking; -1 if unsupported */
int  console_getc_nonblock(void);    /* -1 = nothing available      */

/* LEDs (0 on QEMU, 8 on the F3 Discovery ring) */
int  board_led_count(void);
void board_led_set(int idx, int on);

/* End of the world: exit simulation (QEMU) or hang blinking (board). */
void platform_exit(int code) __attribute__((noreturn));

#endif /* BOARD_H */
