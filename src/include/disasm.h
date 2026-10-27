#ifndef DISASM_H
#define DISASM_H

#include "rv_types.h"

/* 指令类别（可视化用） */
#define CLS_ALU     0
#define CLS_LOAD    1
#define CLS_STORE   2
#define CLS_BRANCH  3
#define CLS_JUMP    4
#define CLS_CSR     5
#define CLS_SYSTEM  6
#define CLS_ATOMIC  7
#define CLS_FENCE   8
#define CLS_UIMM    9

/* 反汇编一条指令，返回 1 表示识别成功 */
int rv_disasm(u32 pc, u32 insn, char *buf, int bufsz);
/* 指令类别 */
int rv_insn_class(u32 insn);

#endif