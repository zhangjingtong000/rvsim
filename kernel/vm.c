#include "kernel.h"

/* 简单的物理页分配器：从内核末尾之后开始 */
static u32 g_next_page = 0;

extern u8 _kernel_end[];

u32 *page_alloc(void)
{
    if (!g_next_page) {
        g_next_page = ((u32)_kernel_end + 4095u) & ~4095u;
    }
    u32 *p = (u32 *)g_next_page;
    g_next_page += 4096;
    memset(p, 0, 4096);
    return p;
}

/* 建立恒等映射（VA == PA），用于 S 模式平直访问 */
static void map_identity_1g(u32 *root)
{
    /* Sv32：一个 4MB 超页覆盖 0..4MB；这里映射 0x80000000..0x80400000 */
    for (u32 i = 0; i < 2; i++) {
        u32 va = 0x80000000u + i * 0x400000u;
        u32 *l1 = page_alloc();
        root[VPN2(va)] = ((u32)l1 >> 12 << 10) | PTE_V;
        for (u32 j = 0; j < 1024; j++) {
            u32 pa = va + j * 4096u;
            l1[j] = (u32)(((pa >> 12) << 10) | PTE_V | PTE_R | PTE_W | PTE_X | PTE_A | PTE_D);
        }
    }
    /* 设备区恒等映射：UART 供 S/U 模式输出，syscon 供关机 */
    u32 *l1dev = page_alloc();
    root[VPN2(UART_BASE)] = ((u32)l1dev >> 12 << 10) | PTE_V;
    for (u32 j = 0; j < 1024; j++) {
        u32 pa = (UART_BASE & ~0x3FFFFFu) + j * 4096u;
        l1dev[j] = (u32)(((pa >> 12) << 10) | PTE_V | PTE_R | PTE_W | PTE_A | PTE_D);
    }

    /* CLINT：定时器与软件中断 */
    u32 *l1clint = page_alloc();
    root[VPN2(CLINT_BASE)] = ((u32)l1clint >> 12 << 10) | PTE_V;
    for (u32 j = 0; j < 1024; j++) {
        u32 pa = (CLINT_BASE & ~0x3FFFFFu) + j * 4096u;
        l1clint[j] = (u32)(((pa >> 12) << 10) | PTE_V | PTE_R | PTE_W | PTE_A | PTE_D);
    }

    u32 *l1sys = page_alloc();
    root[VPN2(SYSCON_BASE)] = ((u32)l1sys >> 12 << 10) | PTE_V;
    for (u32 j = 0; j < 1024; j++) {
        u32 pa = (SYSCON_BASE & ~0x3FFFFFu) + j * 4096u;
        l1sys[j] = (u32)(((pa >> 12) << 10) | PTE_V | PTE_R | PTE_W | PTE_A | PTE_D);
    }
}

u32 *kernel_pgtbl(void)
{
    u32 *root = page_alloc();
    map_identity_1g(root);
    return root;
}