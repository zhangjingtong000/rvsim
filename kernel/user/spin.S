# 跑飞演示：死循环，每 20 万条指令打一个字符，永不退出。
#
# 存在的意义是让「进程跑飞」这件事可复现：以前遇到跑飞的程序只能整机
# reset，而 reset 会把所有进程一起重置。有了它，Monitor 里「终止进程」
# 的定时器中断那条路径才有东西可终止，也才能被黑盒测到。
.section .text
.globl _start
_start:
    li      a7, 3
    ecall                       # getpid
    addi    s0, a0, 65          # 'A' + pid，多个跑飞进程能分清
1:
    addi    sp, sp, -16
    sb      s0, 0(sp)
    mv      a0, sp
    li      a7, 1
    li      a1, 1
    ecall                       # write(&ch, 1)
    addi    sp, sp, 16
    li      t1, 200000
2:  addi    t1, t1, -1
    bnez    t1, 2b
    j       1b                  # 永远不退出
