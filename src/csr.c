#include "include/csr.h"

u64 g_mtime = 0;

void csr_reset(csr_t *c)
{
    u8 saved = c->priv;
    for (size_t i = 0; i < sizeof(*c); i++) ((u8 *)c)[i] = 0;
    c->priv        = saved;
    c->misa        = MISA_VALUE;
    c->mhartid     = 0;
    c->mvendorid   = 0;
    c->marchid     = 0;
    c->mimpid      = 0;
    c->mcycle      = 0;
    c->minstret    = 0;
    /* mtimecmp 复位为全 1：否则 mtime>=mtimecmp 立刻成立，Linux 启动即中断风暴 */
    /* （该值在 clint.c 里维护，这里只管 CSR） */
}

/* 计数器访问检查：M 总是允许；S 需 mcounteren；U 还需 scounteren。
   bit: CY=0, TM=1, IR=2 */
static bool counter_ok(csr_t *c, int bit)
{
    if (c->priv == PRV_M) return true;
    if (!(c->mcounteren & (1u << bit))) return false;
    if (c->priv == PRV_U && !(c->scounteren & (1u << bit))) return false;
    return true;
}

/* ── 读取：返回 false 表示应触发 illegal instruction ── */
bool csr_read_priv(csr_t *c, u32 addr, u32 *out)
{
    u32 v = 0;
    u8  need = (u8)((addr >> 8) & 3);   /* CSR 地址的 [9:8] 位 = 最低特权级 */

    if (c->priv < need) return false;   /* 特权级不足 */

    switch (addr) {
    case CSR_MSTATUS:   v = c->mstatus; break;
    case CSR_MSTATUSH:  v = c->mstatush; break;
    case CSR_MISA:      v = c->misa; break;
    case CSR_MEDELEG:   v = c->medeleg; break;
    case CSR_MIDELEG:   v = c->mideleg; break;
    case CSR_MIE:       v = c->mie; break;
    case CSR_MTVEC:     v = c->mtvec; break;
    case CSR_MCOUNTEREN:v = c->mcounteren; break;
    case CSR_MCOUNTINHIBIT: v = c->mcountinhibit; break;
    case CSR_MSCRATCH:  v = c->mscratch; break;
    case CSR_MEPC:      v = c->mepc; break;
    case CSR_MCAUSE:    v = c->mcause; break;
    case CSR_MTVAL:     v = c->mtval; break;
    case CSR_MIP:       v = c->mip; break;
    case CSR_MNSTATUS:  v = c->mnstatus; break;
    case CSR_MCYCLE:    v = (u32)(c->mcycle); break;
    case CSR_MCYCLEH:   v = (u32)(c->mcycle >> 32); break;
    case CSR_MINSTRET:  v = (u32)(c->minstret); break;
    case CSR_MINSTRETH: v = (u32)(c->minstret >> 32); break;
    case CSR_MVENDORID: v = c->mvendorid; break;
    case CSR_MARCHID:   v = c->marchid; break;
    case CSR_MIMPID:    v = c->mimpid; break;
    case CSR_MHARTID:   v = c->mhartid; break;

    /* PMP */
    case CSR_PMPCFG0: case CSR_PMPCFG0+1: case CSR_PMPCFG0+2: case CSR_PMPCFG0+3:
        v = c->pmpcfg[addr - CSR_PMPCFG0]; break;

    /* sstatus 是 mstatus 的视图 */
    case CSR_SSTATUS:   v = c->mstatus & SSTATUS_MASK; break;
    case CSR_SIE:       v = c->mie & c->mideleg; break;
    case CSR_SIP:       v = c->mip & c->mideleg; break;
    case CSR_STVEC:     v = c->stvec; break;
    case CSR_SCOUNTEREN:v = c->scounteren; break;
    case CSR_SSCRATCH:  v = c->sscratch; break;
    case CSR_SEPC:      v = c->sepc; break;
    case CSR_SCAUSE:    v = c->scause; break;
    case CSR_STVAL:     v = c->stval; break;
    case CSR_SATP:
        if (c->priv == PRV_S && (c->mstatus & MSTATUS_TVM)) return false;   /* TVM=1 时 S 模式不可读 */
        v = c->satp; break;

    /* 只读影子计数器 */
    case CSR_CYCLE:     if (!counter_ok(c, 0)) return false;
                        v = (u32)(c->mcycle); break;
    case CSR_CYCLEH:    if (!counter_ok(c, 0)) return false;
                        v = (u32)(c->mcycle >> 32); break;
    case CSR_INSTRET:   if (!counter_ok(c, 2)) return false;
                        v = (u32)(c->minstret); break;
    case CSR_INSTRETH:  if (!counter_ok(c, 2)) return false;
                        v = (u32)(c->minstret >> 32); break;
    case CSR_TSELECT:   v = 0xFFFFFFFFu; break;   /* 硬连线：无触发器可选 */
    case CSR_TCONTROL:  v = 0; break;
    case CSR_TIME:      if (!counter_ok(c, 1)) return false;
                        v = (u32)g_mtime; break;
    case CSR_TIMEH:     if (!counter_ok(c, 1)) return false;
                        v = (u32)(g_mtime >> 32); break;

    default:
        if (addr >= CSR_PMPADDR0 && addr < CSR_PMPADDR0 + 16) {
            v = c->pmpaddr[addr - CSR_PMPADDR0];
            break;
        }
        return false;   /* 未实现的 CSR */
    }
    *out = v;
    return true;
}

/* ── 写入：返回 false 表示应触发 illegal instruction ── */
bool csr_write_priv(csr_t *c, u32 addr, u32 val)
{
    u8 need = (u8)((addr >> 8) & 3);
    if (c->priv < need) return false;

    switch (addr) {
    case CSR_MSTATUS:
        /* 未实现的字段 read-only 0：只保留可写位 */
        c->mstatus = val & (MSTATUS_SIE | MSTATUS_MIE | MSTATUS_SPIE | MSTATUS_MPIE
                          | MSTATUS_SPP | MSTATUS_MPP | MSTATUS_MPRV
                          | MSTATUS_SUM | MSTATUS_MXR
                          | MSTATUS_TVM | MSTATUS_TW | MSTATUS_TSR);
        break;
    case CSR_MSTATUSH:  c->mstatush = val & 0; break;   /* 全 RO 0 */
    case CSR_MISA:
        /* WARL：写只读位被忽略。本项目 misa 固定，只允许写不改变取值 */
        (void)val;
        break;
    case CSR_MEDELEG:   c->medeleg = val & 0x0000B3FFu; break;  /* 只允许异常位 */
    case CSR_MIDELEG:   c->mideleg = val & (MIP_SSIP | MIP_STIP | MIP_SEIP); break;
    case CSR_MIE:       c->mie = val & 0x00000AAAu; break;   /* SSIP|MSIP|STIP|MTIP|SEIP|MEIP */
    case CSR_MTVEC:
        c->mtvec = (val & ~3u) | (val & 1u);   /* MODE 只保留 0/1，2/3 归一为 0 */
        break;
    case CSR_MCOUNTEREN: c->mcounteren = val; break;
    case CSR_MCOUNTINHIBIT: c->mcountinhibit = val; break;
    case CSR_MSCRATCH:  c->mscratch = val; break;
    case CSR_MEPC:      c->mepc = val & ~3u; break;
    case CSR_MCAUSE:    c->mcause = val; break;
    case CSR_MTVAL:     c->mtval = val; break;
    case CSR_MIP:
        /* 只允许写 SSIP/MSIP/STIP；MTIP/MEIP 由硬件驱动 */
        c->mip = (c->mip & ~(MIP_SSIP | MIP_MSIP | MIP_STIP))
               | (val & (MIP_SSIP | MIP_MSIP | MIP_STIP));
        break;
    case CSR_MNSTATUS:
        /* 只允许写 INIT_RNMI(bit3)；其余只读 0 */
        c->mnstatus = val & 0x8u;
        if (val & 0x8u) c->mnstatus &= ~0x8u;  /* 写 1 触发但不保持 */
        break;
    case CSR_MCYCLE:    c->mcycle = (c->mcycle & 0xFFFFFFFF00000000ull) | val; break;
    case CSR_MCYCLEH:   c->mcycle = (c->mcycle & 0x00000000FFFFFFFFull) | ((u64)val << 32); break;
    case CSR_MINSTRET:
        c->minstret = (c->minstret & 0xFFFFFFFF00000000ull) | val;
        c->minstret--;   /* 抵消本条指令的自增 */
        break;
    case CSR_MINSTRETH:
        c->minstret = (c->minstret & 0x00000000FFFFFFFFull) | ((u64)val << 32);
        c->minstret--;
        break;
    /* 只读身份寄存器：写应 illegal */
    case CSR_MVENDORID: case CSR_MARCHID: case CSR_MIMPID: case CSR_MHARTID:
        return false;

    case CSR_PMPCFG0: case CSR_PMPCFG0+1: case CSR_PMPCFG0+2: case CSR_PMPCFG0+3:
        c->pmpcfg[addr - CSR_PMPCFG0] = val; break;

    case CSR_SSTATUS:
        c->mstatus = (c->mstatus & ~SSTATUS_MASK) | (val & SSTATUS_MASK); break;
    case CSR_SIE: {
        u32 mask = c->mideleg;
        c->mie = (c->mie & ~mask) | (val & mask); break;
    }
    case CSR_SIP: {
        u32 mask = c->mideleg & (MIP_SSIP | MIP_STIP);
        c->mip = (c->mip & ~mask) | (val & mask); break;
    }
    case CSR_STVEC:     c->stvec = (val & ~3u) | (val & 1u); break;
    case CSR_SCOUNTEREN:c->scounteren = val; break;
    case CSR_SSCRATCH:  c->sscratch = val; break;
    case CSR_SEPC:      c->sepc = val & ~3u; break;
    case CSR_SCAUSE:    c->scause = val; break;
    case CSR_STVAL:     c->stval = val; break;
    case CSR_SATP:
        if (c->priv == PRV_S && (c->mstatus & MSTATUS_TVM)) return false;   /* TVM=1 时 S 模式不可写 */
        /* RV32: MODE=bit31, ASID=30:22, PPN=21:0；不支持的 MODE 整条写无效 */
        if ((val >> 31) == 0 || (val >> 31) == 1) c->satp = val;
        break;

    case CSR_TSELECT:   break;   /* WARL：写忽略 */
    case CSR_TCONTROL:  break;
    /* 只读影子计数器：写应 illegal */
    case CSR_CYCLE: case CSR_CYCLEH: case CSR_INSTRET: case CSR_INSTRETH:
    case CSR_TIME:  case CSR_TIMEH:
        return false;

    default:
        if (addr >= CSR_PMPADDR0 && addr < CSR_PMPADDR0 + 16) {
            c->pmpaddr[addr - CSR_PMPADDR0] = val; break;
        }
        return false;
    }
    return true;
}

/* ── trap 进入 ── */
void csr_raise(csr_t *c, u32 cause, u32 tval)
{
    u8 from  = c->priv;
    bool irq = (cause >> 31) != 0;
    u32 code = cause & 0x7FFFFFFFu;

    /* 委派判定：中断看 mideleg，异常看 medeleg */
    bool deleg = irq ? ((c->mideleg >> code) & 1) : ((c->medeleg >> code) & 1);
    u8 target = (from == PRV_M || !deleg) ? PRV_M : PRV_S;

    if (target == PRV_M) {
        u32 m = c->mstatus;
        m = (m & ~MSTATUS_MPIE) | ((m & MSTATUS_MIE) ? MSTATUS_MPIE : 0);
        m &= ~MSTATUS_MIE;
        m = (m & ~MSTATUS_MPP) | ((u32)from << 11);
        c->mstatus = m;
        c->mcause  = cause;
        c->mtval   = tval;
        c->mepc    = 0;   /* 由调用方填 */
        c->priv    = PRV_M;
    } else {
        u32 m = c->mstatus;
        m = (m & ~MSTATUS_SPIE) | ((m & MSTATUS_SIE) ? MSTATUS_SPIE : 0);
        m &= ~MSTATUS_SIE;
        m = (m & ~MSTATUS_SPP) | ((from == PRV_S) ? MSTATUS_SPP : 0);
        c->mstatus = m;
        c->scause  = cause;
        c->stval   = tval;
        c->sepc    = 0;
        c->priv    = PRV_S;
    }
}

/* ── mret / sret 公共返回逻辑 ── */
void csr_return(csr_t *c, int is_mret)
{
    if (is_mret) {
        u32 m = c->mstatus;
        u8 pp = (u8)((m & MSTATUS_MPP) >> 11);
        m = (m & ~MSTATUS_MIE) | ((m & MSTATUS_MPIE) ? MSTATUS_MIE : 0);
        m |= MSTATUS_MPIE;                    /* xPIE <- 1 */
        m = (m & ~MSTATUS_MPP) | MSTATUS_MPP_U;
        c->mstatus = m;
        if (pp != PRV_M) c->mstatus &= ~MSTATUS_MPRV;
        c->priv = pp;
    } else {
        u32 m = c->mstatus;
        u8  spp = (m & MSTATUS_SPP) ? PRV_S : PRV_U;   /* 目标级由 SPP 决定 */
        m = (m & ~MSTATUS_SIE) | ((m & MSTATUS_SPIE) ? MSTATUS_SIE : 0);
        m |= MSTATUS_SPIE;                    /* xPIE <- 1 */
        m &= ~MSTATUS_SPP;                    /* SPP <- 0（U） */
        c->mstatus = m;
        c->priv = spp;
    }
}