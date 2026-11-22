#include "shared.h"
#include "noise_gate.h"
#include "gate_diag_m7.h"
#include "insert_drive.h"
void insert_drive_audio(struct insert_drive *,float *,float *,unsigned);
#define NG_MAGIC 0x4e473937u
struct gate_bank {uint32_t magic;struct noise_gate track[12];float input_peak[12][2];uint8_t *meter_ctx;};
#define NG_BANK ((struct gate_bank *)0x38800400u)
_Static_assert(sizeof(struct noise_gate)==40,"gate track ABI");
_Static_assert(0x400+sizeof(struct gate_bank)<=0x680,"gate before diagnostic reports");
_Static_assert(0x400+sizeof(struct gate_bank)<=0xb00,"gate below M4 UI state");
struct ng_bus {uint32_t cap,n;float *l,*r;uint8_t silent,stereo;};
struct ng_event {uint8_t type,pad[7];uint32_t target;uint16_t id,pad2;int32_t value;uint32_t pad3;};
typedef unsigned (*ng_fn)(uint8_t *,void *);
#define ng_native ((ng_fn)0x08043b91u)
static struct gate_bank *ng_bank(void) {
    backup_enable();PWR_CR1|=1u<<8;
    if(NG_BANK->magic!=NG_MAGIC) {
        for(unsigned i=0;i<12;i++){ng_reset(&NG_BANK->track[i],0);NG_BANK->input_peak[i][0]=NG_BANK->input_peak[i][1]=0.f;}
        NG_BANK->meter_ctx=0;NG_BANK->magic=NG_MAGIC;
    }
    return NG_BANK;
}
void *gate_construct(uint8_t *obj,unsigned bands) {
    void *result=((void *(*)(void *,unsigned))0x08042e61u)(obj,bands);
    struct gate_bank *b=ng_bank();b->meter_ctx=0;
    gd_audio_reset();
    *(uint32_t *)(obj+INS_CHANNEL_OFFSET)=0xffffffffu;
    ins_reset((struct insert_drive *)(obj+INS_STATE_OFFSET));
    for(unsigned i=0;i<12;i++)if(b->track[i].owner==obj)ng_reset(&b->track[i],obj);
    return result;
}
unsigned gate_process(uint8_t *obj,void *ctx) {
    unsigned ch=*(uint32_t *)(obj+0x18);
    /* The same native EQ class is also used on Main. Never gate that node. */
    if(ch>=12) {
        /* MIDI-learned Gate CCs arrive on Main's queue, addressed to the track. */
        void *q=((void *(*)(void *,unsigned))0x080532d5u)(ctx,ch);
        struct ng_event ev;
        for(unsigned i=0;((int (*)(void *,unsigned,void *))0x0804e1b9u)(q,i,&ev);i++)
            if(ev.type==0x39 && ev.target<12 && ev.id==NG_ID && ng_bank()->track[ev.target].owner)
                ng_event(&ng_bank()->track[ev.target],ev.value);
        return ng_native(obj,ctx);
    }
    struct noise_gate *g=&ng_bank()->track[ch];if(g->owner!=obj)ng_reset(g,obj);
    struct insert_drive *insert=(struct insert_drive *)(obj+INS_STATE_OFFSET);
    if(*(uint32_t *)(obj+INS_CHANNEL_OFFSET)!=ch) {
        ins_reset(insert);*(uint32_t *)(obj+INS_CHANNEL_OFFSET)=ch;
    }
    void *q=((void *(*)(void *,unsigned))0x080532d5u)(ctx,ch);
    struct ng_event ev;
    unsigned received=0;
    for(unsigned i=0;((int (*)(void *,unsigned,void *))0x0804e1b9u)(q,i,&ev);i++)
        if(ev.type==0x39 && ev.target==ch) {
            if(ev.id==NG_ID){ng_event(g,ev.value);received++;}
            else if(insert->magic==INS_MAGIC)ins_event(insert,ev.id,ev.value);
        }
    /* Outside Gate metering, Off keeps the original cheap exact bypass.
     * Native report wrappers retain the M7 mixer pointer, never an M4 pointer. */
    uint8_t *mix=NG_BANK->meter_ctx;
    int metering=mix && *(uint16_t *)(mix+0xe6c)==NG_ID;
    int active=g->threshold!=NG_OFF || g->gain!=1.f;
    float diag_left=0.f,diag_right=0.f;
    if(active || metering) {
        unsigned port=(*(unsigned (**)(void *))(*(uint32_t *)obj+0x54))(obj);
        struct ng_bus *bus=((struct ng_bus *(*)(void *,unsigned))0x0805331du)(ctx,port);
        unsigned n=((unsigned (*)(void *))0x0804cd41u)(bus);if(n>32)n=32;
        float left=0.f,right=0.f;
        if(!bus->silent && bus->n) {
            left=bus->l?ng_scan(bus->l,n):0.f;
            right=bus->stereo && bus->r?ng_scan(bus->r,n):left;
        }
        if(metering){NG_BANK->input_peak[ch][0]=left;NG_BANK->input_peak[ch][1]=right;}
        diag_left=left;diag_right=right;
        if(active) {
            if(bus->silent || !bus->n)ng_process_peak(g,0,0,n,0.f);
            else ng_process_peak(g,bus->l,bus->stereo?bus->r:0,n,left>right?left:right);
        }
    }
    gd_audio_tick(ch,g,received,diag_left,diag_right,active || metering);
    if(insert->magic==INS_MAGIC && ins_active(insert)) {
        unsigned port=(*(unsigned (**)(void *))(*(uint32_t *)obj+0x54))(obj);
        struct ng_bus *bus=((struct ng_bus *(*)(void *,unsigned))0x0805331du)(ctx,port);
        if(bus && !bus->silent && bus->n) {
            unsigned n=((unsigned (*)(void *))0x0804cd41u)(bus);
            insert_drive_audio(insert,bus->l,bus->stereo?bus->r:0,n);
        } else {
            ins_quiet(insert);insert->enable=0.f;
            if(INS_IS_BANK(insert->running_mode)){struct fsh_state *fs=fsh_bank_state(ch);
                if(fs){if(INS_IS_RESO(insert->running_mode))rsn_quiet((struct rsn_state *)fs);else fsh_quiet(fs);}}
        }
    }
    return ng_native(obj,ctx);
}

/* Gate's meter must show the signal feeding its detector, on the Gate fader's
 * scale. Native meters fill linearly from -60 dB (empty) to 0 dB (full); the
 * Gate fader is linear across -96..-12 dB. Publishing 60*(p+96)/84-60 puts
 * the meter top level with the fader handle exactly when the detector input
 * reaches the threshold, as on TRACK's bar. Capped just below 0 so the
 * native >=0 dB clip branch is never taken. */
static float gate_meter_db(float peak) {
    float db=((float (*)(float))0x08055c75u)(peak);
    float m=(db+96.f)*(60.f/84.f)-60.f;
    if(!(m>-60.f))m=-60.f;
    if(m>-.000001f)m=-.000001f;
    return m;
}
static int gate_meter_active(uint8_t *mix,unsigned ch) {
    if(NG_BANK->magic!=NG_MAGIC)return 0;
    NG_BANK->meter_ctx=mix;
    return ch<12 && *(uint16_t *)(mix+0xe6c)==NG_ID;
}
void gate_meter_mono(void *report,unsigned ch,uint8_t *mix,float db) {
    if(gate_meter_active(mix,ch)) {
        float p=NG_BANK->input_peak[ch][0];if(NG_BANK->input_peak[ch][1]>p)p=NG_BANK->input_peak[ch][1];
        db=gate_meter_db(p);
    }
    ((void (*)(void *,unsigned,float))0x0804bab1u)(report,ch,db);
}
void gate_meter_stereo(void *report,unsigned ch,uint8_t *mix,float l,float r) {
    if(gate_meter_active(mix,ch)) {
        l=gate_meter_db(NG_BANK->input_peak[ch][0]);
        r=gate_meter_db(NG_BANK->input_peak[ch][1]);
    }
    ((void (*)(void *,unsigned,float,float))0x0804bacdu)(report,ch,l,r);
}
__attribute__((naked)) void gate_meter_mono_shim(void) {__asm volatile("mov r2,r8\nb gate_meter_mono");}
__attribute__((naked)) void gate_meter_stereo_shim(void) {__asm volatile("mov r2,r8\nb gate_meter_stereo");}

/* S9b: the inserts and the synth add their ranges and their track-queue
 * routing to the shared MIDI map code (learn_m7.h; Cobalt's own ids, the
 * Gate threshold included, are already in its table). */
#include "synth_params.h"
#define LEARN_EXTRA_RANGES \
    {INS_MODE,5,0,16,0,0},{INS_DRIVE,8,0,0,0,1000},{INS_TONE,8,0,0,0,1000},{INS_MIX,8,0,0,0,1000}, \
    {INS_OUTPUT,7,0,0,-18000,6000},{INS_ON,5,0,2,0,0},{INS_CHORD,5,0,INS_CHORD_COUNT,0,0}, \
    {SYN_ON,5,0,2,0,0},{SYN_ENGINE,5,0,SYN_ENGINE_COUNT,0,0},{SYN_CHANNEL,1,0,0,1,16}, \
    {SYN_HARMONICS,8,0,0,0,1000},{SYN_TIMBRE,8,0,0,0,1000},{SYN_MORPH,8,0,0,0,1000}, \
    {SYN_ATTACK,1,0,0,1,2000},{SYN_RELEASE,1,0,0,5,5000},{SYN_LEVEL,8,0,0,0,1000}, \
    {SYN_DRONE,5,0,2,0,0},{SYN_CUTOFF,1,0,0,20,20000},{SYN_LFO_RATE,1,0,0,0,127}, \
    {SYN_LFO_DEST,5,0,SYN_DEST_COUNT,0,0},{SYN_LFO_AMOUNT,8,0,0,0,1000}, \
    {SYN_ENV_DEST,5,0,SYN_DEST_COUNT,0,0},{SYN_ENV_AMOUNT,1,0,0,-100,100}, \
    {SYN_ENV_DECAY,1,0,0,5,5000},{SYN_ENV_ATTACK,1,0,0,0,5000}, \
    {SYN_LFO_SHAPE,5,0,SYN_LFO_SHAPE_COUNT,0,0},{SYN_LFO_SYNC,1,0,0,0,127}, \
    {SYN_LFO_MODE,5,0,2,0,0},{SYN_SEED,5,0,SYN_SEED_COUNT,0,0}, \
    {SYN_LFO2_RATE,1,0,0,0,127},{SYN_LFO2_DEST,5,0,SYN_DEST_COUNT,0,0},{SYN_LFO2_AMOUNT,8,0,0,0,1000}, \
    {SYN_LFO2_SHAPE,5,0,SYN_LFO_SHAPE_COUNT,0,0},{SYN_LFO2_SYNC,1,0,0,0,127},{SYN_LFO2_MODE,5,0,2,0,0}, \
    {SYN_RESO,8,0,0,0,1000},
#define LEARN_EXTRA_TRACK(id) (((id)>=INS_MODE && (id)<=INS_ON) || (id)==INS_CHORD || ((id)>=SYN_LFO2_FIRST && (id)<=SYN_LFO2_MODE) || (id)==SYN_RESO)
/* S16: log controls follow the same curve by CC as by encoder: the CC
 * fraction of the linear value maps to lo*(hi/lo)^fraction over value +
 * offset. S17: an LFO speed CC (learned on LFO S or on its division) acts
 * on the speed of the LFO's current mode, like the encoder: divisions in
 * Sync, the rate in Free. It never switches the mode. */
struct learn_log {uint16_t id;int16_t offset;int32_t lo,hi;};
static const struct learn_log learn_logs[]={
    {SYN_CUTOFF,0,20,20000},{SYN_LFO_RATE,SYN_LFO_RATE_OFFSET,10,20000},{SYN_LFO2_RATE,SYN_LFO_RATE_OFFSET,10,20000},
    {SYN_ENV_ATTACK,SYN_ENV_ATTACK_OFFSET,0,5000},{SYN_ENV_DECAY,0,5,5000},
    {SYN_ATTACK,SYN_ATTACK_OFFSET,1,2000},{SYN_RELEASE,0,5,5000}, /* S23 */
};
static float learn_lg2(float x) {
    union{float f;uint32_t u;} v={x};
    int e=(int)((v.u>>23)&255u)-127;v.u=(v.u&0x007fffffu)|0x3f800000u;
    float m=v.f-1.f;
    return (float)e+m*(1.4425449f+m*(-.7181452f+m*(.4575485f+m*(-.2779042f+m*(.121797f+m*-.0258411f)))));
}
static float learn_ex2(float x) {
    int i=(int)x;if((float)i>x)i--;
    float f=x-(float)i;
    union{float f;uint32_t u;} v={1.f+f*(.6931472f+f*(.2402265f+f*(.0555041f+f*(.0096181f+f*.0013334f))))};
    v.u+=(uint32_t)i<<23;return v.f;
}
static int learn_log_value(const struct learn_log *c,float fraction) {
    if(fraction<=0.f)return c->lo;
    if(fraction>=1.f)return c->hi;
    float lo=(float)(c->lo+c->offset),hi=(float)(c->hi+c->offset);
    int v=(int)(lo*learn_ex2(fraction*learn_lg2(hi/lo))+.5f)-c->offset;
    return v<c->lo?c->lo:v>c->hi?c->hi:v;
}
/* The stored division of LFO 1 or 2 of track ch: 1..12 = Sync, else Free;
 * -1 when the track has no live synth voice. */
static int learn_lfo_sync(unsigned ch,unsigned lfo2) {
    const uint8_t *bank=(const uint8_t *)SYN_BANK_ADDRESS;
    if(ch>=12u || *(const uint32_t *)bank!=SYN_VOICE_MAGIC)return -1;
    const uint8_t *voice=*(const uint8_t *const *)(bank+4u+ch*4u);
    if(!voice || *(const uint32_t *)voice!=SYN_VOICE_MAGIC)return -1;
    return lfo2?((const int16_t *)(voice+SYN_VOICE_LFO2_OFFSET))[SYN_LFO2_INDEX(SYN_LFO2_SYNC)]
               :((const int16_t *)(voice+SYN_VOICE_VALUES_OFFSET))[SYN_INDEX(SYN_LFO_SYNC)];
}
static void learn_event(uint8_t *event) {
    unsigned id=*(uint16_t *)(event+0xc),ch=*(uint32_t *)(event+8);
    int32_t *value=(int32_t *)(event+0x10);
    unsigned lfo2=id==SYN_LFO2_RATE || id==SYN_LFO2_SYNC;
    if(lfo2 || id==SYN_LFO_RATE || id==SYN_LFO_SYNC) {
        unsigned rate=lfo2?SYN_LFO2_RATE:SYN_LFO_RATE,division=lfo2?SYN_LFO2_SYNC:SYN_LFO_SYNC;
        /* S18: all four LFO speed ids take the raw CC (range 0..127 above),
         * so a CC learned on the division still has 128 steps in Free. */
        float fraction=(float)*value*(1.f/127.f);
        int sync=learn_lfo_sync(ch,lfo2);
        if(sync<0)sync=0; /* no live voice: change the rate only, never the mode */
        if(SYN_LFO_SYNCED(sync)) {
            int d=1+(int)(fraction*(float)(SYN_LFO_DIVISIONS-1u)+.5f);
            *(uint16_t *)(event+0xc)=(uint16_t)division;*value=d<1?1:d>(int)SYN_LFO_DIVISIONS?(int)SYN_LFO_DIVISIONS:d;
        } else {
            *(uint16_t *)(event+0xc)=(uint16_t)rate;*value=learn_log_value(&learn_logs[lfo2?2:1],fraction);
        }
        return;
    }
    for(unsigned i=0;i<sizeof learn_logs/sizeof learn_logs[0];i++) {
        const struct learn_log *c=&learn_logs[i];
        if(c->id==id){*value=learn_log_value(c,(float)(*value-c->lo)/(float)(c->hi-c->lo));return;}
    }
}
#define LEARN_EVENT(event) learn_event(event)
#include "learn_m7.h"
