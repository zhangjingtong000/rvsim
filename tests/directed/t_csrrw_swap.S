# 验证 csrrw 在 rd == rs1 时的语义：必须先读旧值再写，
# 且 rd 拿到的是 CSR 的旧值。
# 场景：csrrw sp, sscratch, sp（trap 入口的换栈写法）
.section .text
.globl _start
_start:
    la      sp, stack_top
    li      t0, 0x1111
    csrw    sscratch, t0
    li      t1, 0x2222
    csrrw   t2, sscratch, t1     # t2 应 = 0x1111，sscratch 应 = 0x2222
    li      t3, 0x1111
    bne     t2, t3, fail_rd
    csrr    t4, sscratch
    li      t3, 0x2222
    bne     t4, t3, fail_wr

    # 关键场景：rd == rs1
    li      sp, 0x3333
    li      t0, 0x4444
    csrw    sscratch, t0
    csrrw   sp, sscratch, sp     # sp 应 = 0x4444（旧 CSR），sscratch 应 = 0x3333
    li      t3, 0x4444
    bne     sp, t3, fail_swap_rd
    csrr    t4, sscratch
    li      t3, 0x3333
    bne     t4, t3, fail_swap_wr

    la      t0, tohost
    li      t1, 1
    sw      t1, 0(t0)
1:  j       1b
fail_rd:
    li      a0, 'R'
    j       out
fail_wr:
    li      a0, 'W'
    j       out
fail_swap_rd:
    li      a0, 'D'
    j       out
fail_swap_wr:
    li      a0, 'S'
    j       out
out:
    li      t0, 0x10000000
    sb      a0, 0(t0)
2:  j       2b
.section .bss
.align 4
stack_bottom: .skip 4096
stack_top:
.section .tohost,"aw",@progbits
.align 4
.globl tohost
tohost: .word 0
