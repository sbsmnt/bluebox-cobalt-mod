/* DJ EQ M7 payload: 0x080B4000-0x080B8000. */
MEMORY { CAVE (rx) : ORIGIN = 0x080B4000, LENGTH = 0x4000 }
SECTIONS {
  .cave : { *(.text .text.* .rodata .rodata.*) } > CAVE
  /DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.exidx* .ARM.extab* .comment .note*) }
}
