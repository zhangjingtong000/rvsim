# 迷你内核 M 模式启动
.section .text.init
.globl _start

_start:
    # 关中断，清 mstatus
    csrw    mstatus, zero
    csrw    mie, zero

    # 设置栈
    la      sp, stack_top

    # 设置 M 模式 trap 入口
    la      t0, m_trap_entry
    csrw    mtvec, t0

    # 委托异常与中断给 S 模式
    li      t0, 0x0000B3FF          # 常见异常
    csrw    medeleg, t0
    li      t0, 0x00000222          # SSIP | STIP | SEIP
    csrw    mideleg, t0

    # 清 bss
    la      t0, _bss_start
    la      t1, _bss_end
1:
    bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:

    # 跳 C 主函数
    call    kmain

3:  j       3b

# ── M 模式 trap 入口 ──
.align 4
.globl m_trap_entry
m_trap_entry:
    addi    sp, sp, -128
    sw      ra, 0(sp)
    sw      t0, 4(sp)
    sw      t1, 8(sp)
    sw      t2, 12(sp)
    sw      a0, 16(sp)
    sw      a1, 20(sp)
    sw      a2, 24(sp)
    sw      a3, 28(sp)
    sw      a4, 32(sp)
    sw      a5, 36(sp)
    sw      a6, 40(sp)
    sw      a7, 44(sp)
    sw      t3, 48(sp)
    sw      t4, 52(sp)
    sw      t5, 56(sp)
    sw      t6, 60(sp)

    csrr    a0, mcause
    csrr    a1, mepc
    csrr    a2, mtval
    call    m_trap_handler
    csrw    mepc, a0

    lw      ra, 0(sp)
    lw      t0, 4(sp)
    lw      t1, 8(sp)
    lw      t2, 12(sp)
    lw      a0, 16(sp)
    lw      a1, 20(sp)
    lw      a2, 24(sp)
    lw      a3, 28(sp)
    lw      a4, 32(sp)
    lw      a5, 36(sp)
    lw      a6, 40(sp)
    lw      a7, 44(sp)
    lw      t3, 48(sp)
    lw      t4, 52(sp)
    lw      t5, 56(sp)
    lw      t6, 60(sp)
    addi    sp, sp, 128
    mret

.section .bss
.align 4
stack_bottom:
    .skip 8192
stack_top: