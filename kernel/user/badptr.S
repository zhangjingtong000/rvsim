# 坏指针防护演示：先尝试读内核地址（应被拒绝），再尝试越界指针，
# 最后正常打印自己的字符，证明进程没被拖垮。
.section .text
.globl _start
_start:
    # 1) write(0x80000000, 64) —— 内核地址，应返回 -1
    li      a0, 0x80000000
    li      a1, 64
    li      a7, 1
    ecall
    li      t0, -1
    bne     a0, t0, bad

    # 2) write(0x90000000, 8) —— 越界，应返回 -1
    li      a0, 0x90000000
    li      a1, 8
    li      a7, 1
    ecall
    li      t0, -1
    bne     a0, t0, bad

    # 3) 正常打印，证明内核仍然可用
    li      a7, 3
    ecall
    addi    s0, a0, 65
    addi    sp, sp, -16
    sb      s0, 0(sp)
    mv      a0, sp
    li      a1, 1
    li      a7, 1
    ecall
    li      a7, 0
    li      a0, 0
    ecall
bad:
    li      a0, 'X'
    mv      a1, sp
    li      a7, 1
    li      a1, 1
    ecall
    li      a7, 0
    li      a0, 1
    ecall
