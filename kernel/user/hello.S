# 用户程序：每个进程用自己的 buf（base + pid*4），打印自己的字母后 yield
.section .text
.globl _start
_start:
    li      a7, 3              # getpid
    ecall
    mv      s0, a0             # s0 = pid

    addi    a0, s0, 65         # 'A' + pid
    mv      s1, a0             # s1 = 要打印的字符

    slli    t0, s0, 2
    la      t1, buf
    add     t0, t1, t0         # 本进程的 buf 地址
    sb      s1, 0(t0)
    mv      s2, t0             # s2 = buf 地址

1:
    li      a7, 1              # write(buf, 1)
    mv      a0, s2
    li      a1, 1
    ecall

    li      a7, 4              # yield
    ecall

    li      t1, 5000
2:  addi    t1, t1, -1
    bnez    t1, 2b
    j       1b

.section .data
.align 4
buf:
    .space 64