# 定向测试：mcounteren=0 时 U 模式 rdcycle 必须 illegal（cause=2）
.section .text
.globl _start
_start:
    la      t0, trap_handler
    csrw    mtvec, t0
    csrw    mcounteren, zero
    la      t0, u_code
    csrw    mepc, t0
    li      t0, 0x1800
    csrc    mstatus, t0          # MPP = 0 -> U
    mret

u_code:
    rdcycle t1                   # 应触发 illegal
    li      t0, 0x10000000
    li      t1, 'B'              # 没 trap = 违规
    sb      t1, 0(t0)
1:  j       1b

trap_handler:
    csrr    t0, mcause
    li      t2, 0x10000000
    li      t1, 2                # illegal instruction
    bne     t0, t1, other
    li      t1, 'T'              # 正确 trap
    sb      t1, 0(t2)
    j       2f
other:
    li      t1, '?'
    sb      t1, 0(t2)
2:  j       2b
