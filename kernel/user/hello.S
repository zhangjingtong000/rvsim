# 用户态测试程序：write + exit
.section .text
.globl _start
_start:
    li      a7, 1          # write
    la      a0, msg
    li      a1, 22
    ecall

    li      a7, 1
    la      a0, msg2
    li      a1, 20
    ecall

    li      a7, 0          # exit
    li      a0, 0
    ecall

1:  j       1b

.section .rodata
msg:
    .asciz "Hello from U mode!\n\0\0"
msg2:
    .asciz "RISC-V rocks!\n\0\0\0"