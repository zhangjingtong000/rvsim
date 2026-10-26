#ifndef RV_CSR_H
#define RV_CSR_H

#include "rv_types.h"

/* mstatus 位（设计文档 3.3 节，已按 v20260120 逐位核对） */
#define MSTATUS_SIE   0x00000002u
#define MSTATUS_MIE   0x00000008u
#define MSTATUS_SPIE  0x00000020u
#define MSTATUS_MPIE  0x00000080u
#define MSTATUS_SPP   0x00000100u
#define MSTATUS_MPP   0x00001800u
#define MSTATUS_FS    0x00006000u
#define MSTATUS_XS    0x00018000u
#define MSTATUS_MPRV  0x00020000u
#define MSTATUS_SUM   0x00040000u
#define MSTATUS_MXR   0x00080000u
#define MSTATUS_TVM   0x00100000u
#define MSTATUS_TW    0x00200000u
#define MSTATUS_TSR   0x00400000u
#define MSTATUS_MPP_U (0u << 11)
#define MSTATUS_MPP_S (1u << 11)
#define MSTATUS_MPP_M (3u << 11)

#define SSTATUS_MASK  0x800DE122u

#define MIP_SSIP      0x00000002u
#define MIP_MSIP      0x00000008u
#define MIP_STIP      0x00000020u
#define MIP_MTIP      0x00000080u
#define MIP_SEIP      0x00000200u
#define MIP_MEIP      0x00000800u

#define CAUSE_MISALIGNED_FETCH     0
#define CAUSE_FETCH_ACCESS         1
#define CAUSE_ILLEGAL_INSTRUCTION  2
#define CAUSE_BREAKPOINT           3
#define CAUSE_MISALIGNED_LOAD      4
#define CAUSE_LOAD_ACCESS          5
#define CAUSE_MISALIGNED_STORE     6
#define CAUSE_STORE_ACCESS         7
#define CAUSE_ECALL_U              8
#define CAUSE_ECALL_S              9
#define CAUSE_ECALL_M              11
#define CAUSE_FETCH_PAGE_FAULT    12
#define CAUSE_LOAD_PAGE_FAULT     13
#define CAUSE_STORE_PAGE_FAULT    15

#define IRQ_S_SOFTWARE   1
#define IRQ_M_SOFTWARE   3
#define IRQ_S_TIMER      5
#define IRQ_M_TIMER      7
#define IRQ_S_EXTERNAL   9
#define IRQ_M_EXTERNAL  11

#define CSR_MSTATUS     0x300
#define CSR_MISA        0x301
#define CSR_MEDELEG     0x302
#define CSR_MIDELEG     0x303
#define CSR_MIE         0x304
#define CSR_MTVEC       0x305
#define CSR_MCOUNTEREN  0x306
#define CSR_MSTATUSH    0x310
#define CSR_MCOUNTINHIBIT 0x320
#define CSR_MSCRATCH    0x340
#define CSR_MEPC        0x341
#define CSR_MCAUSE      0x342
#define CSR_MTVAL       0x343
#define CSR_MIP         0x344
#define CSR_MNSTATUS    0x744
#define CSR_MCYCLE      0xB00
#define CSR_MINSTRET    0xB02
#define CSR_MCYCLEH     0xB80
#define CSR_MINSTRETH   0xB82
#define CSR_PMPCFG0     0x3A0
#define CSR_PMPADDR0    0x3B0
#define CSR_MVENDORID   0xF11
#define CSR_MARCHID     0xF12
#define CSR_MIMPID      0xF13
#define CSR_MHARTID     0xF14

#define CSR_TSELECT     0x7A0
#define CSR_TCONTROL    0x7A5

#define CSR_SSTATUS     0x100
#define CSR_SIE         0x104
#define CSR_STVEC       0x105
#define CSR_SCOUNTEREN  0x106
#define CSR_SSCRATCH    0x140
#define CSR_SEPC        0x141
#define CSR_SCAUSE      0x142
#define CSR_STVAL       0x143
#define CSR_SIP         0x144
#define CSR_SATP        0x180

#define CSR_CYCLE       0xC00
#define CSR_TIME        0xC01
#define CSR_INSTRET     0xC02
#define CSR_CYCLEH      0xC80
#define CSR_TIMEH       0xC81
#define CSR_INSTRETH    0xC82

#define MISA_VALUE      0x40141101u

typedef struct {
    /* 当前特权级（唯一一份） */
    u8  priv;
    /* M 模式 */
    u32 mstatus, mstatush;
    u32 mtvec, mepc, mcause, mtval, mscratch;
    u32 mie, mip, mideleg, medeleg;
    u32 misa, mhartid, mvendorid, marchid, mimpid;
    u32 mcounteren, mcountinhibit;
    u64 mcycle, minstret;
    u32 mnstatus;
    u32 pmpcfg[4];
    u32 pmpaddr[16];
    /* 真正独立的 S 模式 CSR */
    u32 stvec, sepc, scause, stval;
    u32 satp, sscratch, scounteren;
} csr_t;

void csr_reset(csr_t *c);
bool csr_read_priv(csr_t *c, u32 addr, u32 *out);
bool csr_write_priv(csr_t *c, u32 addr, u32 val);
void csr_raise(csr_t *c, u32 cause, u32 tval);
void csr_return(csr_t *c);

extern u64 g_mtime;   /* CLINT 的 mtime，供 time CSR 使用 */

#endif