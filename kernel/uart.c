#include <stdarg.h>
#include "kernel.h"

/* 最近输出的尾部。assert 用它检查「产物」，而不只是内核状态。
   注意命令回显也会进这里，所以调用方要给出 mark，只查 mark 之前的部分。 */
#define UART_TAIL_CAP 160
static char g_uart_tail[UART_TAIL_CAP];
static u32  g_uart_tail_n;

void uart_putc(char c)
{
    volatile u8 *thr = (volatile u8 *)(UART_BASE + 0);
    volatile u8 *lsr = (volatile u8 *)(UART_BASE + 5);
    while (!(*lsr & 0x20)) { }   /* 等 THRE */
    *thr = (u8)c;

    if (g_uart_tail_n < UART_TAIL_CAP - 1) {
        g_uart_tail[g_uart_tail_n++] = c;
    } else {
        for (u32 i = 1; i < UART_TAIL_CAP - 1; i++) g_uart_tail[i - 1] = g_uart_tail[i];
        g_uart_tail[UART_TAIL_CAP - 2] = c;
    }
}

u32 uart_tail_len(void) { return g_uart_tail_n; }

/* mark 处之前是否以 "rvsh> " 结尾 —— AI 用它判断「发命令时系统是否空闲」。
   传入命令起始位置而不是当前末尾：命令回显已经把 prompt 挤走了，
   拿当前末尾判断永远是 false。 */
int uart_at_prompt(u32 mark)
{
    static const char *pat = "rvsh> ";
    if (mark > g_uart_tail_n) mark = g_uart_tail_n;
    if (mark < 6) return 0;
    for (u32 i = 0; i < 6; i++)
        if (g_uart_tail[mark - 6 + i] != pat[i]) return 0;
    return 1;
}

/* 在 [0, mark) 范围内查找子串，避免匹配到当前命令的回显 */
int uart_tail_has(u32 mark, const char *needle)
{
    if (mark > g_uart_tail_n) mark = g_uart_tail_n;
    u32 nl = 0;
    while (needle[nl]) nl++;
    if (nl == 0 || nl > mark) return 0;
    for (u32 i = 0; i + nl <= mark; i++) {
        u32 j = 0;
        while (j < nl && g_uart_tail[i + j] == needle[j]) j++;
        if (j == nl) return 1;
    }
    return 0;
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
/* 输出最近内容（调试与审计用）。换行替换成 | 便于单行显示。 */
void uart_tail_dump(void)
{
    /* 必须先快照：uart_putc 自己会移位这个缓冲，
       直接边遍历边输出会把内容改坏（自毁）。 */
    char tmp[UART_TAIL_CAP];
    u32 n = g_uart_tail_n;
    for (u32 i = 0; i < n; i++) tmp[i] = g_uart_tail[i];
    for (u32 i = 0; i < n; i++) uart_putc(tmp[i] == '\n' ? '|' : tmp[i]);
    uart_putc('\n');
}
