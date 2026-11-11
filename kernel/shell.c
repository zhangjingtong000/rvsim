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
static u32  g_cmd_mark;   /* 命令回显之前的输出长度 */
static char g_cmd_snap[4097];   /* +1：uart_tail_copy 会写终止符 */  /* 命令开始时的输出快照，供 assert uart 查找 */
static int  g_cmd_at_prompt; /* 命令开始时系统是否停在提示符 */
static int  g_len = 0;

static void prompt(void) { uart_puts("\nrvsh> "); }

static int streq(const char *a, const char *b)
{
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

static void cmd_help(void)
{
#ifdef AIOS_MINIMAL
    /* 基线组的能力集是固定的，手写即可 */
    uart_puts("命令: help ps run reset mem uptime\n");
#else
    extern void cap_help(void);
    cap_help();
    uart_puts("输入 manifest 查看每个能力的用法与参数\n");
#endif
}

static void cmd_ps(void)
{
    extern void shell_ps_hook(void);
    shell_ps_hook();
}

/* 由 UART 中断或轮询喂入字符 */
static int strlen_safe(const char *s)
{
    int n = 0;
    while (s && s[n]) n++;
    return n;
}

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
    /* 键在 '='、':' 或空格处结束——否则 "oob 18" 会被整个当成键名 */
    while (*p && *p != '=' && *p != ':' && *p != ' ' && kl < 15) key[kl++] = *p++;
    key[kl] = 0;
    /* 同时接受 key=value 与 key value：实测 AI 经常写成后者，
       每错一次就浪费一步。这是可用性缺陷，不是被测变量。 */
    if (*p == '=' || *p == ':' || *p == ' ') {
        p++;
    } else {
        uart_puts("{\"ok\":false,\"why\":\"格式应为 key=value\"}\n");
        return;
    }

    /* uart=<子串>：检查命令回显之前的输出里有没有它 */
    if (streq(key, "uart")) {
        extern int mem_has(const char *, u32, const char *);
        int ok = mem_has(g_cmd_snap, g_cmd_mark, p);
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
    {
        extern int evt_type_known(const char *);
        extern int evt_count_denied(void);
        extern int plan_count(void);
        extern int snap_count(void);

        if (evt_type_known(key)) {
            got = evt_count_named(key);
        } else if (streq(key, "oob")) {
            got = evt_count_denied();
        } else if (streq(key, "events")) {
            got = evt_count_all();
        } else if (streq(key, "nproc")) {
            got = proc_count_live();
        } else if (streq(key, "plans")) {
            got = plan_count();
        } else if (streq(key, "snaps")) {
            got = snap_count();
        } else if (streq(key, "at_prompt")) {
            got = g_cmd_at_prompt;
        } else {
            uart_puts("{\"ok\":false,\"why\":\"未知断言键\",\"hint\":\"spawn|exit|ecall|restore|reset|snapshot|plan|apply|sbrk|events|nproc|oob|plans|snaps|at_prompt|uart\"}\n");
            return;
        }
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
    extern u32 plan_make(u8, const char *);
    const char *p = line + 5;                 /* 跳过 "plan " */
    u8 kind = 0;
    const char *intent = 0, *argstr = "";
    int irr = 0;

    if (starts_with(p, "run ")) {
        kind = PLAN_RUN;     intent = "run";     argstr = p + 4; irr = 0;
    } else if (streq(p, "reset")) {
        kind = PLAN_RESET;   intent = "reset";   argstr = "";    irr = 0;
    } else if (starts_with(p, "restore ")) {
        kind = PLAN_RESTORE; intent = "restore"; argstr = p + 8; irr = 1;
    } else {
        uart_puts("{\"ok\":false,\"why\":\"用法: plan run <prog> | plan reset | plan restore <snap_id>\"}\n");
        return;
    }
    if (kind != PLAN_RESET && !argstr[0]) {
        uart_puts("{\"ok\":false,\"why\":\"缺少参数\"}\n");
        return;
    }
    if (kind == PLAN_RUN) {
        extern int kprog_valid(const char *);
        if (!kprog_valid(argstr)) {
            uart_puts("{\"ok\":false,\"why\":\"未知程序\",\"hint\":\"hello|fork|badptr\"}\n");
            return;
        }
    }
    if (kind == PLAN_RESTORE) {
        extern int snap_exists(u32);
        int sid = 0;
        for (const char *q = argstr; *q >= '0' && *q <= '9'; q++) sid = sid * 10 + (*q - '0');
        if (!snap_exists((u32)sid)) {
            uart_puts("{\"ok\":false,\"why\":\"快照不存在\",\"hint\":\"先 snapshot 或看 snaps\"}\n");
            return;
        }
    }

    u32 id = plan_make(kind, argstr[0] ? argstr : 0);
    if (!id) { uart_puts("{\"ok\":false,\"why\":\"计划槽已满\"}\n"); return; }

    uart_puts("{\"plan\":");
    uart_dec((int)id);
    uart_puts(",\"intent\":\"");
    uart_puts(intent);
    uart_puts("\",\"arg\":\"");
    uart_puts(argstr);
    uart_puts("\",\"irreversible\":");
    uart_puts(irr ? "true" : "false");
    uart_puts("}\n");
}

static void cmd_apply(const char *line)
{
    extern int plan_take(u32, u8 *, char *, u32);
    extern void kernel_apply_run(const char *);
    extern void kernel_apply_reset(void);
    extern int kernel_apply_restore(u32);
    const char *p = line + 6;                 /* 跳过 "apply " */
    int id = 0;
    while (*p >= '0' && *p <= '9') { id = id * 10 + (*p - '0'); p++; }
    if (id <= 0) { uart_puts("{\"ok\":false,\"why\":\"用法: apply <plan_id>\"}\n"); return; }

    u8 kind = 0;
    char arg[12];
    if (!plan_take((u32)id, &kind, arg, sizeof(arg))) {
        uart_puts("{\"ok\":false,\"why\":\"计划不存在或已被执行\"}\n");
        return;
    }

    /* 先把能预见的失败挡在这里：一次调用只给一个结论，
       不能先报成功再报失败。 */
    if (kind == PLAN_RESTORE) {
        extern int snap_exists(u32);
        int sid = 0;
        for (const char *q = arg; *q >= '0' && *q <= '9'; q++) sid = sid * 10 + (*q - '0');
        if (!snap_exists((u32)sid)) {
            uart_puts("{\"apply\":false,\"plan\":");
            uart_dec(id);
            uart_puts(",\"why\":\"快照不存在\"}\n");
            g_len = 0; g_line[0] = 0;
            return;
        }
    }

    uart_puts("{\"apply\":true,\"plan\":");
    uart_dec(id);
    uart_puts("}\n");
    { extern void kernel_note_apply(u32); kernel_note_apply((u32)id); }

    /* kernel_apply_run 进用户态后不返回，shell_input 尾部的
       g_len = 0 永远执行不到，必须在这里手动清行缓冲。 */
    g_len = 0;
    g_line[0] = 0;

    if (kind == PLAN_RUN) {
        kernel_apply_run(arg);
    } else if (kind == PLAN_RESET) {
        kernel_apply_reset();
    } else {
        int sid = 0;
        for (const char *q = arg; *q >= '0' && *q <= '9'; q++)
            sid = sid * 10 + (*q - '0');
        if (!kernel_apply_restore((u32)sid))
            uart_puts("{\"ok\":false,\"why\":\"快照不存在\"}\n");
        else
            /* 计划标了 irreversible，执行后就必须给出可见的后果提示，
               否则「不可逆」只是清单上的一句话。 */
            uart_puts("{\"warning\":\"进程状态已被回滚，此操作不可撤销\"}\n");
    }
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
            else if (streq(g_line, "run hello")) {
                extern int g_run_flag, g_fork_flag;
                g_fork_flag = 0;
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
                uart_tail_dump();
            }
            else if (ADV && streq(g_line, "status")) {
                extern void status_json(int);
                status_json(g_cmd_at_prompt);
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
            else if (ADV && starts_with(g_line, "setpc ")) {
                extern void proc_set_pc(int, u32);
                const char *q = g_line + 6;
                int pid = 0; u32 addr = 0;
                int got_pid = 0, got_addr = 0;
                while (*q >= '0' && *q <= '9') { pid = pid * 10 + (*q - '0'); q++; got_pid = 1; }
                while (*q == ' ') q++;
                if (!got_pid) { uart_puts("{\"ok\":false,\"why\":\"用法: setpc <pid> <addr>\"}\n"); g_len = 0; g_line[0] = 0; return; }
                if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) {
                    q += 2;
                    while (*q) {
                        u32 d;
                        if (*q >= '0' && *q <= '9') d = (u32)(*q - '0');
                        else if (*q >= 'a' && *q <= 'f') d = (u32)(*q - 'a' + 10);
                        else if (*q >= 'A' && *q <= 'F') d = (u32)(*q - 'A' + 10);
                        else break;
                        addr = addr * 16u + d; q++; got_addr = 1;
                    }
                } else {
                    while (*q >= '0' && *q <= '9') { addr = addr * 10u + (u32)(*q - '0'); q++; got_addr = 1; }
                }
                if (!got_addr || *q) { uart_puts("{\"ok\":false,\"why\":\"用法: setpc <pid> <addr>\"}\n"); g_len = 0; g_line[0] = 0; return; }
                proc_set_pc(pid, addr);
            }
            else if (ADV && starts_with(g_line, "setreg ")) {
                extern void proc_set_reg(int, int, u32);
                const char *q = g_line + 7;
                int pid = 0, reg = 0;
                u32 val = 0;
                int got_pid = 0, got_reg = 0, got_val = 0;
                while (*q >= '0' && *q <= '9') { pid = pid * 10 + (*q - '0'); q++; got_pid = 1; }
                while (*q == ' ') q++;
                while (*q >= '0' && *q <= '9') { reg = reg * 10 + (*q - '0'); q++; got_reg = 1; }
                while (*q == ' ') q++;
                if (!got_pid || !got_reg) {
                    uart_puts("{\"ok\":false,\"why\":\"用法: setreg <pid> <reg> <val>\"}\n");
                    g_len = 0; g_line[0] = 0; return;
                }
                /* 支持十进制与 0x 前缀 */
                if (q[0] == '0' && (q[1] == 'x' || q[1] == 'X')) {
                    q += 2;
                    while (*q) {
                        u32 d;
                        if (*q >= '0' && *q <= '9') d = (u32)(*q - '0');
                        else if (*q >= 'a' && *q <= 'f') d = (u32)(*q - 'a' + 10);
                        else if (*q >= 'A' && *q <= 'F') d = (u32)(*q - 'A' + 10);
                        else break;
                        val = val * 16u + d; q++; got_val = 1;
                    }
                } else {
                    while (*q >= '0' && *q <= '9') { val = val * 10u + (u32)(*q - '0'); q++; got_val = 1; }
                }
                if (!got_val || *q) {
                    uart_puts("{\"ok\":false,\"why\":\"用法: setreg <pid> <reg> <val>\"}\n");
                    g_len = 0; g_line[0] = 0; return;
                }
                proc_set_reg(pid, reg, val);
            }
            else if (ADV && starts_with(g_line, "regs")) {
                extern void proc_dump_regs(int);
                int pid = 0;
                const char *q = g_line + 4;
                if (*q == ' ') {
                    q++;
                    int got = 0;
                    while (*q >= '0' && *q <= '9') { pid = pid * 10 + (*q - '0'); q++; got = 1; }
                    if (!got || *q) {
                        uart_puts("{\"ok\":false,\"why\":\"用法: regs [pid]\"}\n");
                        g_len = 0; g_line[0] = 0; return;
                    }
                }
                proc_dump_regs(pid);
            }
            else if (ADV && streq(g_line, "ls")) {
                uart_puts("{\"files\":[");
                int first = 1;
                char nm[24]; u32 sz;
                for (u32 i = 0; i < 32; i++) {
                    if (!fs_entry(i, nm, sizeof(nm), &sz)) continue;
                    if (!first) uart_puts(",");
                    first = 0;
                    uart_puts("{\"name\":\"");
                    uart_puts(nm);
                    uart_puts("\",\"size\":");
                    uart_dec((int)sz);
                    uart_puts("}");
                }
                uart_puts("]}\n");
            }
            else if (ADV && starts_with(g_line, "cat ")) {
                const char *q = g_line + 4;
                static u8 buf[4096];
                int n = fs_read(q, buf, sizeof(buf));
                if (n < 0) {
                    uart_puts("{\"ok\":false,\"why\":\"文件不存在\"}\n");
                } else {
                    uart_puts("{\"name\":\"");
                    uart_puts(q);
                    uart_puts("\",\"size\":");
                    uart_dec(n);
                    uart_puts(",\"data\":\"");
                    for (int i = 0; i < n; i++) {
                        char c = (char)buf[i];
                        if (c == '"') uart_puts("\\\"");
                        else if (c == '\n') uart_puts("\\n");
                        else if (c == '\\') uart_puts("\\\\");
                        else uart_putc(c);
                    }
                    uart_puts("\"}\n");
                }
            }
            else if (ADV && starts_with(g_line, "write ")) {
                const char *q = g_line + 6;
                char nm[24]; int nl = 0;
                while (*q && *q != ' ' && nl < 23) nm[nl++] = *q++;
                nm[nl] = 0;
                if (*q == ' ') q++;
                int n = fs_write(nm, (const u8 *)q, (u32)strlen_safe(q));
                if (n < 0) {
                    uart_puts("{\"ok\":false,\"why\":\"写入失败\"}\n");
                } else {
                    uart_puts("{\"write\":\"");
                    uart_puts(nm);
                    uart_puts("\",\"bytes\":");
                    uart_dec(n);
                    uart_puts("}\n");
                }
            }
            else if (ADV && starts_with(g_line, "rm ")) {
                const char *q = g_line + 3;
                if (fs_remove(q) == 0) {
                    uart_puts("{\"removed\":\"");
                    uart_puts(q);
                    uart_puts("\"}\n");
                } else {
                    uart_puts("{\"ok\":false,\"why\":\"文件不存在\"}\n");
                }
            }
            else if (ADV && streq(g_line, "verify")) {
                extern void verify_dump(void);
                verify_dump();
            }
            else if (ADV && streq(g_line, "replay")) {
                extern void evt_replay(void);
                evt_replay();
            }
            else if (ADV && streq(g_line, "resume")) {
                extern void kernel_resume(void);
                g_len = 0;
                g_line[0] = 0;
                kernel_resume();
            }
            else if (ADV && streq(g_line, "guide")) {
                extern void cap_guide(void);
                cap_guide();
            }
            else if (ADV && streq(g_line, "manifest")) {
                extern void cap_manifest(void);
                cap_manifest();
            }
            else if (ADV && streq(g_line, "log")) {
                /* 默认只给最近 30 条：全量倾倒会刷屏并挤爆上下文窗口 */
                extern void evt_dump_last(u32);
                evt_dump_last(30);
            }
            else if (ADV && streq(g_line, "log all")) {
                extern void evt_dump_all(void);
                evt_dump_all();
            }
            else if (ADV && streq(g_line, "log count")) {
                extern void evt_count_dump(void);
                evt_count_dump();
            }
            else if (ADV && starts_with(g_line, "log count ")) {
                extern int evt_count_named(const char *);
                extern int evt_type_known(const char *);
                const char *q = g_line + 10;
                extern int evt_count_denied(void);
                if (streq(q, "oob")) {
                    /* oob 不是事件类型，是被拒绝的操作数 */
                    uart_puts("{\"count\":{\"oob\":");
                    uart_dec(evt_count_denied());
                    uart_puts("}}\n");
                } else if (!evt_type_known(q)) {
                    uart_puts("{\"ok\":false,\"why\":\"未知计数键\",\"hint\":\"spawn|exit|ecall|restore|oob\"}\n");
                } else {
                    uart_puts("{\"count\":{\"");
                    uart_puts(q);
                    uart_puts("\":");
                    uart_dec(evt_count_named(q));
                    uart_puts("}}\n");
                }
            }
            else if (ADV && starts_with(g_line, "log find ")) {
                extern void evt_dump_actor(u32);
                const char *q = g_line + 9;
                int pid = 0;
                while (*q >= '0' && *q <= '9') { pid = pid * 10 + (*q - '0'); q++; }
                if (*q) uart_puts("{\"ok\":false,\"why\":\"用法: log find <pid>\"}\n");
                else evt_dump_actor((u32)pid);
            }
            else if (ADV && starts_with(g_line, "log last ")) {
                extern void evt_dump_last(u32);
                int n = 0;
                const char *q = g_line + 9;
                while (*q >= '0' && *q <= '9') { n = n * 10 + (*q - '0'); q++; }
                if (n <= 0) uart_puts("{\"ok\":false,\"why\":\"用法: log last <n>\"}\n");
                else evt_dump_last((u32)n);
            }
            else if (streq(g_line, "mem")) {
                extern u32 g_pf_count;
                uart_puts("物理内存 64 MB @ 0x80000000\n");
                uart_puts("缺页次数 = ");
                uart_dec((int)g_pf_count);
                uart_putc('\n');
            }
            else {
                /* 结构化失败 + 可操作建议：让模型能自己纠正，
                   而不是在同一个错误上反复试。 */
                uart_puts("{\"ok\":false,\"why\":\"unknown command\",\"echo\":\"");
                uart_puts(g_line);
                uart_puts("\",\"hint\":\"输入 help 或 manifest 查看可用能力\"}\n");
            }
        }
        g_len = 0;
        prompt();
    } else if (ch == 0x7F || ch == 0x08) {   /* 退格 */
        if (g_len > 0) { g_len--; uart_puts("\b \b"); }
    } else if (g_len < SHELL_BUF - 1 && ch >= 0x20 && ch < 0x7F) {
        /* 命令的第一个字符：此刻缓冲区里还只有提示符，
           回显还没进来。assert 查产物时以这里为界。 */
        if (g_len == 0) {
            /* 必须在命令第一个字符到达时立刻判定：缓冲是滑窗，
               等命令执行完再回头看，prompt 早被回显挤走了。 */
            extern int uart_at_prompt(u32);
            g_cmd_mark = uart_tail_len();
            g_cmd_at_prompt = uart_at_prompt(g_cmd_mark);
            /* 快照一份：滑动窗口会让绝对位置失效，assert uart 必须查这份 */
            uart_tail_copy(g_cmd_snap, g_cmd_mark);
        }
        g_line[g_len++] = ch;
        uart_putc(ch);
    }
}

void shell_start(void)
{
    uart_puts("\n=== rvsh ===\n");
    /* 明确声明这不是 POSIX shell：实测模型会本能地写 ps -ef、log | tail，
       然后连错好几轮。声明一次比纠正五次便宜。 */
    if (ADV) uart_puts("简易 shell（非 POSIX，不支持管道/重定向），输入 manifest 查看能力\n");
    else     uart_puts("简易 shell（非 POSIX，不支持管道/重定向），输入 help 查看命令\n");
    prompt();
}