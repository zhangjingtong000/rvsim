#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "include/bus.h"
#include "include/fdt.h"

clint_t  g_clint;
uart_t   g_uart;

/* UART 输出环形缓冲。WASM 环境没有真正的终端，前端靠它拿到 guest 的输出；
   原生环境只是额外存一份，不影响 putchar 行为。 */
#define UOUT_CAP 262144   /* 256KB：log all 输出几百条事件时不能被截断 */
static u8  g_uout[UOUT_CAP];
static u32 g_uout_head, g_uout_tail;

int bus_uart_out_pending(void)
{
    return (int)((g_uout_tail - g_uout_head) & (UOUT_CAP - 1));
}
void bus_uart_out_clear(void) { g_uout_head = g_uout_tail = 0; }
int bus_uart_out_take(u8 *dst, int max)
{
    int n = 0;
    while (n < max && g_uout_head != g_uout_tail) {
        dst[n++] = g_uout[g_uout_head];
        g_uout_head = (g_uout_head + 1) & (UOUT_CAP - 1);
    }
    return n;
}
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
    disp_init();
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
        {   /* 同时进环形缓冲，供前端取走 */
            u32 nxt = (g_uout_tail + 1) & (UOUT_CAP - 1);
            if (nxt != g_uout_head) { g_uout[g_uout_tail] = (u8)ch; g_uout_tail = nxt; }
        }
        /* 滚动窗口，检测 "login:" 与 "# " 提示符 */
        memmove(g_out_win, g_out_win + 1, 7);
        g_out_win[7] = ch;
        if (strstr(g_out_win, "login:")) g_login_seen = true;
        if (strstr(g_out_win, "assword:")) g_password_seen = true;
        /* 只在「# 空格」这个边界上认提示符，而且只触发一次。
           原来只要 login 过，之后任何输出里的 '#' 都会触发一次命令注入——
           cat 一个含 # 的文件就会在用户不知情时执行 uname -a。 */
        if (g_login_seen && ch == ' ' && g_out_win[6] == '#' && !g_shell_prompt_seen) {
            g_shell_prompt_seen = true;
            g_login_seen = false;   /* 一次性：注入完就不再匹配 */
        }
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

/* ── 显示控制器 ──
   控制块走 MMIO，像素留在 RAM。宿主按平面 z 序做矩形合成，
   等价于真实显示控制器的 plane 合成；像素内容全部由 guest 光栅化。 */
disp_t g_disp;

#define DISP_ID      0x52565331u     /* "RVS1" */
#define DISP_VERSION 0x00010000u

static void disp_plane_reset(u32 n)
{
    disp_plane_t *p = &g_disp.plane[n];
    p->fb_addr = 0; p->x = 0; p->y = 0; p->w = 0; p->h = 0;
    p->stride = 0; p->src_x = 0; p->src_y = 0;
    p->format = 0; p->zorder = n; p->flags = 0;
    p->d_seq = 0;
    p->d_x0 = p->d_y0 = p->d_x1 = p->d_y1 = 0;
}

void disp_init(void)
{
    g_disp.w = 0; g_disp.h = 0; g_disp.format = 0; g_disp.enable = 0;
    g_disp.frame_seq = 0;
    g_disp.dmg_x0 = g_disp.dmg_y0 = 0;
    g_disp.dmg_x1 = g_disp.dmg_y1 = 0;
    g_disp.cursor_x = g_disp.cursor_y = g_disp.cursor_en = 0;
    g_disp.mouse_x = g_disp.mouse_y = g_disp.mouse_btn = g_disp.mouse_ev = 0;
    g_disp.key_code = g_disp.key_scan = g_disp.key_mods = g_disp.key_ev = 0;
    for (u32 i = 0; i < DISP_PLANES; i++) disp_plane_reset(i);
}

static bool disp_read(u32 off, int len, u32 *val)
{
    (void)len;
    disp_plane_t *pl = 0;
    u32 pidx = 0, preg = 0;
    if (off >= 0x100u) {
        pidx = (off - 0x100u) / 0x40u;
        preg = (off - 0x100u) % 0x40u;
        if (pidx >= DISP_PLANES) return false;
        pl = &g_disp.plane[pidx];
    }

    if (pl) {
        switch (preg) {
        case 0x00: *val = pl->fb_addr; break;
        case 0x04: *val = pl->x; break;
        case 0x08: *val = pl->y; break;
        case 0x0C: *val = pl->w; break;
        case 0x10: *val = pl->h; break;
        case 0x14: *val = pl->stride; break;
        case 0x18: *val = pl->src_x; break;
        case 0x1C: *val = pl->src_y; break;
        case 0x20: *val = pl->format; break;
        case 0x24: *val = pl->zorder; break;
        case 0x28: *val = pl->flags; break;
        case 0x2C: *val = pl->d_seq; break;
        default:   *val = 0; break;
        }
        return true;
    }

    switch (off) {
    case 0x00: *val = DISP_ID; break;
    case 0x04: *val = DISP_VERSION; break;
    case 0x08: *val = g_disp.w; break;
    case 0x0C: *val = g_disp.h; break;
    case 0x10: *val = g_disp.format; break;
    case 0x14: *val = g_disp.enable; break;
    case 0x18: *val = g_disp.frame_seq; break;
    case 0x38: *val = g_disp.cursor_x; break;
    case 0x3C: *val = g_disp.cursor_y; break;
    case 0x40: *val = g_disp.cursor_en; break;
    case 0x44: *val = g_disp.mouse_x; break;
    case 0x48: *val = g_disp.mouse_y; break;
    case 0x4C: *val = g_disp.mouse_btn; break;
    case 0x50: *val = g_disp.mouse_ev; break;
    case 0x54: *val = g_disp.key_code; break;
    case 0x58: *val = g_disp.key_scan; break;
    case 0x5C: *val = g_disp.key_mods; break;
    case 0x60: *val = g_disp.key_ev; break;
    default:   *val = 0; break;
    }
    return true;
}

static bool disp_write(u32 off, int len, u32 val)
{
    (void)len;
    disp_plane_t *pl = 0;
    u32 preg = 0;
    if (off >= 0x100u) {
        u32 pidx = (off - 0x100u) / 0x40u;
        preg = (off - 0x100u) % 0x40u;
        if (pidx >= DISP_PLANES) return false;
        pl = &g_disp.plane[pidx];
    }

    if (pl) {
        switch (preg) {
        case 0x00: pl->fb_addr = val; break;
        case 0x04: pl->x = val; break;
        case 0x08: pl->y = val; break;
        case 0x0C: pl->w = val; break;
        case 0x10: pl->h = val; break;
        case 0x14: pl->stride = val; break;
        case 0x18: pl->src_x = val; break;
        case 0x1C: pl->src_y = val; break;
        case 0x20: pl->format = val; break;
        case 0x24: pl->zorder = val; break;
        case 0x28: pl->flags = val; break;
        case 0x30: pl->d_x0 = val; break;
        case 0x34: pl->d_y0 = val; break;
        case 0x38: pl->d_x1 = val; break;
        /* 0x38 写 d_x1，0x3C 写 d_y1 并提交。原来 0x3C 又写了一次 d_x1，
           d_y1 永远保持 0——一旦前端开始用 damage rect，只会重绘一条线。 */
        case 0x3C: pl->d_y1 = val; pl->d_seq++; break;   /* 写 d_y1 即提交该平面 */
        default: break;
        }
        return true;
    }

    switch (off) {
    case 0x08: g_disp.w = val; break;
    case 0x0C: g_disp.h = val; break;
    case 0x10: g_disp.format = val; break;
    case 0x14: g_disp.enable = val; break;
    case 0x20: g_disp.dmg_x0 = val; break;
    case 0x24: g_disp.dmg_y0 = val; break;
    case 0x28: g_disp.dmg_x1 = val; break;
    case 0x2C: g_disp.dmg_y1 = val; break;
    case 0x30:
        if (val) g_disp.frame_seq++;
        break;
    /* 以下由前端写：鼠标/键盘事件 */
    case 0x44: g_disp.mouse_x = val; break;
    case 0x48: g_disp.mouse_y = val; break;
    case 0x4C: g_disp.mouse_btn = val; break;
    case 0x50: g_disp.mouse_ev++; break;
    case 0x54: g_disp.key_code = val; break;
    case 0x58: g_disp.key_scan = val; break;
    case 0x5C: g_disp.key_mods = val; break;
    case 0x60: g_disp.key_ev++; break;
    default: break;
    }
    return true;
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
    if (addr >= DISP_BASE && addr < DISP_BASE + DISP_SIZE)
        return disp_read(addr - DISP_BASE, len, val);
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
    if (addr >= DISP_BASE && addr < DISP_BASE + DISP_SIZE)
        return disp_write(addr - DISP_BASE, len, val);
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