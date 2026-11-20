.section .rodata
.globl user_prog_start
.globl user_prog_end
.globl fork_prog_start
.globl fork_prog_end
.align 4
user_prog_start:
    .incbin "build/user_prog.elf"
user_prog_end:
.align 4
fork_prog_start:
    .incbin "build/fork_prog.elf"
fork_prog_end:
.align 4
.globl badptr_prog_start
.globl badptr_prog_end
badptr_prog_start:
    .incbin "build/badptr_prog.elf"
badptr_prog_end:
.align 4
.globl fdtest_prog_start
.globl fdtest_prog_end
fdtest_prog_start:
    .incbin "build/fdtest_prog.elf"
fdtest_prog_end:

.align 4
.globl spin_prog_start
.globl spin_prog_end
spin_prog_start:
    .incbin "build/spin_prog.elf"
spin_prog_end:
