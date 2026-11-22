/* PC map M4 payload: own cave in the zero flash 0x0808A0D0-0x080A0000 (S9 moved
 * it from 0x080BC000: the synth/insert UI needs the whole cm4 cave). */
MEMORY {
 PCMAP (rx) : ORIGIN = 0x08090000, LENGTH = 0x8000
}
SECTIONS {
  .cave : { *(.text .text.* .rodata .rodata.*) } > PCMAP
  /DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.exidx* .comment) }
}
