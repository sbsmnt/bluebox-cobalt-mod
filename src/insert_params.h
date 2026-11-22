#ifndef COBALT_INSERT_PARAMS_H
#define COBALT_INSERT_PARAMS_H
/* 0x1b9 is already Tape Lo C in Cobalt 1.0.0. These five following
 * definition slots are empty in the pinned release and stock tables. */
#define INS_MODE 0x1bau
#define INS_DRIVE 0x1bbu
#define INS_TONE 0x1bcu
#define INS_MIX 0x1bdu
#define INS_OUTPUT 0x1beu
/* S8a insert On/Off, after the synth block (0x1bf..0x1d4). Default On, so
 * projects saved before it keep their inserts. */
#define INS_ON 0x1d5u
/* S8d: id of the DRV / FLTR group button. UI only: never stored or sent. */
#define INS_GROUP 0x1d6u
/* F6: CHRD's chord (0..7: MIN9 MAJ9 MIN7 MAJ7 SUS 5THS 4THS DIM), after the
 * synth's Reso (0x1dd). Default MIN9, the F4/F5 chord. */
#define INS_CHORD 0x1deu
#define INS_CHORD_COUNT 8u
#define INS_CHANNEL_OFFSET 0x1ccu
#define INS_STATE_OFFSET 0x1d0u
#define INS_OBJECT_BYTES 0x240u
#endif
