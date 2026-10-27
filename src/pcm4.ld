/* PC map M4 payload: upper half of the M4 code cave, after Cobalt's cm4 code
 * (0x080B8000, checked < 0x4000 bytes in make_release.py). */
MEMORY {
 PCMAP (rx) : ORIGIN = 0x080BC000, LENGTH = 0x4000
}
SECTIONS {
  .cave : { *(.text .text.* .rodata .rodata.*) } > PCMAP
  /DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.exidx* .comment) }
}
