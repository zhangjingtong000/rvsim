# mcounteren=0 时 U 模式 rdcycle 必须 illegal（cause=2）
.section .text
.globl _start
_start:
    la      t0, trap_handler
    csrw    mtvec, t0
    csrw    mcounteren, zero
    la      t0, u_code
    csrw    mepc, t0
    li      t0, 0x1800
    csrc    mstatus, t0          # MPP = U
    mret
u_code:
    rdcycle t1                   # 应触发 illegal
    j       fail
trap_handler:
    csrr    t0, mcause
    li      t1, 2
    bne     t0, t1, fail
    la      t0, tohost
    li      t1, 1
    sw      t1, 0(t0)
1:  j       1b
fail:
    li      t0, 0x10000000
    li      t1, 'F'
    sb      t1, 0(t0)
2:  j       2b
.section .tohost,"aw",@progbits
.align 4
.globl tohost
tohost: .word 0
