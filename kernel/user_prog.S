# 把用户程序二进制嵌入内核镜像
.section .rodata
.globl user_prog_start
.globl user_prog_end
.align 4
user_prog_start:
    .incbin "build/user_prog.bin"
user_prog_end: