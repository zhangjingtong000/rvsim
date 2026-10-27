#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "include/cpu.h"
#include "include/bus.h"
#include "include/mmu.h"

/* ── 存取辅助（先过 MMU，misaligned 由硬件分解） ── */
static u32 g_fault_cause = 0;

static bool load_u32_phys(cpu_t *c, u32 pa, u32 *out)
{
    (void)c;
    return bus_read(pa, 4, out);
}
static bool load_u16_phys(cpu_t *c, u32 pa, u32 *out)
{
    (void)c;
    return bus_read(pa, 2, out);
}
static bool load_u8_phys(cpu_t *c, u32 pa, u32 *out)
{
    (void)c;
    return bus_read(pa, 1, out);
}
static bool store_u32_phys(cpu_t *c, u32 pa, u32 v)
{
    (void)c;
    return bus_write(pa, 4, v);
}
static bool store_u16_phys(cpu_t *c, u32 pa, u32 v)
{
    (void)c;
    return bus_write(pa, 2, v);
}
static bool store_u8_phys(cpu_t *c, u32 pa, u32 v)
{
    (void)c;
    return bus_write(pa, 1, v);
}

/* 翻译单个字节/半字（用于 misaligned 分解，可能跨页） */
static bool trans(cpu_t *c, u32 va, acc_t acc, u32 *pa)
{
    return mmu_translate(c->csr.satp, c->csr.priv, c->csr.mstatus, va, acc, pa, &g_fault_cause);
}

static bool mem_read32(cpu_t *c, u32 va, u32 *v)
{
    if ((va & 3) == 0) {
        u32 pa;
        if (!trans(c, va, ACC_LOAD, &pa)) return false;
        return load_u32_phys(c, pa, v);
    }
    u32 r = 0;
    for (int i = 0; i < 4; i++) {
        u32 pa, b;
        if (!trans(c, va + i, ACC_LOAD, &pa)) return false;
        if (!load_u8_phys(c, pa, &b)) return false;
        r |= (b & 0xFF) << (8 * i);
    }
    *v = r; return true;
}

static bool mem_read16(cpu_t *c, u32 va, u32 *v)
{
    if ((va & 1) == 0) {
        u32 pa;
        if (!trans(c, va, ACC_LOAD, &pa)) return false;
        return load_u16_phys(c, pa, v);
    }
    u32 b0, b1, pa;
    if (!trans(c, va, ACC_LOAD, &pa) || !load_u8_phys(c, pa, &b0)) return false;
    if (!trans(c, va + 1, ACC_LOAD, &pa) || !load_u8_phys(c, pa, &b1)) return false;
    *v = (b0 & 0xFF) | ((b1 & 0xFF) << 8);
    return true;
}

static bool mem_read8(cpu_t *c, u32 va, u32 *v)
{
    u32 pa;
    if (!trans(c, va, ACC_LOAD, &pa)) return false;
    return load_u8_phys(c, pa, v);
}

static bool mem_write32(cpu_t *c, u32 va, u32 v)
{
    if ((va & 3) == 0) {
        u32 pa;
        if (!trans(c, va, ACC_STORE, &pa)) return false;
        return store_u32_phys(c, pa, v);
    }
    for (int i = 0; i < 4; i++) {
        u32 pa;
        if (!trans(c, va + i, ACC_STORE, &pa)) return false;
        if (!store_u8_phys(c, pa, (v >> (8 * i)) & 0xFF)) return false;
    }
    return true;
}

static bool mem_write16(cpu_t *c, u32 va, u32 v)
{
    if ((va & 1) == 0) {
        u32 pa;
        if (!trans(c, va, ACC_STORE, &pa)) return false;
        return store_u16_phys(c, pa, v);
    }
    u32 pa;
    if (!trans(c, va, ACC_STORE, &pa) || !store_u8_phys(c, pa, v & 0xFF)) return false;
    if (!trans(c, va + 1, ACC_STORE, &pa) || !store_u8_phys(c, pa, (v >> 8) & 0xFF)) return false;
    return true;
}

static bool mem_write8(cpu_t *c, u32 va, u32 v)
{
    u32 pa;
    if (!trans(c, va, ACC_STORE, &pa)) return false;
    return store_u8_phys(c, pa, v & 0xFF);
}

/* ── 取指（过 MMU） ── */
static bool fetch(cpu_t *c, u32 *insn)
{
    u32 pa;
    if (!trans(c, c->pc, ACC_FETCH, &pa)) return false;
    if ((pa & 3) == 0) return load_u32_phys(c, pa, insn);
    u32 r = 0;
    for (int i = 0; i < 4; i++) {
        u32 b;
        if (!load_u8_phys(c, pa + i, &b)) return false;
        r |= (b & 0xFF) << (8 * i);
    }
    *insn = r; return true;
}

/* ── trap：填 mepc/sepc 后跳到 mtvec/stvec ── */
static void do_trap(cpu_t *c, u32 cause, u32 tval)
{
    u32 epc = c->pc;
    if (getenv("RVSIM_TRAP")) fprintf(stderr, "[trap] cause=%u tval=%08x epc=%08x pc=%08x\n", cause, tval, epc, c->pc);
    csr_raise(&c->csr, cause, tval);
    if (getenv("RVSIM_TRAP"))
        fprintf(stderr, "[trap2] cause=%u epc=%08x target=%d mepc_set=%08x sepc_set=%08x\n",
                cause, epc, c->csr.priv, c->csr.mepc, c->csr.sepc);
    if (c->csr.priv == PRV_M) {
        c->csr.mepc = epc;
        if (getenv("RVSIM_TRAP")) fprintf(stderr, "[set] mepc=%08x mtvec=%08x\n", c->csr.mepc, c->csr.mtvec);
        u32 base = c->csr.mtvec & ~3u;
        u32 mode = c->csr.mtvec & 3u;
        if (mode == 1 && (cause >> 31)) base += 4u * (cause & 0x7FFFFFFFu);
        c->pc = base;
    } else {
        c->csr.sepc = epc;
        u32 base = c->csr.stvec & ~3u;
        u32 mode = c->csr.stvec & 3u;
        if (mode == 1 && (cause >> 31)) base += 4u * (cause & 0x7FFFFFFFu);
        c->pc = base;
    }
}

/* ── 中断检查：在指令之间注入，按 mideleg 判定委派 ── */
static bool check_interrupt(cpu_t *c)
{
    u32 pending = c->csr.mip & c->csr.mie;
    if (!pending) return false;

    static const u32 bit[]  = { MIP_MEIP, MIP_MSIP, MIP_MTIP, MIP_SEIP, MIP_SSIP, MIP_STIP };
    static const u32 code[] = { IRQ_M_EXTERNAL, IRQ_M_SOFTWARE, IRQ_M_TIMER,
                                IRQ_S_EXTERNAL, IRQ_S_SOFTWARE, IRQ_S_TIMER };
    for (int i = 0; i < 6; i++) {
        if (!(pending & bit[i])) continue;
        bool delegated = (c->csr.mideleg >> code[i]) & 1u;
        if (delegated) {
            if (c->csr.priv == PRV_M) continue;
            if (!(c->csr.mstatus & MSTATUS_SIE)) continue;
        } else {
            if (c->csr.priv == PRV_M && !(c->csr.mstatus & MSTATUS_MIE)) {
                if (!getenv("RVSIM_IGNORE_MIE")) continue;
            }
        }
        do_trap(c, (u32)code[i] | 0x80000000u, 0);
        return true;
    }
    return false;
}

/* ── 写回 ── */
static inline void wr(cpu_t *c, u32 rd, u32 val)
{
    if (rd) c->x[rd] = val;
}

void cpu_reset(cpu_t *c)
{
    memset(c, 0, sizeof(*c));
    c->csr.priv = PRV_M;
    csr_reset(&c->csr);
    c->pc = RAM_BASE;
    c->running = true;
    c->max_cycles = 50000000ull;
}

/* ── 立即数提取 ── */
static inline u32 imm_i(u32 i) { return (u32)((s32)i >> 20); }
static inline u32 imm_s(u32 i) { return (u32)(((s32)i >> 25) << 5) | ((i >> 7) & 0x1F); }
static inline u32 imm_b(u32 i)
{
    u32 v = (((i >> 31) & 1) << 12) | (((i >> 7) & 1) << 11)
          | (((i >> 25) & 0x3F) << 5) | (((i >> 8) & 0xF) << 1);
    return (u32)((s32)(v << 19) >> 19);   /* 符号扩展到 32 位 */
}
static inline u32 imm_u(u32 i) { return i & 0xFFFFF000u; }
static inline u32 imm_j(u32 i)
{
    u32 v = (((i >> 31) & 1) << 20) | (((i >> 12) & 0xFF) << 12)
          | (((i >> 20) & 1) << 11) | (((i >> 21) & 0x3FF) << 1);
    return (u32)((s32)(v << 11) >> 11);
}

/* ── 单条执行 ── */
bool cpu_step(cpu_t *c)
{
    if (!c->running || c->halted) return false;
    if (c->cycles >= c->max_cycles) {
        fprintf(stderr, "\n[rvsim] 达到 max_cycles=%llu，停止\n", (unsigned long long)c->max_cycles);
        c->halted = true;
        return false;
    }

    /* 每条指令（含 trap 路径）统一计数一次，防止某条路径漏加导致 cycles 不增 */
    c->cycles++;
    c->csr.mcycle = c->cycles;
    if (!(c->csr.mcountinhibit & 0x4)) c->csr.minstret++;

    /* 中断注入（在指令之间） */
    if (check_interrupt(c)) return true;

    u32 insn = 0;
    if (!fetch(c, &insn)) {
        do_trap(c, g_fault_cause ? g_fault_cause : CAUSE_FETCH_ACCESS, c->pc);
        return true;
    }
    c->cur_instr = insn;
    u32 opcode = insn & 0x7F;
    u32 rd     = (insn >> 7) & 0x1F;
    u32 funct3 = (insn >> 12) & 0x7;
    u32 rs1    = (insn >> 15) & 0x1F;
    u32 rs2    = (insn >> 20) & 0x1F;
    u32 funct7 = (insn >> 25) & 0x7F;
    u32 next_pc = c->pc + 4;
    u32 a = c->x[rs1], b = c->x[rs2];

    switch (opcode) {
    case 0x37:  /* LUI */
        wr(c, rd, imm_u(insn)); break;
    case 0x17:  /* AUIPC */
        wr(c, rd, c->pc + imm_u(insn)); break;
    case 0x6F:  /* JAL：目标 misaligned 时 rd 不得写回 */
        next_pc = c->pc + imm_j(insn);
        if (next_pc & 3) { do_trap(c, CAUSE_MISALIGNED_FETCH, next_pc); return true; }
        wr(c, rd, c->pc + 4);
        break;
    case 0x67: { /* JALR：目标 misaligned 时 rd 不得写回 */
        u32 t = (a + imm_i(insn)) & ~1u;
        if (t & 3) { do_trap(c, CAUSE_MISALIGNED_FETCH, t); return true; }
        wr(c, rd, c->pc + 4);
        next_pc = t;
        break;
    }
    case 0x63: { /* BRANCH */
        bool take = false;
        switch (funct3) {
        case 0: take = (a == b); break;
        case 1: take = (a != b); break;
        case 4: take = ((s32)a <  (s32)b); break;
        case 5: take = ((s32)a >= (s32)b); break;
        case 6: take = (a <  b); break;
        case 7: take = (a >= b); break;
        default: do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true;
        }
        if (getenv("RVSIM_BR") && c->pc >= 0x80002270u && c->pc <= 0x80002280u)
            fprintf(stderr, "[br] pc=%08x insn=%08x a(x%d)=%08x b(x%d)=%08x take=%d\n",
                    c->pc, insn, rs1, a, rs2, b, take);
        if (take) {
            next_pc = c->pc + imm_b(insn);
            if (next_pc & 3) { do_trap(c, CAUSE_MISALIGNED_FETCH, next_pc); return true; }
        }
        break;
    }
    case 0x03: { /* LOAD */
        u32 addr = a + imm_i(insn), v = 0;
        bool ok = true;
        switch (funct3) {
        case 0: ok = mem_read8 (c, addr, &v); v = (u32)(s32)(s8)v;  break;
        case 1: ok = mem_read16(c, addr, &v); v = (u32)(s32)(s16)v; break;
        case 2: ok = mem_read32(c, addr, &v); break;
        case 4: ok = mem_read8 (c, addr, &v); break;
        case 5: ok = mem_read16(c, addr, &v); break;
        default: do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true;
        }
        if (!ok) { do_trap(c, g_fault_cause ? g_fault_cause : CAUSE_LOAD_ACCESS, addr); return true; }
        wr(c, rd, v);
        break;
    }
    case 0x23: { /* STORE */
        u32 addr = a + imm_s(insn);
        bool ok = true;
        switch (funct3) {
        case 0: ok = mem_write8 (c, addr, b); break;
        case 1: ok = mem_write16(c, addr, b); break;
        case 2: ok = mem_write32(c, addr, b); break;
        default: do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true;
        }
        if (!ok) { do_trap(c, g_fault_cause ? g_fault_cause : CAUSE_STORE_ACCESS, addr); return true; }
        break;
    }
    case 0x13: { /* OP-IMM */
        u32 i = imm_i(insn);
        switch (funct3) {
        case 0: wr(c, rd, a + i); break;
        case 2: wr(c, rd, ((s32)a < (s32)i) ? 1u : 0u); break;
        case 3: wr(c, rd, (a < i) ? 1u : 0u); break;
        case 4: wr(c, rd, a ^ i); break;
        case 6: wr(c, rd, a | i); break;
        case 7: wr(c, rd, a & i); break;
        case 1:   /* SLLI：RV32 的 shamt 只有 5 位，insn[25] 非零必须 illegal */
            if (insn & 0x02000000u) { do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true; }
            wr(c, rd, a << ((insn >> 20) & 0x1F)); break;
        case 5: {   /* SRLI / SRAI：insn[25] 非零 illegal，insn[30] 区分算术/逻辑 */
            if (insn & 0x02000000u) { do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true; }
            u32 sh = (insn >> 20) & 0x1F;
            if (insn & 0x40000000u) wr(c, rd, (u32)((s32)a >> sh));
            else                    wr(c, rd, a >> sh);
            break;
        }
        }
        break;
    }
    case 0x33: { /* OP */
        if (funct7 == 0x01) {   /* RV32M */
            switch (funct3) {
            case 0: wr(c, rd, a * b); break;
            case 1: wr(c, rd, (u32)(((s64)(s32)a * (s64)(s32)b) >> 32)); break;
            case 2: wr(c, rd, (u32)(((s64)(s32)a * (s64)(u32)b) >> 32)); break;
            case 3: wr(c, rd, (u32)(((u64)a * (u64)b) >> 32)); break;
            case 4: {   /* DIV */
                if (b == 0) wr(c, rd, 0xFFFFFFFFu);
                else if (a == 0x80000000u && b == 0xFFFFFFFFu) wr(c, rd, 0x80000000u);
                else wr(c, rd, (u32)((s32)a / (s32)b));
                break;
            }
            case 5: wr(c, rd, b ? a / b : 0xFFFFFFFFu); break;
            case 6: {   /* REM */
                if (b == 0) wr(c, rd, a);
                else if (a == 0x80000000u && b == 0xFFFFFFFFu) wr(c, rd, 0);
                else wr(c, rd, (u32)((s32)a % (s32)b));
                break;
            }
            case 7: wr(c, rd, b ? a % b : a); break;
            }
        } else {
            switch (funct3) {
            case 0: if (funct7 & 0x20) wr(c, rd, a - b); else wr(c, rd, a + b); break;
            case 1: if (funct7 != 0) { do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true; }
                    wr(c, rd, a << (b & 0x1F)); break;
            case 2: wr(c, rd, ((s32)a < (s32)b) ? 1u : 0u); break;
            case 3: wr(c, rd, (a < b) ? 1u : 0u); break;
            case 4: wr(c, rd, a ^ b); break;
            case 5: if (funct7 & 0x20) wr(c, rd, (u32)((s32)a >> (b & 0x1F)));
                    else               wr(c, rd, a >> (b & 0x1F)); break;
            case 6: wr(c, rd, a | b); break;
            case 7: wr(c, rd, a & b); break;
            }
        }
        break;
    }
    case 0x0F:  /* FENCE / FENCE.I：单 hart 顺序执行，作 NOP */
        break;
    case 0x73: { /* SYSTEM */
        if (funct3 == 0) {
            if (insn == 0x00000073) {        /* ECALL */
                u32 cause = (c->csr.priv == PRV_U) ? CAUSE_ECALL_U
                          : (c->csr.priv == PRV_S) ? CAUSE_ECALL_S : CAUSE_ECALL_M;
                do_trap(c, cause, 0);
                return true;
            }
            if (insn == 0x00100073) { do_trap(c, CAUSE_BREAKPOINT, c->pc); return true; }  /* EBREAK */
            if (insn == 0x30200073) {        /* MRET */
                if (c->csr.priv != PRV_M) { do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true; }
                c->pc = c->csr.mepc;
                csr_return(&c->csr);   /* 由 csr_return 读 MPP 并设置目标特权级，不要提前改 priv */
                return true;
            }
            if (insn == 0x10200073) {        /* SRET */
                if (c->csr.priv < PRV_S) { do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true; }
                if (c->csr.priv == PRV_S && (c->csr.mstatus & MSTATUS_TSR)) {
                    do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true;   /* TSR=1 时 S 模式非法 */
                }
                c->pc = c->csr.sepc;
                csr_return(&c->csr);
                return true;
            }
            if (insn == 0x10500073) {        /* WFI：不阻塞，作 NOP（规范允许） */
                if (c->csr.priv < PRV_M && (c->csr.mstatus & MSTATUS_TW)) {
                    do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true;   /* TW=1 时低特权级非法 */
                }
                break;
            }
            if ((insn & 0xFE007FFF) == 0x12000073) {   /* SFENCE.VMA */
                if (c->csr.priv == PRV_U) { do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true; }
                if (c->csr.priv == PRV_S && (c->csr.mstatus & MSTATUS_TVM)) {
                    do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true;   /* TVM=1 时 S 模式非法 */
                }
                break;                                  /* 无 TLB，NOP */
            }
            do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true;
        }
        /* CSR 指令 */
        {
            u32 csr_addr = (insn >> 20) & 0xFFF;
            u32 src = (funct3 & 4) ? rs1 : c->x[rs1];
            u32 old = 0, nv = 0;
            bool ok = csr_read_priv(&c->csr, csr_addr, &old);
            if (!ok) { do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true; }

            switch (funct3 & 3) {
            case 1: nv = src; break;                    /* CSRRW */
            case 2: nv = old | src; break;              /* CSRRS */
            case 3: nv = old & ~src; break;             /* CSRRC */
            }
            /* CSRRW 总是写；CSRRS/CSRRC 仅在 rs1 != x0 时才写
               （csrr a0, mhartid 就是 CSRRS + rs1=x0，不应发起写，否则会误判 illegal） */
            bool do_write = ((funct3 & 3) == 1) || (rs1 != 0);
            if (do_write) {
                if (!csr_write_priv(&c->csr, csr_addr, nv)) {
                    do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true;
                }
                /* mcycle 写入必须同步回 cpu.cycles，否则下一步 c->csr.mcycle = c->cycles
                   会把刚写入的值覆盖掉（这正是设计文档 §3.1 指出的"双份存储"错误） */
                if (csr_addr == CSR_MCYCLE || csr_addr == CSR_MCYCLEH)
                    c->cycles = c->csr.mcycle;
            }
            wr(c, rd, old);
        }
        break;
    }
    case 0x2F: { /* AMO (RV32A)：注意 aq/rl 占 funct7 的低 2 位，必须用 funct5 */
        u32 addr   = a;
        u32 funct5 = (insn >> 27) & 0x1F;
        if (addr & 3) { do_trap(c, CAUSE_MISALIGNED_STORE, addr); return true; }
        u32 old = 0;
        if (!mem_read32(c, addr, &old)) { do_trap(c, CAUSE_LOAD_ACCESS, addr); return true; }

        if (funct5 == 0x02) {          /* LR.W */
            wr(c, rd, old);
            c->lr_valid = true; c->lr_addr = addr;
            break;
        }
        if (funct5 == 0x03) {          /* SC.W */
            if (c->lr_valid && c->lr_addr == addr) {
                if (!mem_write32(c, addr, b)) { do_trap(c, CAUSE_STORE_ACCESS, addr); return true; }
                wr(c, rd, 0);
            } else {
                wr(c, rd, 1);
            }
            c->lr_valid = false;
            break;
        }
        u32 nv = old;
        switch (funct5) {
        case 0x00: nv = old + b; break;                        /* AMOADD  */
        case 0x01: nv = b; break;                              /* AMOSWAP */
        case 0x04: nv = old ^ b; break;                        /* AMOXOR  */
        case 0x08: nv = old | b; break;                        /* AMOOR   */
        case 0x0C: nv = old & b; break;                        /* AMOAND  */
        case 0x10: nv = ((s32)old < (s32)b) ? old : b; break;  /* AMOMIN  */
        case 0x14: nv = ((s32)old > (s32)b) ? old : b; break;  /* AMOMAX  */
        case 0x18: nv = (old < b) ? old : b; break;            /* AMOMINU */
        case 0x1C: nv = (old > b) ? old : b; break;            /* AMOMAXU */
        default: do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn); return true;
        }
        if (!mem_write32(c, addr, nv)) { do_trap(c, CAUSE_STORE_ACCESS, addr); return true; }
        wr(c, rd, old);
        break;
    }
    default:
        do_trap(c, CAUSE_ILLEGAL_INSTRUCTION, insn);
        return true;
    }

    c->pc = next_pc;
    return true;
}