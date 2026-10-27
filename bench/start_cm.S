# CoreMark 专用启动：初始化栈后直接调 main
.section .text.init
.globl _start
_start:
    csrw    mstatus, zero
    csrw    mie, zero
    la      sp, cm_stack_top
    la      t0, cm_trap
    csrw    mtvec, t0
    la      t0, _bss_start
    la      t1, _bss_end
1:  bgeu    t0, t1, 2f
    sw      zero, 0(t0)
    addi    t0, t0, 4
    j       1b
2:
    call    main
    /* main 正常返回：写 syscon 关机。
       原来这里是 j . 自循环，会一直空转到 --max-cycles，导致退出码为「未判定」。 */
    li      t0, 0x00100000      /* syscon */
    li      t1, 0x5555
    sw      t1, 0(t0)
3:  j       3b

cm_trap:
    li      t0, 0x10000000
    li      t1, 'X'
    sb      t1, 0(t0)
1:  j       1b

.section .bss
.align 4
    .skip 262144
cm_stack_top: