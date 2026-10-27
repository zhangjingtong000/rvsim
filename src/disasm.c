#include <stdio.h>
#include <string.h>
#include "rv_types.h"
#include "disasm.h"

static const char *xreg(int i)
{
    static const char *n[32] = {
        "zero","ra","sp","gp","tp","t0","t1","t2",
        "s0","s1","a0","a1","a2","a3","a4","a5",
        "a6","a7","s2","s3","s4","s5","s6","s7",
        "s8","s9","s10","s11","t3","t4","t5","t6"
    };
    return n[i & 31];
}

static const char *csrname(u32 a)
{
    switch (a) {
    case 0x300: return "mstatus";
    case 0x301: return "misa";
    case 0x304: return "mie";
    case 0x305: return "mtvec";
    case 0x340: return "mscratch";
    case 0x341: return "mepc";
    case 0x342: return "mcause";
    case 0x343: return "mtval";
    case 0x344: return "mip";
    case 0x100: return "sstatus";
    case 0x104: return "sie";
    case 0x105: return "stvec";
    case 0x140: return "sscratch";
    case 0x141: return "sepc";
    case 0x142: return "scause";
    case 0x143: return "stval";
    case 0x144: return "sip";
    case 0x180: return "satp";
    case 0xB00: return "mcycle";
    case 0xC00: return "cycle";
    case 0xC01: return "time";
    case 0xC02: return "instret";
    case 0xB02: return "minstret";
    default:    return NULL;
    }
}

int rv_disasm(u32 pc, u32 insn, char *buf, int bufsz)
{
    if (bufsz < 48) return 0;

    u32 op   = insn & 0x7F;
    u32 rd   = (insn >> 7) & 0x1F;
    u32 f3   = (insn >> 12) & 0x7;
    u32 rs1  = (insn >> 15) & 0x1F;
    u32 rs2  = (insn >> 20) & 0x1F;
    u32 f7   = (insn >> 25) & 0x7F;
    s32 imm_i = (s32)insn >> 20;
    s32 imm_s = (s32)(((insn >> 25) << 5) | ((insn >> 7) & 0x1F));
    if (imm_s & 0x800) imm_s |= ~0xFFF;
    s32 imm_b = (s32)((((insn >> 31) & 1) << 12) | (((insn >> 7) & 1) << 11) |
                     (((insn >> 25) & 0x3F) << 5) | (((insn >> 8) & 0xF) << 1));
    if (imm_b & 0x1000) imm_b |= ~0x1FFF;
    s32 imm_j = (s32)((((insn >> 31) & 1) << 20) | (((insn >> 12) & 0xFF) << 12) |
                     (((insn >> 20) & 1) << 11) | (((insn >> 21) & 0x3FF) << 1));
    if (imm_j & 0x100000) imm_j |= ~0x1FFFFF;
    u32 uimm = insn >> 12;

    if (insn == 0x00000013u) { snprintf(buf, bufsz, "nop"); return 1; }
    if (insn == 0x00000073u) { snprintf(buf, bufsz, "ecall"); return 1; }
    if (insn == 0x00100073u) { snprintf(buf, bufsz, "ebreak"); return 1; }
    if (insn == 0x30200073u) { snprintf(buf, bufsz, "mret"); return 1; }
    if (insn == 0x10200073u) { snprintf(buf, bufsz, "sret"); return 1; }
    if (insn == 0x10500073u) { snprintf(buf, bufsz, "wfi"); return 1; }

    switch (op) {
    case 0x37: snprintf(buf, bufsz, "lui %s, 0x%x", xreg(rd), insn >> 12); return 1;
    case 0x17: snprintf(buf, bufsz, "auipc %s, 0x%x", xreg(rd), insn >> 12); return 1;
    case 0x6F: snprintf(buf, bufsz, "jal %s, 0x%08x", xreg(rd), pc + imm_j); return 1;
    case 0x67: snprintf(buf, bufsz, "jalr %s, %d(%s)", xreg(rd), imm_i, xreg(rs1)); return 1;
    case 0x63: {
        static const char *b[] = {"beq","bne","?","?","blt","bge","bltu","bgeu"};
        snprintf(buf, bufsz, "%s %s, %s, 0x%08x", b[f3] ? b[f3] : "?",
                 xreg(rs1), xreg(rs2), pc + imm_b);
        return 1;
    }
    case 0x03: {
        static const char *l[] = {"lb","lh","lw","?","lbu","lhu","?","?"};
        snprintf(buf, bufsz, "%s %s, %d(%s)", l[f3], xreg(rd), imm_i, xreg(rs1));
        return 1;
    }
    case 0x23: {
        static const char *s[] = {"sb","sh","sw","?","?","?","?","?"};
        snprintf(buf, bufsz, "%s %s, %d(%s)", s[f3], xreg(rs2), imm_s, xreg(rs1));
        return 1;
    }
    case 0x13: {
        static const char *o[] = {"addi","slti","sltiu","xori","ori","andi","?","?"};
        if (f3 == 1) { snprintf(buf, bufsz, "slli %s, %s, %d", xreg(rd), xreg(rs1), rs2); return 1; }
        if (f3 == 5) {
            if (f7 == 0x20) snprintf(buf, bufsz, "srai %s, %s, %d", xreg(rd), xreg(rs1), rs2);
            else            snprintf(buf, bufsz, "srli %s, %s, %d", xreg(rd), xreg(rs1), rs2);
            return 1;
        }
        snprintf(buf, bufsz, "%s %s, %s, %d", o[f3], xreg(rd), xreg(rs1), imm_i);
        return 1;
    }
    case 0x33: {
        if (f7 == 0x01) {
            static const char *m[] = {"mul","mulh","mulhsu","mulhu","div","divu","rem","remu"};
            snprintf(buf, bufsz, "%s %s, %s, %s", m[f3], xreg(rd), xreg(rs1), xreg(rs2));
            return 1;
        }
        if (f7 == 0x20 && f3 == 0) { snprintf(buf, bufsz, "sub %s, %s, %s", xreg(rd), xreg(rs1), xreg(rs2)); return 1; }
        if (f7 == 0x20 && f3 == 5) { snprintf(buf, bufsz, "sra %s, %s, %s", xreg(rd), xreg(rs1), xreg(rs2)); return 1; }
        {
            static const char *o[] = {"add","sll","slt","sltu","xor","srl","or","and"};
            snprintf(buf, bufsz, "%s %s, %s, %s", o[f3], xreg(rd), xreg(rs1), xreg(rs2));
        }
        return 1;
    }
    case 0x2F: {
        u32 f5 = (insn >> 27) & 0x1F;
        static const char *a[] = {"amoadd","amoswap","?","?","amoxor","?","?","?",
                                  "amoor","?","?","?","amoand","?","?","?"};
        if (f3 == 2) {
            if (f5 == 0x02) { snprintf(buf, bufsz, "lr.w %s, (%s)", xreg(rd), xreg(rs1)); return 1; }
            if (f5 == 0x03) { snprintf(buf, bufsz, "sc.w %s, %s, (%s)", xreg(rd), xreg(rs2), xreg(rs1)); return 1; }
            if (f5 < 16 && a[f5][0] != '?')
                { snprintf(buf, bufsz, "%s.w %s, %s, (%s)", a[f5], xreg(rd), xreg(rs2), xreg(rs1)); return 1; }
        }
        snprintf(buf, bufsz, ".word 0x%08x", insn);
        return 1;
    }
    case 0x0F:
        if (f3 == 1) { snprintf(buf, bufsz, "fence.i"); return 1; }
        snprintf(buf, bufsz, "fence"); return 1;
    case 0x73: {
        const char *cn = csrname(uimm);
        char tmp[24];
        if (!cn) { snprintf(tmp, sizeof tmp, "0x%03x", uimm); cn = tmp; }
        switch (f3) {
        case 1: snprintf(buf, bufsz, "csrrw %s, %s, %s", xreg(rd), cn, xreg(rs1)); return 1;
        case 2: snprintf(buf, bufsz, "csrrs %s, %s, %s", xreg(rd), cn, xreg(rs1)); return 1;
        case 3: snprintf(buf, bufsz, "csrrc %s, %s, %s", xreg(rd), cn, xreg(rs1)); return 1;
        case 5: snprintf(buf, bufsz, "csrrwi %s, %s, %u", xreg(rd), cn, rs1); return 1;
        case 6: snprintf(buf, bufsz, "csrrsi %s, %s, %u", xreg(rd), cn, rs1); return 1;
        case 7: snprintf(buf, bufsz, "csrrci %s, %s, %u", xreg(rd), cn, rs1); return 1;
        default: break;
        }
        snprintf(buf, bufsz, ".word 0x%08x", insn);
        return 1;
    }
    default:
        snprintf(buf, bufsz, ".word 0x%08x", insn);
        return 0;
    }
}

/* 指令类别，用于可视化阶段视图 */
int rv_insn_class(u32 insn)
{
    u32 op = insn & 0x7F;
    switch (op) {
    case 0x03: return CLS_LOAD;
    case 0x23: return CLS_STORE;
    case 0x63: return CLS_BRANCH;
    case 0x6F:
    case 0x67: return CLS_JUMP;
    case 0x73: {
        u32 f3 = (insn >> 12) & 7;
        if (f3 == 0) return CLS_SYSTEM;
        return CLS_CSR;
    }
    case 0x2F: return CLS_ATOMIC;
    case 0x0F: return CLS_FENCE;
    case 0x37: return CLS_UIMM;
    case 0x17: return CLS_UIMM;
    default:   return CLS_ALU;
    }
}
