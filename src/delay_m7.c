/* Desktop 1.5.2 delay port, adapted from Bluelex dly_m7.h. See NOTICE.md. */
#include "shared.h"
#include "granular.h"
#include "shoegaze.h"
/* Granular/Shoegaze hot loops, compiled separately at -O2 (dsp_fast_m7.c).
 * Native delay wrappers and everything else in this unit stay at -Os. */
void g_render_o2(struct granular *g,const float *in_l,const float *in_r,float *out_l,float *out_r,unsigned n);
void sg_run_o2(struct shoegaze *s,float *l,float *r,unsigned n,float level);
#include "ring_clear.h"
#include "spectral_freeze.h"
#include "spiral_m7.h"
void sf_process_o2(struct spectral_freeze *s,float *l,float *r,unsigned n);
void sf_init_o2(struct spectral_freeze *s,struct sf_fast *fast);
void sf_blur_process_o2(struct spectral_freeze *s,float *l,float *r,unsigned n);
/* FX1 Freeze state link: M7-only backup-SRAM line 0xe80 (free in this build). */
#define FRZ_LINK ((volatile struct frz_link *)0x38800e80u)
_Static_assert(sizeof(struct spectral_freeze)==139592,"Freeze state size (tests/test_granular.py arena accounting)");
/* Freeze bookkeeping (free backup SRAM 0xea0..0xebf, M7 only): [3] how many
 * times the state was set up again, [4] blocks before it may be again,
 * [5] the fast block (DTCM) or 0, [6] the Freeze toggle last block,
 * [7] unused (was the FRZ22-23 diagnostic). */
#define FRZ_AUX ((volatile uint32_t *)0x38800ea0u)
/* The controls, kept outside the state (whose memory the stock firmware
 * clears once after boot): restored when the state is set up again. Free
 * backup SRAM 0xec0..0xedf, M7 only. */
#define FRZ_CTLS 13u
struct frz_ctl {uint32_t magic;int16_t v[FRZ_CTLS];};   /* Freeze: freeze, fade, drift, tone, level, time, tmode; Blur: blur, drift, tone, level; G30 Spiral: shift, spread */
#define FRZ_CTL ((volatile struct frz_ctl *)0x38800ec0u)
#define FRZ_CTL_MAGIC 0x4652433bu
_Static_assert(sizeof(struct frz_ctl)<=0x20,"Freeze controls in one line below the CPU meter");
static const uint16_t frz_ids[FRZ_CTLS]={F_FREEZE,F_FADE,F_DRIFT,F_TONE,F_LEVEL,F_TIME,F_TMODE,B_BLUR,B_DRIFT,B_TONE,B_LEVEL,S_SHIFT,S_SPREAD};
static void frz_record(unsigned id,int value) {
    volatile struct frz_ctl *c=FRZ_CTL;
    if(c->magic!=FRZ_CTL_MAGIC) {
        static const int16_t def[FRZ_CTLS]={0,300,150,750,500,3,0,500,0,750,475,832,0};   /* G30: Time 1/8 (G26 default), Tone 750 (G35), Flux Level 475 (G34), Shift 832 (G33: +77 Hz), Spread off */
        for(unsigned i=0;i<FRZ_CTLS;i++)c->v[i]=def[i];
        c->magic=FRZ_CTL_MAGIC;
    }
    for(unsigned i=0;i<FRZ_CTLS;i++)if(frz_ids[i]==id)c->v[i]=(int16_t)value;
}
/* Set the state up (boot, or again after it was found cleared), with the
 * fast block if there is one and the controls last received. */
static void frz_setup(struct spectral_freeze *s) {
    sf_init_o2(s,(struct sf_fast *)FRZ_AUX[5]);
    if(FRZ_CTL->magic==FRZ_CTL_MAGIC) {
        for(unsigned i=0;i<FRZ_CTLS;i++)sf_event(s,frz_ids[i],FRZ_CTL->v[i]);
        s->last_freeze=(float)s->freeze;   /* a Freeze left On needs a new press */
    }
}
/* Timed Freeze from the transport (as the Out 2 clock reads it): ctx[0] is
 * the transport, +0x30 its tick at the block start (960 per quarter note),
 * +0x38 nonzero while playing, 0x08058910 turns a tick into a sample offset
 * from the block start. With Time set it runs on its own (auto_on), the
 * Freeze toggle not needed: on the transport's beats while it plays, at the
 * project tempo from its own clock while stopped. Time Off: the toggle works
 * by hand. Step: a capture before every step boundary, so the
 * new snapshot takes over on the beat. Gate: the hold sounds on even steps
 * and fades out on odd ones; its capture runs during the closed step. */
/* G17: 1/64 (60 ticks) is shorter than a capture plus a hop (FRZ_LEAD, about
 * 64 ms): there the captures run back to back and each new snapshot takes
 * over at the next boundary it reaches, every second step or so. Gate still
 * opens and closes on every 1/64. */
static const uint16_t frz_ticks[F_TIMES]={0,60,120,240,480,960,1920,3840};
#define FRZ_LEAD (SF_CAPTURE_SAMPLES+SF_H+128u)   /* capture (~1.2k samples) + one hop for the new frame */
static unsigned frz_mod(int64_t pos,unsigned step) {
    uint64_t a=pos<0?(uint64_t)-pos:(uint64_t)pos;
    unsigned hi=(unsigned)(a>>32)%step,lo=(unsigned)a%step;
    unsigned r=(hi*((0xffffffffu%step+1u)%step)+lo)%step;
    return pos<0 && r?step-r:r;
}
static int frz_offset(uint8_t *tr,int64_t t){return ((int (*)(void *,const int64_t *))0x08058911u)(tr,&t);}
static void frz_clock_timed(struct spectral_freeze *f,void *ctx,unsigned n) {
    f->gate_closed=0;f->auto_on=0;
    if(!f->time || f->time>=(int)F_TIMES)return;
    uint32_t t0=*(uint32_t *)ctx;
    if(!((t0>=0x20000000u && t0<0x2001ffc0u) || (t0>=0x24000000u && t0<0x2407ffc0u) || (t0>=0x30000000u && t0<0x30047fc0u)))return;
    uint8_t *tr=(uint8_t *)t0;
    f->auto_on=1;                        /* Time set: runs on its own, no press needed */
    int64_t pos=*(const int64_t *)(tr+0x30);
    unsigned step=frz_ticks[f->time];
    if(tr[0x38]) {
        /* playing: on the transport's own beats */
        unsigned r=frz_mod(pos,step);
        if(f->tmode)f->gate_closed=frz_mod(pos,2u*step)>=step;
        int64_t t=pos+(r?(int64_t)(step-r):0);
        if(f->tmode && frz_mod(t,2u*step))t+=step;
        int off=frz_offset(tr,t);
        if(off<=(int)FRZ_LEAD && (uint32_t)t!=f->trig_tick){f->trigger=1;f->trig_tick=(uint32_t)t;}
        f->fpos=0;f->fk=0;f->ftrig=0;
        return;
    }
    /* stopped: the same steps at the project tempo, from a free-running
     * clock (samples per quarter from the tempo, as the stock conversion
     * gives it while stopped too); it starts on an open step */
    int spq=frz_offset(tr,pos+960)-frz_offset(tr,pos);
    if(spq<9600 || spq>144000)spq=24000;         /* 20..300 BPM, else 120 */
    uint32_t len=(uint32_t)spq*step/960u,unit=f->tmode?2u*len:len;
    while(f->fpos>=unit){f->fpos-=unit;f->fk++;}
    if(f->tmode)f->gate_closed=f->fpos>=len;
    uint32_t to=unit-f->fpos;                    /* samples to the next (opening) boundary */
    if(to<=FRZ_LEAD && f->ftrig!=f->fk+1u){f->trigger=1;f->ftrig=f->fk+1u;}
    f->fpos+=n;
}
/* G17: the Freeze toggle works as on the delays, whatever Time is: a press
 * captures at once and holds, Step / Gate are paused while it is on, and
 * letting go returns to the timed Freeze (or to silence with Time Off).
 * FRZ_AUX[6]: the toggle as seen last block (a press is its rising edge). */
static void frz_clock(struct spectral_freeze *f,void *ctx,unsigned n) {
    unsigned man=f->freeze!=0,was=FRZ_AUX[6]!=0u,trig=f->trigger;
    FRZ_AUX[6]=man;
    frz_clock_timed(f,ctx,n);      /* keeps the step clock running underneath */
    if(man){f->gate_closed=0;f->auto_on=0;f->trigger=trig|(was?0u:2u);}   /* G27: 2 = a press, restarts a running capture */
}
/* The allocation: the state plus at least 4 bytes for the tail canary;
 * G30: then Spiral's state (the modes never run together). */
#define FRZ_SPI_OFF ((sizeof(struct spectral_freeze)+4u+31u)&~31u)
#define FRZ_BYTES (FRZ_SPI_OFF+sizeof(struct spiral_state))
static struct spectral_freeze *frz_state(void) {
    struct spectral_freeze *s=FRZ_LINK->magic==FRZ_LINK_MAGIC?FRZ_LINK->state:0;
    return s && s->magic==SF_MAGIC?s:0;
}
void rc_step_o2(struct ring_clear *c,uint32_t *lines,unsigned skipping);
/* Tape Lo C state: free backup SRAM between the Granular state (0x00..0xab)
 * and the Shoegaze link at 0x100. */
#define XL_STATE ((struct x_locut *)0x388000b0u)
#include "delay_types.h"
_Static_assert(0xb0+sizeof(struct x_locut)<=0x100,"Tape Lo C state below the Shoegaze link");
/* G30 Spiral: its state after the Freeze state (any mode may use the link). */
static struct spiral_state *spi_state(void) {
    uint8_t *s=FRZ_LINK->magic==FRZ_LINK_MAGIC?(uint8_t *)FRZ_LINK->state:0;
    return s?(struct spiral_state *)(s+FRZ_SPI_OFF):0;
}
static int spi_knob(void){return FRZ_CTL->magic==FRZ_CTL_MAGIC?FRZ_CTL->v[11]:832;}
/* G31: Spiral has no PING / QUAD. Their native handlers are inline in the
 * delay body and also set the line lengths, so Spiral posts PING 0 / QUAD 0
 * into this block's own queue (the stock post at 0x0804e1e8, as the UI's
 * events arrive) and, on leaving, the values last received: the saved
 * settings are never touched. FRZ_LINK pad[2]: bit 8 known, bit 0 PING,
 * bit 1 QUAD. */
static void spi_keep(unsigned id,int value) {
    uint32_t k=FRZ_LINK->pad[2];
    if(!(k&0x100u))k=0x100u;
    uint32_t bit=id==0x36?1u:2u;
    FRZ_LINK->pad[2]=value?k|bit:k&~bit;
}
static void spi_ping_quad(uint8_t *obj,void *q,int spiral) {
    uint32_t k=FRZ_LINK->pad[2];
    unsigned ping=spiral?0u:k&1u,quad=spiral?0u:(k>>1)&1u;
    if(!spiral && !(k&0x100u))return;
    if(obj[0x274]==ping && obj[0x276]==quad)return;
    if(spiral && !(k&0x100u))FRZ_LINK->pad[2]=0x100u|(obj[0x274]?1u:0u)|(obj[0x276]?2u:0u);   /* never received: keep what it was */
    uint32_t a[6]={0x39,0,*(uint32_t *)(obj+0x18),0x36,ping,0},b[6]={0x39,0,*(uint32_t *)(obj+0x18),0x37,quad,0};
    ((void (*)(void *,void *))0x0804e1e9u)(q,a);
    ((void (*)(void *,void *))0x0804e1e9u)(q,b);
}
static float spi_sign(int ch){return !ch && FRZ_CTL->magic==FRZ_CTL_MAGIC && FRZ_CTL->v[12]?-1.f:1.f;}   /* SPRD: the right channel (ch 0) the other way */
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
    uint32_t live;               /* G30: this block's line reads need work (drift, pitch, Tape, Spiral) */
};
#define DS ((struct dly_shared *)0x38800d00u)   /* reserved below the CPU meter at 0xf00 */
#define DS_MAGIC 0x44454c35u

_Static_assert(0xd00 + sizeof(struct dly_shared) <= 0xe60, "delay state below the Freeze link line");

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

    int st = d->pitch_on && XS->mode!=2 && XS->mode!=FX1_SPIRAL_MODE ? d->pitch : 0;
    if (st < -12) st = -12;
    if (st > 12) st = 12;
    d->p_ratio = rv_exp2((float)st * (1.f / 12.f));
    d->p_ratio2 = d->p_ratio * d->p_ratio;
    /* G30: one flag for dly_read's early return (x_prepare ran first) */
    d->live = d->m0 > 0.f || d->m1 > 0.f || (d->pitch_on && d->pitch) ||
              (XS->magic==X_MAGIC && (XS->tape0>0.f || XS->tape1>0.f || XS->mode==FX1_SPIRAL_MODE));
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
    if(!d->live)return;   /* G30: nothing to do on this block's reads */
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
    struct spiral_state *spi=XS->mode==FX1_SPIRAL_MODE?spi_state():0;   /* G30 */
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
        if(spi)spi_line(spi,out,n,(unsigned)li,spi_sign(ch));
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
    if(spi)spi_line(spi,out,n,(unsigned)li,spi_sign(ch));
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
/* Stasis / Blur for one block, kept out of line (the delay modes' code is
 * unchanged). */
static void __attribute__((noinline)) frz_run(struct spectral_freeze *frz,void *ctx,float *l,float *r,unsigned n) {
    if(XS->mode==FX1_BLUR_MODE)sf_blur_process_o2(frz,l,r,n);
    else{frz_clock(frz,ctx,n);sf_process_o2(frz,l,r,n);}
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
        if(ev.id==S_SHIFT || ev.id==S_SPREAD)frz_record(ev.id,ev.value);   /* G30 Spiral */
        if(ev.id==0x36 || ev.id==0x37)spi_keep(ev.id,ev.value);            /* G31: the saved PING / QUAD */
        if(F_IS_FREEZE(ev.id)){frz_record(ev.id,ev.value);struct spectral_freeze *f=frz_state();if(f)sf_event(f,ev.id,ev.value);}
        if (ev.id == D_FLUTTER) d->flutter = ev.value;
        else if (ev.id == D_SEND) d->send = ev.value;
        else if (ev.id == D_PITCH) d->pitch = ev.value;
        else if (ev.id == D_PITCHON) d->pitch_on = ev.value;
    }
    /* Leaving Freeze mode stops it; pad[0] marks a Freeze that ran, so other
     * modes pay one flag read per block. */
    if(!FX1_SPECTRAL(XS->mode) && FRZ_LINK->pad[0]){struct spectral_freeze *f=frz_state();if(f)sf_stop(f);FRZ_LINK->pad[0]=0;}
    /* G30: pad[1] marks a Spiral that ran; leaving it clears its lines */
    if(XS->mode!=FX1_SPIRAL_MODE && FRZ_LINK->pad[1]){struct spiral_state *sp=spi_state();if(sp)spi_prepare(sp,0,0,0);spi_ping_quad(obj,q,0);FRZ_LINK->pad[1]=0;}
    if(XS->mode==FX1_SPIRAL_MODE)spi_ping_quad(obj,q,1);
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
    /* G14: Freeze and Blur never read Granular's history, so stop priming it
     * (and copying its input) once Granular and Reverse have faded out:
     * about 2.7k instructions a block. Emptied, so a later Granular builds up
     * from fresh input and never plays audio from before. Its grains are
     * already idle here. The mode is tested first: other modes pay one
     * compare (Delay Normal's budget). G32: Spiral too (the plain delay
     * lines, no grains). */
    if(__builtin_expect(FX1_SPEC_PAGE(XS->mode),0) && gran && !GS->mode && !(GS->fade>0.f) && XS->rev_mix==0.f){GS->valid=0;gran=0;}
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
    if(XS->mode==FX1_SPIRAL_MODE) {
        /* G30 Spiral: this block's rotation; pad[1] marks it ran */
        struct spiral_state *sp=spi_state();
        if(sp){spi_prepare(sp,1,spi_knob(),n);FRZ_LINK->pad[1]=1;}
    }
    unsigned wr0=gran?GS->wr:0,valid0=gran?GS->valid:0;
    dly_prepare(d, n);
    /* Fully faded-in Granular replaces the whole delay output, so the native
     * rings, filters and line reads are skipped. Its events still run: see
     * dly_body_events. Any fade, gated capture or other mode runs it fully. */
    int skip=gran && GS->mode && XS->mode==1 && !gated && GS->fade==1.f && ib->l;
    /* Freeze (mode 6) and Blur (7) replace the whole delay output like a
     * fully faded-in Granular: native events only, rings cleared in the
     * background. They share one state. */
    struct spectral_freeze *frz=FX1_SPECTRAL(XS->mode)?frz_state():0;
    /* G8: on device the state's memory is found cleared some time after boot
     * (marker and tail canary zero, cursor never rewound). With the link
     * intact, set the state up again; count it, at most every 750 blocks. */
    if(FX1_SPECTRAL(XS->mode)) {
        if(FRZ_AUX[4])FRZ_AUX[4]--;
        struct spectral_freeze *st=FRZ_LINK->state;
        if(!frz && !FRZ_AUX[4] && FRZ_LINK->magic==FRZ_LINK_MAGIC && (uint32_t)st>=0xc0000000u && (uint32_t)st<0xc3400000u) {
            frz_setup(st);
            FRZ_AUX[3]++;FRZ_AUX[4]=750u;
            frz=frz_state();
        }
        if(frz)sf_engine(frz,(unsigned)XS->mode);
    }
    if(frz && ib->l)skip=1;
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
    if(frz && ib->l) {
        /* The send is still on the bus (the native body did not run). */
        float *l,*r;fw_stereo(bus,&l,&r);
        if(ib->silent){for(unsigned i=0;i<n;i++)l[i]=r[i]=0.f;ib->silent=0;}
        frz_run(frz,ctx,l,r,n);
        FRZ_LINK->pad[0]=1;
    } else if(FX1_SPECTRAL(XS->mode) && n && ib->l && !ib->silent) {
        /* No Freeze state (arena full): silent rather than the stock delay. */
        float *l,*r;fw_stereo(bus,&l,&r);
        for(unsigned i=0;i<n;i++)l[i]=r[i]=0.f;
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
#include "insert_freqshift.h"
/* F3: the twelve tracks' frequency shifter states, from the same boot-time
 * arena (bump allocator; asserts past its limit, so check first). The link
 * is rewritten every boot; without room the shifter types play dry. */
static void fsh_allocate(void) {
    FSH_LINK->magic=0;FSH_LINK->bank=0;FSH_LINK->tracks=0;FSH_LINK->spare=0;
    uint32_t bytes=(FSH_BANK_BYTES+31u)&~31u;
    uint32_t cursor=*(volatile uint32_t *)0x24000004u;
    uint32_t limit=*(volatile uint32_t *)0x24000000u;
    if(cursor>=0xc0000000u && cursor<=limit && bytes<=limit-cursor && cursor<=0xc3400000u-bytes) {
        struct fsh_state *bank=((void *(*)(unsigned))0x080412a5u)(bytes);
        if(bank) {
            for(unsigned ch=0;ch<FSH_TRACKS;ch++)fsh_init(bank+ch,500,0,0);
            FSH_LINK->bank=bank;FSH_LINK->tracks=FSH_TRACKS;FSH_LINK->magic=FSH_LINK_MAGIC;
        }
    }
}
/* FX1 Freeze: one spectral freeze instance (about 205 KB) from the same
 * boot-time arena, after the other blocks. Without it Freeze plays silent. */
static void frz_allocate(void) {
    FRZ_LINK->magic=0;FRZ_LINK->state=0;
    FRZ_AUX[3]=FRZ_AUX[4]=FRZ_AUX[5]=FRZ_AUX[6]=0;
    /* The hold's FFT buffer and tables (36 KB) in DTCM when the stock pool
     * there (0x20000000 + used, counter 0x240006c0, limit 0x1f800) keeps at
     * least 16 KB for later stock requests; else they stay in SDRAM. */
    {
        uint32_t used=*(volatile uint32_t *)0x240006c0u,need=(sizeof(struct sf_fast)+15u)&~15u;
        if(used<0x1f800u && need+0x4000u<=0x1f800u-used) {
            FRZ_AUX[5]=0x20000000u+used;*(volatile uint32_t *)0x240006c0u=used+need;
        }
    }
    uint32_t bytes=FRZ_BYTES;
    uint32_t cursor=*(volatile uint32_t *)0x24000004u;
    uint32_t limit=*(volatile uint32_t *)0x24000000u;
    if(cursor>=0xc0000000u && cursor<=limit && bytes<=limit-cursor && cursor<=0xc3400000u-bytes) {
        struct spectral_freeze *s=((void *(*)(unsigned))0x080412a5u)(bytes);
        if(s) {
            frz_setup(s);
            FRZ_LINK->state=s;FRZ_LINK->magic=FRZ_LINK_MAGIC;
        }
    }
}
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
    fsh_allocate();
    frz_allocate();
    return result;
}
