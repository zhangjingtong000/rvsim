#ifndef RV_FDT_H
#define RV_FDT_H

#include "rv_types.h"

/* 设备地址表：默认值在 bus.c 里初始化，dtb 存在时由 fdt_apply() 覆盖。
   所有外设访问都走这张表，不再直接引用编译期宏 —— 这样换一份 dtb 就能改设备布局。 */
typedef struct {
    u32 ram_base;
    u32 ram_size;
    u32 clint_base;
    u32 plic_base;
    u32 uart_base;
    u32 syscon_base;
    u8  clint_from_dtb;
    u8  plic_from_dtb;
    u8  uart_from_dtb;
} devmap_t;

extern devmap_t g_devmap;

/* 用编译期默认值填充 g_devmap */
void devmap_defaults(void);

/* 扫描内存里的 dtb，把能识别的设备基址写进 g_devmap。
   dtb_phys 为 0 或 magic 不对时返回 0（保持默认值）。
   返回成功识别的设备项数。 */
int fdt_apply(u32 dtb_phys);

/* 只做校验，不修改 g_devmap（给 --dtb 参数做前置检查用） */
int fdt_check(u32 dtb_phys);

#endif