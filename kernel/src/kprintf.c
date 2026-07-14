/* Tiny printf for the kernel: no heap, no libc stdio, ~1 KB of code.
 * Supports: %c %s %d %i %u %x %X %p %% with optional zero-pad width (%08x).
 *
 * os_printf formats into a static buffer under irq_lock, so concurrent tasks
 * can't interleave characters mid-line. Tradeoff (documented): interrupts are
 * masked for the duration of the console write, so keep prints short in
 * timing-sensitive code. For a teaching kernel this simplicity wins.
 */

#include <stdarg.h>
#include <stdint.h>
#include "kprintf.h"
#include "board.h"
#include "critical.h"

static void emit(char *buf, int size, int *pos, char c)
{
    if (*pos < size - 1)
        buf[*pos] = c;
    (*pos)++;
}

static void emit_num(char *buf, int size, int *pos,
                     uint32_t v, int base, int is_signed,
                     int upper, int width, char pad)
{
    char tmp[12]; /* 32-bit worst case: 10 digits + sign */
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    int n = 0, neg = 0;

    if (is_signed && (int32_t)v < 0) {
        neg = 1;
        v = (uint32_t)(-(int32_t)v);
    }
    do {
        tmp[n++] = digits[v % (uint32_t)base];
        v /= (uint32_t)base;
    } while (v);

    if (neg && pad == '0') { emit(buf, size, pos, '-'); neg = 0; width--; }
    for (int i = n + neg; i < width; i++)
        emit(buf, size, pos, pad);
    if (neg)
        emit(buf, size, pos, '-');
    while (n--)
        emit(buf, size, pos, tmp[n]);
}

int os_vsnprintf(char *buf, int size, const char *fmt, va_list ap)
{
    int pos = 0;

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            emit(buf, size, &pos, *fmt);
            continue;
        }
        fmt++;

        char pad = ' ';
        int width = 0, left = 0;
        if (*fmt == '-') { left = 1; fmt++; }
        if (*fmt == '0') { pad = '0'; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        if (*fmt == 'l') /* longs are 32-bit here too */
            fmt++;

        switch (*fmt) {
        case 'c':
            emit(buf, size, &pos, (char)va_arg(ap, int));
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = 0;
            for (const char *p = s; *p; p++) len++;
            if (!left)
                for (int i = len; i < width; i++) emit(buf, size, &pos, ' ');
            for (; *s; s++) emit(buf, size, &pos, *s);
            if (left)
                for (int i = len; i < width; i++) emit(buf, size, &pos, ' ');
            break;
        }
        case 'd': case 'i':
            emit_num(buf, size, &pos, va_arg(ap, uint32_t), 10, 1, 0, width, pad);
            break;
        case 'u':
            emit_num(buf, size, &pos, va_arg(ap, uint32_t), 10, 0, 0, width, pad);
            break;
        case 'x':
            emit_num(buf, size, &pos, va_arg(ap, uint32_t), 16, 0, 0, width, pad);
            break;
        case 'X':
            emit_num(buf, size, &pos, va_arg(ap, uint32_t), 16, 0, 1, width, pad);
            break;
        case 'p':
            emit(buf, size, &pos, '0');
            emit(buf, size, &pos, 'x');
            emit_num(buf, size, &pos, va_arg(ap, uint32_t), 16, 0, 0, 8, '0');
            break;
        case '%':
            emit(buf, size, &pos, '%');
            break;
        default: /* unknown specifier: print it raw */
            emit(buf, size, &pos, '%');
            emit(buf, size, &pos, *fmt);
            break;
        }
    }

    buf[pos < size - 1 ? pos : size - 1] = '\0';
    return pos;
}

int os_snprintf(char *buf, int size, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = os_vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

static char g_printf_buf[OS_PRINTF_BUF];

int os_printf(const char *fmt, ...)
{
    va_list ap;
    uint32_t key = irq_lock();

    va_start(ap, fmt);
    int n = os_vsnprintf(g_printf_buf, sizeof g_printf_buf, fmt, ap);
    va_end(ap);
    console_write(g_printf_buf);

    irq_unlock(key);
    return n;
}
