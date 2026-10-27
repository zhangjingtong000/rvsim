# M -> S 切换、S 模式 trap（mscratch 换栈 + 保存全部 32 个寄存器）、S -> U 切换

.section .text
.globl enter_s_mode
enter_s_mode:
    mv      t0, a0
    mv      sp, a1
    csrw    mepc, t0
    li      t1, 0x1800
    csrc    mstatus, t1
    li      t1, 0x0800
    csrs    mstatus, t1
    li      t1, 0x20000
    csrc    mstatus, t1
    la      t2, s_trap_entry
    csrw    stvec, t2
    la      t2, s_kstack_top
    csrw    sscratch, t2
    csrc    mstatus, 2
    /* MPIE=1：保证 mret 后 MIE=1，否则定时器中断被永久关闭 */
    li      t1, 0x80
    csrs    mstatus, t1
    mret

# ── S 模式 trap：帧布局 = 32 个通用寄存器 + 用户 sp（共 132 字节） ──
.align 4
.globl s_trap_entry
s_trap_entry:
    # 把 csrrw 拆成三条独立指令：既能绕开“csrrw 在 rd==rs1 时是否先读后写”的
    # 实现差异，也便于单独验证 csrw sscratch 是否真的写进去了。
    csrr    t0, sscratch            # t0 = 内核栈顶（enter_u_mode 设的）
    csrw    sscratch, sp            # sscratch = 用户 sp（这一次是独立的 csrw）
    mv      sp, t0                  # sp = 内核栈
    addi    sp, sp, -132
    sw      x1,  0(sp)
    sw      x2,  4(sp)              # 占位，稍后填用户 sp
    sw      x3,  8(sp)
    sw      x4, 12(sp)
    sw      x5, 16(sp)
    sw      x6, 20(sp)
    sw      x7, 24(sp)
    sw      x8, 28(sp)
    sw      x9, 32(sp)
    sw      x10,36(sp)
    sw      x11,40(sp)
    sw      x12,44(sp)
    sw      x13,48(sp)
    sw      x14,52(sp)
    sw      x15,56(sp)
    sw      x16,60(sp)
    sw      x17,64(sp)
    sw      x18,68(sp)
    sw      x19,72(sp)
    sw      x20,76(sp)
    sw      x21,80(sp)
    sw      x22,84(sp)
    sw      x23,88(sp)
    sw      x24,92(sp)
    sw      x25,96(sp)
    sw      x26,100(sp)
    sw      x27,104(sp)
    sw      x28,108(sp)
    sw      x29,112(sp)
    sw      x30,116(sp)
    sw      x31,120(sp)
    csrr    t0, sscratch
    sw      t0, 4(sp)               # 用户 sp 存到 x2 槽位

    csrr    a0, scause
    csrr    a1, sepc
    csrr    a2, stval
    mv      a3, sp
    call    s_trap_handler
    csrw    sepc, a0

    lw      x1,  0(sp)
    lw      x3,  8(sp)
    lw      x4, 12(sp)
    lw      x5, 16(sp)
    lw      x6, 20(sp)
    lw      x7, 24(sp)
    lw      x8, 28(sp)
    lw      x9, 32(sp)
    lw      x10,36(sp)
    lw      x11,40(sp)
    lw      x12,44(sp)
    lw      x13,48(sp)
    lw      x14,52(sp)
    lw      x15,56(sp)
    lw      x16,60(sp)
    lw      x17,64(sp)
    lw      x18,68(sp)
    lw      x19,72(sp)
    lw      x20,76(sp)
    lw      x21,80(sp)
    lw      x22,84(sp)
    lw      x23,88(sp)
    lw      x24,92(sp)
    lw      x25,96(sp)
    lw      x26,100(sp)
    lw      x27,104(sp)
    lw      x28,108(sp)
    lw      x29,112(sp)
    lw      x30,116(sp)
    lw      x31,120(sp)
    lw      t0, 4(sp)               # 下一个进程（或本进程）的用户 sp
    csrw    sscratch, t0            # 先写进 sscratch：这是真正发生上下文切换的地方
    lw      t0, 16(sp)              # 必须补回 x5：上面那行已把刚恢复的 x5 覆盖成 sp
    addi    sp, sp, 132
    csrrw   sp, sscratch, sp        # sp <- 用户 sp，sscratch <- 内核栈
    sret

.globl enter_u_mode
enter_u_mode:
    # a0 = entry, a1 = sp, a2 = pid
    la      t0, s_kstack_top
    csrw    sscratch, t0
    csrw    sepc, a0
    mv      sp, a1
    mv      a0, a2          # a0 = pid（用户程序靠它区分自己）
    li      t0, 0x100
    csrc    sstatus, t0     # SPP = 0 -> U
    li      t0, 0x40000
    csrs    sstatus, t0     # SUM = 1
    li      t0, 0x20
    csrs    sstatus, t0     # SPIE = 1，保证 sret 后 SIE = 1，否则定时器中断被永久关闭
    sret

.section .bss
.align 4
s_kstack_bottom:
    .skip 4096
s_kstack_top: