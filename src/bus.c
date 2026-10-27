#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "include/bus.h"
#include "include/fdt.h"

clint_t  g_clint;
uart_t   g_uart;
plic_t   g_plic;
syscon_t g_syscon;
bool g_login_seen = false;
bool g_shell_prompt_seen = false;
bool g_password_seen = false;

static char g_out_win[9] = {0};

static u8 g_ram[RAM_SIZE] __attribute__((aligned(4096)));

/* mtime 推进：每 cycles 个周期 +1，使 guest 看到的主频匹配 dtb 的 1MHz */
#define MTIME_DIV 10u

void bus_init(void)
{
    memset(g_ram, 0, sizeof(g_ram));
    memset(&g_clint, 0, sizeof(g_clint));
    g_clint.mtimecmp = ~0ull;          /* 关键：复位全 1，避免中断风暴 */
    memset(&g_uart, 0, sizeof(g_uart));
    g_uart.lsr = 0x60;                 /* THRE | TEMT */
    memset(&g_plic, 0, sizeof(g_plic));
    memset(&g_syscon, 0, sizeof(g_syscon));
    g_mtime = 0;
}

u8 *bus_ram(void) { return g_ram; }

/* ── CLINT ── */
static bool clint_read(u32 off, int len, u32 *val)
{
    u64 v;
    if (off == CLINT_MTIME)            v = g_mtime;
    else if (off == CLINT_MTIME + 4)   v = g_mtime >> 32;
    else if (off == CLINT_MTIMECMP_BASE)    v = g_clint.mtimecmp;
    else if (off == CLINT_MTIMECMP_BASE + 4) v = g_clint.mtimecmp >> 32;
    else if (off < 0x4000)             v = (off & 4) ? 0 : g_clint.msip;
    else return false;

    if (len == 4) *val = (u32)v;
    else if (len == 8) *val = (u32)v;   /* 本实现 32 位总线，8 字节访问由调用方拆 */
    else *val = (u32)v;
    return true;
}

static bool clint_write(u32 off, int len, u32 val)
{
    if (off == CLINT_MTIMECMP_BASE) {
        if (len == 8) g_clint.mtimecmp = val;
        else g_clint.mtimecmp = (g_clint.mtimecmp & 0xFFFFFFFF00000000ull) | val;
    } else if (off == CLINT_MTIMECMP_BASE + 4) {
        g_clint.mtimecmp = (g_clint.mtimecmp & 0x00000000FFFFFFFFull) | ((u64)val << 32);
    } else if (off < 0x4000) {
        g_clint.msip = (off & 4) ? g_clint.msip : val;
    } else {
        return false;
    }
    return true;
}

/* ── UART 16550A ── */
static bool uart_read(u32 off, int len, u32 *val)
{
    (void)len;

    u8 v = 0;
    switch (off) {
    case 0:                                                /* RBR：从 FIFO 取 */
        if (g_uart.fifo_head != g_uart.fifo_tail) {
            v = g_uart.fifo[g_uart.fifo_head];
            g_uart.fifo_head = (g_uart.fifo_head + 1) % UART_RX_FIFO;
        } else {
            v = g_uart.rbr;
        }
        if (g_uart.fifo_head == g_uart.fifo_tail) g_uart.lsr &= ~0x01u;
        break;
    case 1: v = g_uart.ier; break;
    case 2:                                                 /* IIR */
        if ((g_uart.lsr & 0x01) && (g_uart.ier & 0x01))
            v = 0xC4;                                       /* RX data available */
        else if (g_uart.ier & 0x02)
            v = 0xC2;                                       /* THR empty（THRI 使能即认为可写） */
        else
            v = 0xC1;                                       /* no interrupt pending */
        break;
    case 3: v = g_uart.lcr; break;
    case 4: v = g_uart.mcr; break;
    case 5: v = g_uart.lsr; break;
    case 6: v = 0; break;                                   /* MSR */
    case 7: v = 0; break;                                   /* SCR */
    default: return false;
    }
    *val = v;
    return true;
}

static bool uart_write(u32 off, int len, u32 val)
{
    (void)len;

    switch (off) {
    case 0: {                                                    /* THR */
        char ch = (char)(val & 0xFF);
        putchar(ch); fflush(stdout);
        /* 滚动窗口，检测 "login:" 与 "# " 提示符 */
        memmove(g_out_win, g_out_win + 1, 7);
        g_out_win[7] = ch;
        if (strstr(g_out_win, "login:")) g_login_seen = true;
        if (strstr(g_out_win, "assword:")) g_password_seen = true;
        if (g_login_seen && (ch == '#')) g_shell_prompt_seen = true;
        /* TX 完成：THR 为空，若 THRI 使能则触发中断让驱动继续写 */
        if (g_uart.ier & 0x02) {
            g_plic.pending |= (1u << PLIC_UART_IRQ);
            g_plic.enable  |= (1u << PLIC_UART_IRQ);
            if (g_plic.priority[PLIC_UART_IRQ] == 0) g_plic.priority[PLIC_UART_IRQ] = 1;
        }
        break;
    }
    case 1:
        g_uart.ier = (u8)val;
        if (g_uart.ier & 0x02) {   /* 使能 TX 中断时立即触发一次（THR 为空） */
            g_plic.pending |= (1u << PLIC_UART_IRQ);
            g_plic.enable  |= (1u << PLIC_UART_IRQ);
            if (g_plic.priority[PLIC_UART_IRQ] == 0) g_plic.priority[PLIC_UART_IRQ] = 1;
        }
        break;
    case 3: g_uart.lcr = (u8)val; break;
    case 4: g_uart.mcr = (u8)val; break;
    case 5: break;                                              /* FCR */
    case 7: break;
    default: break;
    }
    return true;
}

/* 让模拟器把内核的串口输入喂进来 */
void uart_push_rx(u8 byte)
{
    u32 next = (g_uart.fifo_tail + 1) % UART_RX_FIFO;
    if (next == g_uart.fifo_head) return;   /* FIFO 满，丢弃 */
    g_uart.fifo[g_uart.fifo_tail] = byte;
    g_uart.fifo_tail = next;
    g_uart.lsr |= 0x01u;
    g_uart.rx_pending = true;
    g_plic.pending |= (1u << PLIC_UART_IRQ);
    g_plic.enable  |= (1u << PLIC_UART_IRQ);
    if (g_plic.priority[PLIC_UART_IRQ] == 0) g_plic.priority[PLIC_UART_IRQ] = 1;
}

/* ── PLIC ── */
static bool plic_read(u32 off, int len, u32 *val)
{
    (void)len;
    if (off < PLIC_PRIORITY_BASE + 4 * (PLIC_NDEV + 1)) {
        u32 id = (off - PLIC_PRIORITY_BASE) / 4;
        *val = g_plic.priority[id];
        return true;
    }
    if (off >= PLIC_PENDING_BASE && off < PLIC_PENDING_BASE + 8) {
        *val = g_plic.pending;
        return true;
    }
    if (off >= PLIC_ENABLE_BASE && off < PLIC_ENABLE_BASE + 0x80) {
        *val = g_plic.enable;
        return true;
    }
    if (off == PLIC_THRESHOLD_BASE) {
        *val = g_plic.threshold;
        return true;
    }
    if (off == PLIC_THRESHOLD_BASE + 4) {   /* claim：context 块内偏移 0x4 */
        /* claim：返回当前最高优先级的 pending 且已使能的中断 */
        u32 pend = g_plic.pending & g_plic.enable;
        if (pend) {
            u32 best = 0, bestpri = 0;
            for (u32 i = 1; i <= PLIC_NDEV; i++) {
                if ((pend >> i) & 1u) {
                    if (g_plic.priority[i] > bestpri) { bestpri = g_plic.priority[i]; best = i; }
                }
            }
            if (bestpri > g_plic.threshold) {
                g_plic.pending &= ~(1u << best);
                g_plic.claimed = best;
                *val = best;
                return true;
            }
        }
        *val = 0;
        return true;
    }
    return false;
}

static bool plic_write(u32 off, int len, u32 val)
{
    (void)len;
    if (off < PLIC_PRIORITY_BASE + 4 * (PLIC_NDEV + 1)) {
        g_plic.priority[(off - PLIC_PRIORITY_BASE) / 4] = val;
        return true;
    }
    if (off >= PLIC_ENABLE_BASE && off < PLIC_ENABLE_BASE + 0x80) {
        g_plic.enable = val;
        return true;
    }
    if (off == PLIC_THRESHOLD_BASE) { g_plic.threshold = val; return true; }
    if (off == PLIC_THRESHOLD_BASE + 4) {   /* complete */
        /* complete：清 claimed 对应的 pending（已在 claim 时清了） */
        g_plic.claimed = 0;
        return true;
    }
    return false;
}

/* ── syscon ── */
static bool syscon_write(u32 off, u32 val)
{
    if (off == 0) {
        if (val == 0x5555) { g_syscon.poweroff = true; return true; }
        if (val == 0x7777) { g_syscon.reboot   = true; return true; }
        return true;
    }
    return false;
}

/* ── 总线入口 ── */
/* ── 内存热图：4KB 页粒度的访问计数（只统计 RAM 前 8MB） ── */
#define HOT_PAGES 2048
static u32 g_hot_read[HOT_PAGES];
static u32 g_hot_write[HOT_PAGES];

void bus_record_access(u32 addr, int is_write)
{
    if (addr >= RAM_BASE && (u64)addr < (u64)RAM_BASE + (u64)HOT_PAGES * 4096u) {
        u32 pg = (addr - RAM_BASE) >> 12;
        if (is_write) g_hot_write[pg]++; else g_hot_read[pg]++;
    }
}

u32 bus_hot_read(u32 page)  { return page < HOT_PAGES ? g_hot_read[page] : 0; }
u32 bus_hot_write(u32 page) { return page < HOT_PAGES ? g_hot_write[page] : 0; }
void bus_hot_clear(void)
{
    for (u32 i = 0; i < HOT_PAGES; i++) { g_hot_read[i] = 0; g_hot_write[i] = 0; }
}

bool bus_read(u32 addr, int len, u32 *val)
{
    bus_record_access(addr, 0);
    /* 不变量 1：RAM 优先（0x8000_0000 与 UART 的槽号冲突） */
    if (addr >= RAM_BASE && (u64)addr + len <= (u64)RAM_BASE + RAM_SIZE) {
        u32 off = addr - RAM_BASE;
        if (len == 1)      *val = g_ram[off];
        else if (len == 2) *val = (u32)g_ram[off] | ((u32)g_ram[off+1] << 8);
        else               *val = (u32)g_ram[off] | ((u32)g_ram[off+1] << 8)
                               | ((u32)g_ram[off+2] << 16) | ((u32)g_ram[off+3] << 24);
        return true;
    }
    /* 不变量 2：逐设备判断区间，绝不按高位折叠 */
    if (addr >= g_devmap.clint_base && addr < g_devmap.clint_base + CLINT_SIZE)
        return clint_read(addr - g_devmap.clint_base, len, val);
    if (addr >= g_devmap.uart_base && addr < g_devmap.uart_base + UART_SIZE)
        return uart_read(addr - g_devmap.uart_base, len, val);
    if (addr >= g_devmap.plic_base && addr < g_devmap.plic_base + PLIC_SIZE)
        return plic_read(addr - g_devmap.plic_base, len, val);
    if (addr >= g_devmap.syscon_base && addr < g_devmap.syscon_base + SYSCON_SIZE) { *val = 0; return true; }
    return false;
}

bool bus_write(u32 addr, int len, u32 val)
{
    bus_record_access(addr, 1);
    if (addr >= RAM_BASE && (u64)addr + len <= (u64)RAM_BASE + RAM_SIZE) {
        u32 off = addr - RAM_BASE;
        if (len == 1)      g_ram[off] = (u8)val;
        else if (len == 2) { g_ram[off] = (u8)val; g_ram[off+1] = (u8)(val >> 8); }
        else { g_ram[off] = (u8)val; g_ram[off+1] = (u8)(val >> 8);
               g_ram[off+2] = (u8)(val >> 16); g_ram[off+3] = (u8)(val >> 24); }
        return true;
    }
    if (addr >= g_devmap.clint_base && addr < g_devmap.clint_base + CLINT_SIZE)
        return clint_write(addr - g_devmap.clint_base, len, val);
    if (addr >= g_devmap.uart_base && addr < g_devmap.uart_base + UART_SIZE)
        return uart_write(addr - g_devmap.uart_base, len, val);
    if (addr >= g_devmap.plic_base && addr < g_devmap.plic_base + PLIC_SIZE)
        return plic_write(addr - g_devmap.plic_base, len, val);
    if (addr >= g_devmap.syscon_base && addr < g_devmap.syscon_base + SYSCON_SIZE)
        return syscon_write(addr - g_devmap.syscon_base, val);
    return false;
}

void bus_tick(csr_t *csr, u64 cycles)
{
    (void)cycles;

    static u64 acc = 0;
    acc++;
    if (acc >= MTIME_DIV) { acc = 0; g_mtime++; }
    bus_update_irqs(csr);
}

void bus_update_irqs(csr_t *csr)
{
    u32 mip = 0;
    if (g_clint.msip)                 mip |= MIP_MSIP;
    if (g_mtime >= g_clint.mtimecmp)  mip |= MIP_MTIP;
    /* PLIC：有 pending 且使能且优先级高于阈值 → MEIP */
    u32 pe = g_plic.pending & g_plic.enable;
    if (pe & (1u << PLIC_UART_IRQ)) {
        if (g_plic.priority[PLIC_UART_IRQ] > g_plic.threshold) mip |= MIP_MEIP;
    }
    csr->mip = (csr->mip & (MIP_SSIP | MIP_STIP | MIP_SEIP)) | mip;
}