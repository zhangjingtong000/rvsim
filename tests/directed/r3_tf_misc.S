# F: mcountinhibit CY/IR, page-crossing misaligned access, AMO aq/rl, software interrupt + priority
.section .text
.globl _start
_start:
    la   t0, handler
    csrw mtvec, t0
    la   s5, dead
    li   s1, 0xDEAD

    # ---- 1: mcountinhibit.CY=1 freezes mcycle ----
    csrw mcountinhibit, zero
    li   t0, 0x1000
    csrw mcycle, t0
    li   t0, 1
    csrw mcountinhibit, t0
    csrr a0, mcycle
    li   t2, 60
1:  nop
    addi t2, t2, -1
    bnez t2, 1b
    csrr a1, mcycle
    bne  a0, a1, f1

    # ---- 2: mcountinhibit.IR=1 freezes minstret ----
    csrw mcountinhibit, zero
    li   t0, 0x2000
    csrw minstret, t0
    li   t0, 4
    csrw mcountinhibit, t0
    csrr a0, minstret
    li   t2, 60
2:  nop
    addi t2, t2, -1
    bnez t2, 2b
    csrr a1, minstret
    bne  a0, a1, f2
    csrw mcountinhibit, zero

    # ---- 3: misaligned load straddling a page boundary ----
    li   t0, 0x80000FFC
    li   t1, 0xAABBCCDD
    sw   t1, 0(t0)
    li   t1, 0x11223344
    sw   t1, 4(t0)
    li   t0, 0x80000FFE
    lw   t2, 0(t0)
    li   t3, 0x3344AABB
    bne  t2, t3, f3
    li   t0, 0x80000FFF
    lhu  t2, 0(t0)
    li   t3, 0x44AA
    bne  t2, t3, f4
    li   t0, 0x80001001
    lhu  t2, 0(t0)
    li   t3, 0x2233
    bne  t2, t3, f5

    # ---- 4: AMO aq/rl does not change the arithmetic ----
    la   t0, amo_t
    li   t1, 5
    sw   t1, 0(t0)
    li   t2, 7
    amoadd.w.aqrl a0, t2, 0(t0)
    li   t3, 5
    bne  a0, t3, f6
    lw   t4, 0(t0)
    li   t3, 12
    bne  t4, t3, f7
    li   t2, 0x0F0F
    amoswap.w.aq a0, t2, 0(t0)
    lw   t4, 0(t0)
    li   t3, 0x0F0F
    bne  t4, t3, f8

    # ---- 5: software interrupt SSIP -> M, cause 0x80000001 ----
    li   t0, 2
    csrw mie, t0
    csrw mideleg, zero
    li   t0, 8
    csrs mstatus, t0          # MIE=1
    li   s1, 0x80000001
    la   s5, irq1_ok
    li   t0, 2
    csrw mip, t0
    j    f9
irq1_ok:
    csrw mip, zero

    # ---- 6: MSIP via CLINT (mip.MSIP is hardware driven) ----
    li   t0, 0x02000000
    li   t1, 1
    sw   t1, 0(t0)
    li   t0, 0xA             # MSIP|SSIP enabled
    csrw mie, t0
    li   s1, 0x80000003       # machine software interrupt
    la   s5, irq2_ok
    li   t0, 8
    csrs mstatus, t0
    j    f10
irq2_ok:
    li   t0, 0x02000000
    sw   zero, 0(t0)
    csrw mip, zero
    j    pass

f1:  li t6, 'a'; j mk
f2:  li t6, 'b'; j mk
f3:  li t6, 'c'; j mk
f4:  li t6, 'd'; j mk
f5:  li t6, 'e'; j mk
f6:  li t6, 'f'; j mk
f7:  li t6, 'g'; j mk
f8:  li t6, 'h'; j mk
f9:  li t6, 'i'; j mk
f10: li t6, 'j'; j mk
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
    csrw mip, zero
    jr   s5
hf:
    li   t6, 'p'
    j    mk

.align 4
amo_t: .word 0
.section .tohost,"aw",@progbits
.align 4
.globl tohost
tohost: .word 0
