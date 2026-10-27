# 演示：每进程在自己栈上写字符、每轮校验 sp 与栈内容，20 轮后退出
.section .text
.globl _start
_start:
    li      a7, 3
    ecall
    addi    s0, a0, 65
    addi    sp, sp, -16
    mv      s3, sp              # 记录调整后的 sp（之后必须保持不变）
    sb      s0, 0(sp)
    li      s2, 20
loop:
    bne     sp, s3, broken      # 被抢占后 sp 必须恢复
    mv      a0, sp
    li      a7, 1
    li      a1, 1
    ecall
    bne     sp, s3, broken
    lb      t0, 0(sp)
    bne     t0, s0, broken      # 栈内容不能被别的进程覆盖
    li      t1, 30000
2:  addi    t1, t1, -1
    bnez    t1, 2b
    addi    s2, s2, -1
    bnez    s2, loop
    li      a7, 0
    li      a0, 0
    ecall
broken:
    li      a0, 'Z'
    mv      a1, sp
    li      a7, 1
    li      a1, 1
    ecall
    li      a7, 0
    li      a0, 1
    ecall
