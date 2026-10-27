# 无 F 扩展时 sstatus.FS 必须 read-only 0
.section .text
.globl _start
_start:
    li      t0, 0x2000       # FS=01
    csrs    sstatus, t0
    csrr    t1, mstatus
    li      t2, 0x6000       # FS 掩码
    and     t1, t1, t2
    bnez    t1, fail
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
