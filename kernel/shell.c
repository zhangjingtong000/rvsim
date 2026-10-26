#include "kernel.h"

#define SHELL_BUF 128

static char g_line[SHELL_BUF];
static int  g_len = 0;

static void prompt(void) { uart_puts("\nrvsh> "); }

static int streq(const char *a, const char *b)
{
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

static void cmd_help(void)
{
    uart_puts("命令: help ps run mem uptime\n");
}

static void cmd_ps(void)
{
    extern void shell_ps_hook(void);
    shell_ps_hook();
}

/* 由 UART 中断或轮询喂入字符 */
void shell_input(char ch)
{
    if (ch == '\r' || ch == '\n') {
        g_line[g_len] = 0;
        uart_putc('\n');
        if (g_len > 0) {
            if (streq(g_line, "help"))        cmd_help();
            else if (streq(g_line, "ps"))     cmd_ps();
            else if (streq(g_line, "uptime")) {
                volatile u64 *mtime = (volatile u64 *)(CLINT_BASE + 0xBFF8u);
                uart_puts("mtime = ");
                uart_dec((int)(*mtime));
                uart_putc('\n');
            }
            else if (streq(g_line, "run")) {
                extern int g_run_flag;
                g_run_flag = 1;
            }
            else if (streq(g_line, "mem")) {
                extern u32 g_pf_count;
                uart_puts("物理内存 64 MB @ 0x80000000\n");
                uart_puts("缺页次数 = ");
                uart_dec((int)g_pf_count);
                uart_putc('\n');
            }
            else uart_puts("未知命令，试 help\n");
        }
        g_len = 0;
        prompt();
    } else if (ch == 0x7F || ch == 0x08) {   /* 退格 */
        if (g_len > 0) { g_len--; uart_puts("\b \b"); }
    } else if (g_len < SHELL_BUF - 1 && ch >= 0x20 && ch < 0x7F) {
        g_line[g_len++] = ch;
        uart_putc(ch);
    }
}

void shell_start(void)
{
    uart_puts("\n=== rvsh ===\n");
    uart_puts("简易 shell，输入 help 查看命令\n");
    prompt();
}