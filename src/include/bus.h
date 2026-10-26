#ifndef RV_BUS_H
#define RV_BUS_H

#include "rv_types.h"
#include "csr.h"

/* 设备状态 */
typedef struct {
    u64 mtimecmp;
    u32 msip;
} clint_t;

typedef struct {
    u8  rbr;        /* 接收缓冲 */
    u8  ier;        /* 中断使能 */
    u8  lcr;
    u8  mcr;
    u8  lsr;        /* bit5=THRE, bit0=DR */
    bool rx_pending;
} uart_t;

typedef struct {
    u32 priority[PLIC_NDEV + 1];
    u32 pending;
    u32 enable;
    u32 threshold;
    u32 claimed;
} plic_t;

typedef struct {
    bool poweroff;
    bool reboot;
    u32  exit_code;
} syscon_t;

extern clint_t  g_clint;
extern uart_t   g_uart;
extern plic_t   g_plic;
extern syscon_t g_syscon;

void  bus_init(void);
u8   *bus_ram(void);
/* 返回 false 表示访问未映射（调用方决定是否 access fault） */
bool  bus_read(u32 addr, int len, u32 *val);
bool  bus_write(u32 addr, int len, u32 val);

/* 由主循环推进：更新 mtime、CLINT 中断、PLIC → mip */
void  bus_tick(csr_t *csr, u64 cycles);
/* 从中断控制器汇总出 mip 的硬件位 */
void  bus_update_irqs(csr_t *csr);

#endif