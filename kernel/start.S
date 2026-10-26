# 迷你内核 M 模式启动
.section .text.init
.globl _start

_start:
    csrw    mstatus, zero
    csrw    mie, zero
    la      sp, stack_top
    csrw    mscratch, sp          # M 模式 trap 换栈用
    la      t0, m_trap_entry
    csrw    mtvec, t0

    # 委托异常（中断不委托，M 模式自己处理定时器）
    li      t0, 0x0000B3FF
    csrw    medeleg, t0
    csrw    mideleg, zero

    # 清 bss
    la      t0, _bss_start
    la      t1, _bss_end
1:  bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:
    call    kmain
3:  j       3b

# M 模式 trap：用 mscratch 换到内核栈
.align 4
.globl m_trap_entry
m_trap_entry:
    csrrw   sp, mscratch, sp      # sp <- 内核栈，mscratch <- 用户 sp
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
    sw      s0, 64(sp)
    sw      s1, 68(sp)
    csrr    t0, sscratch
    sw      t0, 72(sp)            # 用户 sp（如果来自 S 模式）

    csrr    a0, mcause
    csrr    a1, mepc
    csrr    a2, mtval
    mv      a3, sp
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
    lw      s0, 64(sp)
    lw      s1, 68(sp)
    addi    sp, sp, 128
    csrrw   sp, mscratch, sp      # 恢复用户 sp，mscratch <- 内核栈
    mret

.section .bss
.align 4
stack_bottom:
    .skip 8192
stack_top: