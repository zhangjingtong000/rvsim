# A: trap tval/mepc, JAL/JALR misaligned rd suppression, LR cleared by trap, AMO misalign
.section .text
.globl _start
_start:
    la   t0, handler
    csrw mtvec, t0
    la   s4, dead

    # 1) misaligned load
    li   s1, 4
    li   s2, 0x80002002
    la   s3, i1
    la   s5, r1
    li   t0, 0x80002002
i1: lw   a0, 0(t0)
r1:

    # 2) misaligned store
    li   s1, 6
    li   s2, 0x80002002
    la   s3, i2
    la   s5, r2
    li   t0, 0x80002002
i2: sw   a0, 0(t0)
r2:

    # 3) JALR misaligned target: rd must not be written
    la   t0, dead
    addi t0, t0, 2
    li   a1, 0x5A5A5A5A
    li   s1, 0
    mv   s2, t0
    la   s3, i3
    la   s5, r3
i3: jalr a1, 0(t0)
r3:
    li   t1, 0x5A5A5A5A
    bne  a1, t1, m1

    # 4) JAL misaligned target: rd must not be written
    li   a1, 0x12345678
    li   s1, 0
    la   s3, i4
    la   s5, r4
    la   t0, i4
    addi s2, t0, 6
i4: .word 0x006005EF          # jal a1, +6
r4:
    li   t1, 0x12345678
    bne  a1, t1, m2

    # 5) trap must clear LR reservation
    la   t0, shared
    lw   zero, 0(t0)
    lr.w t1, 0(t0)
    li   s1, 11
    mv   s2, zero
    la   s3, i5
    la   s5, r5
i5: ecall
r5:
    li   t2, 0x1111
    sc.w t3, t2, 0(t0)
    bnez t3, r5ok
    lw   t4, 0(t0)
    li   t5, 0x1111
    beq  t4, t5, m3
r5ok:

    # 6) positive control: LR+SC same addr must succeed
    la   t0, shared
    lr.w t1, 0(t0)
    li   t2, 0x2222
    sc.w t3, t2, 0(t0)
    bnez t3, m4
    lw   t4, 0(t0)
    li   t5, 0x2222
    bne  t4, t5, m4

    # 7) SC with no reservation must fail
    la   t0, shared
    li   t2, 0x3333
    sc.w t3, t2, 0(t0)
    beqz t3, m5

    # 8) AMO misaligned -> store/AMO address misaligned
    la   t0, shared
    addi t0, t0, 2
    li   s1, 6
    mv   s2, t0
    la   s3, i8
    la   s5, r8
i8: amoadd.w t1, t2, 0(t0)
r8:
    j    pass

m1: li t6, '1'; j  mk
m2: li t6, '2'; j  mk
m3: li t6, '3'; j  mk
m4: li t6, '4'; j  mk
m5: li t6, '5'; j  mk
mk: li t0, 0x10000000
    sb t6, 0(t0)
    j  dead

pass:
    la   t0, tohost
    li   t1, 1
    sw   t1, 0(t0)
dead:
    j    dead

handler:
    csrr t5, mcause
    bne  t5, s1, hf
    csrr t5, mtval
    bne  t5, s2, hf
    csrr t5, mepc
    bne  t5, s3, hf
    jr   s5
hf:
    li   t6, 'C'
    j    mk

.align 4
shared: .word 0x12345678
.section .tohost,"aw",@progbits
.align 4
.globl tohost
tohost: .word 0
