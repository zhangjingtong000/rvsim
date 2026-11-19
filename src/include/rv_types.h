#ifndef RV_TYPES_H
#define RV_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef int64_t  s64;

/* 物理内存映射（与 docs/01-需求文档.md 1.4 节一致，QEMU virt 布局） */
#define RAM_BASE     0x80000000u
#define RAM_SIZE     (64u * 1024u * 1024u)

#define SYSCON_BASE  0x00100000u
#define SYSCON_SIZE  0x00001000u

#define CLINT_BASE   0x02000000u
#define CLINT_SIZE   0x00010000u

#define PLIC_BASE    0x0C000000u
#define PLIC_SIZE    0x00400000u

#define UART_BASE    0x10000000u
/* 显示控制器：控制块放 MMIO，帧缓冲放 RAM（guest 写像素是普通 store，
   宿主能零拷贝拿到指针建 ImageData）。 */
#define DISP_BASE    0x10003000u
#define DISP_SIZE    0x1000u
#define FB_BASE      0x83000000u
#define FB_MAX_W     1024u
#define FB_MAX_H     768u
#define FB_MAX_BPP   4u
#define UART_SIZE    0x00000100u

/* CLINT 寄存器偏移 */
#define CLINT_MSIP_BASE      0x0000u
#define CLINT_MTIMECMP_BASE  0x4000u
#define CLINT_MTIME          0xBFF8u

/* PLIC 寄存器偏移 */
#define PLIC_PRIORITY_BASE   0x0000000u
#define PLIC_PENDING_BASE    0x0001000u
#define PLIC_ENABLE_BASE     0x0002000u
#define PLIC_THRESHOLD_BASE  0x0200000u   /* context 块：+0x0 threshold, +0x4 claim/complete */
#define PLIC_CLAIM_BASE      0x0200004u

#define PLIC_UART_IRQ        10u
#define PLIC_NDEV            31u

/* 特权级 */
#define PRV_U 0
#define PRV_S 1
#define PRV_M 3

#endif