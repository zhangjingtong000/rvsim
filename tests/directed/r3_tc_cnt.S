# C: mcounteren/scounteren three-level gating, both directions
.section .text
.globl _start
_start:
    la   t0, handler
    csrw mtvec, t0
    la   s2, dead
    li   s1, 0xDEAD

    # 1) M mode, mcounteren=0 -> all counters readable (must NOT trap)
    csrw mcounteren, zero
    rdcycle a0
    rdinstret a1
    csrr a2, time

    # 2) S mode, mcounteren=0 -> rdcycle illegal (cause 2)
    li   s1, 2
    la   s2, c2
    la   t0, s_a
    csrw mepc, t0
    li   t0, 0x1800
    csrc mstatus, t0
    li   t0, 0x800
    csrs mstatus, t0
    csrw mcounteren, zero
    mret
s_a:
    rdcycle a0
    j    bads
c2:
    # 3) S mode, mcounteren.CY=1 -> rdcycle must SUCCEED (reverse test)
    li   s1, 9
    la   s2, c3
    la   t0, s_b
    csrw mepc, t0
    li   t0, 0x1800
    csrc mstatus, t0
    li   t0, 0x800
    csrs mstatus, t0
    li   t0, 1
    csrw mcounteren, t0
    mret
s_b:
    rdcycle a0
    ecall
c3:
    # 4) U mode, mcounteren=7 but scounteren=0 -> illegal
    li   s1, 2
    la   s2, c4
    la   t0, u_a
    csrw mepc, t0
    li   t0, 0x1800
    csrc mstatus, t0
    li   t0, 7
    csrw mcounteren, t0
    csrw scounteren, zero
    mret
u_a:
    rdcycle a0
    j    badu
c4:
    # 5) U mode, mcounteren=7 AND scounteren=7 -> must SUCCEED
    li   s1, 8
    la   s2, c5
    la   t0, u_b
    csrw mepc, t0
    li   t0, 0x1800
    csrc mstatus, t0
    li   t0, 7
    csrw mcounteren, t0
    li   t0, 7
    csrw scounteren, t0
    mret
u_b:
    rdcycle a0
    ecall
c5:
    # 6) U mode, mcounteren=0 but scounteren=7 -> still illegal
    li   s1, 2
    la   s2, c6
    la   t0, u_c
    csrw mepc, t0
    li   t0, 0x1800
    csrc mstatus, t0
    csrw mcounteren, zero
    li   t0, 7
    csrw scounteren, t0
    mret
u_c:
    rdcycle a0
    j    badu2
c6:
    # 7) M mode: writing the read-only shadow counter is illegal
    li   s1, 2
    la   s2, c7
    li   t0, 1
    csrw cycle, t0
    j    badw
c7:
    li   s1, 0xDEAD
    j    pass

bads: li t6, 'S'; j mk
badu: li t6, 'U'; j mk
badu2: li t6, 'V'; j mk
badw: li t6, 'W'; j mk
mk:  li t0, 0x10000000
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
    jr   s2
hf:
    li   t6, 'X'
    j    mk

.section .tohost,"aw",@progbits
.align 4
.globl tohost
tohost: .word 0
