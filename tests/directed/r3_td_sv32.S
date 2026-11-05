# D: Sv32 - A/D hardware update, superpage PPN check, SUM/MXR via MPRV
# M mode drives all data accesses as S (MPRV=1, MPP=S); identity superpage keeps
# code/page-table reachable.
.section .text
.globl _start
_start:
    la   t0, handler
    csrw mtvec, t0
    la   s5, dead

    # ---- build root table at 0x80100000, L0 at 0x80101000 ----
    li   t0, 0x80100000
    li   t1, 0x80101000

    # root[0x100] = non-leaf -> L0 table
    li   t2, 0x100
    slli t3, t2, 2
    add  t3, t0, t3
    li   t4, 0x80101
    slli t4, t4, 10
    ori  t4, t4, 1            # V
    sw   t4, 0(t3)

    # root[0x200] = identity superpage 0x80000000, V|R|W|X|A|D
    li   t2, 0x200
    slli t3, t2, 2
    add  t3, t0, t3
    li   t4, 0x200
    slli t4, t4, 20
    ori  t4, t4, 0xCF
    sw   t4, 0(t3)

    # root[0x140] = superpage with PPN[0]=1 (illegal) -> must page fault
    li   t2, 0x140
    slli t3, t2, 2
    add  t3, t0, t3
    li   t4, 0x200
    slli t4, t4, 20
    li   t5, 1
    slli t5, t5, 10
    or   t4, t4, t5
    ori  t4, t4, 0xCF
    sw   t4, 0(t3)

    # L0[0] = leaf PA 0x80102000, V|R|W|X, A=D=0
    li   t4, 0x80102
    slli t4, t4, 10
    ori  t4, t4, 0x0F
    sw   t4, 0(t1)
    # L0[1] = leaf PA 0x80103000, V|R|W|X|U  (U page for SUM test)
    li   t4, 0x80103
    slli t4, t4, 10
    ori  t4, t4, 0x1F
    sw   t4, 4(t1)
    # L0[2] = leaf PA 0x80104000, V|X only (R=0, for MXR test)
    li   t4, 0x80104
    slli t4, t4, 10
    ori  t4, t4, 0x09
    sw   t4, 8(t1)

    # ---- enable Sv32 and drive data accesses as S ----
    li   t0, 0x80080100
    csrw satp, t0
    li   t0, 0x1800
    csrc mstatus, t0
    li   t0, 0x800
    csrs mstatus, t0          # MPP=S
    li   t0, 0x20000
    csrs mstatus, t0          # MPRV=1

    # 1) load from 0x40000000 -> must succeed, set A
    li   s1, 0xDEAD
    li   t0, 0x40000000
    lw   t1, 0(t0)
    li   t3, 0x80101000
    lw   t4, 0(t3)
    andi t5, t4, 0x40         # A
    beqz t5, d1
    andi t5, t4, 0x80         # D must still be 0
    bnez t5, d2

    # 2) store to 0x40000000 -> D must be set
    li   t0, 0x40000000
    li   t1, 0x777
    sw   t1, 0(t0)
    lw   t4, 0(t3)
    andi t5, t4, 0x80
    beqz t5, d3

    # 3) U page + SUM=0 -> load page fault (13), tval=VA
    li   t0, 0x40000
    csrc mstatus, t0          # SUM=0
    li   s1, 13
    li   t0, 0x40001000
    mv   s2, t0
    la   s3, i_sum0
    la   s5, r_sum0
i_sum0: lw t1, 0(t0)
r_sum0:

    # 4) same access with SUM=1 -> must succeed
    li   t0, 0x40000
    csrs mstatus, t0
    li   s1, 0xDEAD
    la   s5, dead
    li   t0, 0x40001000
    lw   t1, 0(t0)

    # 5) X-only page, MXR=0 -> load page fault
    li   t0, 0x80000
    csrc mstatus, t0          # MXR=0
    li   s1, 13
    li   t0, 0x40002000
    mv   s2, t0
    la   s3, i_mxr0
    la   s5, r_mxr0
i_mxr0: lw t1, 0(t0)
r_mxr0:

    # 6) MXR=1 -> load from X-only page succeeds
    li   t0, 0x80000
    csrs mstatus, t0
    li   s1, 0xDEAD
    la   s5, dead
    li   t0, 0x40002000
    lw   t1, 0(t0)

    # 7) misaligned superpage PPN[0]!=0 -> page fault
    li   s1, 13
    li   t0, 0x50000000
    mv   s2, t0
    la   s3, i_big
    la   s5, r_big
i_big: lw t1, 0(t0)
r_big:

    # 8) satp.MODE=0 -> bare, 0x40000000 is unmapped physical -> access fault (5)
    csrw satp, zero
    li   s1, 5
    li   t0, 0x40000000
    mv   s2, t0
    la   s3, i_bare
    la   s5, r_bare
i_bare: lw t1, 0(t0)
r_bare:
    j    pass

d1: li t6, 'a'; j mk
d2: li t6, 'b'; j mk
d3: li t6, 'c'; j mk
mk: li t0, 0x10000000
    sb t6, 0(t0)
    addi t6, t6, -0x60
    slli t6, t6, 1
    ori  t6, t6, 1
    la   t0, tohost
    sw   t6, 0(t0)
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
    # restore S-context for data accesses
    li   t5, 0x1800
    csrc mstatus, t5
    li   t5, 0x800
    csrs mstatus, t5
    li   t5, 0x20000
    csrs mstatus, t5
    jr   s5
hf:
    li   t6, 'p'
    j    mk

.section .tohost,"aw",@progbits
.align 4
.globl tohost
tohost: .word 0
