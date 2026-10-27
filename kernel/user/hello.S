# 演示程序：打印 pid 字母若干次后退出（退出后内核进 shell）
.section .text
.globl _start
_start:
    li      a7, 3              # getpid
    ecall
    addi    a0, a0, 65         # 'A' + pid
    mv      s0, a0
    li      s2, 25             # 循环 25 次

loop:
    la      s1, buf
    sb      s0, 0(s1)
    li      a7, 1              # write(buf, 1)
    mv      a0, s1
    li      a1, 1
    ecall

    li      t1, 2000
2:  addi    t1, t1, -1
    bnez    t1, 2b

    addi    s2, s2, -1
    bnez    s2, loop

    li      a7, 0              # exit(0)
    li      a0, 0
    ecall

.section .data
.align 4
buf:
    .space 8
