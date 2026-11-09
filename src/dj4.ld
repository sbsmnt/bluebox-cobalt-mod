/* DJ EQ M4 payload: after the cue icon code (0x080D8000). */
MEMORY { CAVE (rx) : ORIGIN = 0x080DC000, LENGTH = 0x4000 }
SECTIONS {
  .cave : { *(.text .text.* .rodata .rodata.*) } > CAVE
  /DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.exidx* .comment) }
}
