#ifndef COBALT_SYNTH_PARAMS_H
#define COBALT_SYNTH_PARAMS_H
#define SYN_ON 0x1bfu
#define SYN_ENGINE 0x1c0u
#define SYN_CHANNEL 0x1c1u
#define SYN_HARMONICS 0x1c2u
#define SYN_TIMBRE 0x1c3u
#define SYN_MORPH 0x1c4u
#define SYN_ATTACK 0x1c5u
#define SYN_RELEASE 0x1c6u
#define SYN_LEVEL 0x1c7u
#define SYN_DRONE 0x1c8u
/* Keep S1's Level key reserved for project compatibility; gain now belongs
 * to the native track fader. New settings never reinterpret an old key. */
#define SYN_CUTOFF 0x1c9u
#define SYN_LFO_RATE 0x1cau
#define SYN_LFO_DEST 0x1cbu
#define SYN_LFO_AMOUNT 0x1ccu
#define SYN_ENV_DEST 0x1cdu
#define SYN_ENV_AMOUNT 0x1ceu
#define SYN_ENV_DECAY 0x1cfu
#define SYN_ENV_ATTACK 0x1d0u
#define SYN_LFO_SHAPE 0x1d1u
#define SYN_LFO_SYNC 0x1d2u
#define SYN_LFO_SHAPE_COUNT 4u
#define SYN_LFO_SYNC_COUNT 13u
/* Stored LFO Sync: 0 Free (S5), 1..12 synced division, 13..24 Free that
 * remembers division value-12. The DSP syncs only on 1..12. */
#define SYN_LFO_SYNC_MAX 24u
#define SYN_LFO_DIVISIONS 12u
#define SYN_LFO_DEFAULT_DIVISION 9u /* 1/4 */
/* B-menu Free/Sync row. Mirrors the stored sync value; DSP ignores it. */
#define SYN_LFO_MODE 0x1d3u
/* B-menu Cluster seed 1..32 (stored 0..31). DSP keeps it in its own byte. */
#define SYN_SEED 0x1d4u
#define SYN_SEED_COUNT 32
/* Env Attack encoder/ring curve: log of (ms+offset). 200 keeps the first
 * detents close to the rest of the travel. */
#define SYN_ENV_ATTACK_OFFSET 200
/* S25: Attack (1..2000 ms) log of (ms+offset): pure log from 1 ms made the
 * first detents jump (1 -> 2 ms was 9% of the travel). */
#define SYN_ATTACK_OFFSET 50
/* S15: LFO rate travel is logarithmic over rate + 500 mHz, so the slow end
 * (0.01-1 Hz) takes about a third of the travel instead of two thirds. */
#define SYN_LFO_RATE_OFFSET 500
/* S17: what the M7 MIDI map code (C, gate_m7.c) reads of a voice to send an
 * LFO speed CC to the parameter of the LFO's current mode. Checked against
 * the C++ layout in synth_m7.cc. */
#define SYN_VOICE_MAGIC 0x53594e3au
#define SYN_BANK_ADDRESS 0x38800180u
#define SYN_VOICE_VALUES_OFFSET 6u
#define SYN_VOICE_LFO2_OFFSET 856u
/* S10: second LFO, same ranges as the first (rate mHz 10..20000, Dest
 * Off/Timbre/Harm/Morph/Cutoff, amount 0..1000, four shapes, stored sync
 * 0..24 with its B-menu Free/Sync mirror). Ids after INS_GROUP (0x1d6), free
 * in both stock definition tables. The DSP keeps rate..sync in lfo2[]. */
#define SYN_LFO2_RATE 0x1d7u
#define SYN_LFO2_DEST 0x1d8u
#define SYN_LFO2_AMOUNT 0x1d9u
#define SYN_LFO2_SHAPE 0x1dau
#define SYN_LFO2_SYNC 0x1dbu
#define SYN_LFO2_MODE 0x1dcu
/* S18: filter resonance 0..1000, gentle (Q 0.71 .. 2.5). */
#define SYN_RESO 0x1ddu
#define SYN_LFO2_FIRST SYN_LFO2_RATE
#define SYN_LFO2_LAST SYN_LFO2_SYNC
#define SYN_LFO2_INDEX(id) ((id)-SYN_LFO2_FIRST)
#define SYN_LFO2_PARAMS (SYN_LFO2_LAST-SYN_LFO2_FIRST+1u)
/* Synth parameters edited from the TRACK page and the B menu. */
#define SYN_EDITABLE(id) (((id)>=SYN_ON && (id)<=SYN_LAST) || ((id)>=SYN_LFO2_FIRST && (id)<=SYN_LFO2_LAST) || (id)==SYN_RESO)
#define SYN_LFO_SYNCED(v) ((v)>=1 && (v)<=(int)SYN_LFO_DIVISIONS)
#define SYN_LFO_DIVISION(v) (SYN_LFO_SYNCED(v)?(v):(v)>(int)SYN_LFO_DIVISIONS && (v)<=(int)SYN_LFO_SYNC_MAX?(v)-(int)SYN_LFO_DIVISIONS:(int)SYN_LFO_DEFAULT_DIVISION)
#define SYN_DEST_COUNT 5u
/* Spectral F2: two engines, 0 VA and 1 Cluster. Older projects' ordinals
 * still load: 2 (VA) plays VA, 3 (Cluster) and 4 (Swarm before S14) play
 * Cluster, and the removed 0 FM / 1 Shape now mean VA / Cluster. */
#define SYN_ENGINE_COUNT 2u
#define SYN_ENGINE_STORED_MAX 4
#define SYN_ENGINE_CLUSTER(v) ((v)==1 || (v)>=3)
#define SYN_RNG_ADDRESS 0x388001b4u
#define SYN_LAST SYN_LFO_SYNC
/* The obsolete Level key stays in projects but consumes no DSP state. */
#define SYN_INDEX(id) ((id)-SYN_ON-((id)>SYN_LEVEL?1u:0u))
#define SYN_RUNTIME_PARAMS (SYN_LAST-SYN_ON)
#define SYN_SOURCE_OFFSET 0x5a8u
/* S10: +0x20 for the second LFO (12 tracks: 384 bytes more M7 heap). */
#define SYN_SOURCE_BYTES 0xa20u
#endif
