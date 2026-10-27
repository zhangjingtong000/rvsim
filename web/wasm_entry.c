/* WASM 前端接口：单步执行 + 状态导出 */
#include <emscripten.h>
#include <string.h>
#include "../src/include/cpu.h"
#include "../src/include/bus.h"
#include "../src/include/loader.h"
#include "../src/include/disasm.h"
extern unsigned bus_hot_read(unsigned page);
extern unsigned bus_hot_write(unsigned page);
extern void bus_hot_clear(void);

static cpu_t g_cpu;
static int   g_last_result = 0;

EMSCRIPTEN_KEEPALIVE
void rv_reset(void)
{
    bus_init();
    cpu_reset(&g_cpu);
    g_cpu.max_cycles = 0x7FFFFFFFFFFFFFFFull;
    g_last_result = 0;
}

EMSCRIPTEN_KEEPALIVE
int rv_load(const unsigned char *elf, int len)
{
    if (!loader_load_elf_mem(elf, len, &g_cpu.img)) return 0;
    g_cpu.pc = g_cpu.img.entry;
    g_cpu.running = true;
    return 1;
}

EMSCRIPTEN_KEEPALIVE
int rv_step(void)
{
    if (!g_cpu.running || g_cpu.halted) return 1;
    if (!cpu_step(&g_cpu)) return 1;
    /* tohost 检查 */
    if (g_cpu.img.has_tohost) {
        u32 v = 0;
        bus_read(g_cpu.img.tohost_addr, 4, &v);
        if (v == 1) { g_last_result = 1; g_cpu.halted = true; return 1; }
        if (v & 1) { g_last_result = -1; g_cpu.halted = true; return 1; }
    }
    if (g_syscon.poweroff || g_syscon.reboot) { g_cpu.halted = true; return 1; }
    return 0;
}

EMSCRIPTEN_KEEPALIVE int rv_pc(void)      { return (int)g_cpu.pc; }
EMSCRIPTEN_KEEPALIVE int rv_reg(int i)    { return (i >= 0 && i < 32) ? (int)g_cpu.x[i] : 0; }
EMSCRIPTEN_KEEPALIVE int rv_priv(void)    { return (int)g_cpu.csr.priv; }
EMSCRIPTEN_KEEPALIVE int rv_cycles(void)  { return (int)g_cpu.cycles; }
EMSCRIPTEN_KEEPALIVE int rv_result(void)  { return g_last_result; }
EMSCRIPTEN_KEEPALIVE int rv_insn(void)    { return (int)g_cpu.cur_instr; }
EMSCRIPTEN_KEEPALIVE int rv_csr(int a)    { u32 v = 0; csr_read_priv(&g_cpu.csr, (u32)a, &v); return (int)v; }
EMSCRIPTEN_KEEPALIVE int rv_mem(int a)    { u32 v = 0; bus_read((u32)a, 1, &v); return (int)v; }
EMSCRIPTEN_KEEPALIVE int rv_tohost(void)  { return (int)g_cpu.img.tohost_addr; }
/* ── 可视化扩展接口 ── */
EMSCRIPTEN_KEEPALIVE
int rv_disasm_at(unsigned pc, char *buf, int bufsz)
{
    u32 insn = 0;
    bus_read((u32)pc, 4, &insn);
    return rv_disasm((u32)pc, insn, buf, bufsz);
}

EMSCRIPTEN_KEEPALIVE int rv_class(void)  { return rv_insn_class((u32)g_cpu.cur_instr); }
EMSCRIPTEN_KEEPALIVE int rv_hot_r(int pg) { return (int)bus_hot_read((u32)pg); }
EMSCRIPTEN_KEEPALIVE int rv_hot_w(int pg) { return (int)bus_hot_write((u32)pg); }
EMSCRIPTEN_KEEPALIVE void rv_hot_clr(void) { bus_hot_clear(); }
