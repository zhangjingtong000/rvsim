#include <stdarg.h>
#include "kernel.h"

void uart_putc(char c)
{
    volatile u8 *thr = (volatile u8 *)(UART_BASE + 0);
    volatile u8 *lsr = (volatile u8 *)(UART_BASE + 5);
    while (!(*lsr & 0x20)) { }   /* 等 THRE */
    *thr = (u8)c;
}

void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

void uart_hex(u32 v)
{
    uart_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        int d = (v >> i) & 0xF;
        uart_putc(d < 10 ? '0' + d : 'a' + d - 10);
    }
}

void uart_dec(int v)
{
    if (v == 0) { uart_putc('0'); return; }
    if (v < 0) { uart_putc('-'); v = -v; }
    char buf[16];
    int n = 0;
    while (v) { buf[n++] = '0' + (v % 10); v /= 10; }
    while (n--) uart_putc(buf[n]);
}

void kprintf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { uart_putc(*p); continue; }
        p++;
        switch (*p) {
        case 's': uart_puts(va_arg(ap, const char *)); break;
        case 'x': uart_hex(va_arg(ap, u32)); break;
        case 'd': uart_dec(va_arg(ap, int)); break;
        case 'c': uart_putc((char)va_arg(ap, int)); break;
        default: uart_putc('%'); uart_putc(*p); break;
        }
    }
    va_end(ap);
}

void *memset(void *d, int c, size_t n)
{
    u8 *p = d;
    while (n--) *p++ = (u8)c;
    return d;
}

void *memcpy(void *d, const void *s, size_t n)
{
    u8 *dp = d;
    const u8 *sp = s;
    while (n--) *dp++ = *sp++;
    return d;
}