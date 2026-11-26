#ifndef BLUEBOX_GRANULAR_PARAMS_H
#define BLUEBOX_GRANULAR_PARAMS_H
#define G_MODE 0x1a0
#define G_POS 0x1a1
#define G_SIZE 0x1a2
#define G_DENSITY 0x1a3
#define G_JITTER 0x1a4
#define G_FREEZE 0x1a5
#define G_REVERSE 0x1a6
#define T_DRIVE 0x1a7
#define T_DUCK 0x1a8
#define T_RELEASE 0x1a9
#define T_SEGMENT 0x1aa
#define T_TONE 0x1ab
#define T_HICUT 0x1ac
#define T_WILD 0x1ad
#define T_LOCUT 0x1b9 /* exp.8 Tape Lo C, Hz; stock slot verified empty */
/* FX1 mode 6, Freeze (spectral freeze, spectral_freeze.h). Free definition
 * slots below 0x20A, so the audio core's MIDI learn table can hold them. */
#define F_FREEZE 0x19c  /* Off / On; a new press captures again */
#define F_FADE 0x19d    /* 0..1000: 10 ms .. 4 s */
#define F_DRIFT 0x19e   /* 0..1000: exact hold .. moving wash */
#define F_TONE 0x19f    /* 0..1000: dark .. bright tilt, 500 flat */
#define F_LEVEL 0x1e6   /* 0..1000: wet level, 500 = unity */
#define F_TIME 0x1e7    /* list: 1/64, 1/32, 1/16, 1/8, 1/4, 1/2, 1 bar (transport synced); G26: no Off, value v = step v+1 of F_TIMES */
#define F_TMODE 0x1eb   /* list: Step (new capture each step), Gate (hold every other step) */
#define F_TIMES 8          /* internal steps, 0 unused (was Off) */
#define F_TIME_ITEMS 7
/* FX1 mode 7, Blur (spectral blur, the same engine and memory) */
#define B_BLUR 0x1ec    /* 0..1000: blur time 20 ms .. 10 s, full = hold */
#define B_DRIFT 0x1ed   /* 0..1000: phase wander, a diffuse wash */
#define B_TONE 0x1ee    /* 0..1000: dark .. bright tilt, 500 flat */
#define B_LEVEL 0x1ef   /* 0..1000: wet level, 500 = +6 dB */
#define F_IS_FREEZE(id) (((id)>=F_FREEZE && (id)<=F_TONE) || (id)==F_LEVEL || (id)==F_TIME || (id)==F_TMODE || ((id)>=B_BLUR && (id)<=B_LEVEL))
#define FX1_FREEZE_MODE 6
#define FX1_BLUR_MODE 7
#define FX1_SPECTRAL(m) ((unsigned)(m)-6u<2u)   /* the spectral engine: Freeze (6) or Blur (7) */
/* G30: FX1 mode 8, Spiral: the stock delay with a frequency shifter on each
 * line's read, inside the feedback loop (spiral_m7.h): every repeat moves by
 * Shift Hz again, a barber-pole rise or fall. */
#define FX1_SPIRAL_MODE 8
#define FX1_MODES 9
#define FX1_SPEC_PAGE(m) ((unsigned)(m)-6u<3u)  /* on the list's Spectral page: Stasis, Flux, Spiral */
#define S_SHIFT 0x1df   /* 0..1000: 500 = 0 Hz, left falls, right rises (as SPEC's Shift, ~0.1 Hz .. 2.5 kHz) */
#define S_SPREAD 0x1e0  /* Off / On: the right channel's repeats shift the other way */
#endif
