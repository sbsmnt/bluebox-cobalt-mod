/* Cue icon M4 payload: after the M7 cue code (0x08098000), past the PC map cave; the synth cave owns 0x080C8000-0x080E0000. */
MEMORY { CAVE (rx) : ORIGIN = 0x0809C000, LENGTH = 0x4000 }
SECTIONS {
  .cave : { *(.text .text.* .rodata .rodata.*) } > CAVE
  /DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.exidx* .comment) }
}
