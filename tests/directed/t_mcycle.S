# mcycle 写入必须生效（被执行循环覆盖就是 bug）
# PASS: tohost=1；FAIL: UART 打印 'B' 后死循环
.section .text
.globl _start
_start:
    csrw    mcycle, zero
    li      t0, 1000
    csrw    mcycle, t0
    csrr    t1, mcycle
    li      t2, 1000
    blt     t1, t2, fail
    li      t2, 1010
    bgt     t1, t2, fail
    la      t0, tohost
    li      t1, 1
    sw      t1, 0(t0)
1:  j       1b
fail:
    li      t0, 0x10000000
    li      t1, 'B'
    sb      t1, 0(t0)
2:  j       2b
.section .tohost,"aw",@progbits
.align 4
.globl tohost
tohost: .word 0
