#include <stdio.h>
#include <string.h>
#include <capstone/capstone.h>
#include "include/rv_types.h"
#include "disasm.h"

/* 反汇编器：capstone 5.0.7（源码编译，仅 RISCV 架构）
 * 注意：不加 CS_MODE_RISCVC —— 本项目不做 C 扩展，
 *      加了会把非法编码/数据当成 16 位指令，产生"反汇编器比 CPU 聪明"的错位。
 */
static csh g_cs;
static int g_cs_state;   /* 0=未初始化 1=成功 -1=失败 */

static int cs_ready(void)
{
    if (g_cs_state == 0) {
        g_cs_state = (cs_open(CS_ARCH_RISCV, CS_MODE_RISCV32, &g_cs) == CS_ERR_OK) ? 1 : -1;
        if (g_cs_state == 1) {
            cs_option(g_cs, CS_OPT_DETAIL, CS_OPT_OFF);
            cs_option(g_cs, CS_OPT_SYNTAX, CS_OPT_SYNTAX_DEFAULT);
        }
    }
    return g_cs_state == 1;
}

int rv_disasm(u32 pc, u32 insn, char *buf, int bufsz)
{
    if (!buf || bufsz < 32) return 0;
    if (!cs_ready()) {
        snprintf(buf, bufsz, ".word 0x%08x", insn);
        return 0;
    }
    u8 code[4] = {
        (u8)(insn & 0xFF), (u8)((insn >> 8) & 0xFF),
        (u8)((insn >> 16) & 0xFF), (u8)((insn >> 24) & 0xFF)
    };
    cs_insn *ci = NULL;
    size_t n = cs_disasm(g_cs, code, sizeof code, (u64)pc, 1, &ci);
    int ok = 0;
    if (n == 1 && ci) {
        if (ci->op_str[0])
            snprintf(buf, bufsz, "%s %s", ci->mnemonic, ci->op_str);
        else
            snprintf(buf, bufsz, "%s", ci->mnemonic);
        ok = 1;
        cs_free(ci, n);
    } else {
        snprintf(buf, bufsz, ".word 0x%08x", insn);
    }
    return ok;
}

/* 指令类别（可视化阶段视图用，不依赖 capstone） */
int rv_insn_class(u32 insn)
{
    u32 op = insn & 0x7F;
    switch (op) {
    case 0x03: return CLS_LOAD;
    case 0x23: return CLS_STORE;
    case 0x63: return CLS_BRANCH;
    case 0x6F:
    case 0x67: return CLS_JUMP;
    case 0x73: return ((insn >> 12) & 7) ? CLS_CSR : CLS_SYSTEM;
    case 0x2F: return CLS_ATOMIC;
    case 0x0F: return CLS_FENCE;
    case 0x37:
    case 0x17: return CLS_UIMM;
    default:   return CLS_ALU;
    }
}
