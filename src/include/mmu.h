#ifndef RV_MMU_H
#define RV_MMU_H

#include "rv_types.h"

typedef enum { ACC_FETCH = 0, ACC_LOAD = 1, ACC_STORE = 2 } acc_t;

/* 返回 true 表示翻译成功；false 时 *cause 填 page fault 的 cause */
bool mmu_translate(u32 satp, u8 priv, u32 mstatus, u32 va, acc_t acc,
                   u32 *pa_out, u32 *cause_out);

#endif