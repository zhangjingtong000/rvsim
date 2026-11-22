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
    /* mcycle 的基准偏移：程序可见的 mcycle = mcycle_base + cycles。
       写 mcycle 时只改 base，不动 cycles——cycles 是「已经执行了多少条」的
       只增计数，也是 --max-cycles 唯一的预算依据，不能让程序清零。 */
    u64  mcycle_base;
    u64  max_cycles;
    u32  last_pc;        /* 死循环检测：上一轮 pc */
    u32  pc_stuck;       /* 连续停在同一 pc 的次数 */
    u32  wfi_count;      /* 连续 wfi 次数 */
    /* 镜像信息（tohost 等） */
    loaded_t img;
    /* 中断标志（由 bus 置位） */
    bool wakeup;
} cpu_t;

void cpu_reset(cpu_t *c);
/* 执行一条指令；返回 false 表示停机 */
bool cpu_step(cpu_t *c);

#endif