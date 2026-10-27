OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
SECTIONS {
 .blank 0x08065468 : { *(.blank) }
 .identify 0x08065648 : { *(.identify) }
 .erase 0x08065762 : { *(.erase) }
 .program 0x08065A48 : { *(.program) }
}
ASSERT(SIZEOF(.blank)<=0x80, "blank block too long")
ASSERT(SIZEOF(.identify)<=0x54, "identify block too long")
ASSERT(SIZEOF(.erase)<=0x36, "erase block too long")
ASSERT(SIZEOF(.program)<=0x58, "program block too long")
