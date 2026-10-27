# 定向测试：无 F 扩展时 sstatus.FS 必须 read-only 0
.section .text
.globl _start
_start:
    li      t0, 0x2000       # FS = 01 (Initial)
    csrs    sstatus, t0
    csrr    t1, mstatus
    li      t2, 0x6000       # FS 位掩码
    and     t1, t1, t2
    bnez    t1, fail
    j       ok
fail:
    li      a0, 'Y'          # FS 可写 = 违规
    j       print
ok:
    li      a0, 'N'          # FS 保持 0 = 正确
print:
    li      t0, 0x10000000   # UART THR
    sb      a0, 0(t0)
1:  j       1b
