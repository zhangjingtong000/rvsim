# 迷你内核 M 模式启动
.section .text.init
.globl _start

_start:
    csrw    mstatus, zero
    csrw    mie, zero
    la      sp, stack_top
    csrw    mscratch, sp
    la      t0, m_trap_entry
    csrw    mtvec, t0

    # 委派常见异常给 S 模式，但不委派 ecall from S（留给 SBI）
    li      t0, 0x0000B1FF
    csrw    medeleg, t0
    # 委派 S 模式中断：SSIP | STIP | SEIP
    li      t0, 0x00000222
    csrw    mideleg, t0

    la      t0, _bss_start
    la      t1, _bss_end
1:  bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:
    call    kmain
3:  j       3b

# M 模式 trap：帧布局与 S 模式一致 = xN 在 (N-1)*4，共 128 字节
.align 4
.globl m_trap_entry
m_trap_entry:
    csrrw   sp, mscratch, sp
    addi    sp, sp, -128
    sw      x1,   0(sp)
    sw      x2,   4(sp)
    sw      x3,   8(sp)
    sw      x4,  12(sp)
    sw      x5,  16(sp)
    sw      x6,  20(sp)
    sw      x7,  24(sp)
    sw      x8,  28(sp)
    sw      x9,  32(sp)
    sw      x10, 36(sp)
    sw      x11, 40(sp)
    sw      x12, 44(sp)
    sw      x13, 48(sp)
    sw      x14, 52(sp)
    sw      x15, 56(sp)
    sw      x16, 60(sp)
    sw      x17, 64(sp)
    sw      x18, 68(sp)
    sw      x19, 72(sp)
    sw      x20, 76(sp)
    sw      x21, 80(sp)
    sw      x22, 84(sp)
    sw      x23, 88(sp)
    sw      x24, 92(sp)
    sw      x25, 96(sp)
    sw      x26,100(sp)
    sw      x27,104(sp)
    sw      x28,108(sp)
    sw      x29,112(sp)
    sw      x30,116(sp)
    sw      x31,120(sp)
    csrr    t0, mscratch
    sw      t0,   4(sp)          # 用户 sp 存到 x2 槽位

    csrr    a0, mcause
    csrr    a1, mepc
    csrr    a2, mtval
    mv      a3, sp
    call    m_trap_handler
    csrw    mepc, a0

    lw      x1,   0(sp)
    lw      x3,   8(sp)
    lw      x4,  12(sp)
    lw      x5,  16(sp)
    lw      x6,  20(sp)
    lw      x7,  24(sp)
    lw      x8,  28(sp)
    lw      x9,  32(sp)
    lw      x10, 36(sp)
    lw      x11, 40(sp)
    lw      x12, 44(sp)
    lw      x13, 48(sp)
    lw      x14, 52(sp)
    lw      x15, 56(sp)
    lw      x16, 60(sp)
    lw      x17, 64(sp)
    lw      x18, 68(sp)
    lw      x19, 72(sp)
    lw      x20, 76(sp)
    lw      x21, 80(sp)
    lw      x22, 84(sp)
    lw      x23, 88(sp)
    lw      x24, 92(sp)
    lw      x25, 96(sp)
    lw      x26,100(sp)
    lw      x27,104(sp)
    lw      x28,108(sp)
    lw      x29,112(sp)
    lw      x30,116(sp)
    lw      x31,120(sp)
    # 注意：不能在这里 lw 到 t0！x5 上面刚恢复好，再写 t0 会把它覆盖成栈里的旧 sp。
    # M 模式返回不需要读栈帧里的 sp——mscratch 恒为 M 栈顶，csrrw 自会换回来。
    addi    sp, sp, 128
    csrrw   sp, mscratch, sp
    mret

.section .bss
.align 4
stack_bottom:
    .skip 8192
stack_top: