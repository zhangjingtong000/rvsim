#include "kernel.h"

#define SHELL_BUF 128

/* A/B 对照实验的基线组：只保留基础命令。
   manifest / assert / plan / apply / status 这些「AI 友好」能力在 A 组
   里根本不存在——两组的能力集必须真的不同，否则测的只是提示词。 */
#ifdef AIOS_MINIMAL
#define ADV 0
#else
#define ADV 1
#endif

static char g_line[SHELL_BUF];
static u32  g_cmd_mark;   /* 命令回显之前的输出长度，供 assert 查产物 */
static int  g_len = 0;

static void prompt(void) { uart_puts("\nrvsh> "); }

static int streq(const char *a, const char *b)
{
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

static void cmd_help(void)
{
    if (ADV) {
        uart_puts("命令: help status manifest ps run reset mem uptime log assert plan apply plans snapshot snaps restore tail\n");
    } else {
        uart_puts("命令: help ps run reset mem uptime\n");
    }
}

static void cmd_ps(void)
{
    extern void shell_ps_hook(void);
    shell_ps_hook();
}

/* 由 UART 中断或轮询喂入字符 */
static int starts_with(const char *a, const char *b)
{
    while (*b) { if (*a++ != *b++) return 0; }
    return 1;
}

/* assert key=value —— 内核直接回答「是不是这样」。
   输出固定为一行 JSON，便于机器解析。 */
static void cmd_assert(const char *line)
{
    extern int evt_count_named(const char *);
    extern int evt_count_all(void);
    extern int proc_count_live(void);

    const char *p = line + 7;          /* 跳过 "assert " */
    char key[16]; int kl = 0;
    /* 键在 '=' 或空格处结束——否则 "oob 18" 会被整个当成键名 */
    while (*p && *p != '=' && *p != ' ' && kl < 15) key[kl++] = *p++;
    key[kl] = 0;
    /* 同时接受 key=value 与 key value：实测 AI 经常写成后者，
       每错一次就浪费一步。这是可用性缺陷，不是被测变量。 */
    if (*p == '=') {
        p++;
    } else if (*p == ' ') {
        p++;
    } else {
        uart_puts("{\"ok\":false,\"why\":\"格式应为 key=value\"}\n");
        return;
    }

    /* uart=<子串>：检查命令回显之前的输出里有没有它 */
    if (streq(key, "uart")) {
        extern int uart_tail_has(u32, const char *);
        int ok = uart_tail_has(g_cmd_mark, p);
        uart_puts("{\"assert\":\"uart\",\"want\":\"");
        uart_puts(p);
        uart_puts("\",\"ok\":");
        uart_puts(ok ? "true" : "false");
        uart_puts("}\n");
        return;
    }

    int want = 0, neg = 0;
    if (*p == '-') { neg = 1; p++; }
    while (*p >= '0' && *p <= '9') { want = want * 10 + (*p - '0'); p++; }
    if (neg) want = -want;

    int got;
    if (streq(key, "spawn") || streq(key, "exit") || streq(key, "ecall")
        || streq(key, "restore"))
        got = evt_count_named(key);
    else if (streq(key, "oob")) {
        extern int evt_count_denied(void);
        got = evt_count_denied();
    }
    else if (streq(key, "events"))
        got = evt_count_all();
    else if (streq(key, "nproc"))
        got = proc_count_live();
    else {
        uart_puts("{\"ok\":false,\"why\":\"未知断言键\"}\n");
        return;
    }

    uart_puts("{\"assert\":\"");
    uart_puts(key);
    uart_puts("\",\"want\":");
    uart_dec(want);
    uart_puts(",\"got\":");
    uart_dec(got);
    uart_puts(",\"ok\":");
    uart_puts(got == want ? "true" : "false");
    uart_puts("}\n");
}

static void cmd_plan(const char *line)
{
    extern u32 plan_make_run(const char *);
    const char *p = line + 5;                 /* 跳过 "plan " */
    if (!(p[0] == 'r' && p[1] == 'u' && p[2] == 'n' && p[3] == ' ')) {
        uart_puts("{\"ok\":false,\"why\":\"用法: plan run <prog>\"}\n");
        return;
    }
    p += 4;
    if (!p[0]) { uart_puts("{\"ok\":false,\"why\":\"缺少程序名\"}\n"); return; }
    u32 id = plan_make_run(p);
    if (!id) { uart_puts("{\"ok\":false,\"why\":\"计划槽已满\"}\n"); return; }
    uart_puts("{\"plan\":");
    uart_dec((int)id);
    uart_puts(",\"intent\":\"run\",\"prog\":\"");
    uart_puts(p);
    uart_puts("\",\"will\":\"spawn 3 procs\",\"irreversible\":false}\n");
}

static void cmd_apply(const char *line)
{
    extern int plan_take_run(u32, char *, u32);
    extern void kernel_apply_run(const char *);
    const char *p = line + 6;                 /* 跳过 "apply " */
    int id = 0;
    while (*p >= '0' && *p <= '9') { id = id * 10 + (*p - '0'); p++; }
    if (id <= 0) { uart_puts("{\"ok\":false,\"why\":\"用法: apply <plan_id>\"}\n"); return; }
    char prog[12];
    if (!plan_take_run((u32)id, prog, sizeof(prog))) {
        uart_puts("{\"ok\":false,\"why\":\"计划不存在或已被执行\"}\n");
        return;
    }
    /* 先确认再执行：apply 成功的信息必须在进入用户态之前发出去 */
    uart_puts("{\"apply\":true,\"plan\":");
    uart_dec(id);
    uart_puts("}\n");

    /* kernel_apply_run 进用户态后不返回，shell_input 尾部的
       g_len = 0 永远执行不到。必须在这里手动清行缓冲，
       否则下次回到 shell 会把新输入接在旧命令后面。 */
    g_len = 0;
    g_line[0] = 0;

    kernel_apply_run(prog);
}

static void cmd_snapshot(void)
{
    extern u32 snap_take(void);
    u32 id = snap_take();
    if (!id) { uart_puts("{\"ok\":false,\"why\":\"快照槽已满\"}\n"); return; }
    uart_puts("{\"snapshot\":");
    uart_dec((int)id);
    uart_puts("}\n");
}

static void cmd_restore(const char *line)
{
    extern int snap_restore(u32);
    const char *p = line + 8;                 /* 跳过 "restore " */
    int id = 0;
    while (*p >= '0' && *p <= '9') { id = id * 10 + (*p - '0'); p++; }
    if (id <= 0) { uart_puts("{\"ok\":false,\"why\":\"用法: restore <snap_id>\"}\n"); return; }
    if (!snap_restore((u32)id)) {
        uart_puts("{\"ok\":false,\"why\":\"快照不存在\"}\n");
        return;
    }
    uart_puts("{\"restore\":");
    uart_dec(id);
    uart_puts(",\"ok\":true}\n");
}

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
                extern int g_run_flag, g_fork_flag;
                g_fork_flag = 0;
                g_run_flag = 1;
            }
            else if (streq(g_line, "run badptr")) {
                extern int g_run_flag, g_badptr_flag;
                g_badptr_flag = 1;
                g_run_flag = 1;
            }
            else if (streq(g_line, "run fork")) {
                extern int g_run_flag, g_fork_flag;
                g_fork_flag = 1;
                g_run_flag = 1;
            }
            else if (streq(g_line, "reset")) {
                extern int krestart(void);
                int n2 = krestart();
                uart_puts("已重置 ");
                uart_dec(n2);
                uart_puts(" 个进程到就绪状态\n");
            }
            else if (ADV && streq(g_line, "tail")) {
                extern void uart_tail_dump(void);
                extern u32  uart_tail_len(void);
                uart_puts("mark=");
                uart_dec((int)g_cmd_mark);
                uart_puts(" len=");
                uart_dec((int)uart_tail_len());
                uart_puts(" : ");
                uart_tail_dump();
            }
            else if (ADV && streq(g_line, "status")) {
                extern void status_json(void);
                status_json();
            }
            else if (ADV && streq(g_line, "snapshot")) {
                cmd_snapshot();
            }
            else if (ADV && streq(g_line, "snaps")) {
                extern void snap_list(void);
                snap_list();
            }
            else if (ADV && starts_with(g_line, "restore ")) {
                cmd_restore(g_line);
            }
            else if (ADV && starts_with(g_line, "plan ")) {
                cmd_plan(g_line);
            }
            else if (ADV && starts_with(g_line, "apply ")) {
                cmd_apply(g_line);
            }
            else if (ADV && streq(g_line, "plans")) {
                extern void plan_list(void);
                plan_list();
            }
            else if (ADV && starts_with(g_line, "assert ")) {
                cmd_assert(g_line);
            }
            else if (ADV && streq(g_line, "manifest")) {
                extern void cap_manifest(void);
                cap_manifest();
            }
            else if (ADV && streq(g_line, "log")) {
                extern void evt_dump(void);
                evt_dump();
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
        /* 命令的第一个字符：此刻缓冲区里还只有提示符，
           回显还没进来。assert 查产物时以这里为界。 */
        if (g_len == 0) g_cmd_mark = uart_tail_len();
        g_line[g_len++] = ch;
        uart_putc(ch);
    }
}

void shell_start(void)
{
    uart_puts("\n=== rvsh ===\n");
    uart_puts("简易 shell，输入 help 或 manifest 查看能力\n");
    prompt();
}