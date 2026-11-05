/* Phones Src AUTO: like a DJ mixer's PFL, the phones hear the cue bus while
 * any track has its cue on (Toggle) or turned up (Fader), whether or not the
 * track is playing or has an input, and the main mix otherwise. Two hooks:
 * - the mixer node records its own address, so the cue and input state of
 *   every channel strip can be read, including strips it skips because no
 *   input is selected;
 * - the native output node (master, phones, OUT2/OUT3) reads that state,
 *   publishes it for the track header icons (cue_m4.c) and sets its native
 *   Phones Src MAIN copy flag before running. */
#include "shared.h"
#define CUE_MAGIC 0x43554534u
#define CUE_SRC_ID 0x178u   /* aux1source: 0 CUE, 1 MAIN, 2 AUTO (Cobalt) */
#define CUE_AUTO 2
#define CUE_HOLD 4800u      /* 100 ms at 48 kHz before falling back to Main */
#define MIXER_VT 0x0806af2cu
/* shown: tracks with cue on; inputs: tracks with an input selected. */
struct cue_state {uint32_t magic;int32_t src;uint32_t hold;uint8_t *mixer;uint32_t shown,inputs,seq;};
#define CUE ((struct cue_state *)0x38800660u)
/* The gate bank (0x400, 584 bytes) ends at 0x648; gate diagnostics start at 0x680. */
_Static_assert(0x660+sizeof(struct cue_state)<=0x680,"cue state: one cache line before the gate diagnostics");
/* Native send: +0 on, +4 level, +8 dB, +0xc idle, +0x10 gain now, +0x14 target. */
struct cue_send {uint8_t on,pad[3];float level,db;uint8_t idle,pad2[3];float gain,target;};
struct cue_event {uint8_t type,pad[7];uint32_t target;uint16_t id,pad2;int32_t value;uint32_t pad3;};
/* Mixer: strip count at +0xe68, strips of 0x130 bytes from +0x20. A strip is
 * skipped while both input flags (+6, +0xa) are 0; its phones-bus sends are
 * the pre-fader one at +0xa0 and the post-fader one at +0xb8. */
#define STRIPS 0xe68u
#define STRIP0 0x20u
#define STRIP_SIZE 0x130u
#define cue_native ((unsigned (*)(uint8_t *,void *))0x0804e21du)
#define cue_mixer_native ((unsigned (*)(uint8_t *,void *))0x0804fbadu)
#define cue_queue ((void *(*)(void *,unsigned))0x080532d5u)
#define cue_next ((int (*)(void *,unsigned,void *))0x0804e1b9u)
#define cue_frames ((unsigned (*)(void *))0x0804cd41u)
#define cue_bus_at ((void *(*)(void *,unsigned))0x0805331du)

/* Phones Src list for the M4 settings page (stock: CUE, MAIN). */
const char *const cue_phones_names[3]={"CUE","MAIN","AUTO"};

static struct cue_state *cue_state(void) {
    backup_enable();PWR_CR1|=1u<<8;
    struct cue_state *s=CUE;
    if(s->magic!=CUE_MAGIC){s->src=0;s->hold=0;s->mixer=0;s->shown=0;s->inputs=0;s->seq=0;s->magic=CUE_MAGIC;}
    return s;
}
static int cue_ram(uint32_t a,uint32_t n) {
    return (a>=0x24000000u && a<=0x24080000u-n) || (a>=0x30000000u && a<=0x30048000u-n) ||
           (a>=0xc0000000u && a<=0xc4000000u-n);
}

unsigned cue_mixer_process(uint8_t *mixer,void *ctx) {
    cue_state()->mixer=mixer;
    return cue_mixer_native(mixer,ctx);
}

/* Cue (bit per track) and input masks from the mixer's channel strips. */
static void cue_scan(struct cue_state *s,unsigned *cued,unsigned *inputs) {
    uint8_t *m=s->mixer;*cued=*inputs=0;
    if(!cue_ram((uint32_t)m,STRIPS+4u) || *(uint32_t *)m!=MIXER_VT)return;
    int n=*(int *)(m+STRIPS);if(n>12)n=12;
    for(int i=0;i<n;i++) {
        uint8_t *strip=m+STRIP0+(unsigned)i*STRIP_SIZE;
        const struct cue_send *pre=(const struct cue_send *)(strip+0xa0),*post=(const struct cue_send *)(strip+0xb8);
        if(pre->target!=0.f || post->target!=0.f)*cued|=1u<<i;
        if(strip[6] || strip[0xa])*inputs|=1u<<i;
    }
}

unsigned cue_out_process(uint8_t *obj,void *ctx) {
    struct cue_state *s=cue_state();
    void *q=cue_queue(ctx,*(uint32_t *)(obj+0x18));
    struct cue_event ev;
    for(unsigned i=0;cue_next(q,i,&ev);i++)
        if(ev.type==0x39 && ev.target==0x0cu && ev.id==CUE_SRC_ID)s->src=ev.value;
    unsigned cued,inputs;cue_scan(s,&cued,&inputs);
    if(s->shown!=cued || s->inputs!=inputs) {
        s->shown=cued;s->inputs=inputs;s->seq++;
        __asm volatile("dsb" ::: "memory");
        SCB_DCCMVAC=(uint32_t)s;
        __asm volatile("dsb\nisb" ::: "memory");
    }
    if(s->src==CUE_AUTO) {
        unsigned n=cue_frames(cue_bus_at(ctx,0x0fu));
        if(cued)s->hold=CUE_HOLD;
        else s->hold=s->hold>n ? s->hold-n : 0;
        /* Native flag at +0x88: nonzero copies Main over the phones bus. */
        obj[0x88]=s->hold==0;
    }
    return cue_native(obj,ctx);
}
