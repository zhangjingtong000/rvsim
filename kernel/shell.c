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
static int  g_input_at = 0;   /* 当前输入行以 @ 开头，不回显 */
static u32  g_cmd_mark;   /* 命令回显之前的输出长度 */
static char g_cmd_snap[4097];   /* +1：uart_tail_copy 会写终止符 */  /* 命令开始时的输出快照，供 assert uart 查找 */
static int  g_cmd_at_prompt; /* 命令开始时系统是否停在提示符 */
static int  g_len = 0;

/* ── 用户 ──
   0 = root（可写一切），1 = ai，2 = guest。
   权限检查挂在 write/rm/chmod 上，这是「多用户」真正有意义的地方。 */
static const char *g_user_names[] = { "root", "ai", "guest" };
#define NUSERS 3

static int user_lookup(const char *name)
{
    for (int i = 0; i < NUSERS; i++) {
        const char *a = name, *b = g_user_names[i];
        while (*a && *b && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return i;
    }
    return -1;
}

static const char *user_name(u32 uid)
{
    return (uid < NUSERS) ? g_user_names[uid] : "?";
}

/* 提示符本身不带换行：每条命令执行完，回显或命令输出已经落了行尾，
   再补一个 \n 就多出空行（clear 之后第一行永远空着）。 */
static void prompt(void) {
    extern void term_set_attr(unsigned char);
    term_set_attr(1);                 /* 提示符 */
    uart_puts("rvsh> ");
    term_set_attr(2);                 /* 接下来是用户输入的回显 */
}

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
    const char *digits = p;
    while (*p >= '0' && *p <= '9') { want = want * 10 + (*p - '0'); p++; }
    if (neg) want = -want;
    /* 解析后还有残留字符（比如 assert at_prompt=true 里的 "true"）就报错，
       不能静默当成 0——那样「断言通过」的含义和字面完全相反。 */
    if (p == digits || (*p && *p != ' ' && *p != '\n' && *p != '\r')) {
        uart_puts("{\"ok\":false,\"why\":\"值必须是整数\",\"hint\":\"at_prompt 用 1/0 表示\"}\n");
        return;
    }

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
    uart_puts(",\"ok\":true,\"prog\":\"");
    /* 必须显式声明：隐式声明按 int 处理，指针被截断成 32 位再传进
       uart_puts，会去读一个非法地址。 */
    { extern const char *snap_prog(u32);
      uart_puts(snap_prog((u32)id)); }
    uart_puts("\"}\n");
}

void shell_input(char ch)
{
    if (ch == '\r' || ch == '\n') {
        /* 命令输出一律先回到普通色：提示符打完把属性切到了「输入回显」，
           不切回来的话，整段命令输出都会被当成用户敲的字上色。 */
        { extern void term_set_attr(unsigned char); term_set_attr(0); }
        /* @ 开头的行是调试面板的内部查询，对用户终端必须零痕迹：
           连回车换行和随后的提示符都不该写进网格，否则按一次 F12
           就会在终端里留下几行空白。 */
        int was_quiet = g_input_at;
        g_line[g_len] = 0;
        if (!was_quiet) uart_putc('\n');

        /* 解析 "cmd > file"：把 > 之后的部分当文件名，命令部分留给分派。
           echo 例外：它的参数里出现 > 是正常内容，不该被当重定向
           （`echo 你好 <>&` 原来会输出 `你好 <`，然后把 `&` 当文件名）。 */
        if (!starts_with(g_line, "echo ")) {
            char *gt = 0;
            for (char *p = g_line; *p; p++) if (*p == '>') { gt = p; break; }
            if (gt) {
                char *e = gt;
                while (e > g_line && e[-1] == ' ') e--;
                *e = 0;
                *gt = 0;
                char *f = gt + 1;
                while (*f == ' ') f++;
                if (*f) redirect_begin(f);
            }
        }

        if (g_len > 0) {

extern int g_term_silent;
            /* @ 前缀：只回 UART，不写窗口网格（调试面板查询用） */
            if (g_line[0] == '@') {
                g_term_silent = 1;
                for (int k = 0; g_line[k]; k++) g_line[k] = g_line[k + 1];
            }
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
                extern char g_exec_name[32];
                /* 清掉上次的程序名，回到默认的 hello.elf。
                   不清的话 run fork 之后再裸 run 会重跑 fork。 */
                g_exec_name[0] = 0;
                g_fork_flag = 0;
                g_run_flag = 1;
            }
                        else if (starts_with(g_line, "run ")) {
                /* run <任意文件名>：从 RAMFS 装载。
                   旧的 run / run fork / run badptr 仍然可用（裸名会自动补 .bin）。 */
                extern char g_exec_name[32];
                extern int g_run_flag;
                const char *q = g_line + 4;
                if (*q) {
                    int n = 0;
                    while (*q && n < 31) g_exec_name[n++] = *q++;
                    g_exec_name[n] = 0;
                    g_run_flag = 1;
                } else {
                    uart_puts("{\"ok\":false,\"why\":\"用法: run <文件名>\"}\n");
                }
            }
            else if (streq(g_line, "reset")) {
                extern int krestart(void);
                int n2 = krestart();
                uart_puts("已重置 ");
                uart_dec(n2);
                uart_puts(" 个进程到就绪状态\n");
            }
            else if (ADV && (starts_with(g_line, "kill ") || starts_with(g_line, "suspend ")
                          || starts_with(g_line, "resume "))) {
                /* 进程操作。kill 结束并回收物理页；suspend 退出调度；
                   resume 重新就绪并立刻切进用户态。
                   正在运行的进程杀不掉——它占着 CPU，shell 根本收不到命令，
                   那种情况由 Monitor 里的「终止」交给定时器中断处理。 */
                extern int proc_kill(u32), proc_suspend(u32), proc_resume(u32);
                extern int g_resume_flag;
                int is_kill = g_line[0] == 'k';
                int is_susp = g_line[0] == 's';
                const char *q = g_line;
                while (*q && *q != ' ') q++;
                while (*q == ' ') q++;
                u32 pid = 0; int has = 0;
                while (*q >= '0' && *q <= '9') { pid = pid * 10u + (u32)(*q - '0'); q++; has = 1; }
                if (!has) {
                    uart_puts("{\"ok\":false,\"why\":\"用法: kill|suspend|resume <pid>\"}\n");
                } else {
                    int r = is_kill ? proc_kill(pid) : (is_susp ? proc_suspend(pid) : proc_resume(pid));
                    if (r == 0) {
                        uart_puts("{\"ok\":true,\"op\":\"");
                        uart_puts(is_kill ? "kill" : (is_susp ? "suspend" : "resume"));
                        uart_puts("\",\"pid\":");
                        uart_dec((int)pid);
                        uart_puts("}\n");
                        if (!is_kill && !is_susp) g_resume_flag = 1;
                    } else if (r == -2) {
                        uart_puts("{\"ok\":false,\"why\":\"进程正在运行，请在 Monitor 里终止\"}\n");
                    } else {
                        uart_puts("{\"ok\":false,\"why\":\"进程不存在或状态不符\"}\n");
                    }
                }
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
                if (!got_pid) { uart_puts("{\"ok\":false,\"why\":\"用法: setpc <pid> <addr>\"}\n"); goto shell_done; }
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
                if (!got_addr || *q) { uart_puts("{\"ok\":false,\"why\":\"用法: setpc <pid> <addr>\"}\n"); goto shell_done; }
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
                    goto shell_done;
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
                    goto shell_done;
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
                        goto shell_done;
                    }
                }
                proc_dump_regs(pid);
            }
            else if (ADV && streq(g_line, "ll")) {
                /* 人类可读的文件表；ls 保留 JSON 给契约测试与 AI 调用 */
                char nm2[24]; u32 sz2;
                for (u32 i2 = 0; i2 < 16; i2++) {
                    if (!fs_entry(i2, nm2, sizeof(nm2), &sz2)) continue;
                    if (nm2[0] == 0) continue;
                    uart_puts(fs_entry_type(i2) == 1 ? "d  " : "-  ");
                    char nb[12]; int nl2 = 0;
                    if (sz2 == 0) nb[nl2++] = '0';
                    else { char t2[12]; int tl = 0; u32 v2 = sz2;
                           while (v2) { t2[tl++] = (char)('0' + v2 % 10); v2 /= 10; }
                           for (int k2 = tl - 1; k2 >= 0; k2--) nb[nl2++] = t2[k2]; }
                    nb[nl2] = 0;
                    uart_puts(nb);
                    /* 网格会过滤 \t（控制字符），用空格补齐到 8 列 */
                    for (int pad = nl2; pad < 8; pad++) uart_putc(' ');
                    uart_puts(nm2);
                    uart_puts("\n");
                }
            }
            else if (ADV && (streq(g_line, "ls") || starts_with(g_line, "ls "))) {
                /* ls [path]：path 为空表示根，否则只列该前缀之下的条目 */
                const char *dir = (g_line[2] == ' ') ? g_line + 3 : "";
                uart_puts("{\"files\":[");
                int first = 1;
                char nm[24]; u32 sz;
                for (u32 i = 0; i < 16; i++) {
                    if (!fs_entry(i, nm, sizeof(nm), &sz)) continue;
                    if (!fs_under(nm, dir)) continue;
                    if (!first) uart_puts(",");
                    first = 0;
                    uart_puts("{\"name\":\"");
                    uart_puts(nm);
                    uart_puts("\",\"size\":");
                    uart_dec((int)sz);
                    uart_puts(",\"type\":\"");
                    uart_puts(fs_entry_type(i) == 1 ? "dir" : "file");
                    uart_puts("\",\"owner\":\"");
                    uart_puts(user_name((u32)fs_entry_owner(i)));
                    uart_puts("\"}");
                }
                uart_puts("]}\n");
            }
            else if (ADV && starts_with(g_line, "login ")) {
                const char *q = g_line + 6;
                int u = user_lookup(q);
                if (u < 0) {
                    uart_puts("{\"ok\":false,\"why\":\"未知用户\",\"hint\":\"root|ai|guest\"}\n");
                } else {
                    g_cur_uid = (u32)u;
                    uart_puts("{\"login\":\"");
                    uart_puts(q);
                    uart_puts("\",\"uid\":");
                    uart_dec(u);
                    uart_puts("}\n");
                }
            }
            else if (ADV && starts_with(g_line, "echo ")) {
                uart_puts(g_line + 5);
                uart_putc('\n');
            }
            else if (ADV && streq(g_line, "dumpterm")) {
                extern void term_grid_dump(void);
                term_grid_dump();
            }
            else if (ADV && starts_with(g_line, "settime ")) {
                /* 宿主注入当前 Unix 时间，任务栏就能显示墙上时间而不是开机秒数 */
                extern u32 g_wall_base;
                extern u64 g_wall_mtime;
                extern u64 clint_mtime_now(void);
                u64 v = 0;
                const char *q2 = g_line + 8;
                while (*q2 >= '0' && *q2 <= '9') v = v * 10 + (u64)(*q2++ - '0');
                g_wall_base = (u32)v;
                g_wall_mtime = clint_mtime_now();
            }
            else if (ADV && streq(g_line, "clear")) {
                /* 清空窗口里的终端网格（不是清 UART 历史） */
                extern void term_grid_reset(void);
                term_grid_reset();
                { extern void fb_commit(u32); fb_commit(0); }   /* 改了像素必须提交，否则前端还显示旧帧 */
            }
            else if (ADV && streq(g_line, "whoami")) {
                uart_puts("{\"user\":\"");
                uart_puts(user_name(g_cur_uid));
                uart_puts("\",\"uid\":");
                uart_dec((int)g_cur_uid);
                uart_puts("}\n");
            }
            else if (ADV && starts_with(g_line, "chmod ")) {
                /* 同时支持两种写法：
                   chmod <file> <0|1>   （旧的，权限位是 0/1）
                   chmod <mode> <file>  （Unix 风格，mode 当八进制看，非 0 即 public）
                   之前只认第一种，用户敲 Unix 语法就会把 mode 当文件名，报「文件不存在」。 */
                const char *q = g_line + 6;
                char a[24]; int al = 0;
                while (*q && *q != ' ' && al < 23) a[al++] = *q++;
                a[al] = 0;
                while (*q == ' ') q++;
                char b[24]; int bl = 0;
                while (*q && *q != ' ' && bl < 23) b[bl++] = *q++;
                b[bl] = 0;

                /* 第一段全是数字且第二段不是纯数字 → 判定为 Unix 风格 chmod */
                int a_is_num = (al > 0), b_is_num = (bl > 0);
                for (int k = 0; k < al; k++) if (a[k] < '0' || a[k] > '9') a_is_num = 0;
                for (int k = 0; k < bl; k++) if (b[k] < '0' || b[k] > '9') b_is_num = 0;

                char nm[24]; int m;
                if (a_is_num && !b_is_num) {
                    int k = 0; while (b[k]) { nm[k] = b[k]; k++; } nm[k] = 0;
                    m = (a[0] == '0' || a[0] == '4' || a[0] == '6') ? 0 : 1;
                } else {
                    int k = 0; while (a[k]) { nm[k] = a[k]; k++; } nm[k] = 0;
                    m = (b[0] == '0') ? 0 : 1;
                }
                if (g_cur_uid != 0) {
                    uart_puts("{\"ok\":false,\"why\":\"只有 root 能改权限\"}\n");
                } else if (fs_chmod(nm, (u32)m) == 0) {
                    /* Unix 风格：改权限成功不说话 */
                } else {
                    uart_puts("{\"ok\":false,\"why\":\"文件不存在\"}\n");
                }
            }
            else if (ADV && starts_with(g_line, "mkdir ")) {
                const char *q = g_line + 6;
                if (*q && fs_mkdir(q) == 0) {
                    /* Unix 风格：成功不说话 */
                } else {
                    uart_puts("{\"ok\":false,\"why\":\"创建目录失败（名字过长或槽位已满）\"}\n");
                }
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
                if (!fs_can_write(nm, g_cur_uid)) {
                    uart_puts("{\"ok\":false,\"why\":\"权限不足（文件属主设为私有）\"}\n");
                    goto shell_done;
                }
                int n = fs_write(nm, (const u8 *)q, (u32)strlen_safe(q));
                if (n < 0) {
                    uart_puts("{\"ok\":false,\"why\":\"写入失败\"}\n");
                }
                /* Unix 风格：写入成功不说话 */
            }
            else if (ADV && starts_with(g_line, "rm ")) {
                const char *q = g_line + 3;
                if (!fs_can_write(q, g_cur_uid)) {
                    uart_puts("{\"ok\":false,\"why\":\"权限不足\"}\n");
                    goto shell_done;
                }
                if (fs_remove(q) == 0) {
                    /* Unix 风格：删除成功不说话 */
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
                /* 页池占用：反复 run/reset 后这里持续上涨就是泄漏 */
                uart_puts("页池: 已分配 ");
                uart_dec((int)vm_pool_used());
                uart_puts(" 页 / 可复用 ");
                uart_dec((int)vm_pool_free_count());
                uart_puts(" 页\n");
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
        { extern int g_term_silent; g_term_silent = 0; }
        if (!was_quiet) prompt();
        g_input_at = 0;
    } else if (ch == 3) {                    /* Ctrl+C：放弃当前行，回到提示符 */
        /* 以前这里没有分支：3 既不等于 \r 也不满足 >= 0x20，直接落到
           shell_done，行缓冲一个字都没清。按一次 Ctrl+C 之后，
           用户敲的每条命令都会被拼在那个看不见的 0x03 后面，全部失效。 */
        { extern void term_set_attr(unsigned char); term_set_attr(5); }
    uart_puts("^C\n");
    { extern void term_set_attr(unsigned char); term_set_attr(0); }
        g_len = 0;
        g_line[0] = 0;
        g_input_at = 0;
        { extern int g_term_silent; g_term_silent = 0; }
        prompt();
    } else if (ch == 0x7F || ch == 0x08) {   /* 退格 */
        /* 这里必须 return。以前删完一个字符会落到下面的 goto shell_done，
           把 g_len 清成 0；于是第二次退格时 g_len > 0 不成立，
           用户看到的就是「只能删一个字母，再按就没反应」。 */
        if (g_len > 0) { g_len--; uart_puts("\b \b"); }
        return;
    } else if (g_len < SHELL_BUF - 1 && (u8)ch >= 0x20 && g_input_at) {
        /* @ 开头的整行都不回显：这是调试面板的内部命令，
           不该出现在用户终端里（否则按一次 F12 就留 5 行 @ps/@ls） */
        g_line[g_len++] = ch;
        return;
    } else if (g_len < SHELL_BUF - 1 && (u8)ch >= 0x20) {
        /* 行首是 @ 就进入静默输入模式（含 @ 本身） */
        if (g_len == 0 && ch == '@') { g_input_at = 1; g_line[g_len++] = ch; return; }
        /* 这里是 (u8)ch 不是 ch：中文的 UTF-8 字节 >= 0xE4，
           原来的 ch < 0x7F 把它们全挡在行缓冲之外，中文命令根本进不来。 */
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
        { extern void term_set_attr(unsigned char); term_set_attr(2); }
        uart_putc(ch);
        return;
    }

    goto shell_done;              /* 普通字符：直接返回，不要清空行缓冲 */

shell_done:
    g_input_at = 0;
    /* 必须放在标签之后：所有 goto shell_done 的分支都会跳过前面的语句，
       之前把清零写在标签前，结果 @ 的静默一旦生效就再也不会恢复。 */
    { extern int g_term_silent; g_term_silent = 0; }
    redirect_end();
    g_len = 0;
    g_line[0] = 0;
}

void shell_start(void)
{
    /* 内核启动阶段的所有日志只走 UART，不进窗口网格：
       用户打开系统应该先看到一个干净的终端，而不是一屏调试输出。 */
    { extern int g_term_silent; g_term_silent = 0; }

    /* 横幅只在开机打一次。进程全部退出后 trap 会再次进入 shell_loop，
       那里也调用本函数；如果每次都重印 MOTD，执行一条 run 就会看到
       屏幕被开机信息顶满、刚才敲的命令被顶出可视区。 */
    { static int banner_done = 0;
      if (banner_done) { prompt(); return; }
      banner_done = 1; }

    /* 只用字模表里有的字符和 ASCII：
       \xc2\xb7 这类 Latin-1 标点和全角逗号都不在 cjk16 字库里，
       写进去会被 term_grid_putc 丢掉，MOTD 就变成一串缺字的乱码。 */
    uart_puts("  rvsim 0.9   RISC-V RV32IMA   M/S/U   Sv32\n");
    uart_puts("  64 MiB RAM   UART 115200 8N1   RAMFS\n");
    /* 明确声明这不是 POSIX shell：实测模型会本能地写 ps -ef、log | tail，
       然后连错好几轮。声明一次比纠正五次便宜。 */
    uart_puts("  输入 help 查看命令");
    if (ADV) uart_puts("  /  manifest 输出能力清单 JSON");
    uart_puts("\n\n");

    /* 常用命令速查：一个空终端只有提示符，用户第一眼不知道该敲什么。
       真实登录 shell 靠 MOTD 解决这件事，这里照做。 */
    /* 每行不超过 57 列：网格是 61 列，写满会触发自动折行，
       折出来的尾巴（比如单独一个 s）看着像 bug。 */
    uart_puts("  help      list all commands     ps     process table\n");
    uart_puts("  ls        files in RAMFS        mem    memory / faults\n");
    uart_puts("  run <f>   run a user program    log    recent events\n");
    uart_puts("  fork      spawn a child         snapshot  snapshot machine\n");
    uart_puts("  cat <f>   show file content     rm     delete a file\n");
    uart_puts("  mkdir <d> create a directory    chmod  permissions\n");
    uart_puts("  write <f> create a file         tail   recent output\n");
    uart_puts("  guide     full walkthrough      manifest  capabilities\n");
    uart_puts("\n");
    prompt();
}