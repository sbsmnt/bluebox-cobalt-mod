/* Exploratory insert: three bounded waveshapers (CLIP, FOLD, WRAP) with a
 * one-pole tone filter, plus the resonant filter types of insert_filter.h.
 * Native mono/stereo blocks at 48 kHz. No allocation, transcendental library
 * calls, oversampling or fast-math in the sample loop. */
#ifndef COBALT_INSERT_DRIVE_H
#define COBALT_INSERT_DRIVE_H
#include <stdint.h>
#include "insert_params.h"
#include "insert_filter.h"
#include "insert_freqshift.h"
#include "insert_resonator.h"
#define INS_MAGIC 0x494e5331u
/* Modes: 0 Off, 1 CLIP, 2 legacy SIN (loads as CLIP), 3 FOLD, 4 WRAP,
 * 5..8 filter LP, BP, HP, DJ, 9..11 frequency shifter SHFT, SPRD, RING,
 * 12..14 resonator bank HARM, CHRD, BELL (9..14: the SPEC group),
 * 15 SAT (DRV group): the per-track saturation of pull request #7 by
 * Alex Bonhomme, Soft curve morphing to an overdriven hard clip.
 * In filter modes Drive is Cutoff, Tone is Resonance and Mix is bipolar Env
 * depth (500 = off). In shifter modes Drive is Shift (500 = 0 Hz), Tone is
 * Feedback and Mix is dry/wet. In resonator modes Drive is Pitch, Tone is
 * Decay and Mix is dry/wet. Shifter and resonator state: the track's slot
 * of the SDRAM bank (fsh_bank_state). */
#define INS_FILTER_FIRST 5
#define INS_FILTER_LAST 8
#define INS_SHIFT_FIRST 9
#define INS_SHIFT_LAST 11
#define INS_RESO_FIRST 12
#define INS_MODE_LAST 14   /* last SPEC type */
#define INS_SAT 15         /* SAT drive type (pull request #7) */
#define INS_IS_FILTER(m) ((m)>=INS_FILTER_FIRST && (m)<=INS_FILTER_LAST)
#define INS_IS_SHIFT(m) ((m)>=INS_SHIFT_FIRST && (m)<=INS_SHIFT_LAST)
#define INS_IS_RESO(m) ((m)>=INS_RESO_FIRST && (m)<=INS_MODE_LAST)
#define INS_IS_BANK(m) ((m)>=INS_SHIFT_FIRST && (m)<=INS_MODE_LAST)
#define INS_IS_FAMILY(m) ((m)>=INS_FILTER_FIRST && (m)<=INS_MODE_LAST)
struct insert_drive {
    uint32_t magic;
    int32_t mode,drive,tone,mix,output;
    /* The drive's private words; the filter's state overlays them (the drive
     * layout is unchanged, so its state stays byte-identical). */
    union {
        struct {
            float gain,blend,enable,level,alpha,norm;
            float lp[2],dc[2];
            int32_t running_mode;
            uint32_t dirty; /* bit0 coefficients stale; bit1 settled; bits2+ block size */
            float target_gain,target_alpha,target_level,target_norm;
        };
        struct flt_state f;
    };
    /* S8a: the chosen type and the On/Off switch. mode above is what runs:
     * the chosen type while On, 0 (fade to dry) while Off. */
    int32_t selected;
    uint8_t on,chord;        /* F6: chord in the spare bytes of the old int32 on */
    uint16_t quiet;          /* SD4: SPEC blocks in a row with silent input and decayed state */
    /* S8f: WRAP anti-aliasing history per channel: previous shaper input
     * and its wrap. Outside the union: the filter never uses them. */
    float wx[2],ww[2];
    /* The frequency shifter's state is not here: see fsh_bank_state. */
};
_Static_assert(__builtin_offsetof(struct insert_drive,enable)==__builtin_offsetof(struct insert_drive,f.reserved_enable),"shared fade word");
_Static_assert(__builtin_offsetof(struct insert_drive,running_mode)==__builtin_offsetof(struct insert_drive,f.reserved_running),"shared running mode");
_Static_assert(__builtin_offsetof(struct insert_drive,dirty)==__builtin_offsetof(struct insert_drive,f.reserved_dirty),"shared dirty flag");
_Static_assert(__builtin_offsetof(struct insert_drive,selected)==88,"drive/filter state keeps its 88-byte layout");
_Static_assert(sizeof(struct insert_drive)==112,"insert sidecar ABI (F3: back to S28's, the shifter state is in SDRAM)");
_Static_assert(INS_STATE_OFFSET+sizeof(struct insert_drive)==INS_OBJECT_BYTES,"sized native allocation");
static inline int ins_clamp(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static inline float ins_exp2(float x) {
    int e=(int)x;if((float)e>x)e--;
    float f=x-(float)e;
    float p=1.f+f*(.69314718f+f*(.24022651f+f*(.05550411f+f*(.00961813f+f*.00133336f))));
    union{float f;uint32_t u;} v={p};v.u+=(uint32_t)e<<23;return v.f;
}
static inline void ins_reset(struct insert_drive *s) {
    s->magic=INS_MAGIC;s->mode=0;s->drive=350;s->tone=650;s->mix=1000;s->output=0;
    s->gain=s->level=s->alpha=s->norm=1.f;s->blend=1.f;s->enable=0.f;
    s->lp[0]=s->lp[1]=s->dc[0]=s->dc[1]=0.f;
    s->running_mode=0;
    s->dirty=1;
    s->target_gain=s->target_alpha=s->target_level=s->target_norm=1.f;
    s->wx[0]=s->wx[1]=s->ww[0]=s->ww[1]=0.f;
    s->selected=0;s->on=1;s->chord=0;s->quiet=0;
}
static inline void ins_select(struct insert_drive *s) {
    int value=s->on?s->selected:0;
    if(value!=s->mode){s->mode=value;s->dirty=1;}
}
static inline void ins_event(struct insert_drive *s,unsigned id,int value) {
    switch(id) {
    case INS_MODE:s->selected=value==2?1:value>=0 && value<=INS_SAT?value:0;ins_select(s);break;
    case INS_ON:s->on=value!=0;ins_select(s);break;
    case INS_CHORD:s->chord=(uint8_t)(value>=0 && value<(int)INS_CHORD_COUNT?value:0);break;
    case INS_DRIVE:value=ins_clamp(value,0,1000);if(value!=s->drive){s->drive=value;s->dirty=1;}break;
    case INS_TONE:value=ins_clamp(value,0,1000);if(value!=s->tone){s->tone=value;s->dirty=1;}break;
    case INS_MIX:value=ins_clamp(value,0,1000);if(value!=s->mix){s->mix=value;s->dirty=1;}break;
    case INS_OUTPUT:value=ins_clamp(value,-18000,6000);if(value!=s->output){s->output=value;s->dirty=1;}break;
    default:break;
    }
}
static inline float ins_curve(float x) {
    if(x>=1.f)return 1.f;
    if(x<=-1.f)return -1.f;
    return x*(1.5f-.5f*x*x);
}
/* Unit-threshold triangle, period four. Bound range reduction before the
 * float->integer conversion: overloads beyond useful float phase precision
 * return zero, never overflow an integer or poison a filter. */
static inline float ins_fold(float x) {
    float ax=__builtin_fabsf(x);
    if(ax<=1.f)return x;
    if(ax<=3.f)return x>0.f?2.f-x:2.f-(x+4.f);
    if(!(ax<=65536.f))return 0.f;
    float phase=x*.25f+.25f;int turn=(int)phase;
    if((float)turn>phase)turn--;
    x-=4.f*(float)turn;
    return x>1.f?2.f-x:x;
}
static inline float ins_wrap(float x) {
    float ax=__builtin_fabsf(x);
    if(ax<1.f)return x;
    if(x>=1.f && x<3.f)return x-2.f;
    if(x>=-3.f && x< -1.f)return x+2.f;
    if(!(ax<=65536.f))return 0.f;
    float phase=x*.5f+.5f;int turn=(int)phase;
    if((float)turn>phase)turn--;
    return x-2.f*(float)turn;
}
/* S8f: first-order antiderivative anti-aliasing for WRAP. The output is
 * the mean of wrap() over the input's path from the previous sample, so a
 * jump from +1 to -1 is spread over the sample instead of aliasing back as
 * noise. wrap's antiderivative is wrap(x)^2/2, so
 *   y = (w^2-p^2)/(2 dx) = (w+p)/2 * (w-p)/dx.
 * Between jumps w-p == dx and y is simply (w+p)/2: no division. The mean of
 * a function bounded by 1 is bounded by 1; the clamp guards rounding when
 * dx is tiny across a jump. */
static inline float ins_wrap_aa(float x,float *px,float *pw) {
    float w=ins_wrap(x),p=*pw,dx=x-*px,sum=w+p,d=w-p,e=d-dx;
    *px=x;*pw=w;
    if(e<.001f && e>-.001f)return .5f*sum;
    float y=.5f*sum*d/dx;
    return y>1.f?1.f:y<-1.f?-1.f:y;
}
/* SAT (pull request #7): the master Soft curve (adapted from BlueLex, as in
 * saturation.h) morphing with Curve m (Tone, 0..1) into a hard clip fed up
 * to 4x the driven signal, so the top of Curve clips much harder. */
static inline float ins_soft(float x) {
    if(x>3.f)return 1.f;
    if(x<-3.f)return -1.f;
    float x2=x*x;
    return x*(27.f+x2)/(27.f+9.f*x2);
}
static inline float ins_sat(float y,float m) {
    float s=ins_soft(y),c=y*(1.f+3.f*m);
    c=c>1.f?1.f:c<-1.f?-1.f:c;
    return s+m*(c-s);
}
static inline float ins_shape(unsigned mode,float x,float m) {
    switch(mode) {
    case 3:return ins_fold(x);
    case 4:return ins_wrap(x);
    case INS_SAT:return ins_sat(x,m);
    default:(void)m;return ins_curve(x); /* mode 1 retains I1's cubic soft clip */
    }
}
static inline float ins_finite(float x){return x>=-1e12f && x<=1e12f?x:0.f;}
static inline float ins_toward(float value,float target,float fraction) {
    float x=value+(target-value)*fraction,d=x-target;
    return d<.000001f && d>-.000001f?target:x;
}
static inline int ins_active(const struct insert_drive *s) {
    if(INS_IS_FAMILY(s->mode) || INS_IS_FAMILY(s->running_mode))return 1;
    return ((s->mode>0 && s->mode<=4) || s->mode==INS_SAT) && s->drive>0 && s->mix>0 ? 1 : s->enable>0.f;
}
static inline void ins_quiet(struct insert_drive *s) {
    if(INS_IS_FILTER(s->running_mode)){flt_quiet(&s->f);return;}
    if(INS_IS_BANK(s->running_mode))return; /* its state is the caller's bank slot */
    s->lp[0]=s->lp[1]=s->dc[0]=s->dc[1]=0.f;
    s->wx[0]=s->wx[1]=s->ww[0]=s->ww[1]=0.f;
}
/* I3's fold/wrap branches round level compensation before the LP update.
 * Preserve that boundary when specializing the loop. This compiler barrier
 * emits no machine instruction; it prevents an extra fused multiply/subtract. */
static inline float ins_shape_result(float x) {
#if defined(__arm__) || defined(__thumb__)
    __asm__("" : "+t"(x));
#endif
    return x;
}
static inline uint32_t ins_bits(float x) {
    union {float f;uint32_t u;} v={x};return v.u;
}
/* Exact states/samples only: these shortcuts never approximate quiet signals
 * or collapse a stereo difference. No extra per-track allocation/state. */
/* 0 independent; 1 bit-identical stereo; 2 bit-identical digital silence. */
static inline unsigned ins_shared_block(struct insert_drive *s,float *l,float *r,unsigned n) {
    if(!r || r==l || ins_bits(s->lp[0])!=ins_bits(s->lp[1]) || ins_bits(s->dc[0])!=ins_bits(s->dc[1]))return 0;
    if(ins_bits(s->wx[0])!=ins_bits(s->wx[1]) || ins_bits(s->ww[0])!=ins_bits(s->ww[1]))return 0;
    uint32_t bits=ins_bits(s->lp[0])|ins_bits(s->dc[0])|ins_bits(s->wx[0])|ins_bits(s->ww[0]);
    for(unsigned i=0;i<n;i++) {
        uint32_t a=ins_bits(l[i]);if(a!=ins_bits(r[i]))return 0;
        bits|=a;
    }
    return bits?1u:2u;
}
/* Constant mode is specialized by the compiler once per block. Settled
 * controls need no six-parameter per-sample ramp; each channel keeps its
 * filter histories in local registers. Preserve the original arithmetic. */
static inline __attribute__((always_inline)) void ins_steady(struct insert_drive *s,float *l,float *r,unsigned n,unsigned mode) {
    float g=s->gain,b=s->blend,e=s->enable,v=s->level,a=s->alpha,k=s->norm,m=(float)s->tone*.001f;
    unsigned relation=ins_shared_block(s,l,r,n);
    if(relation==2u)return;
    unsigned twin=relation==1u;
    unsigned channels=r && r!=l && !twin?2u:1u;
    for(unsigned ch=0;ch<channels;ch++) {
        float *samples=ch?r:l,lp=s->lp[ch],dc=s->dc[ch],px=s->wx[ch],pw=s->ww[ch];
        if(!(ins_bits(lp)|ins_bits(dc)|ins_bits(px)|ins_bits(pw))) {
            unsigned i=0;while(i<n && !ins_bits(samples[i]))i++;
            if(i==n)continue;
        }
        for(unsigned i=0;i<n;i++) {
            float dry=ins_finite(samples[i]),w=(mode==4?ins_wrap_aa(dry*g,&px,&pw):ins_shape(mode,dry*g,m))*k,previous=lp;
            if(mode>=3)w=ins_shape_result(w);
            lp+=(w-lp)*a;
            dc=lp-previous+.999084f*dc;
            float mixed=dry+b*(dc-dry);
            samples[i]=dry+e*(mixed*v-dry);
        }
        s->lp[ch]=lp;s->dc[ch]=dc;s->wx[ch]=px;s->ww[ch]=pw;
    }
    if(twin) {
        for(unsigned i=0;i<n;i++)r[i]=l[i];
        s->lp[1]=s->lp[0];s->dc[1]=s->dc[0];s->wx[1]=s->wx[0];s->ww[1]=s->ww[0];
    }
}
/* Also compiled unchanged into the sanitizer/audio analysis harness. */
static inline void ins_run(struct insert_drive *s,float *l,float *r,unsigned n) {
    if(n>32u)n=32u;
    if(!n || !l)return;
    if(!ins_active(s)){ins_quiet(s);return;}
    /* Crossfade through dry before changing algorithms. Coalesce rapid
     * changes to the latest request; use no simultaneous second shaper. */
    if(s->mode!=s->running_mode && s->enable==0.f) {
        s->running_mode=s->mode;s->dirty=1;ins_quiet(s);
    }
    unsigned mode=(unsigned)s->running_mode;
    if(s->dirty&1u) {
        float d=(float)s->drive*.001f,gain=1.f+31.f*d*d;
        /* S8d: CLIP gets up to 3x more gain over the top 30% of Drive
         * (+9.5 dB at maximum); below 70% it is unchanged. */
        if(mode==1 && d>.7f){float t=(d-.7f)*(1.f/.3f);gain*=1.f+2.f*t*t;}
        if(mode==INS_SAT) {
            /* PR #7: up to 36 dB of drive, level-matched at the 0.2
             * reference through the current curve; Tone is the Curve, so
             * the tone low-pass stays open (alpha 1 passes exactly). */
            gain=ins_exp2(6.f*d);
            s->target_gain=gain;
            s->target_norm=.2f/ins_sat(.2f*gain,(float)s->tone*.001f);
            s->target_alpha=1.f;
        } else {
        float reference=.2f*gain;if(reference>1.f)reference=1.f;
        s->target_gain=gain;
        s->target_norm=.2f/(mode>=3?reference:ins_curve(reference));
        float hz=800.f*ins_exp2(4.321928095f*(float)s->tone*.001f);
        s->target_alpha=1.f-ins_exp2(-.00018884834f*hz);
        }
        s->target_level=ins_exp2((float)s->output/6020.599913f);
        s->dirty=0;
    }
    float gain=s->target_gain,alpha=s->target_alpha,level=s->target_level,norm=s->target_norm;
    float enable=s->mode>0 && s->mode==s->running_mode && s->drive>0 && s->mix>0?1.f:0.f;
    float m=(float)s->tone*.001f; /* SAT curve (unused by the other drives) */
    float blend=(float)s->mix*.001f;
    unsigned stereo=r && r!=l;
    if(!stereo)s->lp[1]=s->dc[1]=s->wx[1]=s->ww[1]=0.f;
    /* Preserve the legacy smoothing's exact float fixed point. Some large
     * gains stall a few ULPs short of the target; do not change their sound
     * merely to recognize settled controls. Recheck for another block size
     * or after a silent bus has reset the bypass fade. */
    if((s->dirty&2u) && (s->dirty>>2)==n && s->enable==enable)goto settled;
    float fraction=(float)n*(1.f/240.f),inv=1.f/(float)n;
    float g1=ins_toward(s->gain,gain,fraction),b1=ins_toward(s->blend,blend,fraction);
    float e1=ins_toward(s->enable,enable,fraction),v1=ins_toward(s->level,level,fraction);
    float a1=ins_toward(s->alpha,alpha,fraction),k1=ins_toward(s->norm,norm,fraction);
    if(g1==s->gain && b1==s->blend && e1==s->enable && v1==s->level && a1==s->alpha && k1==s->norm) {
        s->dirty=2u|(n<<2);
        goto settled;
    }
    float dg=(g1-s->gain)*inv,db=(b1-s->blend)*inv,de=(e1-s->enable)*inv;
    float dv=(v1-s->level)*inv,da=(a1-s->alpha)*inv,dk=(k1-s->norm)*inv;
    float g=s->gain,b=s->blend,e=s->enable,v=s->level,a=s->alpha,k=s->norm;
    for(unsigned i=0;i<n;i++) {
        g+=dg;b+=db;e+=de;v+=dv;a+=da;k+=dk;
        float dl=ins_finite(l[i]),w=(mode==4?ins_wrap_aa(dl*g,&s->wx[0],&s->ww[0]):ins_shape(mode,dl*g,m))*k,previous=s->lp[0];
        s->lp[0]+=(w-s->lp[0])*a;
        s->dc[0]=s->lp[0]-previous+.999084f*s->dc[0]; /* approx 7 Hz high-pass */
        float wet=s->dc[0],mixed=dl+b*(wet-dl);
        l[i]=dl+e*(mixed*v-dl);
        if(stereo) {
            float dr=ins_finite(r[i]);w=(mode==4?ins_wrap_aa(dr*g,&s->wx[1],&s->ww[1]):ins_shape(mode,dr*g,m))*k;previous=s->lp[1];
            s->lp[1]+=(w-s->lp[1])*a;
            s->dc[1]=s->lp[1]-previous+.999084f*s->dc[1];
            wet=s->dc[1];mixed=dr+b*(wet-dr);
            r[i]=dr+e*(mixed*v-dr);
        }
    }
    s->gain=g1;s->blend=b1;s->enable=e1;s->level=v1;s->alpha=a1;s->norm=k1;
    if(e1==0.f)ins_quiet(s);
    return;
settled:
    switch(mode) {
    case 3:ins_steady(s,l,r,n,3);break;
    case 4:ins_steady(s,l,r,n,4);break;
    case INS_SAT:ins_steady(s,l,r,n,INS_SAT);break;
    default:ins_steady(s,l,r,n,1);break;
    }

}
/* Drive state as ins_reset leaves it, without touching the parameters,
 * enable or running_mode: used when the filter hands back to the drives. */
static inline void ins_drive_state(struct insert_drive *s) {
    s->gain=s->level=s->alpha=s->norm=1.f;s->blend=1.f;
    s->lp[0]=s->lp[1]=s->dc[0]=s->dc[1]=0.f;
    s->wx[0]=s->wx[1]=s->ww[0]=s->ww[1]=0.f;
    s->dirty=1;
    s->target_gain=s->target_alpha=s->target_level=s->target_norm=1.f;
}
/* Start a filter or shifter type that is about to fade in. */
static inline void ins_family_start(struct insert_drive *s,struct fsh_state *fs) {
    if(INS_IS_FILTER(s->mode))flt_init(&s->f,(unsigned)(s->mode-INS_FILTER_FIRST),s->drive,s->tone,s->output);
    else if(fs && INS_IS_RESO(s->mode))rsn_init((struct rsn_state *)fs,s->drive,s->mix,s->output);
    else if(fs)fsh_init(fs,s->mix,s->tone,s->output);
    s->dirty=0;
}
/* SD4: SPEC sleeps on silence. A resonator (T60 up to 12 s) or a shifter
 * with feedback keeps ringing after its input stops, so it keeps running
 * until its own state has decayed below -120 dB: the tail always plays out,
 * only the inaudible rest is skipped. Then, while the input stays below
 * -108 dBFS, the block passes unchanged and the controls settle at once;
 * sound wakes it from clear state on the current settings. */
#define INS_QUIET_BITS 0x36800000u   /* |x| < 2^-18 */
#define INS_QUIET_STATE 1e-6f
#define INS_QUIET_BLOCKS 4u
typedef uint32_t __attribute__((may_alias)) ins_alias_bits;
static inline int ins_block_quiet(const float *l,const float *r,unsigned n) {
    const ins_alias_bits *a=(const ins_alias_bits *)l;
    for(unsigned i=0;i<n;i++)if((a[i]&0x7fffffffu)>=INS_QUIET_BITS)return 0;
    if(r && r!=l){a=(const ins_alias_bits *)r;for(unsigned i=0;i<n;i++)if((a[i]&0x7fffffffu)>=INS_QUIET_BITS)return 0;}
    return 1;
}
static inline int ins_small(const float *x,unsigned n) {
    for(unsigned i=0;i<n;i++)if(!(x[i]<INS_QUIET_STATE && x[i]>-INS_QUIET_STATE))return 0;
    return 1;
}
static inline int ins_bank_decayed(const struct fsh_state *fs,int reso) {
    if(reso){const struct rsn_state *q=(const struct rsn_state *)fs;return ins_small(q->y1,RSN_N) && ins_small(q->y2,RSN_N);}
    return ins_small(&fs->h[0][0][0][0],32) && ins_small(fs->q1,2) && ins_small(fs->fb,2) && ins_small(fs->dcy,2);
}
static inline void ins_bank_snap(const struct insert_drive *s,struct fsh_state *fs,int reso) {
    if(reso) {
        struct rsn_state *q=(struct rsn_state *)fs;
        rsn_quiet(q);q->note=rsn_knob_note(s->drive);
        q->wet=(float)s->mix*.001f;q->level=rsn_exp2((float)s->output/6020.599913f);
    } else {
        fsh_quiet(fs);
        float hz=fsh_knob_hz(s->drive);
        if(s->running_mode-INS_SHIFT_FIRST==FSH_RING && hz<0.f)hz=-hz;
        fs->hz=hz;fs->wet=(float)s->mix*.001f;fs->fbg=(float)s->tone*(.001f*FSH_FB_MAX);
        fs->level=fsh_exp2((float)s->output/6020.599913f);
    }
}
/* Entry point for the audio callback. Drive modes run ins_run unchanged.
 * Changing between the drive, filter and shifter families, or between types
 * inside the filter or shifter family, fades the running one out to dry
 * (~5 ms), switches, then fades in. fs is this track's shifter state (0:
 * the shifter types play dry). */
static inline void ins_process(struct insert_drive *s,struct fsh_state *fs,float *l,float *r,unsigned n) {
    if(n>32u)n=32u;
    if(!n || !l)return;
    if(!INS_IS_FAMILY(s->running_mode)) {
        if(!INS_IS_FAMILY(s->mode) || s->enable!=0.f){ins_run(s,l,r,n);return;}
        s->running_mode=s->mode;
        ins_family_start(s,fs);
    }
    float target=s->mode==s->running_mode?1.f:0.f;
    float to=ins_toward(s->enable,target,(float)n*(1.f/240.f));
    if(INS_IS_FILTER(s->running_mode)) {
        if(s->dirty&1u){flt_resonance(&s->f,s->tone);s->dirty=0;}
        int env=(s->mix-500)*2;
        flt_process(&s->f,l,r,n,(unsigned)(s->running_mode-INS_FILTER_FIRST),s->drive,env,s->output,s->enable,to);
    } else {
        s->dirty=0;
        int reso=INS_IS_RESO(s->running_mode),quiet_in=fs && ins_block_quiet(l,r,n);
        if(!quiet_in)s->quiet=0;
        if(quiet_in && s->quiet>=INS_QUIET_BLOCKS)ins_bank_snap(s,fs,reso);   /* asleep */
        else {
            if(fs && reso)rsn_process((struct rsn_state *)fs,l,r,n,
                rsn_row((unsigned)(s->running_mode-INS_RESO_FIRST),s->chord),s->drive,s->tone,s->mix,s->output,s->enable,to);
            else if(fs)fsh_process(fs,l,r,n,(unsigned)(s->running_mode-INS_SHIFT_FIRST),s->drive,s->tone,s->mix,s->output,s->enable,to);
            if(quiet_in && ins_bank_decayed(fs,reso)) {
                if(++s->quiet>=INS_QUIET_BLOCKS)ins_bank_snap(s,fs,reso);
            } else s->quiet=0;
        }
    }
    s->enable=to;
    if(to==0.f && s->mode!=s->running_mode) {
        s->running_mode=s->mode;
        if(INS_IS_FAMILY(s->mode))ins_family_start(s,fs);
        else ins_drive_state(s);
    }
}
#endif
