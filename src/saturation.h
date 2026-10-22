/* Master saturation curve adapted from Bluelex mst_m7.c (see NOTICE.md).
 * Desktop 1.5.2 addresses traced independently. */
#include "tape_saturation.h"
#define TAPE_STATE ((struct master_tape *)0x38800980u)
/* Settled Tape loop, compiled separately at -O2 (tape_fast_m7.c). Soft and
 * everything else here stay at -Os, keeping Soft PCM bit-identical. */
void mt_render_settled_o2(struct master_tape *t,struct mt_block b,float *l,float *r,unsigned n,float mix,float dm,unsigned full_wet,unsigned unity_trim,unsigned drive,float trim,float off_trim_step);
_Static_assert(0x980+sizeof(struct master_tape)<=0xa00,"Tape below M4 UI diagnostics");
typedef void *(*queue_fn)(void *,unsigned);
typedef int (*event_fn)(void *,unsigned,void *);
typedef void *(*bus_fn)(void *,unsigned);
typedef unsigned (*frames_fn)(void *);
typedef void (*stereo_fn)(void *,float **,float **);
#define FW_QUEUE ((queue_fn)0x080532d5u)
#define FW_EVENT ((event_fn)0x0804e1b9u)
#define FW_BUS ((bus_fn)0x0805331du)
#define FW_FRAMES ((frames_fn)0x0804cd41u)
#define FW_STEREO ((stereo_fn)0x0804ce19u)
struct sat_event { uint8_t type,pad[11];uint16_t id,reserved;int32_t value;uint32_t tail; };
struct sat_state {uint32_t magic;int32_t drive;float g,mix;};
#define SAT_STATE ((volatile struct sat_state *)0x38800f80u)
#define SAT_MAGIC 0x44534134u
#define SAT_REF .2f
static float exp2f_(float x)        /* 0 <= x < 8 */
{
    int i = (int)x;
    float f = x - (float)i;
    union { float f; uint32_t u; } v = { 1.f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013334f)))) };
    v.u += (uint32_t)i << 23;
    return v.f;
}
static float sat(float x)
{
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}

static void sat_events(uint8_t *obj,void *ctx) {
    volatile struct sat_state *s=SAT_STATE;
    if(s->magic!=SAT_MAGIC) {s->drive=0;s->g=1.f;s->mix=0.f;s->magic=SAT_MAGIC;}
    if(TAPE_STATE->magic!=MT_MAGIC)mt_init(TAPE_STATE);
    void *q=FW_QUEUE(ctx,*(uint32_t *)(obj+0x18));
    struct sat_event ev;
    for(unsigned i=0;FW_EVENT(q,i,&ev);i++) {
        if(ev.type==0x39 && ev.id!=0x43)mt_event(TAPE_STATE,ev.id,ev.value);
        if(ev.type==0x39 && ev.id==0x43) {
            int d=ev.value;if(d<0)d=0;if(d>1000)d=1000;s->drive=d;
        }
    }
}
static void saturate_soft(uint8_t *obj,void *ctx) {
    volatile struct sat_state *s=SAT_STATE;
    /* Zero is an exact bypass, including the stock chain and its return value. */
    if(s->drive==0 && s->mix==0.f) return;
    float d=(float)s->drive*.001f,g1=exp2f_(6.f*d),mix1=d*20.f;
    if(mix1>1.f)mix1=1.f;
    float g0=s->g,mix0=s->mix;s->g=g1;s->mix=mix1;
    typedef unsigned (*port_fn)(uint8_t *);
    port_fn port=(*(port_fn **)obj)[0x54/4];
    uint8_t *b=FW_BUS(ctx,port(obj));
    if(!b || b[16])return;
    unsigned n=FW_FRAMES(b);if(n>32)n=32;if(!n)return;
    float *l,*r;FW_STEREO(b,&l,&r);
    if(g0==g1 && mix0==mix1) {
        float k=SAT_REF/sat(g1*SAT_REF);
        for(unsigned i=0;i<n;i++) {
            l[i]+=mix1*(sat(l[i]*g1)*k-l[i]);
            r[i]+=mix1*(sat(r[i]*g1)*k-r[i]);
        }
    } else {
        float inv=1.f/(float)n,dg=(g1-g0)*inv,dm=(mix1-mix0)*inv,g=g0,mix=mix0;
        for(unsigned i=0;i<n;i++) {
            g+=dg;mix+=dm;float k=SAT_REF/sat(g*SAT_REF);
            l[i]+=mix*(sat(l[i]*g)*k-l[i]);
            r[i]+=mix*(sat(r[i]*g)*k-r[i]);
        }
    }
}

static void saturate(uint8_t *obj,void *ctx) {
    volatile struct sat_state *s=SAT_STATE;struct master_tape *t=TAPE_STATE;
    if(s->drive==0 && s->mix==0.f) {
        /* Settled Off is exact dry unity, regardless of mode/trim. */
        t->blend=0.f;t->output_gain=1.f;t->drive_ctl=0.f;t->envelope=0.f;t->hf_inv=10.f;
        for(unsigned c=0;c<2;c++)for(unsigned j=0;j<8;j++)t->channel[c][j]=0.f;
        return;
    }
    /* Preserve the tested Soft path, arithmetic and normalization exactly. */
    if(t->mode==0 && t->blend==0.f && t->output==0 && t->output_gain==1.f) {
        saturate_soft(obj,ctx);return;
    }
    float d=(float)s->drive*.001f,g1=exp2f_(6.f*d),mix1=d*20.f;
    if(mix1>1.f)mix1=1.f;
    float g0=s->g,mix0=s->mix;s->g=g1;s->mix=mix1;
    typedef unsigned (*port_fn)(uint8_t *);
    port_fn port=(*(port_fn **)obj)[0x54/4];
    uint8_t *bus=FW_BUS(ctx,port(obj));if(!bus || bus[16])return;
    unsigned n=FW_FRAMES(bus);if(n>32)n=32;if(!n)return;
    float *l,*r;FW_STEREO(bus,&l,&r);if(!l || !r)return;
    unsigned tape=t->mode==1 || t->blend>0.f;
    struct mt_block b=mt_prepare(t,d,n);
    float dg=(g1-g0)/(float)n,dm=(mix1-mix0)/(float)n,g=g0,mix=mix0;
    float k=SAT_REF/sat(g1*SAT_REF);
    float trim=s->drive?mt_exp2((float)t->output/6020.599913f):1.f;
    float off_trim_step=(1.f-t->output_gain)/(float)n;
    /* A one-sided float fade stalls just short of unity. Settle its final
     * 40 ppm explicitly, then avoid the inaudible Soft reference completely.
     * Tape drive/tone/envelope/filter/ADAA processing remains at full rate. */
    if(t->mode==1 && t->blend==1.f) {
        unsigned full_wet=mix0==1.f && mix1==1.f;
        unsigned unity_trim=trim==1.f && t->output_gain==1.f;
        mt_render_settled_o2(t,b,l,r,n,mix,dm,full_wet,unity_trim,s->drive!=0,trim,off_trim_step);
        if(!s->drive) {
            t->blend=0.f;t->output_gain=1.f;t->drive_ctl=0.f;t->envelope=0.f;t->hf_inv=10.f;
            for(unsigned c=0;c<2;c++)for(unsigned j=0;j<8;j++)t->channel[c][j]=0.f;
        }
        return;
    }
    for(unsigned i=0;i<n;i++) {
        g+=dg;mix+=dm;
        if(dg!=0.f)k=SAT_REF/sat(g*SAT_REF);
        float dry_l=l[i],dry_r=r[i];
        if(t->mode==1) {
            if(!(dry_l>=-16.f && dry_l<=16.f))dry_l=0.f;
            if(!(dry_r>=-16.f && dry_r<=16.f))dry_r=0.f;
        }
        float sl=dry_l+mix*(sat(dry_l*g)*k-dry_l);
        float sr=dry_r+mix*(sat(dry_r*g)*k-dry_r);
        if(tape) {
            float hf=mt_hf_gain(t,b,dry_l,dry_r);
            float tl=dry_l+mix*(mt_channel(t,b,0,dry_l,hf)-dry_l);
            float tr=dry_r+mix*(mt_channel(t,b,1,dry_r,hf)-dry_r);
            float target=t->mode==1?1.f:0.f;
            t->blend+=(target-t->blend)*(1.f/960.f);
            if(t->mode && t->blend>=1.f-.00004f)t->blend=1.f;
            if(t->blend<.000001f && !t->mode)t->blend=0.f;
            sl+=t->blend*(tl-sl);sr+=t->blend*(tr-sr);
        }
        t->output_gain+=s->drive?(trim-t->output_gain)*(1.f/960.f):off_trim_step;
        if(mt_abs(trim-t->output_gain)<.000001f)t->output_gain=trim;
        l[i]=t->output_gain==1.f?sl:sl*t->output_gain;
        if(r!=l)r[i]=t->output_gain==1.f?sr:sr*t->output_gain;
    }
    if(!s->drive) {
        t->blend=0.f;t->output_gain=1.f;t->drive_ctl=0.f;t->envelope=0.f;t->hf_inv=10.f;
        for(unsigned c=0;c<2;c++)for(unsigned j=0;j<8;j++)t->channel[c][j]=0.f;
    }
}
