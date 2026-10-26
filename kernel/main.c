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

static u32 *g_kroot;
static u32  g_user_base = 0x80400000u;
static u32  g_kstack_top;   /* M 模式内核栈（mscratch 用） */

/* 每个进程的用户栈（物理页） */
static u32 g_ustack[NPROC];

/* ── M 模式 trap：只处理定时器，切进程 ── */
u32 m_trap_handler(u32 cause, u32 epc, u32 tval, u32 *regs)
{
    if ((cause & 0x80000000u) && ((cause & 0x7FFFFFFFu) == 7)) {
        /* 保存当前进程状态 */
        if (cur_proc >= 0) {
            procs[cur_proc].pc = epc;
            procs[cur_proc].sp = regs[18];   /* 保存的 sp（暂不用） */
        }
        proc_t *next = proc_schedule();
        if (!next) return epc;

        /* 重新装填定时器 */
        proc_set_timer(20000);

        /* a0 = pid，供用户程序识别自己 */
        regs[R_A0] = next->pid;
        return next->pc;
    }
    return epc;
}

/* ── S 模式 trap：系统调用 ── */
u32 s_trap_handler(u32 cause, u32 epc, u32 tval, u32 *regs)
{
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

    /* 建用户页表：1 个 4MB 超页覆盖 0x80400000-0x807FFFFF（U 权限） */
    u32 uvpn2 = VPN2(g_user_base);
    u32 *ul1 = page_alloc();
    g_kroot[uvpn2] = ((u32)ul1 >> 12 << 10) | PTE_V;
    for (u32 j = 0; j < 1024; j++) {
        u32 pa = (g_user_base & ~0x3FFFFFu) + j * 4096u;
        ul1[j] = (u32)(((pa >> 12) << 10) | PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D);
    }

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

    kprintf("[S] 启动 proc %d，入口 %x\n", first->pid, first->pc);
    kprintf("[S] --- 用户输出（定时器每 30 万周期切换进程）---\n");

    enter_u_mode(first->pc, first->sp, first->pid);
    for (;;) { }
}