/* M7 code cave: in the unused, zero-filled flash gap inside the main image (file 0x4ac00-0xa0000). */
MEMORY { CAVE (rx) : ORIGIN = 0x080B0000, LENGTH = 0x8000 }
SECTIONS {
  .cave : { *(.text .text.* .rodata .rodata.*) } > CAVE
  /DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.exidx* .comment) }
}
