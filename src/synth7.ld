/* Upstream Swarm uses this static RNG word. It is a temporary M7-only
 * register in the existing registry reservation; each voice seeds/saves its
 * own state around Render. No writable flash data or runtime init is needed. */
_ZN6stmlib6Random10rng_state_E = 0x388001b4;
MEMORY { CAVE (rx) : ORIGIN = 0x080C8000, LENGTH = 0x18000 }
SECTIONS {
 .cave : {
   KEEP(*(.text.synth_source_construct .text.synth_source_destroy .text.synth_source_delete .text.synth_source_process .text.synth_midi_dispatch))
   *(.text*) *(.rodata*)
 } > CAVE
 /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment*) *(.note*) }
}
