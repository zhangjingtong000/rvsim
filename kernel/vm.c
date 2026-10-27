#include "kernel.h"

/* 简单的物理页分配器：从内核末尾之后开始 */
static u32 g_next_page = 0;

extern u8 _kernel_end[];

u32 *page_alloc(void)
{
    if (!g_next_page) {
        g_next_page = ((u32)_kernel_end + 4095u) & ~4095u;
    }
    /* 上限检查：越过 RAM 末端就返回 NULL，避免把页分配到 g_ram 之外 */
    if (g_next_page + 4096u > 0x84000000u) {
        /* 物理页耗尽：直接关机。继续跑只会空指针解引用，崩得更难看。 */
        extern void uart_puts(const char *);
        uart_puts("\n[kernel] 物理页耗尽，关机\n");
        *(volatile u32 *)0x00100000u = 0x5555u;
        for (;;) { }
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
/* ── 按需分页：缺页时分配物理页并建立映射 ── */
u32 *g_kroot __attribute__((weak));

/* 返回 1 表示映射成功，0 表示地址不合法 */
u32 g_pf_count = 0;

int vm_handle_page_fault(u32 va, int is_store)
{
    g_pf_count++;
    (void)is_store;
    /* 只处理用户地址区间 0x80400000 - 0x807FFFFF */
    if (va < 0x80400000u || va >= 0x80800000u) return 0;

    u32 vpn2 = VPN2(va);
    u32 l1pa = (g_kroot[vpn2] >> 10) << 12;
    u32 *l1;

    if (!(g_kroot[vpn2] & PTE_V)) {
        l1 = page_alloc();
        g_kroot[vpn2] = ((u32)l1 >> 12 << 10) | PTE_V;
    } else {
        l1 = (u32 *)l1pa;
    }

    u32 idx = VPN1(va);
    if (!(l1[idx] & PTE_V)) {
        u32 *pa = page_alloc();
        memset(pa, 0, 4096);
        l1[idx] = (u32)(((u32)pa >> 12 << 10) | PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D);
    }
    return 1;
}

/* 为进程创建独立页表：内核映射共享，用户 L1 独立 */
extern u32 *g_kroot;
#define USER_BASE_ 0x80400000u

u32 *vm_proc_pgtbl(void)
{
    u32 *root = page_alloc();
    memcpy(root, g_kroot, 4096);          /* 复制内核映射（共享） */
    u32 uvpn2 = (USER_BASE_ >> 22) & 0x3FFu;
    u32 *ul1 = page_alloc();              /* 独立的用户 L1 */
    root[uvpn2] = ((u32)ul1 >> 12 << 10) | PTE_V;
    return root;
}

/* 把代码页映射进指定进程的页表 */
void vm_map_code(u32 *root, u32 va, u32 pa)
{
    u32 uvpn2 = (va >> 22) & 0x3FFu;
    u32 *ul1 = (u32 *)(((u32)root[uvpn2] >> 10) << 12);
    u32 idx = (va >> 12) & 0x3FFu;
    ul1[idx] = (u32)(((pa >> 12) << 10) | PTE_V | PTE_R | PTE_W | PTE_X | PTE_U | PTE_A | PTE_D);
}

/* 按需分页改为作用于指定页表 */
int vm_page_fault_on(u32 *root, u32 va, int is_store)
{
    (void)is_store;
    g_pf_count++;
    if (va < 0x80400000u || va >= 0x80800000u) return 0;
    u32 uvpn2 = (va >> 22) & 0x3FFu;
    u32 *ul1;
    if (!(root[uvpn2] & PTE_V)) {
        ul1 = page_alloc();
        root[uvpn2] = ((u32)ul1 >> 12 << 10) | PTE_V;
    } else {
        ul1 = (u32 *)(((u32)root[uvpn2] >> 10) << 12);
    }
    u32 idx = (va >> 12) & 0x3FFu;
    if (!(ul1[idx] & PTE_V)) {
        u32 *pa = page_alloc();
        memset(pa, 0, 4096);
        ul1[idx] = (u32)(((u32)pa >> 12 << 10) | PTE_V | PTE_R | PTE_W | PTE_U | PTE_A | PTE_D);
    }
    return 1;
}
