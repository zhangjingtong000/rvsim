# E: xRET status semantics (MPP/MPIE/MIE/MPRV, SPP/SPIE/SIE), M-mode SRET, sie/sip views
.section .text
.globl _start
_start:
    la   t0, handler
    csrw mtvec, t0
    la   s5, dead
    li   s6, 0

    # ---- A: MRET to M (MPP=M) -> observe directly in M ----
    li   t0, 0x1800
    csrc mstatus, t0
    li   t0, 0x1800
    csrs mstatus, t0          # MPP = M
    li   t0, 0x80
    csrs mstatus, t0          # MPIE = 1
    li   t0, 8
    csrc mstatus, t0          # MIE = 0
    la   t0, a1
    csrw mepc, t0
    mret
a1:
    csrr t0, mstatus
    andi t1, t0, 8            # MIE must be 1 (loaded from MPIE)
    beqz t1, f_a
    andi t1, t0, 0x80         # MPIE must be 1
    beqz t1, f_b
    li   t1, 0x1800
    and  t2, t0, t1           # MPP must be U (0)
    bnez t2, f_c

    # ---- B: MRET to S clears MPRV; MPIE<-1, MIE<-MPIE ----
    li   t0, 0x1800
    csrc mstatus, t0
    li   t0, 0x800
    csrs mstatus, t0          # MPP = S
    li   t0, 0x20000
    csrs mstatus, t0          # MPRV = 1
    li   t0, 0x80
    csrs mstatus, t0          # MPIE = 1
    li   t0, 8
    csrc mstatus, t0          # MIE = 0
    li   s1, 9                # expect ecall from S
    li   s6, 1                # also check MPRV==0 and MPIE==1
    la   s5, b_ok
    la   t0, s_t
    csrw mepc, t0
    mret
s_t:
    ecall
b_ok:
    li   s6, 0

    # ---- C: M-mode SRET must return per SPP (TW=1 + wfi detects S) ----
    li   t0, 0x1800
    csrc mstatus, t0
    li   t0, 0x100
    csrs mstatus, t0          # SPP = S
    li   t0, 0x200000
    csrs mstatus, t0          # TW = 1
    la   t0, s_tw
    csrw sepc, t0
    li   s1, 2                # expect illegal instruction
    la   s5, c_ok
    sret
s_tw:
    wfi                       # TW=1 and priv<S -> illegal
    j    f_d
c_ok:
    li   t0, 0x200000
    csrc mstatus, t0          # TW = 0

    # ---- D: SRET S -> U: SIE<-SPIE, SPIE<-1, SPP<-0 ----
    li   t0, 0x1800
    csrc mstatus, t0
    li   t0, 0x800
    csrs mstatus, t0          # MPP = S
    li   t0, 0x100
    csrc mstatus, t0          # SPP = U -> sret must land in U
    li   t0, 0x20
    csrs mstatus, t0          # SPIE = 1
    li   t0, 0x2
    csrc mstatus, t0          # SIE = 0
    li   t0, 0x80
    csrs mstatus, t0          # MPIE = 1
    li   s1, 8                # expect ecall from U
    li   s6, 2                # check SIE/SPIE/SPP
    la   s5, d_ok
    la   t0, s_in
    csrw mepc, t0
    mret
s_in:
    la   t0, u_in
    csrw sepc, t0
    sret
u_in:
    ecall
d_ok:
    li   s6, 0

    # ---- E: sie/sip are views gated by mideleg ----
    li   t0, 2
    csrw mideleg, t0
    li   t0, 2
    csrw sie, t0
    csrr t1, mie
    andi t1, t1, 2
    beqz t1, f_e
    csrw mideleg, zero
    csrw mie, zero
    li   t0, 2
    csrw sie, t0              # mideleg=0 -> must not change mie
    csrr t1, mie
    andi t1, t1, 2
    bnez t1, f_f
    li   t0, 2
    csrw mideleg, t0
    li   t0, 2
    csrw mip, t0
    csrr t1, sip
    andi t1, t1, 2
    beqz t1, f_g
    csrw mip, zero
    j    pass

f_a: li t6, 'a'; j mk
f_b: li t6, 'b'; j mk
f_c: li t6, 'c'; j mk
f_d: li t6, 'd'; j mk
f_e: li t6, 'e'; j mk
f_f: li t6, 'f'; j mk
f_g: li t6, 'g'; j mk
mk:  li t0, 0x10000000
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
    beqz s6, h_done
    csrr t5, mstatus
    li   t6, 1
    beq  s6, t6, h_m
    # mode 2: SIE==1, SPIE==1, SPP==0
    andi t6, t5, 2
    beqz t6, hf
    andi t6, t5, 0x20
    beqz t6, hf
    andi t6, t5, 0x100
    bnez t6, hf
    j    h_done
h_m:
    # mode 1: MPRV==0, MPIE==1
    li   t6, 0x20000
    and  t6, t5, t6
    bnez t6, hf
    andi t6, t5, 0x80
    beqz t6, hf
h_done:
    jr   s5
hf:
    li   t6, 'p'
    j    mk

.section .tohost,"aw",@progbits
.align 4
.globl tohost
tohost: .word 0
