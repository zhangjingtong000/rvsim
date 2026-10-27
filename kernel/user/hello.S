# 栈校验测试：每个进程在自己的栈上存自己的字符，
# 每轮校验栈内容未被别的进程覆盖。sp 切换错误时必然失败。
.section .text
.globl _start
_start:
    li      a7, 3              # getpid
    ecall
    addi    s0, a0, 65         # 'A' + pid

    addi    sp, sp, -16        # 在自己栈上分配
    sb      s0, 0(sp)          # 写入自己的字符

    li      s2, 20             # 20 轮
loop:
    mv      a0, sp             # buf = 栈地址（不是字符值！）
    li      a7, 1              # write(buf, 1)
    li      a1, 1
    ecall

    lb      t0, 0(sp)          # 校验：栈内容必须还是自己的字符
    bne     t0, s0, broken

    li      t1, 30000          # 忙等，让抢占发生
2:  addi    t1, t1, -1
    bnez    t1, 2b

    addi    s2, s2, -1
    bnez    s2, loop

    li      a7, 0              # exit(0)
    li      a0, 0
    ecall

broken:
    li      a0, '!'            # 栈被别的进程覆盖
    li      a7, 1
    li      a1, 1
    ecall
    li      a7, 0
    li      a0, 1
    ecall
