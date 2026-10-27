# 用户程序：验证 sbrk + 打印 + 抢占轮转
.section .text
.globl _start
_start:
    li      a7, 3              # getpid
    ecall
    mv      s0, a0

    addi    a0, s0, 65         # 'A' + pid
    mv      s1, a0

    li      a7, 5              # sbrk(16)：申请堆空间
    li      a0, 16
    ecall
    mv      s2, a0             # s2 = 堆地址

    sb      s1, 0(s2)          # 把字符写到堆上（验证堆可写）

1:
    li      a7, 1              # write(堆地址, 1)
    mv      a0, s2
    li      a1, 1
    ecall

    li      t1, 8000           # 忙等（不 yield，靠定时器抢占）
2:  addi    t1, t1, -1
    bnez    t1, 2b
    j       1b
