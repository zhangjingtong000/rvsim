# fork 演示：仅 pid 0 调用 fork；父进程打印 P，子进程打印 C。
# 打印 30 轮后父进程退出（子进程继续，看进程表能看到多一个）。
.section .text
.globl _start
_start:
    li      a7, 3
    ecall
    bnez    a0, parent_only

    li      a7, 6              # fork
    ecall
    beqz    a0, child
parent_only:
    li      s0, 'P'
    li      s2, 0
    j       loop
child:
    li      s0, 'C'
    li      s2, 0
loop:
    la      s1, buf
    sb      s0, 0(s1)
    li      a7, 1
    mv      a0, s1
    li      a1, 1
    ecall
    li      t1, 20000
2:  addi    t1, t1, -1
    bnez    t1, 2b
    addi    s2, s2, 1
    li      t0, 30
    blt     s2, t0, loop
    li      a7, 0              # 30 轮后退出
    li      a0, 0
    ecall
.section .data
.align 4
buf:
    .space 64
