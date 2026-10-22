MEMORY { CAVE (rx) : ORIGIN = 0x080A8000, LENGTH = 0x8000 }
SECTIONS { .cave : { *(.text .text.* .rodata .rodata.*) } > CAVE
/DISCARD/ : { *(.data .data.* .bss .bss.* COMMON .ARM.exidx* .comment) } }
