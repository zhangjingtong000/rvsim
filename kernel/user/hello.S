# 寄存器完整性测试：t0(x5) 跨 ecall 往返必须保持不变，
# 同时验证每个进程用自己的栈（栈校验）。
.section .text
.globl _start
_start:
    li      t0, 0x0BADF00D     # 埋一个哨兵到 x5
    li      a7, 3              # getpid -> 触发 S 模式 trap 往返
    ecall
    li      t1, 0x0BADF00D
    bne     t0, t1, bad_x5     # x5 必须还是哨兵

    addi    s0, a0, 65         # 'A' + pid
    addi    sp, sp, -16
    sb      s0, 0(sp)          # 自己的栈上写自己的字符

    li      s2, 20
loop:
    li      t0, 0x0BADF00D     # 每轮重新埋哨兵
    mv      a0, sp
    li      a7, 1              # write(buf,1) -> 又一次 trap 往返
    li      a1, 1
    ecall
    li      t1, 0x0BADF00D
    bne     t0, t1, bad_x5     # x5 不能变

    lb      t0, 0(sp)
    bne     t0, s0, bad_stack  # 栈不能被别的进程覆盖

    li      t1, 30000
2:  addi    t1, t1, -1
    bnez    t1, 2b

    addi    s2, s2, -1
    bnez    s2, loop

    li      a7, 0
    li      a0, 0
    ecall

bad_x5:
    li      a0, 'X'            # x5 被破坏
    j       report
bad_stack:
    li      a0, '!'            # 栈被覆盖
report:
    addi    a1, sp, -8
    sb      a0, 0(a1)
    mv      a0, a1
    li      a7, 1
    li      a1, 1
    ecall
    li      a7, 0
    li      a0, 1
    ecall
