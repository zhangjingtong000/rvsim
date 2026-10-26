# M -> S 切换、S 模式 trap（用 sscratch 换栈）、S -> U 切换

.section .text
.globl enter_s_mode
enter_s_mode:
    # a0 = S 入口地址, a1 = S 栈顶
    mv      t0, a0
    mv      sp, a1
    csrw    mepc, t0
    # 清 MPP，设为 S
    li      t1, 0x1800
    csrc    mstatus, t1
    li      t1, 0x0800
    csrs    mstatus, t1
    # 清 MPRV
    li      t1, 0x20000
    csrc    mstatus, t1
    # 设置 S 模式 trap 入口
    la      t2, s_trap_entry
    csrw    stvec, t2
    # 初始化 sscratch 为 S 模式内核栈（trap 时换栈用，必须非 0）
    la      t2, s_kstack_top
    csrw    sscratch, t2
    csrc    mstatus, 2
    mret

# ── S 模式 trap：用 sscratch 换到内核栈 ──
.align 4
.globl s_trap_entry
s_trap_entry:
    csrrw   sp, sscratch, sp        # sp <- 内核栈，sscratch <- 用户 sp
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
    csrr    t0, sscratch            # 用户 sp
    sw      t0, 64(sp)
    sw      s0, 68(sp)
    sw      s1, 72(sp)

    csrr    a0, scause
    csrr    a1, sepc
    csrr    a2, stval
    mv      a3, sp
    call    s_trap_handler
    csrw    sepc, a0

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
    lw      s0, 68(sp)
    lw      s1, 72(sp)
    addi    sp, sp, 128
    csrrw   sp, sscratch, sp        # 恢复：sp <- 用户 sp，sscratch <- 内核栈
    sret

.globl enter_u_mode
enter_u_mode:
    # a0 = 用户入口, a1 = 用户栈顶
    la      t0, s_kstack_top
    csrw    sscratch, t0            # 内核栈存进 sscratch
    csrw    sepc, a0
    mv      sp, a1
    # SPP = 0 -> U
    li      t0, 0x100
    csrc    sstatus, t0
    # SUM = 1：让 S 模式能读用户内存（syscall 需要）
    li      t0, 0x40000
    csrs    sstatus, t0
    sret

.section .bss
.align 4
s_kstack_bottom:
    .skip 4096
s_kstack_top:
