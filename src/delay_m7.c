/* Desktop 1.5.2 delay port, adapted from Bluelex dly_m7.h. See NOTICE.md. */
#include "shared.h"
#include "granular.h"
#include "shoegaze.h"
/* Granular/Shoegaze hot loops, compiled separately at -O2 (dsp_fast_m7.c).
 * Native delay wrappers and everything else in this unit stay at -Os. */
void g_render_o2(struct granular *g,const float *in_l,const float *in_r,float *out_l,float *out_r,unsigned n);
void sg_run_o2(struct shoegaze *s,float *l,float *r,unsigned n,float level);
#include "ring_clear.h"
void rc_step_o2(struct ring_clear *c,uint32_t *lines,unsigned skipping);
/* Tape Lo C state: free backup SRAM between the Granular state (0x00..0xab)
 * and the Shoegaze link at 0x100. */
#define XL_STATE ((struct x_locut *)0x388000b0u)
#include "delay_types.h"
_Static_assert(0xb0+sizeof(struct x_locut)<=0x100,"Tape Lo C state below the Shoegaze link");
/* Reverse renderer and its feedback injection, built at -O2 (dsp_fast_m7.c). */
void x_reverse_o2(struct delay_types *x,struct granular *g,float *l,float *r,unsigned n,unsigned wr0,unsigned valid0);
void x_reverse_input_o2(struct delay_types *x,struct granular *g,float *l,float *r,unsigned n);
#define XS ((struct delay_types *)0x38800200u)
_Static_assert(sizeof(struct delay_types)==496,"new FX M7 state layout");
_Static_assert(0x200+sizeof(struct delay_types)<=0xb00,"new FX state below M4 UI state");
#define GS ((struct granular *)0x38800000u)
/* Free backup-SRAM gap after the Shoegaze link (0x100..0x10f), below 0x200. */
#define RC ((struct ring_clear *)0x38800120u)
_Static_assert(0x120+sizeof(struct ring_clear)<=0x200,"ring clear state below delay types");
static struct ring_clear *RC_STATE(void *obj) {
    if(RC->magic!=RC_MAGIC || RC->owner!=obj)rc_reset(RC,obj);
    return RC;
}
_Static_assert(sizeof(struct granular)==172,"granular M7 ABI layout (exp.7 appends decor)");
_Static_assert(sizeof(struct granular)<=0xb0,"granular state below the Tape Lo C state at 0xb0");
_Static_assert(sizeof(struct granular)<0xd00,"granular state below delay state");
#define FN(a) ((a)|1u)
typedef unsigned (*process_fn)(uint8_t *,void *);
typedef void *(*bus_fn)(void *,unsigned);
typedef unsigned (*frames_fn)(void *);
typedef void *(*queue_fn)(void *,unsigned);
typedef int (*event_fn)(void *,unsigned,void *);
typedef void (*stereo_fn)(void *,float **,float **);
typedef unsigned (*port_fn)(uint8_t *);
#define fw_bus ((bus_fn)0x0805331du)
#define fw_frames ((frames_fn)0x0804cd41u)
#define fw_queue ((queue_fn)0x080532d5u)
#define fw_next_ev ((event_fn)0x0804e1b9u)
#define fw_stereo ((stereo_fn)0x0804ce19u)
struct fw_ev {uint8_t type,pad[11];uint16_t id,reserved;int32_t value;uint32_t tail;};
static inline float rv_clamp(float x, float lo, float hi) { return x < lo ? lo : x > hi ? hi : x; }
static inline float rv_exp2(float x)
{
    if (x < -126.f) return 0.f;
    if (x > 126.f) x = 126.f;
    float fl = (float)(int)x;
    if (fl > x) fl -= 1.f;
    float f = x - fl;
    float p = 1.f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013334f))));
    union { float f; uint32_t u; } v = { p };
    v.u += (uint32_t)((int)fl) << 23;
    return v.f;
}
#define fw_dly_body ((process_fn)FN(0x0805287c))
#define fw_line_read ((line_fn)FN(0x080593ec))
typedef void (*line_fn)(uint32_t *line, void *bus, int ch);

#define D_FLUTTER 0x3a           /* shown as Drift */
#define D_SEND    0x3d
#define D_PITCH   0x4a
#define D_PITCHON 0x4b
#define D_FS      48000.f        /* the stock delay is hard-wired to 48 kHz */
#define D_MAXN    32u
#define P_FADE    240.f          /* pitch: crossfade at each jump, 5 ms */
#define P_SPAN    4800.f         /* pitch: the read jumps back at least every 100 ms */

struct bus { uint32_t cap, frames; float *l, *r; uint8_t silent, stereo; };

struct dly_shared {
    uint32_t magic;
    uint8_t *obj;                /* the delay we work on */
    int32_t flutter, send, pitch, pitch_on;
    float fl_ph, fl_noise, fl_target, m0, m1;
    uint32_t rng, fl_count;
    float send_g;
    uint32_t stash_n;
    float stash[2][D_MAXN];
    float p_ratio, p_ratio2;     /* 2^(semitones/12), and squared for PING's line C; 1 = off */
    float p_ph[6];               /* per line: samples since its last jump */
};
#define DS ((struct dly_shared *)0x38800d00u)   /* reserved below the CPU meter at 0xf00 */
#define DS_MAGIC 0x44454c35u

_Static_assert(0xd00 + sizeof(struct dly_shared) <= 0xf00, "delay state below the CPU meter");

static struct dly_shared *dly_state(void)
{
    backup_enable();
    PWR_CR1 |= 1u << 8;
    struct dly_shared *d = DS;
    if (d->magic != DS_MAGIC) {
        uint32_t *w = (uint32_t *)d;
        for (unsigned i = 0; i < sizeof *d / 4; i++)
            w[i] = 0;
        d->rng = 0x1234567u;
        d->p_ratio = d->p_ratio2 = 1.f;
        d->magic = DS_MAGIC;
    }
    return d;
}

static float d_sin(float x)                  /* |x| <= pi */
{
    float x2 = x * x;
    return x * (1.f - x2 / 6.f * (1.f - x2 / 20.f * (1.f - x2 / 42.f * (1.f - x2 / 72.f * (1.f - x2 / 110.f)))));
}

static float d_rand(struct dly_shared *d)    /* -1..1 */
{
    d->rng = d->rng * 1664525u + 1013904223u;
    return (float)(int32_t)d->rng * (1.f / 2147483648.f);
}

/* Once per block, before the stock body: the drift amount and the pitch ratio for this block. */
static void dly_prepare(struct dly_shared *d, unsigned n)
{
    /* drift (id "flutter"): a slow 0.6 Hz wobble plus a slower random wander; extra delay 0 .. 2A, A up to 8 ms
     * (about +-2.5 % pitch at full) */
    float depth = rv_clamp((float)d->flutter * 1e-3f, 0.f, 1.f);
    if(XS->magic==X_MAGIC && XS->mode==3)depth=0.f;
    float fa = depth * 0.008f * D_FS;
    d->fl_ph += 0.6f * (float)n / D_FS;
    if (d->fl_ph >= 1.f) d->fl_ph -= 1.f;
    if ((d->fl_count++ & 511u) == 0) d->fl_target = d_rand(d);          /* a new wander target every ~0.34 s */
    d->fl_noise += (d->fl_target - d->fl_noise) * 0.004f;
    float s = d_sin(d->fl_ph * 6.2831853f - 3.1415927f);
    d->m0 = d->m1;
    d->m1 = fa > 0.f ? fa * (1.f + 0.6f * s + 0.4f * d->fl_noise) : 0.f;
    if (d->m0 <= 0.f && d->m1 > 0.f) d->m0 = d->m1;                   /* drift just turned on: no jump from 0 */

    int st = d->pitch_on && XS->mode!=2 ? d->pitch : 0;
    if (st < -12) st = -12;
    if (st > 12) st = 12;
    d->p_ratio = rv_exp2((float)st * (1.f / 12.f));
    d->p_ratio2 = d->p_ratio * d->p_ratio;
}

/* the line's sample m samples older than the stock read's (m < 0: newer), linear interpolation */
static float d_tap(const float *buf, uint32_t size, uint32_t pos, float m)
{
    int32_t mi = (int32_t)m;
    if ((float)mi > m) mi--;
    float fr = m - (float)mi;
    uint32_t p1 = (uint32_t)((int32_t)(pos + 2u * size) - mi) % size;
    uint32_t p0 = p1 ? p1 - 1u : size - 1u;
    return buf[p1] + (buf[p0] - buf[p1]) * fr;
}

/* Replaces bl FUN_080593ec at every line read. */
void dly_read(uint32_t *line, void *bus, int ch)
{
    uint32_t wr = line[1], rd0 = line[2], len = line[4], size = line[5];
    uint32_t dist = wr >= rd0 ? wr - rd0 : wr + size - rd0;
    fw_line_read(line, bus, ch);
    struct dly_shared *d = DS;
    if (d->magic != DS_MAGIC || !d->obj)
        return;
    if(XS->magic==X_MAGIC && XS->mode==5)return;
    const float *buf = (const float *)line[0];
    int32_t li = (int32_t)((uint8_t *)line - (d->obj + 0x30));
    if (!size || !buf || li < 0 || li > 5 * 0x1c || li % 0x1c)
        return;
    li /= 0x1c;
    unsigned n = fw_frames(bus);
    if (n > D_MAXN) n = D_MAXN;
    if (!n) return;
    /* Exact default-off path: do not request another writable stereo view. */
    int tape=XS->magic==X_MAGIC && (XS->tape0>0.f || XS->tape1>0.f);
    /* Read native flags here, after the native queue has applied PING/QUAD.
     * Split WILD over serial PING stages, keeping one effective curve per loop. */
    int split=d->obj[0x274] && (li<2 || (d->obj[0x276] && li<4));
    if (!(d->m0 > 0.f || d->m1 > 0.f || (d->pitch_on && d->pitch)) && !tape) return;
    float *l, *r;
    fw_stereo(bus, &l, &r);
    float *out = ch ? l : r;

    /* how far we may move from the stock read: back, while staying ahead of the write head going round; forward,
     * while staying behind this block's not yet written samples */
    float hi = (float)size - (float)dist - (float)n - 4.f, lo = -((float)dist - (float)n - 2.f);
    if (hi < 0.f) hi = 0.f;
    if (lo > 0.f) lo = 0.f;

    int flutter = dist == len && (d->m0 > 0.f || d->m1 > 0.f);
    uint8_t *obj = d->obj;
    float ratio = li == 2 && obj[0x274] && !obj[0x276] ? d->p_ratio2 : d->p_ratio;
    /* the read slides by (1 - ratio) per sample and jumps back every P samples: P is the delay time, at most 100 ms
     * (so a repeat arrives at most 100 ms early or late), shortened if the slide would run out of room */
    float k = 1.f - ratio, P = (float)dist < P_SPAN ? (float)dist : P_SPAN;
    if (k < -1e-4f && P * -k > -lo) P = -lo / -k;
    if (k > 1e-4f && P * k > hi) P = hi / k;
    int pitch = (k < -1e-4f || k > 1e-4f) && P >= 4.f * P_FADE;
    if (!flutter && !pitch) {
        d->p_ph[li] = 0.f;
        if(tape)x_tape(XS,out,n,(unsigned)li,split);
        return;
    }
    float m = flutter ? d->m0 : 0.f, dm = flutter ? (d->m1 - d->m0) / (float)n : 0.f;
    float ph = d->p_ph[li];
    if (!pitch) ph = 0.f;
    if (ph >= P) ph = 0.f;
    for (unsigned i = 0; i < n; i++, m += dm) {
        float a = rv_clamp(m + k * ph, lo, hi);
        float y = d_tap(buf, size, rd0 + i, a);
        if (ph > P - P_FADE) {               /* crossfade into the read after the jump */
            float g = (ph - (P - P_FADE)) * (1.f / P_FADE);
            float b = rv_clamp(m + k * (ph - P), lo, hi);
            y += (d_tap(buf, size, rd0 + i, b) - y) * g;
        }
        out[i] = y;
        if (pitch && (ph += 1.f) >= P) ph -= P;
    }
    d->p_ph[li] = ph;
    if(tape)x_tape(XS,out,n,(unsigned)li,split);
}

/* Native input gate returns a bus containing the gated input (not a gain
 * envelope). Copy it while valid, before the native delay mutates the bus.
 * Pure Delay/Granular leave capture_n zero and retain their previous path. */
unsigned dly_input_gate(void *gate,void *input,void **output,unsigned advance) {
    unsigned result=((unsigned (*)(void *,void *,void **,unsigned))0x0804df55u)(gate,input,output,advance);
    if(XS->magic==X_MAGIC && XS->capture_n && XS->capture_n<=32 &&
       gate==(uint8_t *)XS->owner+0x124 && XS->capture_l && XS->capture_r) {
        struct bus *b=(struct bus *)*output;
        unsigned n=b?fw_frames(b):0;if(n>XS->capture_n)n=XS->capture_n;
        for(unsigned i=0;i<XS->capture_n;i++) {
            int live=b && !b->silent && b->l && i<n;
            XS->capture_l[i]=live?b->l[i]:0.f;
            XS->capture_r[i]=live?(b->stereo&&b->r?b->r[i]:b->l[i]):0.f;
        }
    }
    return result;
}

/* Native body, events only. The stock body handles every queue event (params,
 * on/off, reset, tempo) and per-block parameter updates, then takes its own
 * idle exit (0x08052964) when the input gate at +0x124 reports idle and line
 * flag +0x80 is set. Present exactly that state for one call: a silent bus,
 * gate[0x14]=1 with countdown [0x18]=0, flag 1. Restore the gate's tracking
 * fields, the flag and the bus afterwards. Events never write those fields;
 * +0x80 is written only by the line writer (0x08059534) during processing. */
static void dly_body_events(uint8_t *obj,void *ctx,struct bus *b) {
    uint8_t *gate=obj+0x124;
    uint8_t g14=gate[0x14],g15=gate[0x15],flag=obj[0x80],silent=b->silent;
    uint32_t g18=*(uint32_t *)(gate+0x18);
    gate[0x14]=1;*(uint32_t *)(gate+0x18)=0;obj[0x80]=1;b->silent=1;
    fw_dly_body(obj,ctx);
    b->silent=silent;obj[0x80]=flag;
    gate[0x14]=g14;gate[0x15]=g15;*(uint32_t *)(gate+0x18)=g18;
}

/* Replaces the desktop delay process slot (0x0806b014, stock FUN_08053178). */
unsigned dly_process(uint8_t *obj, void *ctx)
{
    struct dly_shared *d = dly_state();
    if(XS->magic!=X_MAGIC || XS->owner!=obj)x_reset(XS,obj);
    if (d->obj != obj) {
        d->obj = obj;
        d->stash_n = 0;
    }
    void *q = fw_queue(ctx, *(uint32_t *)(obj + 0x18));
    struct fw_ev ev;
    int filt_event=0;
    for (unsigned i = 0; fw_next_ev(q, i, &ev); i++) {
        if (ev.type != 0x39)
            continue;
        if (ev.id == 0xca) filt_event=1;
        if(GS->magic==G_MAGIC && GS->owner==obj)g_event(GS,ev.id,ev.value);
        x_event(XS,ev.id,ev.value);
        if (ev.id == D_FLUTTER) d->flutter = ev.value;
        else if (ev.id == D_SEND) d->send = ev.value;
        else if (ev.id == D_PITCH) d->pitch = ev.value;
        else if (ev.id == D_PITCHON) d->pitch_on = ev.value;
    }
    if(XS->mode==5) {
        d->stash_n=0;d->send_g=0.f;d->m0=d->m1=0.f;
        d->p_ratio=d->p_ratio2=1.f;
        XS->capture_n=0;XS->tape0=XS->tape1=XS->duck_mix=XS->rev_mix=0.f;
        XS->tape_valid=0;XS->countdown=0;
        XS->heads[0].len=XS->heads[1].len=0;
        XS->envelope=XS->rev_lp[0]=XS->rev_lp[1]=0.f;
        for(unsigned i=0;i<32;i++)XS->previous[0][i]=XS->previous[1][i]=0.f;
        if(GS->magic==G_MAGIC && GS->owner==obj) {
            GS->fade=0.f;GS->valid=GS->wr=GS->countdown=0;
            GS->fb_l=GS->fb_r=0.f;
            for(unsigned i=0;i<4;i++)GS->grains[i].len=0;
        }
        fw_dly_body(obj,ctx);
        rc_step_o2(RC_STATE(obj),(uint32_t *)(obj+0x30),0);
        uint8_t *next=*(uint8_t **)(obj+8);
        return next?(*(process_fn **)next)[3](next,ctx):1;
    }
    port_fn port = (*(port_fn **)obj)[0x54 / 4];
    void *bus = fw_bus(ctx, port(obj));
    unsigned n = fw_frames(bus);
    if (n > D_MAXN) n = D_MAXN;
    float input[2][D_MAXN];
    int gran=GS->magic==G_MAGIC && GS->owner==obj && GS->buffer;
    struct bus *ib=(struct bus *)bus;
    if(gran || XS->mode==3 || XS->duck_mix>0.f)for(unsigned i=0;i<n;i++) {
        input[0][i]=ib->silent?0.f:ib->l[i];
        input[1][i]=ib->silent?0.f:(ib->stereo?ib->r[i]:ib->l[i]);
    }
    if(GS->magic==G_MAGIC && GS->owner==obj && (GS->mode || XS->mode==4) && !GS->buffer) {
        /* Native large arena has no recoverable out-of-memory return. The
         * constructor preflight avoids it; acknowledge fallback to the UI. */
        uint32_t msg[6]={0x39,0,*(uint32_t *)(obj+0x18),G_MODE,0,0};
        ((void (*)(void *,void *))0x0804e1e9u)((uint8_t *)ctx+0x1529c,msg);
        GS->mode=0;XS->mode=0;
    }
    int gated=XS->mode==3 || XS->mode==4 || XS->duck_mix>0.f || XS->rev_mix>0.f;
    XS->capture_n=gated?n:0;XS->capture_l=input[0];XS->capture_r=input[1];
    if(gated)for(unsigned i=0;i<n;i++)input[0][i]=input[1][i]=0.f;
    x_prepare(XS,n);
    unsigned wr0=gran?GS->wr:0,valid0=gran?GS->valid:0;
    dly_prepare(d, n);
    /* Fully faded-in Granular replaces the whole delay output, so the native
     * rings, filters and line reads are skipped. Its events still run: see
     * dly_body_events. Any fade, gated capture or other mode runs it fully. */
    int skip=gran && GS->mode && XS->mode==1 && !gated && GS->fade==1.f && ib->l;
    if(skip)dly_body_events(obj,ctx,ib);
    else if(XS->mode==2) {
        /* exp.8: Tape hides FILT/Cutoff and bypasses the native filter (its
         * enable flag at +0x270) without rewriting the saved setting. A FILT
         * event in this block is applied by the body and kept as it set it. */
        uint8_t filt=obj[0x270];obj[0x270]=0;
        fw_dly_body(obj, ctx);
        if(!filt_event)obj[0x270]=filt;
    } else fw_dly_body(obj, ctx);
    rc_step_o2(RC_STATE(obj),(uint32_t *)(obj+0x30),(unsigned)skip);
    XS->capture_n=0;
    /* +24 is the stock resolved sample period before Ping/Quad multipliers.
     * Native clock/Beat/division events have already been processed above. */
    XS->reverse_frames=obj[0x271]?*(uint32_t *)(obj+0x24):0;
    /* Native feedback writes are complete. Makeup touches the return only. */
    if(n && (XS->tape0>0.f || XS->tape1>0.f) && !((struct bus *)bus)->silent) {
        float *l,*r;fw_stereo(bus,&l,&r);x_tape_return(XS,l,r,n);
    }
    if(gran && (XS->mode==4 || XS->rev_mix!=0.f))x_reverse_input_o2(XS,GS,input[0],input[1],n);
    if(gran) {
        float *l,*r;
        if(GS->mode || GS->fade>0.f) {
            fw_stereo(bus,&l,&r);
            g_render_o2(GS,input[0],input[1],l,r,n);
        } else {
            /* g_render's Normal branch never dereferences output pointers. */
            g_render_o2(GS,input[0],input[1],0,0,n);
        }
    }

    if(n && (XS->mode==3 || XS->duck_mix>0.f || XS->mode==4 || XS->rev_mix>0.f)) {
        float *l,*r;fw_stereo(bus,&l,&r);
        if(gran)x_reverse_o2(XS,GS,l,r,n,wr0,valid0);
        x_duck(XS,input[0],input[1],l,r,n);
    }

    /* keep Send x wet for the reverb's next block */
    float g = rv_clamp((float)d->send * 1e-3f, 0.f, 1.f);
    float g0 = d->send_g;
    d->send_g = g;
    struct bus *b = (struct bus *)bus;
    d->stash_n = 0;
    if (n && (g > 0.f || g0 > 0.f) && !b->silent) {
        float *l, *r;
        n = fw_frames(bus);
        if (n > D_MAXN) n = D_MAXN;
        fw_stereo(bus, &l, &r);
        float dg = (g - g0) / (float)n, gg = g0;
        for (unsigned i = 0; i < n; i++, gg += dg) {
            d->stash[0][i] = l[i] * gg;
            d->stash[1][i] = r[i] * gg;
        }
        d->stash_n = n;
    }
    uint8_t *next = *(uint8_t **)(obj + 0x08);
    if (!next)
        return 1;
    return (*(process_fn **)next)[3](next, ctx);
}

/* Called before the stock desktop reverb (which runs before the delay): add last block's send. */
static void dly_send_into(uint8_t *rev, void *ctx)
{
    struct dly_shared *d = DS;
    backup_enable();
    if (d->magic != DS_MAGIC || !d->stash_n)
        return;
    /* Native bypass leaves the bus dry. Discard our pending send there rather
     * than leaking an extra dry delay return. Include this block's queued edits. */
    unsigned bypass=rev[0x6e]!=0;
    void *q=fw_queue(ctx,*(uint32_t *)(rev+0x18));
    struct fw_ev ev;
    for(unsigned i=0;fw_next_ev(q,i,&ev);i++)
        if(ev.type==0x39 && ev.id==0x15c) bypass=ev.value!=0;
    if(bypass) {d->stash_n=0;return;}
    port_fn port = (*(port_fn **)rev)[0x54 / 4];
    void *bus = fw_bus(ctx, port(rev));
    unsigned n = fw_frames(bus);
    if (n > d->stash_n) n = d->stash_n;
    if (n > D_MAXN) n = D_MAXN;
    if (!n) {d->stash_n=0;return;}
    float *l, *r;
    fw_stereo(bus, &l, &r);                   /* zero-fills a silent bus first */
    for (unsigned i = 0; i < n; i++) {
        l[i] += d->stash[0][i];
        r[i] += d->stash[1][i];
    }
    d->stash_n = 0;
}

#include "shoegaze_m7.h"
/* Stock styles are retained; the experimental style has independent state. */
unsigned delay_reverb_process(uint8_t *obj,void *ctx) {
    dly_send_into(obj,ctx);
    return sg_reverb_process(obj,ctx);
}

/* Native graph creates the single delay before audio starts. Reset volatile FX
 * state there so backup SRAM cannot replay a stale send on a later boot. */
unsigned dly_construct(uint8_t *obj) {
    unsigned result=((unsigned (*)(uint8_t *))0x08052479u)(obj);
    backup_enable();PWR_CR1|=1u<<8;
    DS->magic=0;
    (void)dly_state();
    /* Same boot-time SDRAM arena used by the six stock rings. It is a bump
     * allocator, with a no-op free. Check its actual cursor/limit BEFORE the
     * request: calling it beyond the limit asserts instead of returning NULL.
     * Graph rebuilding reuses the stock delay instance (08050f00). */
    uint32_t cursor=*(volatile uint32_t *)0x24000004u;
    uint32_t limit=*(volatile uint32_t *)0x24000000u;
    float *buffer=0;
    if(cursor>=0xc0000000u && cursor<=limit && G_BYTES<=limit-cursor &&
       cursor<=0xc3400000u-G_BYTES)
        buffer=((void *(*)(unsigned))0x080412a5u)(G_BYTES);
    g_reset(GS,obj,buffer);
    x_reset(XS,obj);
    rc_reset(RC,obj);
    sg_allocate();
    return result;
}
