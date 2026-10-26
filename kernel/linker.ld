OUTPUT_ARCH(riscv)
ENTRY(_start)
BASE = 0x80000000;

SECTIONS
{
    . = BASE;
    .text : {
        *(.text.init)
        *(.text)
    }
    .rodata : { *(.rodata) }
    .data : { *(.data) }
    .bss : {
        _bss_start = .;
        *(.bss)
        *(.sbss)      /* 关键：小 bss 也必须收进来，否则会落在 _kernel_end 之后 */
        *(COMMON)
        _bss_end = .;
    }
    . = ALIGN(4096);
    _kernel_end = .;
}