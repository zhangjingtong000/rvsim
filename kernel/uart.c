#include <stdarg.h>
#include "kernel.h"

/* 最近输出的尾部。assert 用它检查「产物」，而不只是内核状态。
   注意命令回显也会进这里，所以调用方要给出 mark，只查 mark 之前的部分。 */
#define UART_TAIL_CAP 4096   /* 可搜索窗口；tail 只显示尾部，避免刷屏 */
#define UART_TAIL_SHOW 200
/* 环形缓冲：满了之后覆盖最旧的一个字节。
   先前用「整体左移一位」的写法，窗口一扩大到 4KB，
   每输出一个字符就要搬 4095 字节，直接把内核拖死。 */
static char g_uart_tail[UART_TAIL_CAP];
static u32  g_uart_head;     /* 下一个写入位置 */
static u32  g_uart_tail_n;   /* 当前有效字节数 */

void uart_putc(char c)
{
    volatile u8 *thr = (volatile u8 *)(UART_BASE + 0);
    volatile u8 *lsr = (volatile u8 *)(UART_BASE + 5);
    while (!(*lsr & 0x20)) { }   /* 等 THRE */
    *thr = (u8)c;

    g_uart_tail[g_uart_head] = c;
    g_uart_head = (g_uart_head + 1u) % UART_TAIL_CAP;
    if (g_uart_tail_n < UART_TAIL_CAP) g_uart_tail_n++;
}

u32 uart_tail_len(void) { return g_uart_tail_n; }

/* mark 处之前是否以 "rvsh> " 结尾 —— AI 用它判断「发命令时系统是否空闲」。
   传入命令起始位置而不是当前末尾：命令回显已经把 prompt 挤走了，
   拿当前末尾判断永远是 false。 */
/* 把当前尾部内容复制到调用方缓冲。
   为什么要复制：缓冲是滑动窗口，命令回显会挤掉最旧的内容，
   用绝对位置 mark 去回头查找，位置早就失效了（assert uart 会假阳性）。 */
void uart_tail_copy(char *dst, u32 len)
{
    if (len > g_uart_tail_n) len = g_uart_tail_n;
    u32 start = (g_uart_head + UART_TAIL_CAP - g_uart_tail_n) % UART_TAIL_CAP;
    for (u32 i = 0; i < len; i++)
        dst[i] = g_uart_tail[(start + i) % UART_TAIL_CAP];
    dst[len] = 0;
}

/* 在任意缓冲里查找子串 */
int mem_has(const char *buf, u32 len, const char *needle)
{
    u32 nl = 0;
    while (needle[nl]) nl++;
    if (nl == 0 || nl > len) return 0;
    for (u32 i = 0; i + nl <= len; i++) {
        u32 j = 0;
        while (j < nl && buf[i + j] == needle[j]) j++;
        if (j == nl) return 1;
    }
    return 0;
}

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
    /* 只输出尾部 200 字符：缓冲有 4KB，全给出去会刷屏；
       但 assert uart 搜索的是完整 4KB，两者用途不同。 */
    u32 n = g_uart_tail_n;
    u32 start = (g_uart_head + UART_TAIL_CAP - n) % UART_TAIL_CAP;
    u32 from = (n > UART_TAIL_SHOW) ? (n - UART_TAIL_SHOW) : 0;
    for (u32 i = from; i < n; i++) {
        char c = g_uart_tail[(start + i) % UART_TAIL_CAP];
        uart_putc(c == '\n' ? '|' : c);
    }
    uart_putc('\n');
}
