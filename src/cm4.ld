/* M4 code cave (the M4 can execute from bank 1): in the unused, zero-filled flash gap inside the main image (file 0x4ac00-0xa0000). */
/* alpha.6 (from C16): three ART-cached 0x081-page slots reuse the stock PCM24 function bodies in place. */
MEMORY {
 CAVE (rx) : ORIGIN = 0x080B8000, LENGTH = 0x8000
 HOT (rx) : ORIGIN = 0x08108DF4, LENGTH = 0xA4
 HOTDM (rx) : ORIGIN = 0x08108C8C, LENGTH = 0x48
 HOTDS (rx) : ORIGIN = 0x08108CD4, LENGTH = 0x70
}
SECTIONS {
  .cave : { *(.text .text.* .rodata .rodata.*) } > CAVE
  .pcmhot : { KEEP(*(.pcmhot)) } > HOT
  .pcmdm : { KEEP(*(.pcmdm)) } > HOTDM
  .pcmds : { KEEP(*(.pcmds)) } > HOTDS
  /DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.exidx* .comment) }
}
