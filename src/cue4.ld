/* Cue icon M4 payload: in the zero-filled flash gap after the M7 cue code (0x080D0000). */
MEMORY { CAVE (rx) : ORIGIN = 0x080D8000, LENGTH = 0x4000 }
SECTIONS {
  .cave : { *(.text .text.* .rodata .rodata.*) } > CAVE
  /DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.exidx* .comment) }
}
