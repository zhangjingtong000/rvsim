#include "kernel.h"
#include "proc.h"

/* s_trap_entry 帧布局：xN 存在偏移 (N-1)*4 */
#define R_A0 9    /* x10 */
#define R_A1 10   /* x11 */
#define R_A7 16   /* x17 */
#include <stdarg.h>

/* shell 与 trap 处理共享的标志（定义在下方） */
extern int  g_run_flag;
extern int  g_fork_flag;
extern int  g_badptr_flag;
int kload_prog(const char *name);
int  g_exec_flag = 0;
char g_exec_name[32] = {0};
extern char g_exec_name[32];
int krestart(void);

/* 切换到指定页表（S 模式，切完刷 TLB） */
static void proc_switch_satp(u32 *pgtbl)
{
    u32 satp = SATP_SV32 | ((u32)pgtbl >> 12);
    __asm__ volatile("csrw satp, %0; sfence.vma" : : "r"(satp) : "memory");
}


extern u32 *kernel_pgtbl(void);
extern void enter_u_mode(u32 entry, u32 stack, u32 pid);
void shell_loop(void);   /* shell 主循环：开机与进程全退出后共用 */
extern const u8 user_prog_start[];
extern const u8 user_prog_end[];
extern const u8 fork_prog_start[];
extern const u8 fork_prog_end[];
extern const u8 badptr_prog_start[];
extern const u8 badptr_prog_end[];

u32 *g_kroot;   /* 供 vm.c 的按需分页使用 */

/* 当前装载的程序的入口与 PT_LOAD 段。
   此前 pc 直接写死 g_user_base，而 .data/.bss 在第二页没人映射——
   进程一访问就缺页拿到零页，数据全丢。 */
u32  g_entry = 0x80400000u;
static u32 g_img_end = 0x80400000u;   /* ELF 装载后用户区的结束地址 */
static u32 g_seg_va[8], g_seg_end[8];
static int g_seg_n = 0;

/* ── 独立计数器 ──
   审计指出：assert 和 log 都读同一份 g_evts，事件流记错了也发现不了。
   这里在 trap 分派处单独计数，完全不经过 evt_emit。
   两边对不上，就说明事件流漏记或多记。 */
static u32 g_trap_ecall, g_trap_exit, g_trap_fork, g_trap_sbrk, g_trap_write;

/* 进入用户态前的寄存器上下文。setreg/setpc 改的就是 procs[].regs，
   但原来的 enter_u_mode 只设 sepc/sp/a0，其余寄存器保持内核值——
   于是改写永远不生效。这里把整份上下文交给汇编去加载。 */
extern void enter_u_mode_ctx(u32 *ctx, u32 entry);
static u32 g_enter_ctx[32];

static void enter_u_mode_with_regs(proc_t *p)
{
    for (int i = 0; i < 32; i++) g_enter_ctx[i] = p->regs[i];
    g_enter_ctx[0]  = 0;          /* x0 恒零 */
    g_enter_ctx[2]  = p->sp;      /* x2 = 用户 sp */
    /* a0 = pid 由 krestart 写进 regs[10]，这里不再覆盖——
       否则 setreg 改 a0 永远不会生效。 */
    enter_u_mode_ctx(g_enter_ctx, p->pc);
}
static u32  g_user_base = 0x80400000u;

/* 每个进程的用户栈（物理页） */
static u32 g_ustack[NPROC];

/* ── 事件日志 ──
   内核的真相不是「当前状态」，而是「发生过什么」。
   状态可以从事件流重放出来，反过来不成立。
   这是可审计、可回放、可回滚三件事的共同底座。 */
#define EVT_CAP   16384
#define EVT_SPAWN   1
#define EVT_EXIT    2
#define EVT_ECALL   3
#define EVT_RESTORE 4
/* 审计指出：只记 spawn/exit/写类 ecall，内核自己的状态转移全都没记账，
   于是「事件是真相」名不副实。以下补齐。 */
#define EVT_RESET    5   /* 内核把进程重置到就绪 */
#define EVT_SNAPSHOT 6   /* 创建快照 */
#define EVT_PLAN     7   /* 登记计划 */
#define EVT_APPLY    8   /* 执行计划 */
#define EVT_SBRK     9   /* 堆扩展 */
#define EVT_FORK     10  /* fork：父进程视角的派生事件 */
#define EVT_SETREG   11  /* 外部改写进程寄存器（调试） */
#define EVT_GETPID   12  /* getpid 系统调用 */
#define EVT_YIELD    13  /* yield 系统调用 */

/* 失败原因码：审计要能回答「为什么被拒」，而不只是「被拒了」 */
#define ECODE_OK      0
#define ECODE_BAD_ADDR 1   /* 地址不在用户区间 */
#define ECODE_TOO_LONG 2   /* 长度超限被截断 */

typedef struct {
    u32 seq;
    u32 actor;      /* 谁发起的：进程号；-1 表示内核自身 */
    u16 type;
    u8  code;       /* 失败原因码，0 表示成功 */
    u8  pad;
    u32 arg0;
    u32 arg1;
    s32 result;
} evt_t;

static evt_t g_evts[EVT_CAP];
static u32 g_evt_head, g_evt_tail, g_evt_seq;
/* 前置声明：这些函数定义在后面，但事件倾倒函数要用到它们 */
void evt_summary(void);
int  evt_count_all(void);
int  evt_count_named(const char *name);
int  evt_count_denied(void);

void evt_emit_code(u16 type, u32 actor, u32 a0, u32 a1, s32 result, u8 code)
{
    u32 nxt = (g_evt_tail + 1u) % EVT_CAP;
    if (nxt == g_evt_head)                       /* 满了就覆盖最旧的一条 */
        g_evt_head = (g_evt_head + 1u) % EVT_CAP;
    evt_t *e = &g_evts[g_evt_tail];
    e->seq    = ++g_evt_seq;
    e->actor  = actor;
    e->type   = type;
    e->code   = code;
    e->arg0   = a0;
    e->arg1   = a1;
    e->result = result;
    g_evt_tail = nxt;
}

void evt_emit(u16 type, u32 actor, u32 a0, u32 a1, s32 result)
{
    evt_emit_code(type, actor, a0, a1, result, ECODE_OK);
}

static const char *evt_name(u16 t)
{
    switch (t) {
    case EVT_SPAWN: return "spawn";
    case EVT_EXIT:  return "exit";
    case EVT_ECALL: return "ecall";
    case EVT_RESTORE:  return "restore";
    case EVT_RESET:    return "reset";
    case EVT_SNAPSHOT: return "snapshot";
    case EVT_PLAN:     return "plan";
    case EVT_APPLY:    return "apply";
    case EVT_SBRK:     return "sbrk";
    case EVT_FORK:     return "fork";
    case EVT_SETREG:   return "setreg";
    case EVT_GETPID:   return "getpid";
    case EVT_YIELD:    return "yield";
    default:        return "?";
    }
}

/* ── 供 shell 的 assert 使用 ──
   AI 不该自己去数事件流：那既慢又容易数错。
   让内核直接回答「是不是这样」，断言一次调用就完成。 */
static int kstreq(const char *a, const char *b)
{
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == *b;
}

int evt_count_named(const char *name)
{
    int n = 0;
    u32 i = g_evt_head;
    while (i != g_evt_tail) {
        if (kstreq(evt_name(g_evts[i].type), name)) n++;
        i = (i + 1u) % EVT_CAP;
    }
    return n;
}

/* 被拒绝的操作数：ecall 且 code != 0。
   「发生过几次越权」是审计的核心指标，AI 不该自己去数。 */
int evt_count_denied(void)
{
    int n = 0;
    u32 i = g_evt_head;
    while (i != g_evt_tail) {
        if (g_evts[i].type == EVT_ECALL && g_evts[i].code != 0) n++;
        i = (i + 1u) % EVT_CAP;
    }
    return n;
}

/* 类型名是否已知：计数为 0 与类型不存在必须区分开 */
int evt_type_known(const char *name)
{
    const char *ok[] = {"spawn", "exit", "ecall", "restore",
                        "reset", "snapshot", "plan", "apply", "sbrk", "fork",
                        "setreg", "getpid", "yield"};
    for (u32 i = 0; i < 13; i++)
        if (kstreq(ok[i], name)) return 1;
    return 0;
}

int evt_count_all(void)
{
    int n = 0;
    u32 i = g_evt_head;
    while (i != g_evt_tail) { n++; i = (i + 1u) % EVT_CAP; }
    return n;
}

/* 就绪 + 运行中的进程数 */
int proc_count_live(void)
{
    int n = 0;
    for (u32 i = 0; i < NPROC; i++)
        if (procs[i].state == PROC_READY || procs[i].state == PROC_RUN) n++;
    return n;
}

/* log 的轻量查询：把「整段倾倒」换成「按需取用」。
   39 条事件倒给模型会撞上下文窗口，而模型会在边界处数错。 */
/* 从事件流重建「谁还活着」。
   这是审计要的独立 oracle：结果不来自 procs[]，而是只看 spawn/exit 事件。
   把它和 ps 对比，就能发现事件流漏记或多记。
   注意：事件缓冲是环形的，被覆盖过就只能承认 complete=false。 */
/* 把存活集合折叠到 seq <= target 为止的状态 */
static void replay_fold_to(u8 *alive, u32 target)
{
    for (u32 k = 0; k < NPROC; k++) alive[k] = 0;
    u32 i = g_evt_head;
    while (i != g_evt_tail) {
        evt_t *e = &g_evts[i];
        if (e->seq > target) break;
        if (e->type == EVT_SPAWN && e->actor < NPROC) alive[e->actor] = 1;
        else if (e->type == EVT_EXIT && e->actor < NPROC) alive[e->actor] = 0;
        else if (e->type == EVT_RESET) {
            /* reset 也必须折叠：否则回滚到 reset 之后的快照时，
               会把此前已 exit 的进程全算成存活。 */
            for (u32 k = 0; k < NPROC; k++) alive[k] = (k < e->arg0);
        }
        i = (i + 1u) % EVT_CAP;
    }
}

/* 从事件流重建「谁还活着」。
   这是审计要的独立 oracle：结果不来自 procs[]，而是只看事件。
   注意 restore/reset 也必须折叠——否则回滚之后 replay 会给出与 ps 矛盾的答案。 */
void evt_replay(void)
{
    u32 total  = (u32)evt_count_all();
    u32 issued = g_evt_seq;
    int complete = (issued == total);

    u8 alive[NPROC];
    for (u32 k = 0; k < NPROC; k++) alive[k] = 0;

    u32 i = g_evt_head;
    while (i != g_evt_tail) {
        evt_t *e = &g_evts[i];
        if (e->type == EVT_SPAWN && e->actor < NPROC) alive[e->actor] = 1;
        else if (e->type == EVT_EXIT && e->actor < NPROC) alive[e->actor] = 0;
        else if (e->type == EVT_RESTORE) {
            /* 回滚：状态回到快照时（arg1 = 该快照的事件序号） */
            replay_fold_to(alive, e->arg1);
        }
        else if (e->type == EVT_RESET) {
            /* 重置：前 arg0 个进程回到就绪（视为存活） */
            for (u32 k = 0; k < NPROC; k++) alive[k] = (k < e->arg0);
        }
        i = (i + 1u) % EVT_CAP;
    }

    kprintf("{\"replay\":{\"complete\":%s,\"events\":%d,\"issued\":%d,\"alive\":[",
            complete ? "true" : "false", (int)total, (int)issued);
    int first = 1;
    for (u32 k = 0; k < NPROC; k++) {
        if (!alive[k]) continue;
        if (!first) kprintf(",");
        first = 0;
        kprintf("%d", (int)k);
    }
    kprintf("]}}\n");
}

void evt_dump_actor(u32 actor)
{
    kprintf("seq actor type arg0 arg1 result code\n");
    u32 i = g_evt_head;
    while (i != g_evt_tail) {
        evt_t *e = &g_evts[i];
        if (e->actor == actor)
            kprintf("%d %d %s %x %x %d code=%d\n",
                    (int)e->seq, (int)e->actor, evt_name(e->type),
                    (u32)e->arg0, (u32)e->arg1, (int)e->result, (int)e->code);
        i = (i + 1u) % EVT_CAP;
    }
    evt_summary();
}

void evt_count_dump(void)
{
    kprintf("{\"log\":{\"total\":%d,\"spawn\":%d,\"exit\":%d,\"ecall\":%d,\"restore\":%d,"
            "\"reset\":%d,\"snapshot\":%d,\"plan\":%d,\"apply\":%d,\"sbrk\":%d,\"fork\":%d,"
            "\"getpid\":%d,\"yield\":%d,\"setreg\":%d,\"denied\":%d}}\n",
            evt_count_all(), evt_count_named("spawn"), evt_count_named("exit"),
            evt_count_named("ecall"), evt_count_named("restore"),
            evt_count_named("reset"), evt_count_named("snapshot"),
            evt_count_named("plan"), evt_count_named("apply"),
            evt_count_named("sbrk"), evt_count_named("fork"),
            evt_count_named("getpid"), evt_count_named("yield"),
            evt_count_named("setreg"), evt_count_denied());
}

void evt_dump_last(u32 n)
{
    u32 total = (u32)evt_count_all();
    if (n > total) n = total;
    u32 skip = total - n;
    u32 i = g_evt_head;
    while (skip--) i = (i + 1u) % EVT_CAP;
    kprintf("seq actor type arg0 arg1 result code\n");
    while (i != g_evt_tail) {
        evt_t *e = &g_evts[i];
        kprintf("%d %d %s %x %x %d code=%d\n",
                (int)e->seq, (int)e->actor, evt_name(e->type),
                (u32)e->arg0, (u32)e->arg1, (int)e->result, (int)e->code);
        i = (i + 1u) % EVT_CAP;
    }
    evt_summary();
}

/* 每次倾倒事件流都在末尾附一行摘要。
   实测：模型会去读完整 log 然后自己数，且在窗口边界数错。
   与其指望它数对，不如把答案直接放在它一定会读到的地方。 */
void evt_summary(void)
{
    kprintf("[summary] total=%d spawn=%d exit=%d ecall=%d restore=%d reset=%d "
            "snapshot=%d plan=%d apply=%d sbrk=%d fork=%d denied=%d\n",
            evt_count_all(), evt_count_named("spawn"), evt_count_named("exit"),
            evt_count_named("ecall"), evt_count_named("restore"),
            evt_count_named("reset"), evt_count_named("snapshot"),
            evt_count_named("plan"), evt_count_named("apply"),
            evt_count_named("sbrk"), evt_count_named("fork"),
            evt_count_named("getpid"), evt_count_named("yield"),
            evt_count_named("setreg"), evt_count_denied());
}

/* ── 能力清单 ──
   AI 读这一份就知道能做什么、参数取值、哪个操作不可逆。
   关键：清单由内核生成，不由应用注册——应用注册的清单会撒谎。 */
typedef struct {
    const char *name;
    const char *usage;      /* 完整命令形式，避免被理解成键值对 */
    const char *values;     /* 枚举取值（空串表示无） */
    u8          irreversible;
    const char *desc;
} cap_t;

static const cap_t g_caps[] = {
    {"help",     "help",                    "",                0, "列出全部命令"},
    {"manifest", "manifest",                "",                0, "输出本能力清单（机器可解析）"},
    {"status",   "status",                  "",                0, "统一状态入口（JSON）：at_prompt 表示发命令时系统是否停在提示符"},
    {"ps",       "ps",                      "",                0, "进程表（JSON）：pc=用户态入口，nrun=被调度次数"},
    {"run",      "run <file>",              "<name>.bin（RAMFS 里的任意程序；裸名自动补 .bin）", 1, "装载指定程序并启动 3 个初始进程；fork 演示会额外创建子进程（共 4 个）"},
    {"reset",    "reset",                   "",                0, "把全部进程重置到就绪"},
    {"mem",      "mem",                     "",                0, "物理内存大小与累计缺页次数（自启动起）"},
    {"uptime",   "uptime",                  "",                0, "读取虚拟时钟 mtime"},
    {"log",      "log [all] | log count [type] | log last <n> | log find <pid>", "spawn|exit|ecall|restore", 0, "事件流：全部（带摘要）/计数/最近 n 条/按进程过滤"},
    {"tail",     "tail",                    "",                0, "输出最近一段串口内容，换行显示为 |"},
    {"assert",   "assert <key>=<value> | <key>: <value> | <key> <value>",    "spawn|exit|ecall|restore|reset|snapshot|plan|apply|sbrk|fork|setreg|getpid|yield=各类事件条数; events=总条数; nproc=活跃进程数; oob=被拒绝的写入次数; plans/snaps=计划数/快照数; at_prompt=是否停在提示符; uart=<要在最近 4KB 输出里查找的子串>", 0, "断言：内核直接回答是否成立，返回 want/got/ok"},
    {"plan",     "plan run <prog> | plan reset | plan restore <snap_id>",         "run 时 prog 取 hello|fork|badptr；restore 时 snap_id 取 snaps 返回的 id", 0, "登记一个计划但不执行；用 apply <id> 执行"},
    {"plans",    "plans",                   "",                0, "列出待执行的计划"},
    {"apply",    "apply <plan_id>",         "",                1, "执行指定计划"},
    {"snapshot", "snapshot",                "",                0, "保存当前进程状态快照，返回快照编号"},
    {"snaps",    "snaps",                   "",                0, "列出快照：id 用于 restore，evt_seq 是该快照时的事件序号"},
    {"restore",  "restore <snap_id>",       "",                1, "恢复到指定快照（回滚，改变当前进程状态）"},
    {"guide",    "guide",                   "",                0, "使用指南：事件字段语义、序号体系、易错点"},
    {"resume",   "resume",                  "",                0, "让就绪进程开始运行（restore/reset 之后用）"},
    {"replay",   "replay",                  "",                0, "从事件流重建存活进程，与 ps 交叉验证"},
    {"regs",     "regs [pid]",              "",                0, "读某进程的 32 个寄存器（默认 pid 0）"},
    {"verify",   "verify",                  "",                0, "交叉验证：trap 侧独立计数 vs 事件流"},
    {"setreg",   "setreg <pid> <reg> <val>", "",               1, "改写进程寄存器（调试用，不可逆）"},
    {"setpc",    "setpc <pid> <addr>",      "",                1, "改写进程执行点（调试用，不可逆）"},
    {"ls",       "ls",                      "",                0, "列出内存文件"},
    {"cat",      "cat <name>",              "",                0, "读取文件内容"},
    {"write",    "write <name> <text>",     "",                0, "写入文件（覆盖），返回字节数"},
    {"rm",       "rm <name>",               "",                1, "删除文件"},
};

/* 统一状态入口：AI 一次调用就能拿到全貌，
   不必分别去调 ps / log / plans / snaps 再自己拼。 */
void status_json(int at_prompt)
{
    extern int plan_count(void);
    extern int snap_count(void);
    extern u32 uart_tail_len(void);
    kprintf("{\"status\":{\"nproc\":%d,\"events\":%d,\"plans\":%d,\"snaps\":%d,"
            "\"uart_len\":%d,\"at_prompt\":%s}}\n",
            proc_count_live(), evt_count_all(), plan_count(), snap_count(),
            (int)uart_tail_len(), at_prompt ? "true" : "false");
}

/* help 由能力表生成，不再手写字符串——
   手写的那份和 manifest 不同步，顺序也不一致。 */
void cap_help(void)
{
    u32 n = (u32)(sizeof(g_caps) / sizeof(g_caps[0]));
    kprintf("命令:");
    for (u32 i = 0; i < n; i++) kprintf(" %s", g_caps[i].name);
    kprintf("\n");
}

/* ── 面向 AI 的使用指南 ──
   manifest 的 desc 只够写一句话，装不下的东西放这里。
   这份内容需要长期维护：新增能力、改变语义时同步更新。 */
/* 交叉验证：独立计数器 vs 事件流。两者不一致就是事件流的 bug。 */
void verify_dump(void)
{
    int ec = evt_count_named("ecall"), ex = evt_count_named("exit");
    int sb = evt_count_named("sbrk"), fk = evt_count_named("fork");

    /* 关键：evt_count_named("ecall") 只统计 write（只有它记 EVT_ECALL），
       所以必须拿 g_trap_write 去比，不能拿 g_trap_ecall。
       之前拿总数比，导致任何含 exit 的负载 ok 恒为 false。 */
    int ok_w  = ((int)g_trap_write == ec);
    int ok_x  = ((int)g_trap_exit  == ex);
    int ok_s  = ((int)g_trap_sbrk  == sb);
    int ok_f  = ((int)g_trap_fork  == fk);

    /* 未记账的 ecall。getpid/yield 现在也记账了，
       所以这个数应当恒为 0；不为 0 就说明真的漏了某类系统调用。 */
    int gp = evt_count_named("getpid"), yl = evt_count_named("yield");
    int unaccounted = (int)g_trap_ecall - (int)g_trap_write
                    - (int)g_trap_exit - (int)g_trap_sbrk - (int)g_trap_fork
                    - gp - yl;

    /* 回绕之后 evt 侧会被截断，此时一切比较都不可信 */
    int complete = ((int)g_evt_seq == evt_count_all());

    kprintf("{\"verify\":{");
    kprintf("\"complete\":%s,", complete ? "true" : "false");
    kprintf("\"write\":{\"trap\":%d,\"evt\":%d,\"ok\":%s},",
            (int)g_trap_write, ec, ok_w ? "true" : "false");
    kprintf("\"exit\":{\"trap\":%d,\"evt\":%d,\"ok\":%s},",
            (int)g_trap_exit, ex, ok_x ? "true" : "false");
    kprintf("\"sbrk\":{\"trap\":%d,\"evt\":%d,\"ok\":%s},",
            (int)g_trap_sbrk, sb, ok_s ? "true" : "false");
    kprintf("\"fork\":{\"trap\":%d,\"evt\":%d,\"ok\":%s},",
            (int)g_trap_fork, fk, ok_f ? "true" : "false");
    kprintf("\"unaccounted_ecall\":%d", unaccounted);
    kprintf("}}\n");
}

void cap_guide(void)
{
    kprintf("{\"guide\":{\n");
    kprintf(" \"shell\":\"非 POSIX：不支持管道/重定向/通配符\",\n");
    kprintf(" \"output\":\"用户程序直接写串口；run 之后的 AAABBBCCC/PPPPCCCC 是程序输出，不是内核日志\",\n");
    kprintf(" \"events\":{\n");
    kprintf("  \"spawn\":\"arg0=入口地址 arg1=用户栈地址\",\n");
    kprintf("  \"exit\":\"arg0=退出码\",\n");
    kprintf("  \"ecall\":\"arg0=缓冲区 arg1=长度 result=写出字节数 code=0成功/1地址非法/2超长\",\n");
    kprintf("  \"restore\":\"arg0=快照id arg1=该快照时的事件序号\"\n");
    kprintf(" },\n");
    kprintf(" \"seq\":\"log 的 seq 是全局单调序号（含已被覆盖的）；log count 的 total 是当前缓冲区条数，两者不同\",\n");
    kprintf(" \"tips\":[\n");
    kprintf("  \"assert 三种写法等价：key=value、key: value、key value\",\n");
    kprintf("  \"log 默认只给最近 30 条，全量用 log all\",\n");
    kprintf("  \"apply 之后计划即被消耗，plans 会变空\",\n");
    kprintf("  \"restore 只恢复进程状态，事件流 append-only 不可回滚\",\n");
    kprintf("  \"run fork 会额外创建子进程，共 4 个进程\"\n");
    kprintf(" ]\n");
    kprintf("}}\n");
}

void cap_manifest(void)
{
    u32 n = (u32)(sizeof(g_caps) / sizeof(g_caps[0]));
    /* 输出单个 JSON 对象（caps 为数组），而不是逐行独立对象：
       逐行形式要调用方自己拼，容易只解析到第一条。 */
    kprintf("{\"manifest\":1,\"caps\":%d,\"list\":[\n", (int)n);
    for (u32 i = 0; i < n; i++) {
        const cap_t *c = &g_caps[i];
        kprintf("%s{\"cap\":\"%s\",\"usage\":\"%s\",\"values\":\"%s\",\"irreversible\":%s,\"desc\":\"%s\"}",
                i ? ",\n" : "", c->name, c->usage, c->values,
                c->irreversible ? "true" : "false", c->desc);
    }
    kprintf("\n]}\n");
}

/* 把事件流按发生顺序打印出来。这是「真相」视图，不是状态投影。 */
void evt_dump_all(void)
{
    u32 i = g_evt_head;
    /* 内核 kprintf 是精简实现，不支持宽度/对齐说明符，只用基本格式 */
    kprintf("seq actor type arg0 arg1 result code\n");
    while (i != g_evt_tail) {
        evt_t *e = &g_evts[i];
        kprintf("%d %d %s %x %x %d code=%d\n",
                (int)e->seq, (int)e->actor, evt_name(e->type),
                (u32)e->arg0, (u32)e->arg1, (int)e->result, (int)e->code);
        i = (i + 1u) % EVT_CAP;
    }
    evt_summary();
}


/* ── M 模式 trap：只处理定时器，切进程 ── */
u32 m_trap_handler(u32 cause, u32 epc, u32 tval, u32 *regs)
{
    (void)tval;
    u32 code = cause & 0x7FFFFFFFu;

    /* ecall from S：SBI 调用（同步异常，必须优先判断） */
    if (!(cause & 0x80000000u) && code == 9) {
        u32 eid = regs[R_A7];
        if (eid == 0x53534950u) {         /* 'SSIP'：清 STIP + 重装定时器 */
            __asm__ volatile("csrc mip, %0" : : "r"(MIP_STIP));
            proc_set_timer(20000);
            regs[R_A0] = 0;
            return epc + 4;
        }
        regs[R_A0] = (u32)-1;
        return epc + 4;
    }

    /* 其他同步异常：打印后跳过 */
    if (!(cause & 0x80000000u)) {
        kprintf("[M-trap] cause=%x epc=%x tval=%x\n", cause, epc, tval);
        return epc + 4;
    }

    if (code == 7) {          /* MTIP：转成 S 模式定时器中断 */
        proc_set_timer(0x7FFFFFF0u);
        /* 置 STIP 交给 S 模式处理（软件写 mip.STIP） */
        __asm__ volatile("csrs mip, %0" : : "r"(MIP_STIP));
        return epc;           /* 回到被打断的指令，马上会进 S 模式 */
    }

    /* 其他 M 模式中断：忽略并清掉 */
    kprintf("[M-irq] code=%d\n", (int)code);
    return epc;
}

/* ── S 模式 trap：系统调用 ── */
/* 切换到 next 进程：保存当前上下文，装载 next 的上下文 */
u32 s_trap_handler(u32 cause, u32 epc, u32 tval, u32 *regs)
{
    /* 定时器中断（S 模式 STI）：抢占当前进程 */
    if ((cause & 0x80000000u) && ((cause & 0x7FFFFFFFu) == 5)) {
        /* 1) 保存当前、切换进程 */
        if (cur_proc >= 0) {
            for (int i = 0; i < 32; i++) procs[cur_proc].regs[i] = regs[i];
            procs[cur_proc].pc = epc;
        }
        static volatile u32 g_retpc;
        proc_t *next = proc_schedule();
        g_retpc = epc;
        if (next) {
            for (int i = 0; i < 32; i++) regs[i] = next->regs[i];
            g_retpc = next->pc;
            if (next->pgtbl) proc_switch_satp(next->pgtbl);
        }
        /* 2) 通过 ecall 让 M 模式清 STIP 并重装定时器（EID='SSIP'）
              注意：ecall 会穿越 M 模式并恢复寄存器，局部变量必须放静态区 */
        register u32 a7 __asm__("a7") = 0x53534950u;
        __asm__ volatile("ecall" : "+r"(a7) :: "memory");
        return g_retpc;
    }

    /* 缺页异常：按需分配 */
    if (cause == 13 || cause == 15) {
        u32 *pt = (cur_proc >= 0 && procs[cur_proc].pgtbl)
                  ? procs[cur_proc].pgtbl : g_kroot;
        if (vm_page_fault_on(pt, tval, cause == 15))
            return epc;          /* 重试出错的指令 */
        procs[cur_proc].state = PROC_EXIT;
        proc_t *nx = proc_schedule();
        if (nx) return nx->pc;
        *(volatile u32 *)SYSCON_BASE = 0x5555u;
        for (;;) { }
    }

    if (cause == 8) {   /* ecall from U */
        u32 a7 = regs[R_A7];
        u32 a0 = regs[R_A0];

        /* 独立计数：此处不做任何事件记录，纯计数 */
        g_trap_ecall++;
        /* 高频但无副作用的两个调用也记账，让 unaccounted 能为 0 */
        if (a7 == 3) evt_emit(EVT_GETPID, (u32)cur_proc, 0, 0, (s32)cur_proc);
        if (a7 == 7) evt_emit(EVT_YIELD,  (u32)cur_proc, 0, 0, 0);
        if (a7 == 0) g_trap_exit++;
        if (a7 == 1) g_trap_write++;   /* 只有 write 会记 EVT_ECALL */
        if (a7 == 5) g_trap_sbrk++;
        if (a7 == 6) g_trap_fork++;


        if (a7 == 1) {           /* write(buf, len) */
            u32 buf = regs[R_A0];
            u32 len = regs[R_A1];
            /* 用户指针校验（相当于 copy_from_user 的最小实现）：
               1) 只允许用户地址区间——否则用户程序能 write(0x80000000) 把内核 .text 打到串口
               2) 长度设上限——否则 len=0x7FFFFFFF 会长时间占死模拟器
               3) 逐字节判断，越界即截断（跨页时不会去读内核页） */
            u8  code = ECODE_OK;
            u32 raw_len = len;
            if (len > 4096u) { len = 4096u; code = ECODE_TOO_LONG; }
            u32 n = 0;
            for (; n < len; n++) {
                u32 va = buf + n;
                if (va < 0x80400000u || va >= 0x80800000u) {
                    /* 地址越界是更严重的错，优先报告 */
                    code = ECODE_BAD_ADDR;
                    break;
                }
                uart_putc((char)((volatile u8 *)va)[0]);
            }
            /* 一个字节都没写出去说明指针整体非法，返回 -1 让用户程序能察觉 */
            regs[R_A0] = (n == 0 && len > 0) ? (u32)-1 : n;
            evt_emit_code(EVT_ECALL, (u32)cur_proc, buf, raw_len,
                          (n == 0 && len > 0) ? -1 : (s32)n, code);
            return epc + 4;
        }
        if (a7 == 0) {           /* exit */
            kprintf("\n[U] proc %d exit(%d)\n", cur_proc, a0);
            evt_emit(EVT_EXIT, (u32)cur_proc, a0, 0, 0);
            procs[cur_proc].state = PROC_EXIT;
            proc_t *next = proc_schedule();
            if (!next) {
                kprintf("\n[S] 所有进程结束，回到 shell\n");
                shell_loop();          /* 不返回 */
            }
            for (int i = 0; i < 32; i++) regs[i] = next->regs[i];
            return next->pc;
        }
        if (a7 == 5) {           /* sbrk(inc)：扩展用户堆，返回旧堆顶 */
            u32 inc = regs[R_A0];
            proc_t *pc = &procs[cur_proc];
            if (pc->brk == 0) pc->brk = USER_HEAP_BASE + (u32)cur_proc * 0x10000u;
            u32 oldbrk = pc->brk;
            s32 delta = (s32)inc;
            u32 newbrk = (u32)((s32)oldbrk + delta);
            u32 base = USER_HEAP_BASE + (u32)cur_proc * 0x10000u;
            if (newbrk < base || newbrk > USER_HEAP_END) {
                regs[R_A0] = (u32)-1;
                return epc + 4;
            }
            /* 按需映射新增的页到【本进程】页表（用 vm_handle_page_fault 会错映射进 g_kroot，
               预映射对进程不可见，只能靠缺页兜底再分配一页，白占物理内存） */
            u32 *pt = pc->pgtbl ? pc->pgtbl : g_kroot;
            for (u32 a = oldbrk & ~0xFFFu; a < newbrk; a += 4096u)
                vm_page_fault_on(pt, a, 1);
            pc->brk = newbrk;
            regs[R_A0] = oldbrk;
            evt_emit(EVT_SBRK, (u32)cur_proc, oldbrk, newbrk, (s32)oldbrk);
            return epc + 4;
        }
        if (a7 == 6) {           /* fork：复制当前进程 */
            int child = -1;
            for (int i = 0; i < NPROC; i++) {
                if (procs[i].state == PROC_EXIT) { child = i; break; }
            }
            if (child < 0) { regs[R_A0] = (u32)-1; return epc + 4; }

            proc_t *pp = &procs[cur_proc];
            proc_t *cc = &procs[child];
            *cc = *pp;
            cc->pid   = child;
            cc->state = PROC_READY;
            /* 两条事件：一条是父进程视角的 fork，一条是子进程的 spawn。
               只有 spawn 的话，分不清这个进程是开机创建的还是 fork 出来的。 */
            evt_emit(EVT_FORK,  (u32)cur_proc, (u32)child, 0, 1);
            evt_emit(EVT_SPAWN, (u32)child, cc->pc, cc->sp, 0);
            /* 复制当前寄存器（不是上次保存的快照） */
            for (int k = 0; k < 32; k++) cc->regs[k] = regs[k];

            /* 子进程独立页表 + 复制已映射的用户页 */
            u32 *root = vm_proc_pgtbl();
            cc->pgtbl = root;
            u32 uvpn2 = VPN2(g_user_base);
            u32 *src_l1 = (u32 *)(((u32)pp->pgtbl[uvpn2] >> 10) << 12);
            u32 *dst_l1 = (u32 *)(((u32)root[uvpn2] >> 10) << 12);
            for (u32 i = 0; i < 1024; i++) {
                if (!(src_l1[i] & PTE_V)) continue;
                u32 *src_pa = (u32 *)(((u32)src_l1[i] >> 10) << 12);
                u32 *dst_pa = page_alloc();
                memcpy(dst_pa, src_pa, 4096);
                dst_l1[i] = ((u32)dst_pa >> 12 << 10) | (src_l1[i] & 0x3FFu);
            }
            /* 子进程返回 0，父进程返回子 pid */
            cc->regs[R_A0] = 0;
            cc->pc = epc + 4;
            regs[R_A0] = (u32)child;
            return epc + 4;
        }
        if (a7 == 3) {           /* getpid */
            regs[R_A0] = (u32)cur_proc;
            return epc + 4;
        }
        if (a7 == 4) {           /* yield */
            if (cur_proc >= 0) {
                for (int i = 0; i < 32; i++) procs[cur_proc].regs[i] = regs[i];
                procs[cur_proc].pc = epc + 4;
            }
            proc_t *next = proc_schedule();
            if (next) {
                for (int i = 0; i < 32; i++) regs[i] = next->regs[i];
                if (next->pgtbl) proc_switch_satp(next->pgtbl);
                return next->pc;
            }
            return epc + 4;
        }
        regs[R_A0] = (u32)-1;
        return epc + 4;
    }
    kprintf("[S-trap] cause=%x epc=%x tval=%x\n", cause, epc, tval);
    return epc + 4;
}

int g_run_flag = 0;
int g_fork_flag = 0;
int g_badptr_flag = 0;

/* 重置所有进程到入口（shell 的 run 命令用） */
/* 供 plan 在登记阶段就校验参数，而不是等到 apply 才静默跑错 */
int kprog_valid(const char *name)
{
    const char *ok[] = {"hello", "fork", "badptr"};
    for (u32 i = 0; i < 3; i++) {
        const char *a = ok[i]; const char *b = name;
        while (*a && *b && *a == *b) { a++; b++; }
        if (!*a && !*b) return 1;
    }
    return 0;
}

/* 按名字选择要加载的用户程序（hello / fork）。len 返回镜像长度。 */

static char g_loaded_prog[8] = "hello";

/* 从 RAMFS 装载程序到用户代码区。
   名字可以直接给文件名（如 hello.bin），也接受裸名（hello -> hello.bin），
   兼容旧的 run hello / run fork / run badptr。 */
int kload_prog(const char *name)
{
    if (!name || !name[0]) return -1;

    char key[24];
    int n = 0;
    while (name[n] && n < 23) { key[n] = name[n]; n++; }
    key[n] = 0;

    /* 裸名补后缀：先试 .elf，再试 .bin（兼容旧镜像） */
    int has_dot = 0;
    for (int i = 0; key[i]; i++) if (key[i] == '.') { has_dot = 1; break; }

    static u8 img[16384];
    int len = -1;
    if (has_dot) {
        len = fs_read(key, img, sizeof(img));
    } else {
        char t[24];
        for (int i = 0; i <= n; i++) t[i] = key[i];
        if (n + 5 < 23) { t[n]='.'; t[n+1]='e'; t[n+2]='l'; t[n+3]='f'; t[n+4]=0;
                          len = fs_read(t, img, sizeof(img));
                          if (len > 0) { for (int i = 0; i <= n + 4; i++) key[i] = t[i]; } }
        if (len <= 0 && n + 5 < 23) { t[n]='.'; t[n+1]='b'; t[n+2]='i'; t[n+3]='n'; t[n+4]=0;
                          len = fs_read(t, img, sizeof(img));
                          if (len > 0) { for (int i = 0; i <= n + 4; i++) key[i] = t[i]; } }
    }
    if (len <= 0) return -1;

    /* 写用户区前必须切回内核页表：fork 之后 satp 还停在上一个进程的
       页表上，此时按虚拟地址写 0x80400000 会落到错误的物理页。 */
    proc_switch_satp(g_kroot);

    /* 真正的 ELF 加载：按 PT_LOAD 段落到各自的虚拟地址，BSS 清零 */
    u32 entry = 0, img_end = 0;
    if (!elf_load(img, (u32)len, &entry, &img_end)) return -2;
    g_entry = entry;
    g_img_end = img_end;
    g_seg_n = elf_segments(img, (u32)len, g_seg_va, g_seg_end, 8);

    /* 记住装的是哪个程序：快照靠它回滚代码区 */
    { int k = 0; while (key[k] && k < 7) { g_loaded_prog[k] = key[k]; k++; } g_loaded_prog[k] = 0; }

    return len;
}

/* 给一个进程建立独立的代码/数据物理页并映射。
   此前所有进程共享 0x80400000 那一块物理页，一个进程写自己的 .data，
   别的进程就能看到——那不算「独立的地址空间」。 */
static void proc_setup_image(int i)
{
    u32 npages = (g_img_end > 0x80400000u)
               ? ((g_img_end - 0x80400000u + 4095u) / 4096u) : 1u;
    if (npages > 4) npages = 4;

    u32 *pa = page_alloc_n(npages);
    procs[i].code_pa    = (u32)pa;
    procs[i].code_pages = npages;

    /* 把模板（装载在 0x80400000 的那份）复制成本进程私有副本 */
    memcpy((void *)pa, (void *)0x80400000u, npages * 4096u);

    if (!procs[i].pgtbl) procs[i].pgtbl = vm_proc_pgtbl();
    for (u32 p = 0; p < npages; p++)
        vm_map_code(procs[i].pgtbl, 0x80400000u + p * 4096u, (u32)pa + p * 4096u);
}

int krestart(void)
{
    evt_emit(EVT_RESET, (u32)-1, NPROC_INIT, 0, 1);
    for (u32 k = 0; k < NPROC_INIT; k++) {
        procs[k].pc    = g_entry;
        procs[k].sp    = g_ustack[k] + 4096u - 16u;
        procs[k].state = PROC_READY;
        procs[k].brk   = 0;
        memset(procs[k].regs, 0, sizeof(procs[k].regs));
        procs[k].regs[1]  = procs[k].sp;
        procs[k].regs[10] = k;
        proc_setup_image((int)k);
    }
    for (u32 k = NPROC_INIT; k < NPROC; k++) procs[k].state = PROC_EXIT;
    cur_proc = -1;
    front_sync();
    return (int)NPROC_INIT;
}

/* 供 shell 的 ps 命令调用 */
void shell_ps_hook(void)
{
    /* 只输出真正用过的槽：夹着 UNUSED 会让模型以为进程更多，
       而且那些槽的 pid 都是 0，看起来像重复项。 */
    kprintf("{\"nproc\":%d,\"procs\":[", proc_count_live());
    int first = 1;
    for (int i = 0; i < NPROC; i++) {
        if (procs[i].stack_pa == 0) continue;
        if (!first) kprintf(",");
        first = 0;
        const char *st = procs[i].state == PROC_RUN ? "RUN" :
                         procs[i].state == PROC_READY ? "READY" : "EXIT";
        kprintf("{\"pid\":%d,\"state\":\"%s\",\"pc\":\"%x\",\"nrun\":%d}",
                (int)procs[i].pid, st, (u32)procs[i].pc, (int)procs[i].nrun);
    }
    kprintf("]}\n");
}

/* 把内嵌的三个程序镜像灌进 RAMFS。
   这样 run 走的是「读文件 → 装载」，而不是「按名字挑 .incbin 符号」，
   文件系统里的程序才第一次成为真正的可执行物。 */
static void fs_seed(void)
{
    fs_write("hello.elf",  user_prog_start,  (u32)(user_prog_end  - user_prog_start));
    fs_write("fork.elf",   fork_prog_start,  (u32)(fork_prog_end  - fork_prog_start));
    fs_write("badptr.elf", badptr_prog_start,(u32)(badptr_prog_end - badptr_prog_start));
    {
        const char *msg = "rvsim RAMFS: try ls, cat readme.txt, run hello.elf";
        u32 mlen = 0;
        while (msg[mlen]) mlen++;
        fs_write("readme.txt", (const u8 *)msg, mlen);
    }
}

void kmain(void)
{
    kprintf("\n=== rvsim mini-kernel (multiproc) ===\n");

    g_kroot = kernel_pgtbl();
    u32 satp = SATP_SV32 | ((u32)g_kroot >> 12);
    __asm__ volatile("csrw satp, %0; sfence.vma" : : "r"(satp) : "memory");

    /* 文件系统先就位：程序现在是「文件系统里的一个文件」，
       不再是直接 memcpy 的裸二进制。 */
    fs_init();
    fs_seed();
    if (kload_prog("hello.elf") < 0) {
        kprintf("[S] 初始程序装载失败\n");
        for (;;) { }
    }

    /* 用户页表：只映射代码页；数据页与栈页留给缺页异常按需分配 */
    u32 uvpn2 = VPN2(g_user_base);
    u32 *ul1 = page_alloc();
    g_kroot[uvpn2] = ((u32)ul1 >> 12 << 10) | PTE_V;
    {
        u32 code_pa = g_user_base & ~0xFFFu;      /* 代码页物理地址 */
        ul1[VPN1(g_user_base)] = (u32)(((code_pa >> 12) << 10)
                                     | PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D);
    }

    /* 创建 3 个进程，每个 4KB 用户栈 */
    proc_init();
    for (u32 i = NPROC_INIT; i < NPROC; i++) procs[i].state = PROC_EXIT;
    for (u32 i = 0; i < NPROC_INIT; i++) {
        g_ustack[i] = g_user_base + 0x10000u + i * 0x2000u;   /* 64KB 之后，间隔 8KB */
        memset((void *)g_ustack[i], 0, 4096);
        proc_create(i, g_entry, g_ustack[i]);
        /* 先建立私有代码页，再记事件——否则 spawn 里记下的 code_pa 还是 0 */
        proc_setup_image((int)i);
        evt_emit(EVT_SPAWN, i, g_entry, procs[i].code_pa, 0);
    }

    /* 定时器：默认关闭（yield 已足够验证轮转；打开见 RVSIM_TIMER 环境无对应，
       如需启用改成 1）。保留代码路径以便后续完善。 */

    /* 使能 M 模式定时器中断与全局中断（必须在 M 模式做） */
    __asm__ volatile("csrs mie, %0" : : "r"(0x80u));    /* MTIE */
    __asm__ volatile("csrs mstatus, %0" : : "r"(MSTATUS_MIE));
    proc_set_timer(20000);


    /* 进 S 模式 */
    extern void s_main(void);
    enter_s_mode((u32)s_main, 0x803FF000u);
}

/* ── 状态快照 ──
   注意：快照恢复的是「状态」，不是「历史」。事件流是 append-only 的，
   回滚本身也要记一条事件——否则审计就失真了：
   系统看起来从没离开过那个状态，而实际上它被回滚过。 */
#define SNAP_SLOTS 4

#define SNAP_CODE_BYTES 8192u   /* 用户代码区（程序镜像通常 < 8KB） */
#define SNAP_STK_BYTES  4096u   /* 每进程一个栈页 */

typedef struct {
    u8     used;
    u32    id;
    u32    evt_seq;
    proc_t snap[NPROC];
    /* 代码区靠程序名复原。栈不保存：进程在入口重新执行时会自己初始化栈，
       保存旧栈内容既无意义也读不到（进程页表下访问各进程栈页会缺页）。 */
    char   prog[8];
} snap_t;

static snap_t g_snaps[SNAP_SLOTS];
static u32    g_snap_seq;

u32 snap_take(void)
{
    /* 栈页在进程页表里是 U 页，S 模式访问需要 SUM。
       从用户态 trap 回来之后 SUM 未必还开着，这里显式打开。 */
    __asm__ volatile("csrs sstatus, %0" : : "r"(0x40000u));
    for (u32 i = 0; i < SNAP_SLOTS; i++) {
        if (!g_snaps[i].used) {
            g_snaps[i].used    = 1;
            g_snaps[i].id      = ++g_snap_seq;
            g_snaps[i].evt_seq = g_evt_seq;
            memcpy(g_snaps[i].snap, procs, sizeof(procs));
            /* 记录代码区状态：回滚时按这个名字重新装载 */
            {
                int k = 0;
                while (g_loaded_prog[k] && k < 7) { g_snaps[i].prog[k] = g_loaded_prog[k]; k++; }
                g_snaps[i].prog[k] = 0;
            }

            evt_emit(EVT_SNAPSHOT, (u32)-1, g_snaps[i].id, g_snaps[i].evt_seq, 1);
            return g_snaps[i].id;
        }
    }
    return 0;
}

/* 只查存在性，不执行——让 plan/apply 能在「真的做」之前拒绝 */
int snap_exists(u32 id)
{
    for (u32 i = 0; i < SNAP_SLOTS; i++)
        if (g_snaps[i].used && g_snaps[i].id == id) return 1;
    return 0;
}

int snap_restore(u32 id)
{
    __asm__ volatile("csrs sstatus, %0" : : "r"(0x40000u));
    for (u32 i = 0; i < SNAP_SLOTS; i++) {
        if (g_snaps[i].used && g_snaps[i].id == id) {
            memcpy(procs, g_snaps[i].snap, sizeof(procs));
            /* 恢复内存前必须切回内核页表：调用方可能正跑在某个进程页表里，
               直接写公共用户区会落到错误映射上。 */
            /* 代码区复原：按快照记录的程序名重新装载 */
            kload_prog(g_snaps[i].prog);

            cur_proc = -1;
            front_sync();
            evt_emit(EVT_RESTORE, (u32)-1, id, g_snaps[i].evt_seq, 1);
            return 1;
        }
    }
    return 0;
}

int snap_count(void)
{
    int n = 0;
    for (u32 i = 0; i < SNAP_SLOTS; i++) if (g_snaps[i].used) n++;
    return n;
}

void snap_list(void)
{
    int first = 1;
    kprintf("{\"snapshots\":[");
    for (u32 i = 0; i < SNAP_SLOTS; i++) {
        if (!g_snaps[i].used) continue;
        if (!first) kprintf(",");
        first = 0;
        kprintf("{\"id\":%d,\"evt_seq\":%d}", (int)g_snaps[i].id, (int)g_snaps[i].evt_seq);
    }
    kprintf("]}\n");
}

/* ── 执行计划：先看后做 ──
   AI 不该盲试破坏性操作。plan 只生成「会做什么」的描述，apply 才真的执行。
   这就是「预演与提交分离」：预演和提交共享同一份计划，不会走样。 */
#define PLAN_SLOTS 4
#define PLAN_RUN   1

typedef struct {
    u8   used;
    u32  id;
    u8   kind;
    char arg[12];
} plan_t;

static plan_t g_plans[PLAN_SLOTS];
static u32    g_plan_seq;

/* 内核没有 libc，自带一个带边界的复制 */
static void kstrcpy_n(char *d, const char *s, u32 cap)
{
    u32 i = 0;
    while (i + 1u < cap && s[i]) { d[i] = s[i]; i++; }
    d[i] = 0;
}

/* 登记一个计划；槽位用尽返回 0。arg 可为 NULL（无参计划）。 */
u32 plan_make(u8 kind, const char *arg)
{
    for (u32 i = 0; i < PLAN_SLOTS; i++) {
        if (!g_plans[i].used) {
            g_plans[i].used = 1;
            g_plans[i].id   = ++g_plan_seq;
            g_plans[i].kind = kind;
            if (arg) kstrcpy_n(g_plans[i].arg, arg, sizeof(g_plans[i].arg));
            else     g_plans[i].arg[0] = 0;
            evt_emit(EVT_PLAN, (u32)-1, g_plans[i].id, kind, 1);
            return g_plans[i].id;
        }
    }
    return 0;
}

/* 取出并消费某计划。找到返回 1，并通过 kind_out/arg_out 交回内容。 */
int plan_take(u32 id, u8 *kind_out, char *arg_out, u32 cap)
{
    for (u32 i = 0; i < PLAN_SLOTS; i++) {
        if (g_plans[i].used && g_plans[i].id == id) {
            *kind_out = g_plans[i].kind;
            kstrcpy_n(arg_out, g_plans[i].arg, cap);
            g_plans[i].used = 0;
            return 1;
        }
    }
    return 0;
}

int plan_count(void)
{
    int n = 0;
    for (u32 i = 0; i < PLAN_SLOTS; i++) if (g_plans[i].used) n++;
    return n;
}

void plan_list(void)
{
    int first = 1;
    kprintf("{\"plans\":[");
    for (u32 i = 0; i < PLAN_SLOTS; i++) {
        if (!g_plans[i].used) continue;
        if (!first) kprintf(",");
        first = 0;
        const char *in = g_plans[i].kind == PLAN_RUN ? "run" :
                         g_plans[i].kind == PLAN_RESET ? "reset" : "restore";
        kprintf("{\"id\":%d,\"intent\":\"%s\",\"arg\":\"%s\"}",
                (int)g_plans[i].id, in, g_plans[i].arg);
    }
    kprintf("]}\n");
}

/* 让当前就绪进程开始运行。restore / reset 只把进程置为 READY，
   没有这个入口它们就永远停在就绪态——「恢复成功」是名义上的。 */
/* AI 侧也必须能读寄存器：前端一直可以直接 _rv_reg(i)，
   如果 agent 读不到，所谓「同一套接口」就不成立。 */
/* 写寄存器：前端一直能通过 _rv_set_reg 做，AI 侧没有就是不对称。
   这确实是危险操作（改 pc 会让系统跑飞），所以 manifest 里标 irreversible，
   并且每次改写都记一条事件，事后可追。 */
void proc_set_reg(int pid, int reg, u32 val)
{
    if (pid < 0 || pid >= NPROC || procs[pid].stack_pa == 0) {
        kprintf("{\"ok\":false,\"why\":\"进程不存在\"}\n");
        return;
    }
    if (reg < 0 || reg >= 32) {
        kprintf("{\"ok\":false,\"why\":\"寄存器号应为 0..31\"}\n");
        return;
    }
    if (reg == 0) {
        /* x0 在硬件上恒为 0，写它没有意义，明确拒绝而不是假装成功 */
        kprintf("{\"ok\":false,\"why\":\"x0 恒为 0，不可写\"}\n");
        return;
    }
    procs[pid].regs[reg] = val;
    /* x2 是栈指针，进程表里另有一份 sp 字段；不同步的话，
       进入用户态时会用旧 sp 覆盖掉这次改写。 */
    if (reg == 2) procs[pid].sp = val;
    evt_emit(EVT_SETREG, (u32)pid, (u32)reg, val, 1);
    kprintf("{\"setreg\":{\"pid\":%d,\"reg\":%d,\"val\":\"%x\"}}\n", pid, reg, val);
}

/* 改写执行点。前端一直能 _rv_set_pc，AI 侧也要有才叫对称。
   比改通用寄存器更危险（可以跳到任意指令），标 irreversible。 */
void proc_set_pc(int pid, u32 addr)
{
    if (pid < 0 || pid >= NPROC || procs[pid].stack_pa == 0) {
        kprintf("{\"ok\":false,\"why\":\"进程不存在\"}\n");
        return;
    }
    procs[pid].pc = addr;
    evt_emit(EVT_SETREG, (u32)pid, 32u, addr, 1);   /* reg=32 约定表示 pc */
    kprintf("{\"setpc\":{\"pid\":%d,\"pc\":\"%x\"}}\n", pid, addr);
}

void proc_dump_regs(int pid)
{
    if (pid < 0 || pid >= NPROC || procs[pid].stack_pa == 0) {
        kprintf("{\"ok\":false,\"why\":\"进程不存在\"}\n");
        return;
    }
    kprintf("{\"pid\":%d,\"pc\":\"%x\",\"sp\":\"%x\",\"regs\":{",
            pid, (u32)procs[pid].pc, (u32)procs[pid].regs[2]);
    for (int i = 0; i < 32; i++) {
        if (i) kprintf(",");
        kprintf("\"x%d\":\"%x\"", i, (u32)procs[pid].regs[i]);
    }
    kprintf("}}\n");
}

void kernel_resume(void)
{
    proc_t *p = proc_schedule();
    if (!p) { kprintf("{\"ok\":false,\"why\":\"没有可运行的进程\"}\n"); return; }
    kprintf("{\"resume\":true,\"pid\":%d}\n", (int)p->pid);
    if (p->pgtbl) proc_switch_satp(p->pgtbl);
    proc_set_timer(20000);
    enter_u_mode_with_regs(p);
}

/* apply 也要记账：谁在什么时候执行了哪个计划 */
void kernel_note_apply(u32 plan_id)
{
    evt_emit(EVT_APPLY, (u32)-1, plan_id, 0, 1);
}

/* 执行 reset 计划：重置进程到就绪，返回 */
void kernel_apply_reset(void)
{
    int n = krestart();
    kprintf("[S] 已重置 %d 个进程\n", n);
}

/* 执行 restore 计划：回滚到指定快照，返回 1 表示成功 */
int kernel_apply_restore(u32 id)
{
    return snap_restore(id);
}

/* 真正执行：装载、重置、调度、进用户态。不返回。 */
void kernel_apply_run(const char *prog)
{
    if (kload_prog(prog) < 0) {
        /* 装载失败必须明确报错并退回 shell，不能继续调度旧代码 */
        kprintf("[S] 装载失败: %s（文件不存在或过大）\n", prog);
        return;
    }
    int n2 = krestart();
    kprintf("[S] 已重置 %d 个进程，重新调度\n", n2);
    proc_t *p = proc_schedule();
    if (!p) { kprintf("[S] 调度失败\n"); return; }
    if (p->pgtbl) proc_switch_satp(p->pgtbl);
    proc_set_timer(20000);      /* 恢复时间片，进程才能轮转 */
    enter_u_mode_with_regs(p);
}

/* shell 主循环：轮询 UART 收字符；收到 run 就装载镜像、重置进程并调度。
   开机时由 s_main 直接调用；所有用户进程退出后由 trap 处理程序调用。
   enter_u_mode 不返回，所以一旦真正启动进程，本函数就被"挂起"在
   内核栈上，等进程全部退出后基址重新进入。 */
void shell_loop(void)
{
    extern void shell_start(void);
    extern void shell_input(char);
    extern int g_run_flag, g_fork_flag, g_badptr_flag;

    /* shell 不是进程，不该被时间片打断：一旦 s_trap_handler 把 PC
       切到用户进程就会指令缺页。停掉定时器，进用户态前再恢复。 */
    proc_set_timer(0x7FFFFFF0u);

    /* S 模式默认 SUM=0，不能读写用户页（U 位）的映射。
       kload_prog 要把镜像写进 0x80400000 这个用户页，必须开 SUM。
       原来的 shell 跑在 M 模式 trap 里不受此限制，挪到 S 模式后必须显式打开。 */
    __asm__ volatile("csrs sstatus, %0" : : "r"(0x40000u));

    shell_start();
    volatile u8 *lsr = (volatile u8 *)(UART_BASE + 5);
    volatile u8 *rbr = (volatile u8 *)(UART_BASE + 0);
    for (;;) {
        if (*lsr & 0x01) shell_input((char)*rbr);
        if (g_run_flag) {
            /* 必须在调用前算好并清零：kernel_apply_run 进用户态后不返回，
               写在它后面的清零是死代码，第二次 run 会沿用上一次的程序。 */
            const char *prog = g_exec_name[0] ? g_exec_name :
                               (g_badptr_flag ? "badptr" :
                               (g_fork_flag ? "fork" : "hello"));
            g_run_flag   = 0;
            g_badptr_flag = 0;
            g_fork_flag  = 0;
            /* 不清空 g_exec_name：prog 正指向它，清空会让错误信息里的文件名变成空。
               下次 run 会覆盖。 */
            kernel_apply_run(prog);
        }
    }
}

void s_main(void)
{
    kprintf("[S] 进入 S 模式\n");

    /* 使能 S 模式定时器中断 */
    __asm__ volatile("csrs sie, %0" : : "r"(SIE_STIE));
    __asm__ volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE));

    /* 开机直接进 shell。多进程演示改由 run 命令触发：
       否则要先空转约 3000 万条指令看完 60 个 A/B/C 才看得到提示符，
       开机体验等于不可用。 */
    shell_loop();
    for (;;) { }
}