#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* 类型与地址常量只在 src/include/rv_types.h 定义一份，这里不再复制。
   （此前 kernel.h 自己抄了一份 u8/u32/RAM_BASE，改一处忘另一处会静默不一致。） */
#include "../src/include/rv_types.h"

#define USER_HEAP_BASE 0x80600000u
#define USER_HEAP_END  0x80800000u

#define PRV_U 0
#define PRV_S 1
#define PRV_M 3

#define MSTATUS_SIE   0x00000002u
#define MSTATUS_MIE   0x00000008u
#define MSTATUS_SPIE  0x00000020u
#define MSTATUS_MPIE  0x00000080u
#define MSTATUS_SPP   0x00000100u
#define MSTATUS_MPP   0x00001800u
#define MSTATUS_MPRV  0x00020000u
#define MSTATUS_SUM   0x00040000u
#define MSTATUS_MXR   0x00080000u
/* 以下三位此前缺失，导致 kernel/main.c 用 0x8u/0x20u/0x2u 字面量代替 */
#define MSTATUS_TVM   0x00100000u
#define MSTATUS_TW    0x00200000u
#define MSTATUS_TSR   0x00400000u

#define MIP_SSIP  0x00000002u
#define MIP_STIP  0x00000020u
#define MIP_MTIP  0x00000080u

#define MIE_MTIE  0x00000080u
#define MIE_STIE  0x00000020u
#define SIE_STIE  0x00000020u

#define SSTATUS_SIE   MSTATUS_SIE
#define SSTATUS_SPIE  MSTATUS_SPIE
#define SSTATUS_SPP   MSTATUS_SPP

#define SATP_SV32     0x80000000u

/* Sv32 PTE */
#define PTE_V 0x001u
#define PTE_R 0x002u
#define PTE_W 0x004u
#define PTE_X 0x008u
#define PTE_U 0x010u
#define PTE_G 0x020u
#define PTE_A 0x040u
#define PTE_D 0x080u

/* 页表操作宏 */
#define PTE_PPN(pa)   (((pa) >> 12) << 10)
#define VPN2(va)      (((va) >> 22) & 0x3FFu)
#define VPN1(va)      (((va) >> 12) & 0x3FFu)
#define PAGE_OFF(va)  ((va) & 0xFFFu)

void uart_putc(char c);
void uart_puts(const char *s);
void uart_hex(u32 v);
void uart_dec(int v);
u32  uart_tail_len(void);
int  uart_tail_has(u32 mark, const char *needle);
void uart_tail_dump(void);
void kprintf(const char *fmt, ...);
void shell_input(char ch);
void shell_start(void);

void *memset(void *d, int c, size_t n);
void *memcpy(void *d, const void *s, size_t n);

/* 页表 */
u32 *page_alloc(void);
u32 *vm_proc_pgtbl(void);
void vm_map_code(u32 *root, u32 va, u32 pa);
int vm_page_fault_on(u32 *root, u32 va, int is_store);
extern u32 g_pf_count;
void map_page(u32 *root, u32 va, u32 pa, u32 flags);
u32 *build_user_pgtbl(u32 prog_va, const u8 *prog, u32 prog_len, u32 stack_top);

/* 特权级切换 */
void enter_s_mode(u32 s_entry, u32 s_stack);

#endif