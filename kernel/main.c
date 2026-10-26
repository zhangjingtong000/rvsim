#include "kernel.h"
#include "proc.h"

/* s_trap_entry 帧布局：xN 存在偏移 (N-1)*4 */
#define R_A0 9    /* x10 */
#define R_A1 10   /* x11 */
#define R_A7 16   /* x17 */
#include <stdarg.h>

extern u32 *kernel_pgtbl(void);
extern void enter_u_mode(u32 entry, u32 stack, u32 pid);
extern const u8 user_prog_start[];
extern const u8 user_prog_end[];

u32 *g_kroot;   /* 供 vm.c 的按需分页使用 */
static u32  g_user_base = 0x80400000u;
static u32  g_kstack_top;   /* M 模式内核栈（mscratch 用） */

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
        }
        /* 2) 通过 ecall 让 M 模式清 STIP 并重装定时器（EID='SSIP'）
              注意：ecall 会穿越 M 模式并恢复寄存器，局部变量必须放静态区 */
        register u32 a7 __asm__("a7") = 0x53534950u;
        __asm__ volatile("ecall" : "+r"(a7) :: "memory");
        return g_retpc;
    }

    /* 缺页异常：按需分配 */
    if (cause == 13 || cause == 15) {
        if (vm_handle_page_fault(tval, cause == 15))
            return epc;          /* 重试出错的指令 */
        kprintf("[S] proc %d 非法访存 va=%x\n", cur_proc, tval);
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
            for (u32 i = 0; i < len; i++) uart_putc((char)((volatile u8 *)buf)[i]);
            regs[R_A0] = len;
            return epc + 4;
        }
        if (a7 == 0) {           /* exit */
            kprintf("\n[U] proc %d exit(%d)\n", cur_proc, a0);
            procs[cur_proc].state = PROC_EXIT;
            proc_t *next = proc_schedule();
            if (!next) {
                kprintf("[S] 所有进程结束，关机\n");
                *(volatile u32 *)SYSCON_BASE = 0x5555u;
                for (;;) { }
            }
            for (int i = 0; i < 32; i++) regs[i] = next->regs[i];
            return next->pc;
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
    kprintf("[M] 启动\n");

    g_kroot = kernel_pgtbl();
    u32 satp = SATP_SV32 | ((u32)g_kroot >> 12);
    __asm__ volatile("csrw satp, %0; sfence.vma" : : "r"(satp) : "memory");
    kprintf("[M] Sv32 已开 satp=%x\n", satp);

    /* 加载用户程序镜像到 0x80400000 */
    u32 len = (u32)(user_prog_end - user_prog_start);
    memcpy((void *)g_user_base, user_prog_start, len);
    kprintf("[M] 用户程序 %d 字节\n", (int)len);

    /* 用户页表：只映射代码页；数据页与栈页留给缺页异常按需分配 */
    u32 uvpn2 = VPN2(g_user_base);
    u32 *ul1 = page_alloc();
    g_kroot[uvpn2] = ((u32)ul1 >> 12 << 10) | PTE_V;
    {
        u32 code_pa = g_user_base & ~0xFFFu;      /* 代码页物理地址 */
        ul1[VPN1(g_user_base)] = (u32)(((code_pa >> 12) << 10)
                                     | PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D);
    }
    kprintf("[M] 用户页表只映射代码页，其余按需分配（缺页处理）\n");

    /* 创建 3 个进程，每个 4KB 用户栈 */
    proc_init();
    for (u32 i = 0; i < NPROC; i++) {
        g_ustack[i] = g_user_base + 0x10000u + i * 0x2000u;   /* 64KB 之后，间隔 8KB */
        memset((void *)g_ustack[i], 0, 4096);
        proc_create(i, g_user_base, g_ustack[i]);
    }
    kprintf("[M] 创建 %d 个进程\n", NPROC);

    /* 定时器：默认关闭（yield 已足够验证轮转；打开见 RVSIM_TIMER 环境无对应，
       如需启用改成 1）。保留代码路径以便后续完善。 */
    proc_set_timer(200000);

    /* 使能 M 模式定时器中断与全局中断（必须在 M 模式做） */
    __asm__ volatile("csrs mie, %0" : : "r"(0x80u));    /* MTIE */
    __asm__ volatile("csrs mstatus, %0" : : "r"(0x8u)); /* MIE */
    proc_set_timer(20000);

    {
        volatile u32 *mt  = (volatile u32 *)(CLINT_BASE + 0xBFF8u);
        volatile u32 *mth = (volatile u32 *)(CLINT_BASE + 0xBFFCu);
        volatile u32 *mc  = (volatile u32 *)(CLINT_BASE + 0x4000u);
        volatile u32 *mch = (volatile u32 *)(CLINT_BASE + 0x4004u);
        kprintf("[M] mtime=%x.%x mtimecmp=%x.%x\n", *mth, *mt, *mch, *mc);
        u32 mie, mip, mst;
        __asm__ volatile("csrr %0, mie" : "=r"(mie));
        __asm__ volatile("csrr %0, mip" : "=r"(mip));
        __asm__ volatile("csrr %0, mstatus" : "=r"(mst));
        kprintf("[M] mie=%x mip=%x mstatus=%x\n", mie, mip, mst);
    }

    /* 进 S 模式 */
    extern void s_main(void);
    enter_s_mode((u32)s_main, 0x803FF000u);
}

void s_main(void)
{
    kprintf("[S] 进入 S 模式\n");

    /* 调度第一个进程 */
    proc_t *first = proc_schedule();
    if (!first) { kprintf("[S] 无进程\n"); for(;;){} }

    (void)first;
    /* 使能 S 模式定时器中断 */
    __asm__ volatile("csrs sie, %0" : : "r"(0x20u));      /* STIE */
    __asm__ volatile("csrs sstatus, %0" : : "r"(0x2u));    /* SIE */
    kprintf("[S] 定时器中断已启用（抢占式调度）\n");

    proc_t *p0 = proc_schedule();
    if (!p0) { kprintf("[S] 无进程\n"); for (;;) { } }
    kprintf("[S] 启动 proc %d（定时器每 2 万 mtime 单位抢占）\n", p0->pid);
    enter_u_mode(p0->pc, p0->sp, p0->pid);
    for (;;) { }
}