#include "shared.h"
#include "noise_gate.h"
#include "gate_diag_m7.h"
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
    for(unsigned i=0;i<12;i++)if(b->track[i].owner==obj)ng_reset(&b->track[i],obj);
    return result;
}
unsigned gate_process(uint8_t *obj,void *ctx) {
    unsigned ch=*(uint32_t *)(obj+0x18);
    /* The same native EQ class is also used on Main. Never gate that node. */
    if(ch>=12)return ng_native(obj,ctx);
    struct noise_gate *g=&ng_bank()->track[ch];if(g->owner!=obj)ng_reset(g,obj);
    void *q=((void *(*)(void *,unsigned))0x080532d5u)(ctx,ch);
    struct ng_event ev;
    unsigned received=0;
    for(unsigned i=0;((int (*)(void *,unsigned,void *))0x0804e1b9u)(q,i,&ev);i++)
        if(ev.type==0x39 && ev.target==ch && ev.id==NG_ID){ng_event(g,ev.value);received++;}
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
