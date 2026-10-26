#ifndef RV_CPU_H
#define RV_CPU_H

#include "rv_types.h"
#include "csr.h"
#include "loader.h"

typedef struct {
    u32 x[32];
    u32 pc;
    csr_t csr;
    u32 cur_instr;
    /* LR/SC reservation */
    bool lr_valid;
    u32  lr_addr;
    /* 运行状态 */
    bool running;
    bool halted;
    u64  cycles;
    u64  max_cycles;
    /* 镜像信息（tohost 等） */
    loaded_t img;
    /* 中断标志（由 bus 置位） */
    bool wakeup;
} cpu_t;

void cpu_reset(cpu_t *c);
/* 执行一条指令；返回 false 表示停机 */
bool cpu_step(cpu_t *c);

#endif