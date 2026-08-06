OUTPUT_ARCH( "riscv" )
ENTRY( module_entry )

SECTIONS
{
  . = 0x80080000;

  .text : {
    KEEP(*(.text.entry))
    *(.text .text.*)
  }

  .rodata : {
    *(.srodata .srodata.*)
    *(.rodata .rodata.*)
  }

  .data : {
    *(.sdata .sdata.*)
    *(.data .data.*)
  }

  .bss : {
    *(.sbss .sbss.*)
    *(.bss .bss.*)
  }

  /DISCARD/ : {
    *(.riscv.attributes)
    *(.comment)
    *(.note*)
    *(.eh_frame*)
  }
}
