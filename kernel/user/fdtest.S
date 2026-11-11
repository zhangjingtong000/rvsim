# 演示：通过文件描述符读取 RAMFS 里的文件并打印
# a7=8 open(path) / a7=9 read(fd,buf,len) / a7=10 close(fd)
.section .text
.globl _start
_start:
    # fd = open("readme.txt")
    li      a7, 8
    la      a0, path
    ecall
    mv      s0, a0              # fd（-1 表示失败）
    bltz    s0, fail

    # n = read(fd, buf, 40)
    li      a7, 9
    mv      a0, s0
    la      a1, buf
    li      a2, 40
    ecall
    mv      s1, a0
    bltz    s1, fail

    # write(buf, n)
    li      a7, 1
    la      a0, buf
    mv      a1, s1
    ecall

    # close(fd)
    li      a7, 10
    mv      a0, s0
    ecall

    li      a7, 0
    li      a0, 0
    ecall

fail:
    li      a7, 1
    la      a0, msg_fail
    li      a1, 7
    ecall
    li      a7, 0
    li      a0, 1
    ecall

.section .rodata
path:     .asciz "readme.txt"
msg_fail: .ascii "FAILED\n"

.section .bss
.align 4
buf:      .space 64
