# 定向测试：mcycle 写入必须生效（不能被执行循环覆盖）
.section .text
.globl _start
_start:
    csrw    mcycle, zero
    csrw    minstret, zero
    li      t0, 1000
    csrw    mcycle, t0
    csrr    t1, mcycle
    # t1 应在 1000~1010 之间（1000 + 少量指令）
    li      t2, 1000
    blt     t1, t2, fail
    li      t2, 1010
    bgt     t1, t2, fail
    j       ok
fail:
    li      a0, 'B'          # Bad
    j       print
ok:
    li      a0, 'Y'          # Yes
print:
    li      t0, 0x10000000   # UART THR
    sb      a0, 0(t0)
1:  j       1b
