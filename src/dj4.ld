/* DJ EQ M4 payload: 0x0808C000-0x08090000. */
MEMORY { CAVE (rx) : ORIGIN = 0x0808C000, LENGTH = 0x4000 }
SECTIONS {
  .cave : { *(.text .text.* .rodata .rodata.*) } > CAVE
  /DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.exidx* .comment) }
}
