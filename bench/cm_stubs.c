/* CoreMark barebones 需要的几个符号 */
#include "coremark.h"
#include "core_portme.h"

volatile ee_s32 seed1_volatile = 0x3415;
volatile ee_s32 seed2_volatile = 0x3415;
volatile ee_s32 seed3_volatile = 0x66;
volatile ee_s32 seed4_volatile = 4000;
volatile ee_s32 seed5_volatile = 0;

void *align_mem(void *p)
{
    ee_u32 addr = (ee_u32)p;
    addr = (addr + 3u) & ~3u;
    return (void *)addr;
}

extern void uart_putc(char);

/* CoreMark 的内存分配：静态池 */
static ee_u8 g_mem_pool[1024 * 1024] __attribute__((aligned(16)));
static ee_size_t g_mem_used = 0;

void *portable_malloc(ee_size_t size)
{
    if (g_mem_used + size > sizeof(g_mem_pool)) return (void *)0;
    void *p = &g_mem_pool[g_mem_used];
    g_mem_used += (size + 15u) & ~15u;
    return p;
}

void portable_free(void *p) { (void)p; }

/* CoreMark 需要的计时钩子 */
ee_u32 core_init(int argc, char **argv, void *p) { (void)argc; (void)argv; (void)p; return 0; }