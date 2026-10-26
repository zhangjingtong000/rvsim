# 用户程序：纯忙等 + 打印，不主动 yield —— 用来验证定时器抢占
.section .text
.globl _start
_start:
    li      a7, 3              # getpid
    ecall
    mv      s0, a0

    addi    a0, s0, 65         # 'A' + pid
    mv      s1, a0

    slli    t0, s0, 2
    la      t1, buf
    add     t0, t1, t0
    sb      s1, 0(t0)
    mv      s2, t0

1:
    li      a7, 1              # write(buf, 1)
    mv      a0, s2
    li      a1, 1
    ecall

    li      t1, 8000           # 忙等（不 yield，靠定时器抢占）
2:  addi    t1, t1, -1
    bnez    t1, 2b
    j       1b

.section .data
.align 4
buf:
    .space 64