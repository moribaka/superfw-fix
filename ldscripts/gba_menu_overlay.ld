OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(menu_overlay_start)
MEMORY { IWRAM : ORIGIN = 0x03000000, LENGTH = 6K }
SECTIONS {
  .text : { KEEP(*(.overlay_start)) *(.text*) *(.rodata*) *(.glue_7*) } > IWRAM
  .data : { *(.data*) *(.bss*) *(COMMON) . = ALIGN(4); } > IWRAM
  __overlay_end = .;
}
ASSERT(__overlay_end <= 0x03001800,"Overlay transport leaves 1792 bytes for stack")
