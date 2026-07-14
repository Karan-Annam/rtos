#ifndef KPRINTF_H
#define KPRINTF_H

#include <stdarg.h>

/* Max formatted length of a single os_printf call (incl. NUL). */
#define OS_PRINTF_BUF 256

int os_printf(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
int os_snprintf(char *buf, int size, const char *fmt, ...)
        __attribute__((format(printf, 3, 4)));
int os_vsnprintf(char *buf, int size, const char *fmt, va_list ap);

#endif /* KPRINTF_H */
