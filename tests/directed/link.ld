OUTPUT_ARCH(riscv)
ENTRY(_start)
SECTIONS
{
  . = 0x80000000;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
  /* riscv-tests 约定的通过标记：写入 1 表示 PASS */
  . = 0x80001000;
  .tohost : { *(.tohost) }
}
