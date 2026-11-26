/* MIDI-mapped CCs for Cobalt's own parameters, on the M7.
 * The M7 resolves a mapped CC with its own copy of the parameter
 * definitions, built once, lazily, by 0x0805342c into *0x24008e50 (28-byte
 * entries: +0 id, +2 type, +0xc enum count, +0x10 min, +0x14 max). Only the
 * M4 table has Cobalt's ids, so every mapped CC scaled to min = max = 0: the
 * Learn label showed, but the value stayed at 0. learn_defs_build adds the
 * M4 ranges after the stock build. A stock entry is kept unless the range is
 * marked replace (Reverb Style: Gazer is a 16th style).
 * A mapped CC is also posted only to queue 0x0c (0x0804f1ca), while M4
 * parameter commands reach the target track's own queue as well, the one
 * the Gate reads; learn_post adds that post for track parameters. FX
 * parameters already reach their FX queue. */
struct learn_range {uint16_t id;uint8_t type,replace;int32_t count,min,max;};
#ifndef LEARN_EXTRA_RANGES
#define LEARN_EXTRA_RANGES
#endif
#ifndef LEARN_EXTRA_TRACK
#define LEARN_EXTRA_TRACK(id) 0
#endif
static const struct learn_range learn_ranges[]={
    /* Saturate, delay Drift / Send / Pitch / Pitch On */
    {0x43,8,0,0,0,1000},{0x3a,8,0,0,0,1000},{0x3d,8,0,0,0,1000},{0x4a,1,0,0,-12,12},{0x4b,4,0,0,0,1},
    {0x155,5,1,16,0,0}, /* Reverb Style incl. Gazer */
    /* Granular and tape delay types */
    {0x1a0,5,0,9,0,0},{0x1a1,8,0,0,0,1000},{0x1a2,1,0,0,20,250},{0x1a3,1,0,0,1,4},{0x1a4,8,0,0,0,1000},
    {0x1a5,4,0,0,0,1},{0x1a6,4,0,0,0,1},{0x1a7,8,0,0,0,1000},{0x1a8,8,0,0,0,1000},{0x1a9,1,0,0,20,2000},
    {0x1aa,1,0,0,40,600},{0x1ab,8,0,0,0,1000},{0x1ac,1,0,0,500,16000},{0x1ad,4,0,0,0,1},
    {0x1ae,7,0,0,-96000,-12000}, /* Gate threshold */
    /* Gazer reverb */
    {0x1af,8,0,0,0,1000},{0x1b0,8,0,0,0,1000},{0x1b1,8,0,0,0,1000},{0x1b2,8,0,0,0,1000},
    {0x1b3,1,0,0,10,2000},{0x1b4,1,0,0,500,16000},{0x1b5,1,0,0,20,2000},
    /* Master saturation and Tape Lo Cut */
    {0x1b6,5,0,2,0,0},{0x1b7,8,0,0,0,1000},{0x1b8,7,0,0,-12000,6000},{0x1b9,1,0,0,20,2000},
    /* FX1 Freeze: Freeze, Fade, Drift, Tone, Level */
    {0x19c,4,0,0,0,1},{0x19d,8,0,0,0,1000},{0x19e,8,0,0,0,1000},{0x19f,8,0,0,0,1000},{0x1e6,8,0,0,0,1000},
    {0x1e7,5,0,7,0,0},{0x1eb,5,0,2,0,0},   /* Freeze Time, Mode */
    {0x1ec,8,0,0,0,1000},{0x1ed,8,0,0,0,1000},{0x1ee,8,0,0,0,1000},{0x1ef,8,0,0,0,1000},   /* Blur */
    {0x1df,8,0,0,0,1000},{0x1e0,4,0,0,0,1},   /* G30 Spiral Shift, Spread */
    LEARN_EXTRA_RANGES
};
void learn_defs_build(void *owner) {
    ((void (*)(void *))0x0805342du)(owner);
    uint8_t *table=*(uint8_t **)0x24008e50u;
    if(!table)return;
    for(unsigned i=0;i<sizeof learn_ranges/sizeof learn_ranges[0];i++) {
        const struct learn_range *r=&learn_ranges[i];
        uint8_t *e=table+r->id*28u;
        if(*(uint16_t *)e && !r->replace)continue;
        *(uint16_t *)e=r->id;e[2]=r->type;
        *(int32_t *)(e+0xc)=r->count;*(int32_t *)(e+0x10)=r->min;*(int32_t *)(e+0x14)=r->max;
    }
}
static int learn_track_param(unsigned id) {return id==0x1aeu || LEARN_EXTRA_TRACK(id);}
void learn_post(void *queues,uint8_t *event,unsigned queue) {
#ifdef LEARN_EVENT
    /* Optional: reshape a mapped CC's event in place (its value, e.g. on a
     * log curve, or its parameter), so the audio core and the value echoed
     * to the M4 agree. */
    if(event[0]==0x39)LEARN_EVENT(event);
#endif
    unsigned target=*(uint32_t *)(event+8),id=*(uint16_t *)(event+0xc);
    ((void (*)(void *,void *,unsigned))0x08053419u)(queues,event,queue);
    if(event[0]==0x39 && target<12u && target!=queue && learn_track_param(id))
        ((void (*)(void *,void *,unsigned))0x08053419u)(queues,event,target);
}
