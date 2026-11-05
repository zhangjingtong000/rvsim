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
extern int  g_exec_flag;
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
extern const u8 user_prog_start[];
extern const u8 user_prog_end[];
extern const u8 fork_prog_start[];
extern const u8 fork_prog_end[];
extern const u8 badptr_prog_start[];
extern const u8 badptr_prog_end[];

u32 *g_kroot;   /* 供 vm.c 的按需分页使用 */
static u32  g_user_base = 0x80400000u;

/* 每个进程的用户栈（物理页） */
static u32 g_ustack[NPROC];

/* ── M 模式 trap：只处理定时器，切进程 ── */
u32 m_trap_handler(u32 cause, u32 epc, u32 tval, u32 *regs)
{
    (void)tval;
    u32 code = cause & 0x7FFFFFFFu;

    /* ecall from S：SBI 调用（同步异常，必须优先判断） */
    if (!(cause & 0x80000000u) && code == 9) {
        u32 eid = regs[R_A7];
        if (eid == 0x53534950u) {         /* 'SSIP'：清 STIP + 重装定时器 */
            __asm__ volatile("csrc mip, %0" : : "r"(0x20u));
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
        __asm__ volatile("csrs mip, %0" : : "r"(0x20u));
        return epc;           /* 回到被打断的指令，马上会进 S 模式 */
    }

    /* 其他 M 模式中断：忽略并清掉 */
    kprintf("[M-irq] code=%d\n", (int)code);
    return epc;
}

/* ── S 模式 trap：系统调用 ── */
/* 切换到 next 进程：保存当前上下文，装载 next 的上下文 */
static u32 __attribute__((unused)) switch_to(void *unused, u32 *regs, u32 epc, proc_t *next)
{
    (void)unused;
    if (cur_proc >= 0) {
        for (int i = 0; i < 32; i++) procs[cur_proc].regs[i] = regs[i];
        procs[cur_proc].pc = epc;
    }
    for (int i = 0; i < 32; i++) regs[i] = next->regs[i];
    return next->pc;
}

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


        if (a7 == 1) {           /* write(buf, len) */
            u32 buf = regs[R_A0];
            u32 len = regs[R_A1];
            /* 用户指针校验（相当于 copy_from_user 的最小实现）：
               1) 只允许用户地址区间——否则用户程序能 write(0x80000000) 把内核 .text 打到串口
               2) 长度设上限——否则 len=0x7FFFFFFF 会长时间占死模拟器
               3) 逐字节判断，越界即截断（跨页时不会去读内核页） */
            if (len > 4096u) len = 4096u;
            u32 n = 0;
            for (; n < len; n++) {
                u32 va = buf + n;
                if (va < 0x80400000u || va >= 0x80800000u) break;
                uart_putc((char)((volatile u8 *)va)[0]);
            }
            /* 一个字节都没写出去说明指针整体非法，返回 -1 让用户程序能察觉 */
            regs[R_A0] = (n == 0 && len > 0) ? (u32)-1 : n;
            return epc + 4;
        }
        if (a7 == 0) {           /* exit */
            kprintf("\n[U] proc %d exit(%d)\n", cur_proc, a0);
            procs[cur_proc].state = PROC_EXIT;
            proc_t *next = proc_schedule();
            if (!next) {
                kprintf("\n[S] 所有进程结束，进入 shell\n");
                extern void shell_start(void);
                extern void shell_input(char);
                shell_start();
                volatile u8 *lsr2 = (volatile u8 *)(UART_BASE + 5);
                volatile u8 *rbr2 = (volatile u8 *)(UART_BASE + 0);
                for (;;) {
                    if (*lsr2 & 0x01) shell_input((char)*rbr2);
                    if (g_run_flag) {
                        g_run_flag = 0;
                        kload_prog(g_badptr_flag ? "badptr" : (g_fork_flag ? "fork" : "hello"));
                        g_badptr_flag = 0;
                        /* run = 重置所有进程 + 立即调度（不需要先敲 reset） */
                        int n2 = krestart();
                        kprintf("[S] run: 已重置 %d 个进程，重新调度\n", n2);
                        proc_t *p = proc_schedule();
                        if (!p) { kprintf("[S] 调度失败\n"); continue; }
                        if (p->pgtbl) proc_switch_satp(p->pgtbl);
                        enter_u_mode(p->pc, p->sp, p->pid);
                    }
                }
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
/* 按名字选择要加载的用户程序（hello / fork）。len 返回镜像长度。 */
static u32 g_cur_prog_len = 0;

int kload_prog(const char *name)
{
    const u8 *src = user_prog_start;
    u32 len = (u32)(user_prog_end - user_prog_start);
    if (name && name[0]) {
        const char *want_fork = "fork";
        int is_fork = 1;
        for (int i = 0; want_fork[i]; i++) {
            if (name[i] != want_fork[i]) { is_fork = 0; break; }
        }
        if (is_fork && name[4] == 0) {
            src = fork_prog_start;
            len = (u32)(fork_prog_end - fork_prog_start);
        } else {
            const char *want_bad = "badptr";
            int is_bad = 1;
            for (int i = 0; want_bad[i]; i++) {
                if (name[i] != want_bad[i]) { is_bad = 0; break; }
            }
            if (is_bad && name[6] == 0) {
                src = badptr_prog_start;
                len = (u32)(badptr_prog_end - badptr_prog_start);
            }
        }
    }
    if (len == 0 || len > 256u * 1024u) return -1;
    /* shell 上下文运行在某个进程页表里，而这里要写公共用户代码区。
       先切回内核页表并逐页确保映射，再拷贝；之后调度会重新切到进程页表。
       否则写入未映射页（fork_prog 4256 字节跨 2 页）会在 trap 上下文里递归缺页。 */
    proc_switch_satp(g_kroot);
    for (u32 a = g_user_base; a < g_user_base + len; a += 4096u)
        if (!vm_page_fault_on(g_kroot, a & ~0xFFFu, 1)) break;
    memcpy((void *)g_user_base, src, len);
    g_cur_prog_len = len;
    return (int)len;
}

int krestart(void)
{
    for (u32 k = 0; k < NPROC_INIT; k++) {
        procs[k].pc    = g_user_base;
        procs[k].sp    = g_ustack[k] + 4096u - 16u;
        procs[k].state = PROC_READY;
        procs[k].brk   = 0;
        memset(procs[k].regs, 0, sizeof(procs[k].regs));
        procs[k].regs[1]  = procs[k].sp;
        procs[k].regs[10] = k;
    }
    for (u32 k = NPROC_INIT; k < NPROC; k++) procs[k].state = PROC_EXIT;
    cur_proc = -1;
    return (int)NPROC_INIT;
}

/* 供 shell 的 ps 命令调用 */
void shell_ps_hook(void)
{
    kprintf("PID  STATE  NRUN  PC\n");
    for (int i = 0; i < NPROC; i++) {
        const char *st = procs[i].state == PROC_RUN ? "RUN  " :
                         procs[i].state == PROC_READY ? "READY" : "EXIT ";
        kprintf(" %d   %s  %d     %x\n", i, st, (int)procs[i].nrun, procs[i].pc);
    }
}

void kmain(void)
{
    kprintf("\n=== rvsim mini-kernel (multiproc) ===\n");

    g_kroot = kernel_pgtbl();
    u32 satp = SATP_SV32 | ((u32)g_kroot >> 12);
    __asm__ volatile("csrw satp, %0; sfence.vma" : : "r"(satp) : "memory");

    /* 加载用户程序镜像到 0x80400000 */
    u32 len = (u32)(user_prog_end - user_prog_start);
    memcpy((void *)g_user_base, user_prog_start, len);

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
        proc_create(i, g_user_base, g_ustack[i]);
        /* 每个进程独立的页表，只映射代码页 */
        procs[i].pgtbl = vm_proc_pgtbl();
        vm_map_code(procs[i].pgtbl, g_user_base, g_user_base);
    }

    /* 定时器：默认关闭（yield 已足够验证轮转；打开见 RVSIM_TIMER 环境无对应，
       如需启用改成 1）。保留代码路径以便后续完善。 */
    proc_set_timer(200000);

    /* 使能 M 模式定时器中断与全局中断（必须在 M 模式做） */
    __asm__ volatile("csrs mie, %0" : : "r"(0x80u));    /* MTIE */
    __asm__ volatile("csrs mstatus, %0" : : "r"(0x8u)); /* MIE */
    proc_set_timer(20000);


    /* 进 S 模式 */
    extern void s_main(void);
    enter_s_mode((u32)s_main, 0x803FF000u);
}

void s_main(void)
{
    kprintf("[S] 进入 S 模式\n");

    /* 使能 S 模式定时器中断 */
    __asm__ volatile("csrs sie, %0" : : "r"(0x20u));      /* STIE */
    __asm__ volatile("csrs sstatus, %0" : : "r"(0x2u));    /* SIE */

    proc_t *p0 = proc_schedule();
    if (!p0) { kprintf("[S] 无进程\n"); for (;;) { } }
    kprintf("[S] 启动 proc %d pc=%x sp=%x pt=%x\n", p0->pid, p0->pc, p0->sp, (u32)p0->pgtbl);
    if (p0->pgtbl) {
        proc_switch_satp(p0->pgtbl);
        u32 satp_now = 0;
        __asm__ volatile("csrr %0, satp" : "=r"(satp_now));
        kprintf("[S] satp 实际=%x\n", satp_now);
    }
    enter_u_mode(p0->pc, p0->sp, p0->pid);
    for (;;) { }
}