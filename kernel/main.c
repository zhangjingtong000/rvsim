#include "kernel.h"
#include <stdarg.h>

extern u32 *kernel_pgtbl(void);
extern void enter_u_mode(u32 entry, u32 stack);

/* 用户程序（由 user_prog.S 生成，链接进内核镜像） */
extern const u8 user_prog_start[];
extern const u8 user_prog_end[];

static u32 *g_kroot;

/* M 模式 trap 处理器，返回新的 mepc */
u32 m_trap_handler(u32 cause, u32 epc, u32 tval)
{
    kprintf("[M-trap] cause=%x epc=%x tval=%x\n", cause, epc, tval);
    return epc + 4;
}

/* S 模式 trap 处理器，返回新的 sepc */
u32 s_trap_handler(u32 cause, u32 epc, u32 tval, u32 *regs)
{
    if (cause == 8) {   /* ecall from U */
        u32 a7 = regs[11];   /* a7 在保存偏移 44 -> 索引 11 */
        u32 a0 = regs[4];    /* a0 偏移 16 -> 索引 4 */
        if (a7 == 1) {       /* write(buf, len)：a0=buf, a1=len */
            u32 buf = regs[4];   /* a0 */
            u32 len = regs[5];   /* a1 */
            for (u32 i = 0; i < len; i++) uart_putc((char)((volatile u8 *)buf)[i]);
            regs[4] = len;
            epc += 4;
            return epc;
        }
        if (a7 == 0) {       /* exit */
            kprintf("\n[U] 进程退出 (code=%d)\n", a0);
            kprintf("[S] 用户进程全部结束，内核关机\n");
            *(volatile u32 *)SYSCON_BASE = 0x5555u;   /* poweroff */
            for (;;) { }
        }
        regs[4] = (u32)-1;
        epc += 4;
        return epc;
    }
    kprintf("[S-trap] cause=%x epc=%x tval=%x\n", cause, epc, tval);
    return epc + 4;
}

void kmain(void)
{
    kprintf("\n=== rvsim mini-kernel ===\n");
    kprintf("[M] 启动，特权级 = M\n");
    kprintf("[M] 用户程序 %d 字节\n", (int)(user_prog_end - user_prog_start));

    /* 建恒等页表 */
    g_kroot = kernel_pgtbl();
    kprintf("[M] 页表 @ %x\n", (u32)g_kroot);

    /* 设置 satp 进 Sv32 */
    u32 satp = SATP_SV32 | ((u32)g_kroot >> 12);
    __asm__ volatile("csrw satp, %0" : : "r"(satp));
    __asm__ volatile("sfence.vma" ::: "memory");
    kprintf("[M] satp=%x（Sv32 已开）\n", satp);

    /* 用户程序加载到 0x80400000（4MB 处，避开内核） */
    u32 uprog_pa = 0x80400000u;
    u32 uprog_len = (u32)(user_prog_end - user_prog_start);
    memcpy((void *)uprog_pa, user_prog_start, uprog_len);

    /* 映射用户页到 0x80400000（U 权限） */
    u32 uvpn2 = VPN2(0x80400000u);
    u32 *ul1 = page_alloc();
    g_kroot[uvpn2] = ((u32)ul1 >> 12 << 10) | PTE_V;
    for (u32 j = 0; j < 4; j++) {
        u32 pa = 0x80400000u + j * 4096u;
        ul1[VPN1(pa)] = (u32)(((pa >> 12) << 10) | PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D);
    }
    /* 用户栈 */
    for (u32 j = 4; j < 8; j++) {
        u32 pa = 0x80400000u + j * 4096u;
        ul1[VPN1(pa)] = (u32)(((pa >> 12) << 10) | PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D);
    }
    memset((void *)(0x80400000u + 4 * 4096u), 0, 4 * 4096u);

    kprintf("[M] 用户程序映射在 0x80400000，栈顶 0x80408000\n");
    kprintf("[M] 交给 S 模式\n\n");

    /* M -> S */
    extern void s_main(u32 u_entry, u32 u_stack);
    enter_s_mode((u32)s_main, 0x803FF000u);
}

/* S 模式主函数 */
void s_main(u32 u_entry_ignored, u32 u_stack_ignored)
{
    (void)u_entry_ignored; (void)u_stack_ignored;
    kprintf("[S] 已进入 S 模式\n");

    extern void enter_u_mode(u32 entry, u32 stack);

    kprintf("[S] 切换到 U 模式，入口 0x80400000\n");
    kprintf("[S] --- 以下为用户程序输出 ---\n");

    enter_u_mode(0x80400000u, 0x80408000u);

    kprintf("[S] 用户程序返回（不应到此）\n");
    for (;;) { }
}
