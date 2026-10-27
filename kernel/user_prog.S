.section .rodata
.globl user_prog_start
.globl user_prog_end
.globl fork_prog_start
.globl fork_prog_end
.align 4
user_prog_start:
    .incbin "build/user_prog.bin"
user_prog_end:
.align 4
fork_prog_start:
    .incbin "build/fork_prog.bin"
fork_prog_end:
