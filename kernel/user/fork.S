# fork 测试：只有 pid 0 调 fork，父打印 P、子打印 C
.section .text
.globl _start
_start:
    li      a7, 3
    ecall
    bnez    a0, parent_only
    li      a7, 6
    ecall
    beqz    a0, child
parent_only:
    li      s0, 'P'
    j       loop
child:
    li      s0, 'C'
loop:
    la      s1, buf
    sb      s0, 0(s1)
    li      a7, 1
    mv      a0, s1
    li      a1, 1
    ecall
    li      t1, 6000
2:  addi    t1, t1, -1
    bnez    t1, 2b
    j       loop
.section .data
.align 4
buf:
    .space 64
