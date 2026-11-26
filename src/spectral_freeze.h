/* Spectral freeze: FX1 mode 6 (Freeze). One mono instance (G12: mono for
 * CPU), its state in the SDRAM audio arena (FRZ_LINK), its hot buffers in
 * DTCM when there is room (struct sf_fast). Stereo prototype, previews and
 * bench: Bluebox-CFW/cobalt-spectral-freeze.
 *
 * Freeze captures the sound playing just before the press as a spectrum
 * and holds it indefinitely, resynthesised frame by frame:
 *   - The send is summed to mono. STFT of N = 2048 points, hop H = 1024 (50 %
 *     overlap), sine windows for analysis and synthesis (sin^2 overlapped by
 *     half sums to exactly 1).
 *   - Capture: two frames one sample apart, A in the real and B in the
 *     imaginary part of one complex FFT. Each bin keeps its magnitude, its
 *     phase as a unit phasor, and its phase advance per hop as a unit
 *     rotation: the advance over one sample (XB conj(XA), normalised) raised
 *     to the power H by ten squarings. Both frames see the same moment, so a
 *     press right after an onset still holds cleanly, and held tones keep
 *     their exact pitch with no atan2.
 *   - Phase locking: each bin takes the rotation of the magnitude peak it
 *     climbs to, so all bins of a partial turn together.
 *   - Synthesis: per hop, phasor *= rotation (times a random turn per
 *     partial scaled by Drift, the same for all bins of one peak, so a
 *     partial's pitch wanders instead of smearing), times magnitude and the
 *     Tone tilt; the real inverse transform as one N/2-point complex FFT
 *     (even samples real, odd imaginary); sine window, overlap-add. The same
 *     mono hold goes to both outputs.
 *   - No CPU spikes: every pass is cut into chunks of similar cost spread
 *     over the 32 blocks of a hop (capture over 60 blocks). Not frozen
 *     and faded out: only the input ring is written.
 * No libm and no allocation: sin/cos/exp2/log2 are polynomials, used at init
 * and on Tone changes only. Freestanding C, firmware-ready. */
#ifndef COBALT_SPECTRAL_FREEZE_H
#define COBALT_SPECTRAL_FREEZE_H
#include <stdint.h>

#define SF_N 2048u
#define SF_LOG2N 11u
#define SF_M (SF_N/2u)           /* the real inverse's complex FFT size */
#define SF_H 1024u
#define SF_BINS (SF_N/2u+1u)
#define SF_RING 4096u            /* input history (N+1 needed) */
#define SF_CHUNK 128u            /* samples per capture load unit (G25: was 256, a unit measured 12% of a block) */
#define SF_WIN_CHUNK 512u        /* samples per window unit */
#define SF_BIN_CHUNK 64u         /* bins per spectrum build unit */
#define SF_SPLIT_CHUNK 32u       /* bins per Blur split + update unit (heavier per bin) */
#define SF_BLOAD_CHUNK 128u      /* sample pairs per Blur load unit */
#define SF_PACK_CHUNK 256u       /* bins per real-inverse packing unit */
#define SF_ANA_CHUNK 16u         /* bins per analysis unit (ten squarings per bin) */
#define SF_LOCK_CHUNK 64u        /* bins per phase-locking unit (G24: was 128, a unit measured 14% of a block) */
#define SF_BLOCK 32u
#define SF_WBUF 512u             /* Drift wander delay buffer (samples, power of two) */
/* G15: the band processed, bins [SF_LO, SF_HI): 117 Hz .. 16 kHz (23.4 Hz a
 * bin). Below and above, the spectrum stays zero, and the per-bin work
 * (analysis, locking, build, Blur's split) skips those bins. Tone's tilt
 * carries a 4-bin ramp at each end: -6 dB at about 141 Hz and 16 kHz. */
#define SF_LO 5u
#define SF_HI 685u
#define SF_BAND (SF_HI-SF_LO)
#define SF_UNITS(chunk) ((SF_BAND+(chunk)-1u)/(chunk))
#define SF_MAGIC 0x46525a32u     /* mono layout */

/* Synthesis job per hop: build spectrum, pack for the real inverse, inverse
 * FFT, window. */
enum {SJ_IDLE,SJ_BUILD,SJ_PACK,SJ_FFT,SJ_WIN,SJ_DONE};
/* Capture job: load A and B, FFT, analyse, phase locking. */
enum {CJ_IDLE,CJ_LOAD,CJ_FFT,CJ_ANALYSE,CJ_LOCK_L,CJ_LOCK_R,CJ_LOCK_APPLY,CJ_DONE};
/* Blur (FX1 mode 7) per hop, before the synthesis jobs: load the frame that
 * ended at the hop boundary, forward real FFT, split + per-bin update. */
enum {BJ_IDLE,BJ_LOAD,BJ_FFT,BJ_SPLIT,BJ_LOCK,BJ_SYNTH};
/* G24: a cycle clock (DWT CYCCNT in the firmware, none on the host). The
 * cost weights are instruction counts, and on the device some units cost
 * far more than their weight (memory), so several bunched into one block
 * (up to 65% of it). A block now also stops starting units once SF_CYC_CAP
 * cycles have gone since it began (one unit always runs), except in its
 * hop's last SF_CAP_GRACE blocks, where the work must finish (G25: the
 * capture, which has no deadline, is always capped). */
#ifndef SF_NOW
#define SF_NOW() 0u
#endif
#define SF_CYC_CAP 21000u     /* about 8% of a 32-frame block at 399 MHz */
#define SF_CAP_GRACE 4u
#define SF_ENGINE_FREEZE 6
#define SF_ENGINE_BLUR 7

struct sf_fft {unsigned stage,b;};  /* stage 0 (radix-4 pass), then 2..log2n-1; butterfly index */

struct spectral_freeze {
    uint32_t magic;
    /* controls, 0..1000 like the other Cobalt controls */
    int freeze,fade,drift,tone,level;
    /* timed Freeze (set by the host from the transport): trigger starts a
     * capture like a new press; gate_closed fades the hold out (Gate mode) */
    int time,tmode;unsigned trigger,gate_closed,trig_tick,auto_on;   /* auto_on: Time set (runs like a synced delay) */
    unsigned fpos,fk,ftrig;   /* free-running step clock while the transport is stopped: samples into the cycle, cycles, last triggered */
    /* Blur controls (0..1000) and state: engine = the FX1 mode the dynamic
     * state belongs to; bjob, bend: the blur job and its frame's end;
     * bpeak: loudest blurred bin of the last hop (below 1e-3, about
     * -120 dB out: the tail is gone) */
    int blur,bdrift,btone,blevel;
    unsigned engine,bjob,bend,bidle,bdone;float bpeak,bpeak_run;   /* bdone: cost done this hop (Blur: sf_blur_weight; Freeze: its synthesis) */
    /* mono input history (written only while no capture is loading it) */
    float ring[SF_RING];
    unsigned wpos,ring_hold,cap_end;
    /* per-bin frozen state: amp = magnitude x Tone tilt, p = unit phasor,
     * q = unit rotation per hop */
    struct sf_bin {float amp,pr,pi,qr,qi,mag,ur,ui,mi;} bin[SF_BINS];   /* ur, ui, mi: Blur's last input phase and magnitude */
    uint16_t peak[SF_BINS];                  /* phase locking (and Drift): each bin's magnitude peak */
    float tilt[SF_BINS];
    unsigned tilt_next;int tilt_for;
    /* syn (the hold's N/2-point FFT), y (the built spectrum) and the tables
     * are reached through pointers, so the host can put them in fast memory
     * (struct sf_fast); else they stay in here. */
    float *syn,*y,*tw,*win;uint16_t *rev;
    float syn_mem[2*SF_M],y_mem[2*SF_BINS],cap[2*SF_N];
    /* output frames: previous, current, and the one being made */
    float frame[3][SF_N];
    unsigned prev,cur,work,valid;   /* valid: how many of prev/cur hold real frames */
    unsigned hop_pos;
    /* jobs */
    unsigned sjob,sidx,cjob,cidx;struct sf_fft sfft,cfft;
    unsigned cpace;   /* capture pacing: blocks since it started << 16 | cost done */
    unsigned synth_on,has_spectrum,late;
    float gain,gain_step,last_freeze;
    /* G17 Drift's pitch wander: three swept taps on the output */
    float wbuf[SF_WBUF];unsigned wbpos,wcnt[3];float wa[3],wb[3];uint32_t wrng;
    uint32_t rng,hops;
    /* tables */
    float win_mem[SF_N],tw_mem[SF_N];   /* sine window; twiddles cos,sin(2 pi k/N) for k < N/2 */
    uint16_t rev_mem[SF_N];             /* 11-bit reversal; >>1 gives the 10-bit one */
};
/* The hot part: the hold's FFT buffer, its spectrum and the tables. */
struct sf_fast {float syn[2*SF_M],y[2*SF_BINS+2],tw[SF_N],win[SF_N];uint16_t rev[SF_N];};

/* ---- polynomials (init / Tone only) ---------------------------------- */
static inline float sf_sin_pi(float x) {   /* sin(pi x), x in [-1,1] */
    float a=x<0.f?-x:x,s=x<0.f?-1.f:1.f;
    if(a>.5f)a=1.f-a;
    float t=a*3.14159265f,t2=t*t;
    return s*t*(1.f-t2*(1.f/6.f)*(1.f-t2*(1.f/20.f)*(1.f-t2*(1.f/42.f)*(1.f-t2*(1.f/72.f)))));
}
static inline float sf_exp2(float x) {
    int e=(int)x;if((float)e>x)e--;
    float f=x-(float)e;
    float p=1.f+f*(.69314718f+f*(.24022651f+f*(.05550411f+f*(.00961813f+f*.00133336f))));
    union{float f;uint32_t u;} v={p};v.u+=(uint32_t)e<<23;return v.f;
}
static inline float sf_log2(float x) {      /* x > 0 */
    union{float f;uint32_t u;} v={x};float e=(float)(int)((v.u>>23)&255u)-127.f;
    v.u=(v.u&0x007fffffu)|0x3f800000u;float m=v.f-1.f;
    return e+m*(1.4425449f+m*(-.7181452f+m*(.4575485f+m*(-.2779042f+m*.0958096f))));
}

/* ---- init -------------------------------------------------------------- */
static inline void sf_init_fast(struct spectral_freeze *s,struct sf_fast *fast) {
    uint8_t *p=(uint8_t *)s;for(unsigned i=0;i<sizeof *s;i++)p[i]=0;
    if(fast) {
        for(unsigned i=0;i<2*SF_M;i++)fast->syn[i]=0.f;
        for(unsigned i=0;i<2*SF_BINS+2;i++)fast->y[i]=0.f;
        s->syn=fast->syn;s->y=fast->y;s->tw=fast->tw;s->win=fast->win;s->rev=fast->rev;
    } else {s->syn=s->syn_mem;s->y=s->y_mem;s->tw=s->tw_mem;s->win=s->win_mem;s->rev=s->rev_mem;}
    for(unsigned n=0;n<SF_N;n++)s->win[n]=sf_sin_pi(((float)n+.5f)/(float)SF_N);
    for(unsigned k=0;k<SF_N/2u;k++) {
        float x=2.f*(float)k/(float)SF_N;              /* angle / pi */
        s->tw[2*k]=sf_sin_pi(x+.5f>1.f?x+.5f-2.f:x+.5f); /* cos(pi x) = sin(pi (x+1/2)) */
        s->tw[2*k+1]=sf_sin_pi(x);
    }
    for(unsigned n=0;n<SF_N;n++){unsigned r=0;for(unsigned b=0;b<SF_LOG2N;b++)r|=((n>>b)&1u)<<(SF_LOG2N-1u-b);s->rev[n]=(uint16_t)r;}
    for(unsigned k=0;k<SF_BINS;k++){s->tilt[k]=1.f;s->peak[k]=(uint16_t)k;}   /* flat until the first Tone pass */
    s->fade=300;s->drift=150;s->tone=750;s->level=500;
    s->blur=500;s->bdrift=0;s->btone=750;s->blevel=475;s->engine=SF_ENGINE_FREEZE;
    s->prev=0;s->cur=1;s->work=2;
    s->rng=0x2545f491u;s->wrng=0x9e3779b9u;s->tilt_for=-1;
    s->magic=SF_MAGIC;
}
static inline void sf_init(struct spectral_freeze *s){sf_init_fast(s,0);}
#ifdef F_FREEZE
/* Parameter events (FX1 queue), kept whatever the FX1 mode. */
static inline int sf_clamp(int v,int lo,int hi){return v<lo?lo:v>hi?hi:v;}
static inline void sf_event(struct spectral_freeze *s,unsigned id,int value) {
    switch(id) {
    case F_FREEZE:s->freeze=value!=0;break;
    case F_FADE:s->fade=sf_clamp(value,0,1000);break;
    case F_DRIFT:s->drift=sf_clamp(value,0,1000);break;
    case F_TONE:s->tone=sf_clamp(value,0,1000);break;
    case F_LEVEL:s->level=sf_clamp(value,0,1000);break;
    case F_TIME:s->time=sf_clamp(value,0,F_TIME_ITEMS-1)+1;break;   /* G26: no Off */
    case F_TMODE:s->tmode=sf_clamp(value,0,1);break;
    case B_BLUR:s->blur=sf_clamp(value,0,1000);break;
    case B_DRIFT:s->bdrift=sf_clamp(value,0,1000);break;
    case B_TONE:s->btone=sf_clamp(value,0,1000);break;
    case B_LEVEL:s->blevel=sf_clamp(value,0,1000);break;
    default:break;
    }
}
/* Firmware link: the instance from the boot-time SDRAM arena (delay_m7.c
 * dly_construct), found from M7-only backup SRAM. */
/* pad[0]: a Freeze or Blur ran (other modes stop it once); pad[1..5] unused. */
struct frz_link {uint32_t magic;struct spectral_freeze *state;uint32_t pad[6];};
#define FRZ_LINK_MAGIC 0x46524c31u
#endif
/* FX1 left Freeze mode: stop at once, so a later return starts clean (a new
 * press captures). The controls are kept. */
static inline void sf_stop(struct spectral_freeze *s) {
    s->synth_on=0;s->gain=0.f;s->valid=0;s->cjob=CJ_IDLE;s->ring_hold=0;
    s->auto_on=0;s->gate_closed=0;s->trigger=0;
    s->last_freeze=(float)s->freeze;   /* a Freeze left On needs a new press */
    for(unsigned i=0;i<SF_WBUF;i++)s->wbuf[i]=0.f;   /* no wander tail from before */
}
/* Soft knee on the return: unity below 0.7 (-3 dBFS), smoothly towards
 * 1.0 (0 dBFS) above. G35: was unity below 1, towards 2 (+6 dBFS): with a
 * full FX1 send the wet alone could peak well over full scale. */
#define SF_KNEE_T .70794578f
static inline float sf_knee(float y) {
    float a=y<0.f?-y:y;
    if(a<=SF_KNEE_T)return y;
    float o=a-SF_KNEE_T,v=SF_KNEE_T+(1.f-SF_KNEE_T)*o/((1.f-SF_KNEE_T)+o);
    return y<0.f?-v:v;
}

/* ---- incremental FFT (input bit-reversed, in place), 2^log2n points ----- */
/* Stages 0 and 1 together: radix-4 butterflies with twiddles 1 and -/+i (no
 * multiplies), 64 groups of four points per unit. G16: then the radix-2
 * stages two at a time (s and s+1 fused: four points stay in registers, half
 * the loads and stores, 3 twiddle multiplies per 4 points instead of 4),
 * SF_FFT4_UNIT groups of four per unit; an odd last stage (log2n 11) runs
 * radix-2, SF_FFT_UNIT butterflies per unit. Separate forward / inverse
 * loops (no per-butterfly branch). The twiddle table is the N-point one:
 * butterfly j of a stage of half-size 2^stage uses entry j << (LOG2N-1-stage)
 * whatever the transform size. One call does up to `units` units; returns 1
 * when done. */
#define SF_FFT_UNIT 128u
#define SF_FFT4_UNIT 64u
#define SF_R4_UNIT 64u
static inline void sf_radix4(float *__restrict x,unsigned g0,unsigned g1,int inverse) {
    for(unsigned g=g0;g<g1;g++) {
        float *p=x+8u*g;
        float ar=p[0],ai=p[1],br=p[2],bi=p[3],cr=p[4],ci=p[5],dr=p[6],di=p[7];
        float s0r=ar+br,s0i=ai+bi,d0r=ar-br,d0i=ai-bi;     /* stage 0 */
        float s1r=cr+dr,s1i=ci+di,d1r=cr-dr,d1i=ci-di;
        /* stage 1: (s0,s1) twiddle 1; (d0,d1) twiddle -i forward, +i inverse */
        float tr=inverse?-d1i:d1i,ti=inverse?d1r:-d1r;
        p[0]=s0r+s1r;p[1]=s0i+s1i;p[4]=s0r-s1r;p[5]=s0i-s1i;
        p[2]=d0r+tr;p[3]=d0i+ti;p[6]=d0r-tr;p[7]=d0i-ti;
    }
}
#define SF_BFLY_LOOP(SIGN) \
    for(unsigned i=0;i<run;i++) { \
        float wr=w[0],wi=SIGN w[1],ar=a[0],ai=a[1],cr=c[0],ci=c[1]; \
        float tr=cr*wr-ci*wi,ti=cr*wi+ci*wr; \
        a[0]=ar+tr;a[1]=ai+ti;c[0]=ar-tr;c[1]=ai-ti; \
        a+=2;c+=2;w+=wstep; \
    }
/* Stages s and s+1 (h = 2^s): x0..x3 at top+j + {0,h,2h,3h}. Stage s pairs
 * (x0,x1) and (x2,x3) with w1 = W_2h^j; stage s+1 pairs (x0,x2) with
 * w2 = W_4h^j and (x1,x3) with W_4h^(j+h) = w2 * (-i forward, +i inverse). */
#define SF_BFLY4_LOOP(SIGN) \
    for(unsigned i=0;i<run;i++) { \
        float w1r=w1[0],w1i=SIGN w1[1],w2r=w2[0],w2i=SIGN w2[1]; \
        float x0r=p0[0],x0i=p0[1],x1r=p1[0],x1i=p1[1],x2r=p2[0],x2i=p2[1],x3r=p3[0],x3i=p3[1]; \
        float br=x1r*w1r-x1i*w1i,bi=x1r*w1i+x1i*w1r; \
        float dr=x3r*w1r-x3i*w1i,di=x3r*w1i+x3i*w1r; \
        float y0r=x0r+br,y0i=x0i+bi,y1r=x0r-br,y1i=x0i-bi; \
        float y2r=x2r+dr,y2i=x2i+di,y3r=x2r-dr,y3i=x2i-di; \
        float tr=y2r*w2r-y2i*w2i,ti=y2r*w2i+y2i*w2r; \
        float ur=y3r*w2r-y3i*w2i,ui=y3r*w2i+y3i*w2r; \
        float vr=-(SIGN ui),vi=SIGN ur; \
        p0[0]=y0r+tr;p0[1]=y0i+ti;p2[0]=y0r-tr;p2[1]=y0i-ti; \
        p1[0]=y1r+vr;p1[1]=y1i+vi;p3[0]=y1r-vr;p3[1]=y1i-vi; \
        p0+=2;p1+=2;p2+=2;p3+=2;w1+=w1step;w2+=w2step; \
    }
static inline unsigned sf_fft_run(struct spectral_freeze *s,float *__restrict x,struct sf_fft *f,unsigned units,int inverse,unsigned log2n) {
    const unsigned n=1u<<log2n;
    while(units && f->stage<log2n) {
        if(f->stage==0) {
            unsigned g1=f->b+SF_R4_UNIT;if(g1>n/4u)g1=n/4u;
            sf_radix4(x,f->b,g1,inverse);units--;
            if(g1>=n/4u){f->stage=2;f->b=0;}else f->b=g1;
            continue;
        }
        if(f->stage+1u<log2n) {
            /* fused pair of stages: n/4 groups of four points */
            unsigned h=1u<<f->stage,s1=SF_LOG2N-1u-f->stage,s2=s1-1u;
            unsigned b=f->b,end=b+SF_FFT4_UNIT;if(end>n/4u)end=n/4u;
            units--;
            while(b<end) {
                unsigned j=b&(h-1u),g=b>>f->stage,top=g*(h<<2)+j;
                unsigned run=h-j;if(run>end-b)run=end-b;
                const float *w1=s->tw+2u*(j<<s1),*w2=s->tw+2u*(j<<s2);
                const unsigned w1step=2u<<s1,w2step=2u<<s2;
                float *__restrict p0=x+2u*top,*__restrict p1=p0+2u*h,*__restrict p2=p1+2u*h,*__restrict p3=p2+2u*h;
                if(inverse){SF_BFLY4_LOOP(+)}else{SF_BFLY4_LOOP(-)}
                b+=run;
            }
            if(b>=n/4u){f->stage+=2u;f->b=0;}else f->b=b;
            continue;
        }
        unsigned half=1u<<f->stage,shift=SF_LOG2N-1u-f->stage;
        unsigned b=f->b,end=b+SF_FFT_UNIT;if(end>n/2u)end=n/2u;
        units--;
        while(b<end) {
            unsigned j=b&(half-1u),g=b>>f->stage,top=g*(half<<1)+j;
            unsigned run=half-j;if(run>end-b)run=end-b;
            const float *w=s->tw+2u*(j<<shift);const unsigned wstep=2u<<shift;
            float *__restrict a=x+2u*top,*__restrict c=a+2u*half;
            if(inverse){SF_BFLY_LOOP(+)}else{SF_BFLY_LOOP(-)}
            b+=run;
        }
        if(b>=n/2u){f->stage++;f->b=0;}else f->b=b;
    }
    return f->stage>=log2n;
}
/* ---- Tone tilt: gain (f / 1 kHz)^t, t = -1..+1 (-6..+6 dB/oct) -------- */
/* G35: Tone changes the colour, not the loudness. The tilt is scaled so a
 * pink (equal power per octave) input keeps its power over the band
 * (117 Hz .. 16 kHz, gain capped at +18 dB): log2 of that scale is fitted
 * by a quartic in t (within 0.35 dB; -11.9 dB at t = 1, -8.7 dB at -1, 0 flat)
 * and added to the exponent, so it costs no extra exp2. */
#define SF_TILT_C(t) ((t)*(-.525549f+(t)*(-2.724109f+(t)*(.203908f+(t)*1.020996f))))
static inline void sf_tilt_step(struct spectral_freeze *s,unsigned bins) {
    const int tone=s->engine==SF_ENGINE_BLUR?s->btone:s->tone;
    if(s->tilt_for!=tone){s->tilt_for=tone;s->tilt_next=SF_LO;}
    if(s->tilt_next<SF_LO)s->tilt_next=SF_LO;
    if(s->tilt_next>=SF_HI)return;
    float t=(float)(tone-500)*(1.f/500.f),c=SF_TILT_C(t);
    unsigned end=s->tilt_next+bins;if(end>SF_HI)end=SF_HI;
    for(unsigned k=s->tilt_next;k<end;k++) {
        float hz=(float)k*(48000.f/(float)SF_N);
        float e=t*sf_log2(hz*(1.f/1000.f));
        float g=sf_exp2((e>3.f?3.f:e)+c);   /* capped at +18 dB, then the loudness scale */
        /* band edges: 1/4, 1/2, 3/4 over the outer bins */
        unsigned lo=k-SF_LO+1u,hi=SF_HI-k;
        if(lo<4u)g*=(float)lo*.25f;
        if(hi<4u)g*=(float)hi*.25f;
        s->tilt[k]=g;
        s->bin[k].amp=s->bin[k].mag*s->tilt[k];
    }
    s->tilt_next=end;
}

/* ---- capture ------------------------------------------------------------ */
/* Frame A ends one sample before frame B; B ends where the ring stopped.
 * A goes in the real part, B in the imaginary part. */
static inline void sf_cap_load(struct spectral_freeze *s,unsigned from,unsigned count) {
    unsigned start=s->cap_end-1u-SF_N;
    for(unsigned n=from;n<from+count;n++) {
        float w=s->win[n];unsigned d=2u*s->rev[n];
        s->cap[d]=s->ring[(start+n)&(SF_RING-1u)]*w;s->cap[d+1]=s->ring[(start+n+1u)&(SF_RING-1u)]*w;
    }
}
/* Bin k of the two real frames from the packed transform Z:
 * XA = (Z[k]+conj Z[N-k])/2, XB = (Z[k]-conj Z[N-k])/(2i). */
static inline void sf_unpack(const float *z,unsigned k,float *ar,float *ai,float *br,float *bi) {
    unsigned m=(SF_N-k)&(SF_N-1u);
    float p=z[2*k],q=z[2*k+1],r=z[2*m],t=z[2*m+1];
    *ar=.5f*(p+r);*ai=.5f*(q-t);*br=.5f*(q+t);*bi=.5f*(r-p);
}
static inline float sf_rsqrt(float x) {      /* x > 0: bit-hack estimate and three Newton steps */
    union{float f;uint32_t u;} v={x};v.u=0x5f3759dfu-(v.u>>1);
    float r=v.f;r*=1.5f-.5f*x*r*r;r*=1.5f-.5f*x*r*r;r*=1.5f-.5f*x*r*r;
    return r;
}
static inline unsigned sf_capture_chunk(struct spectral_freeze *s) {
    switch(s->cjob) {
    case CJ_LOAD:
        sf_cap_load(s,s->cidx,SF_CHUNK);s->cidx+=SF_CHUNK;
        if(s->cidx>=SF_N){s->cidx=0;s->cfft.stage=0;s->cfft.b=0;s->cjob=CJ_FFT;s->ring_hold=0;}   /* read: the ring may run again */
        return 1;
    case CJ_FFT:
        if(sf_fft_run(s,s->cap,&s->cfft,1,0,SF_LOG2N)){s->cjob=CJ_ANALYSE;s->cidx=0;}
        return 1;
    case CJ_ANALYSE: {
        if(s->cidx<SF_LO)s->cidx=SF_LO;
        unsigned end=s->cidx+SF_ANA_CHUNK;if(end>SF_HI)end=SF_HI;
        for(unsigned k=s->cidx;k<end;k++) {
            float ar,ai,br,bi;sf_unpack(s->cap,k,&ar,&ai,&br,&bi);
            float m2=br*br+bi*bi;
            /* magnitude and unit phase of B */
            float mag=0.f,ur=1.f,ui=0.f;
            if(m2>1e-24f){float r=sf_rsqrt(m2);mag=m2*r;ur=br*r;ui=bi*r;}
            /* advance over one sample = B conj(A), normalised; to the power
             * H = 2^10 by squaring (renormalised each time). Bin-centre
             * advance when silent: e^{i pi k} for H = N/2. */
            float xr=br*ar+bi*ai,xi=bi*ar-br*ai,n2=xr*xr+xi*xi,rr,ri;
            if(n2>1e-30f) {
                float r=sf_rsqrt(n2);rr=xr*r;ri=xi*r;
                for(unsigned q=0;q<SF_LOG2N-1u;q++) {
                    float sr=rr*rr-ri*ri,si=2.f*rr*ri,m=sr*sr+si*si;
                    float g=1.5f-.5f*m;rr=sr*g;ri=si*g;
                }
            } else {rr=(k&1u)?-1.f:1.f;ri=0.f;}
            struct sf_bin *e=&s->bin[k];
            e->mag=mag;e->amp=mag*s->tilt[k];e->pr=ur;e->pi=ui;e->qr=rr;e->qi=ri;
        }
        s->cidx=end;if(end>=SF_HI){s->cidx=SF_LO;s->cjob=CJ_LOCK_L;}
        return 1;}
    case CJ_LOCK_L: {
        /* Left to right: peaks, and bins whose climb goes left (their left
         * neighbour is higher than both): same peak as that neighbour. */
        if(s->cidx<SF_LO)s->cidx=SF_LO;
        unsigned end=s->cidx+SF_LOCK_CHUNK;if(end>SF_HI)end=SF_HI;
        for(unsigned k=s->cidx;k<end;k++) {
            float m=s->bin[k].mag,ml=k>SF_LO?s->bin[k-1].mag:-1.f,mr=k+1u<SF_HI?s->bin[k+1].mag:-1.f;
            s->peak[k]=ml>m && ml>=mr?s->peak[k-1]:mr>m?0xffffu:(uint16_t)k;
        }
        s->cidx=end;if(end>=SF_HI){s->cidx=SF_HI;s->cjob=CJ_LOCK_R;}
        return 1;}
    case CJ_LOCK_R: {
        /* Right to left: bins whose climb goes right. */
        if(s->cidx>SF_HI)s->cidx=SF_HI;
        unsigned stop=s->cidx>SF_LO+SF_LOCK_CHUNK?s->cidx-SF_LOCK_CHUNK:SF_LO;
        for(unsigned k=s->cidx;k-->stop;)
            if(s->peak[k]==0xffffu)s->peak[k]=k+1u<SF_HI?s->peak[k+1]:(uint16_t)k;
        s->cidx=stop;if(stop==SF_LO){s->cjob=CJ_LOCK_APPLY;}
        return 1;}
    case CJ_LOCK_APPLY: {
        if(s->cidx<SF_LO)s->cidx=SF_LO;
        unsigned end=s->cidx+SF_LOCK_CHUNK;if(end>SF_HI)end=SF_HI;
        for(unsigned k=s->cidx;k<end;k++) {
            unsigned p=s->peak[k];
            if(p!=k && p<SF_BINS){s->bin[k].qr=s->bin[p].qr;s->bin[k].qi=s->bin[p].qi;}
        }
        s->cidx=end;if(end>=SF_HI){s->cjob=CJ_DONE;}
        return 1;}
    }
    return 0;
}

/* ---- synthesis ------------------------------------------------------------ */
/* Drift: a random value per partial (its peak bin) or bin and time step, in [-1, 1). */
static inline float sf_drift_rand(unsigned peak,uint32_t hop) {
    uint32_t h=(peak+1u)*2654435761u^hop*0x9e3779b9u;
    h^=h>>15;h*=0x2c1b3c6du;h^=h>>12;h*=0x297a2d39u;h^=h>>15;
    return (float)(int32_t)h*(1.f/2147483648.f);
}
static inline unsigned sf_synth_chunk(struct spectral_freeze *s) {
    switch(s->sjob) {
    case SJ_BUILD: {
        /* Advance each bin's phasor by its rotation and its partial's Drift
         * turn, renormalise, scale by amp: Y[k], k = 0..N/2. */
        /* G17 Drift, top half of the knob: a per-bin random turn of up to
         * +/-pi per hop, scattering the partials into a wash (the first half,
         * the pitch wander, is done on the output: sf_wander). */
        const int blur=s->engine==SF_ENGINE_BLUR;
        float d=(float)(blur?s->bdrift:s->drift)*.001f;
        const float scatter=d>.5f?(2.f*d-1.f)*3.14159265f:0.f;
        if(s->sidx<SF_LO)s->sidx=SF_LO;
        unsigned end=s->sidx+SF_BIN_CHUNK;if(end>SF_HI)end=SF_HI;
        float *__restrict y=s->y;   /* outside the band: always zero */
        struct sf_bin *__restrict e=s->bin+s->sidx;
        const uint32_t hop=s->hops;
        for(unsigned k=s->sidx;k<end;k++,e++) {
            float ar=e->pr*e->qr-e->pi*e->qi,ai=e->pr*e->qi+e->pi*e->qr;
            if(scatter>0.f) {
                float th=scatter*sf_drift_rand(k,hop);
                /* cos / sin of th/2 by series, then doubled: within 0.5% */
                float h=.5f*th,h2=h*h;
                float hc=1.f-h2*(.5f-h2*(1.f/24.f-h2*(1.f/720.f))),hs=h*(1.f-h2*(1.f/6.f-h2*(1.f/120.f)));
                float cr=hc*hc-hs*hs,ci=2.f*hc*hs;
                float t0=ar*cr-ai*ci;ai=ar*ci+ai*cr;ar=t0;
            }
            float n0=ar*ar+ai*ai,g0=1.5f-.5f*n0;g0*=1.5f-.5f*n0*g0*g0;
            if(!(n0>.25f && n0<4.f)){ar=1.f;ai=0.f;g0=1.f;}   /* never let a phasor run away */
            ar*=g0;ai*=g0;
            e->pr=ar;e->pi=ai;
            y[2*k]=ar*e->amp;y[2*k+1]=ai*e->amp;
        }
        s->sidx=end;if(end>=SF_HI){s->sidx=0;s->sjob=SJ_PACK;}
        return 1;}
    case SJ_PACK: {
        /* Real inverse as an N/2-point complex one: for k < N/2,
         * Z[k] = (Y[k] + conj Y[N/2-k]) + i e^{+2 pi i k/N} (Y[k] - conj Y[N/2-k]),
         * written bit-reversed (10 bits); the inverse then gives
         * x[2m] + i x[2m+1], times N. */
        const float *__restrict y=s->y,*__restrict tw=s->tw;
        float *__restrict z=s->syn;const uint16_t *rev=s->rev;
        unsigned end=s->sidx+SF_PACK_CHUNK;if(end>SF_M)end=SF_M;
        for(unsigned k=s->sidx;k<end;k++) {
            float xr=y[2*k],xi=y[2*k+1],cr=y[2*(SF_M-k)],ci=-y[2*(SF_M-k)+1];
            float er=xr+cr,ei=xi+ci,dr=xr-cr,di=xi-ci;
            float wr=tw[2*k],wi=tw[2*k+1];
            float orr=dr*wr-di*wi,oi=dr*wi+di*wr;
            float *d=z+rev[k];   /* = 2*rev10(k): rev11(k) is even for k < N/2 */
            d[0]=er-oi;d[1]=ei+orr;
        }
        s->sidx=end;if(end>=SF_M){s->sidx=0;s->sfft.stage=0;s->sfft.b=0;s->sjob=SJ_FFT;}
        return 1;}
    case SJ_FFT:
        if(sf_fft_run(s,s->syn,&s->sfft,1,1,SF_LOG2N-1u)){s->sjob=SJ_WIN;s->sidx=0;}
        return 1;
    case SJ_WIN: {
        float *o=s->frame[s->work];const float *z=s->syn;
        const float scale=1.f/(float)SF_N;
        for(unsigned n=s->sidx;n<s->sidx+SF_WIN_CHUNK;n++)o[n]=z[n]*s->win[n]*scale;
        s->sidx+=SF_WIN_CHUNK;if(s->sidx>=SF_N)s->sjob=SJ_DONE;
        return 1;}
    }
    return 0;
}
/* G14: synthesis and capture are paced by cost, not by unit count: their
 * units differ up to 8x. Measured ARM cost (-O2, emulator) of the next
 * unit, in hundreds of instructions. Synthesis: 11 build (the band) + 4
 * pack + 4 radix-4 + 16 fused + 4 window per hop (32 blocks); capture: 8
 * load + 8 radix-4 + 32 fused + 8 radix-2 + 43 analysis + 3 x 6 locking over
 * 60 blocks (40 ms: spread out, so a timed Step capturing every 1/32 keeps
 * its peak down). */
static inline unsigned sf_fft_weight(unsigned stage) {
    /* G16: radix-4 pass, fused pairs at stages 2, 4, 6, 8, radix-2 stage 10 */
    return stage==0u?25u:stage==2u?46u:stage==4u?41u:stage<10u?40u:32u;
}
#define SF_FFT_COST(units) ((units)*(25u+46u+41u+40u+40u))   /* units per pass: 4 (N/2), 8 (N) */
static inline unsigned sf_synth_weight(const struct spectral_freeze *s,unsigned build) {
    switch(s->sjob) {
    case SJ_BUILD:return build;
    case SJ_PACK:return 65u;
    case SJ_FFT:return sf_fft_weight(s->sfft.stage);
    case SJ_WIN:return 36u;
    }
    return 0u;
}
static inline unsigned sf_cap_weight(const struct spectral_freeze *s) {
    switch(s->cjob) {
    case CJ_LOAD:return 23u;
    case CJ_FFT:return sf_fft_weight(s->cfft.stage);
    case CJ_ANALYSE:return 32u;
    case CJ_LOCK_L:return 12u;   /* G24: halved with the chunk */
    case CJ_LOCK_R:return 5u;
    case CJ_LOCK_APPLY:return 9u;
    }
    return 0u;
}
/* a build unit costs about 2.7k instructions, 4.5k with Drift (its turns) */
#define SF_BUILD_W(drift) ((drift)>500?46u:27u)   /* scatter only in the top half */
#define SF_SYNTH_COST(bw) (SF_UNITS(SF_BIN_CHUNK)*(bw)+4u*65u+SF_FFT_COST(4u)+4u*36u)
#define SF_CAPTURE_BLOCKS 60u
#define SF_LOCK_COST (SF_UNITS(SF_LOCK_CHUNK)*(12u+5u+9u))
#define SF_CAPTURE_COST ((SF_N/SF_CHUNK)*23u+SF_FFT_COST(8u)+8u*32u+SF_UNITS(SF_ANA_CHUNK)*32u+SF_LOCK_COST)
#define SF_PACE_MAX 320u   /* at most about twice an even share a block */
/* Share of `left` cost over `blocks` blocks; a unit runs while at least half
 * of it fits (and always one). */
static inline unsigned sf_share(unsigned left,unsigned blocks) {
    if(!blocks)blocks=1u;
    unsigned b=(left+blocks-1u)/blocks;
    return b>SF_PACE_MAX?SF_PACE_MAX:b;
}
#define SF_CAPTURE_SAMPLES (60u*SF_BLOCK)

static inline float sf_fade_step(const struct spectral_freeze *s) {
    /* 10 ms .. 4 s (log) for a full 0 <-> 1 swing */
    float sec=.01f*sf_exp2((float)s->fade*(8.643856f*.001f));
    return 1.f/(sec*48000.f);
}

/* G17 Drift, first half of the knob: pitch wander. Three taps on a short
 * delay of the output, each swept by its own smooth random curve (eased
 * between random targets every 0.40 / 0.48 / 0.58 s, each on the other side
 * of the centre from the last), summed: every partial
 * drifts in pitch, up to about +/-50 cents at half the knob, like a slow
 * ensemble. With Drift 0 the output is the same signal delayed by 4 ms, so
 * turning Drift up from 0 never jumps. */
#define SF_WD0 192.f      /* centre delay, samples */
#define SF_WAMP 187.f     /* sweep: slope 3 A / T = 50 cents at T = 0.4 s */
static inline float sf_wrand(struct spectral_freeze *s) {
    s->wrng=s->wrng*1664525u+1013904223u;
    return (float)(int32_t)s->wrng*(1.f/2147483648.f);
}
static inline void sf_wander(struct spectral_freeze *s,float *x,unsigned n,int drift) {
    static const unsigned period[3]={19200u,23040u,27840u};
    float *b=s->wbuf;unsigned p=s->wbpos&(SF_WBUF-1u);
    for(unsigned i=0;i<n;i++)b[(p+i)&(SF_WBUF-1u)]=x[i];
    float d=(float)drift*.001f,w=d<.5f?2.f*d:1.f;
    if(!(w>0.f)) {
        for(unsigned i=0;i<n;i++)x[i]=b[(p+i-(unsigned)SF_WD0)&(SF_WBUF-1u)];
        s->wbpos=p+n;return;
    }
    float d0[3],dd[3];
    for(unsigned t=0;t<3u;t++) {
        unsigned c=s->wcnt[t],T=period[t];
        float u0=(float)c/(float)T;u0=u0*u0*(3.f-2.f*u0);
        float v0=s->wa[t]+(s->wb[t]-s->wa[t])*u0;
        c+=n;
        if(c>=T) {
            /* the next target swings to the other side by 40..100% */
            c-=T;s->wa[t]=s->wb[t];
            float m=.7f+.3f*sf_wrand(s);s->wb[t]=s->wa[t]>0.f?-m:m;
        }
        float u1=(float)c/(float)T;u1=u1*u1*(3.f-2.f*u1);
        float v1=s->wa[t]+(s->wb[t]-s->wa[t])*u1;
        s->wcnt[t]=c;
        d0[t]=SF_WD0+SF_WAMP*w*v0;dd[t]=SF_WAMP*w*(v1-v0)/(float)n;
    }
    const float gm=.33333333f+(.45f-.33333333f)*w;   /* coherent at 0; the level held on a wandering tone at half the knob */
    /* one tight loop per tap: the read position steps by 1 - slope a sample */
    float acc[SF_BLOCK];
    for(unsigned t=0;t<3u;t++) {
        /* from the wrapped write index: a float keeps its fraction */
        float at=(float)(p&(SF_WBUF-1u))+(float)SF_WBUF*4.f-d0[t];const float inc=1.f-dd[t];
        for(unsigned i=0;i<n;i++,at+=inc) {
            unsigned k=(unsigned)at;float f=at-(float)k;
            float a0=b[k&(SF_WBUF-1u)],a1=b[(k+1u)&(SF_WBUF-1u)];
            float v=a0+(a1-a0)*f;
            if(t)acc[i]+=v;else acc[i]=v;
        }
    }
    for(unsigned i=0;i<n;i++)x[i]=acc[i]*gm;
    s->wbpos=p+n;
}
/* One block in place: l/r carry the FX send in and the wet out (the same
 * mono hold on both). r may be l (mono). n <= 32. */
static inline void sf_process(struct spectral_freeze *s,float *l,float *r,unsigned n) {
    if(n>SF_BLOCK)n=SF_BLOCK;
    if(n>SF_H-s->hop_pos)n=SF_H-s->hop_pos;   /* blocks are 32 frames; never straddle a hop */
    if(!n || !l)return;
    if(!r)r=l;
    const uint32_t t0=SF_NOW();
    /* input history, summed to mono. G27: it keeps running while a capture
     * loads unless the next block would overwrite the frame being read (the
     * ring leaves SF_RING-SF_N-1 samples, 64 blocks, of room). G26 and
     * before paused it for every load: at 1/64, back-to-back captures
     * spliced the history into jumps and every snapshot came out smeared. */
    if(!s->ring_hold || s->wpos+n-s->cap_end<=SF_RING-SF_N-1u) {
        for(unsigned i=0;i<n;i++)s->ring[(s->wpos+i)&(SF_RING-1u)]=.5f*(l[i]+r[i]);
        s->wpos+=n;
    }
    /* on: the Freeze toggle, or a timed Freeze running */
    const int on=s->freeze || s->auto_on;
    /* trigger bit 1: a Freeze press (frz_clock), which also restarts a
     * capture already running (G27: a press at 1/64 waited for the timed
     * one and held later material) */
    if(((on && !s->last_freeze) || (s->trigger && on)) && (s->cjob==CJ_IDLE || (s->trigger&2u))) {
        s->cap_end=s->wpos;s->ring_hold=1;s->cjob=CJ_LOAD;s->cidx=0;s->trigger=0;s->cpace=0;
    }
    if(!on)s->trigger=0;
    s->last_freeze=(float)on;
    if(s->cjob!=CJ_IDLE && s->cjob!=CJ_DONE) {
        unsigned blk=s->cpace>>16,done=s->cpace&0xffffu;
        unsigned budget=blk+1u>=SF_CAPTURE_BLOCKS?~0u:sf_share(done<SF_CAPTURE_COST?SF_CAPTURE_COST-done:1u,SF_CAPTURE_BLOCKS-blk),spent=0;
        /* G25: the capture has no hard deadline, so the cap always holds
         * (G24 ran everything left at its 60th block: Step spikes) */
        const int capped=1;
        while(s->cjob!=CJ_DONE) {
            unsigned w=sf_cap_weight(s);
            if(spent && (spent+w/2u>budget || (capped && SF_NOW()-t0>SF_CYC_CAP)))break;
            sf_capture_chunk(s);spent+=w;
        }
        done+=spent;if(done>0xffffu)done=0xffffu;
        s->cpace=((blk+1u)<<16)|done;
    }
    if(s->cjob==CJ_DONE) {
        s->cjob=CJ_IDLE;s->has_spectrum=1;
        if(!s->synth_on){s->synth_on=1;s->valid=0;s->sjob=SJ_BUILD;s->sidx=0;s->hop_pos=0;s->bdone=0;}
    }
    sf_tilt_step(s,64u);
    /* wet gain towards the freeze state; G35: Level 500 = a held tone at
     * about its input level (as Flux), full = +6 dB (was +6 / +12 dB) */
    float target=on && s->has_spectrum && !s->gate_closed?1.f:0.f,step=sf_fade_step(s);
    float lev=(float)s->level*(1.f/500.f);
    if(!s->synth_on) {
        for(unsigned i=0;i<n;i++)l[i]=0.f;
        if(r!=l)for(unsigned i=0;i<n;i++)r[i]=0.f;
        return;
    }
    /* background synthesis of the next frame */
    {
        unsigned left_blocks=(SF_H-s->hop_pos+SF_BLOCK-1u)/SF_BLOCK;
        const unsigned bw=SF_BUILD_W(s->drift),cost=SF_SYNTH_COST(bw);
        unsigned budget=sf_share(s->bdone<cost?cost-s->bdone:1u,left_blocks),spent=0;
        const int capped=left_blocks>SF_CAP_GRACE;
        while(s->sjob!=SJ_DONE) {
            unsigned w=sf_synth_weight(s,bw);
            if(spent && (spent+w/2u>budget || (capped && SF_NOW()-t0>SF_CYC_CAP)))break;
            sf_synth_chunk(s);spent+=w;
        }
        s->bdone+=spent;
    }
    /* Frames not made yet count as silence (weights 0; frames are always
     * finite: sf_init zeroes them). */
    const float *pf=s->frame[s->prev]+SF_H,*cf=s->frame[s->cur];
    const float wp=s->valid>=2u?1.f:0.f,wc=s->valid>=1u?1.f:0.f;
    float g=s->gain;unsigned pos=s->hop_pos;
    for(unsigned i=0;i<n;i++) {
        if(g<target){g+=step;if(g>target)g=target;}
        else if(g>target){g-=step;if(g<target)g=target;}
        l[i]=(wp*pf[pos+i]+wc*cf[pos+i])*(g*lev);
    }
    sf_wander(s,l,n,s->drift);
    for(unsigned i=0;i<n;i++){float y=sf_knee(l[i]);l[i]=y;if(r!=l)r[i]=y;}
    s->gain=g;s->hop_pos=pos+n;
    if(s->hop_pos>=SF_H) {
        /* hop boundary: the frame made during this hop becomes current */
        if(s->sjob!=SJ_DONE){s->late++;while(s->sjob!=SJ_DONE)sf_synth_chunk(s);}
        unsigned old=s->prev;s->prev=s->cur;s->cur=s->work;s->work=old;
        if(s->valid<2u)s->valid++;
        s->hop_pos=0;s->hops++;
        /* A closed gate keeps the hold running silently, so the next open
         * step sounds at once. */
        if(g==0.f && target==0.f && !(on && s->gate_closed)){s->synth_on=0;return;}
        s->sjob=SJ_BUILD;s->sidx=0;s->bdone=0;
    }
}
/* ---- engine switch ----------------------------------------------------- */
/* FX1 moved between Freeze (6) and Blur (7): the state is shared, so start
 * the new engine clean (controls and tables kept). */
static inline void sf_engine(struct spectral_freeze *s,unsigned engine) {
    if(s->engine==engine)return;
    sf_stop(s);
    s->engine=engine;s->has_spectrum=0;s->sjob=SJ_IDLE;s->bjob=BJ_IDLE;s->hop_pos=0;s->bidle=1;
    s->bpeak=s->bpeak_run=0.f;s->tilt_for=-1;
    for(unsigned k=0;k<SF_BINS;k++) {
        struct sf_bin *e=&s->bin[k];
        e->amp=e->mag=e->mi=0.f;e->pr=1.f;e->pi=0.f;e->qr=(k&1u)?-1.f:1.f;e->qi=0.f;e->ur=1.f;e->ui=0.f;
    }
    for(unsigned f=0;f<3u;f++)for(unsigned n=0;n<SF_N;n++)s->frame[f][n]=0.f;
}

/* ---- Blur (FX1 mode 7) ------------------------------------------------- */
/* Every hop: the frame that ended at the hop boundary, a forward real FFT
 * (an N/2-point complex one, even samples real and odd imaginary, then
 * split), and per bin: the magnitude glides towards the input's with the
 * Blur time; while the input carries the bin, the output takes its phase,
 * and its advance (the phase change over a steady hop) is learnt; in the
 * tail each bin runs on by its last advance. Then the same synthesis as
 * Freeze. Blur full: the glide stops, the blurred sound
 * holds. */
static inline float sf_blur_keep(const struct spectral_freeze *s) {
    /* per-hop keep factor: tau = 20 ms .. 10 s (log), 1 at full */
    if(s->blur>=1000)return 1.f;
    float tau=.02f*sf_exp2((float)s->blur*(8.965784f*.001f));     /* 2^8.97 = 500 */
    return sf_exp2(-((float)SF_H/48000.f)/(tau*.69314718f));
}
static inline unsigned sf_blur_chunk(struct spectral_freeze *s) {
    switch(s->bjob) {
    case BJ_LOAD: {
        /* z[m] = x[2m] + i x[2m+1], bit-reversed (10 bits), into syn */
        unsigned start=s->bend-SF_N,end=s->sidx+SF_BLOAD_CHUNK;
        float *z=s->syn;const float *w=s->win;
        for(unsigned m=s->sidx;m<end;m++) {
            float *d=z+s->rev[m];   /* = 2*rev10(m) */
            d[0]=s->ring[(start+2u*m)&(SF_RING-1u)]*w[2u*m];
            d[1]=s->ring[(start+2u*m+1u)&(SF_RING-1u)]*w[2u*m+1u];
        }
        s->sidx=end;if(end>=SF_M){s->sidx=0;s->sfft.stage=0;s->sfft.b=0;s->bjob=BJ_FFT;}
        return 1;}
    case BJ_FFT:
        if(sf_fft_run(s,s->syn,&s->sfft,1,0,SF_LOG2N-1u)){s->bjob=BJ_SPLIT;s->sidx=0;}
        return 1;
    case BJ_SPLIT: {
        /* X[k] = E + e^{-2 pi i k/N} O, E = (Z[k] + conj Z[M-k])/2,
         * O = (Z[k] - conj Z[M-k])/(2i), k = 0..N/2 (Z[M] = Z[0]) */
        const float *z=s->syn,*tw=s->tw;
        const float keep=sf_blur_keep(s),take=1.f-keep;
        if(s->sidx<SF_LO)s->sidx=SF_LO;
        unsigned end=s->sidx+SF_SPLIT_CHUNK;if(end>SF_HI)end=SF_HI;
        float peak=s->bpeak_run;
        for(unsigned k=s->sidx;k<end;k++) {
            unsigned a=k&(SF_M-1u),b=(SF_M-k)&(SF_M-1u);
            float zr=z[2*a],zi=z[2*a+1],cr=z[2*b],ci=-z[2*b+1];
            float er=.5f*(zr+cr),ei=.5f*(zi+ci),dr=zr-cr,di=zi-ci;
            float orr=.5f*di,oi=-.5f*dr;                     /* (d)/(2i) */
            float wr,wi;                                     /* e^{-2 pi i k/N} */
            if(k<SF_M){wr=tw[2*k];wi=-tw[2*k+1];}else{wr=-1.f;wi=0.f;}
            float xr=er+orr*wr-oi*wi,xi=ei+orr*wi+oi*wr;
            struct sf_bin *e=&s->bin[k];
            float m2=xr*xr+xi*xi,mag=0.f;
            if(m2>1e-24f) {
                float r=sf_rsqrt(m2);mag=m2*r;
                float ur=xr*r,ui=xi*r;
                /* the input's advance over the hop, u conj(u_prev), learnt only
                 * from steady frames (within 1.25x of the last one); then the
                 * phase locking below gives every bin its peak's advance, so a
                 * tail's partials turn as one (a stopping note still turns the
                 * side bins of its frame differently) */
                if((mag>=.8f*e->mi && mag<=1.25f*e->mi) || e->mag<1e-9f){e->qr=ur*e->ur+ui*e->ui;e->qi=ui*e->ur-ur*e->ui;}
                e->ur=ur;e->ui=ui;
                /* while the input carries this bin, the output takes its phase
                 * (the bins of a partial stay in step, as at the input); in
                 * the tail it runs on by its last advance (synthesis) */
                if(mag>=.25f*e->mag){e->pr=ur;e->pi=ui;}
            }
            e->mi=mag;
            e->mag=keep*e->mag+take*mag;
            e->amp=e->mag*s->tilt[k];
            if(e->mag>peak)peak=e->mag;
        }
        s->bpeak_run=peak;
        s->sidx=end;if(end>=SF_HI){s->sidx=0;s->bjob=BJ_LOCK;s->cjob=CJ_LOCK_L;s->cidx=SF_LO;}
        return 1;}
    case BJ_LOCK:
        /* Freeze's phase locking passes on the blurred magnitudes (the
         * capture job is free in Blur mode) */
        sf_capture_chunk(s);
        if(s->cjob==CJ_DONE){s->cjob=CJ_IDLE;s->bjob=BJ_SYNTH;s->sjob=SJ_BUILD;}
        return 1;
    case BJ_SYNTH:
        sf_synth_chunk(s);
        if(s->sjob==SJ_DONE)s->bjob=BJ_IDLE;
        return 1;
    }
    return 0;
}
/* G14: Blur paced by cost like Freeze (an even count left some blocks at
 * 11k instructions and others at 34k). Per hop the weights add up to
 * SF_BLUR_COST, about 15.6k a block. */
static inline unsigned sf_blur_weight(const struct spectral_freeze *s) {
    switch(s->bjob) {
    case BJ_LOAD:return 31u;
    case BJ_FFT:return sf_fft_weight(s->sfft.stage);
    case BJ_SPLIT:return 28u;
    case BJ_LOCK:return sf_cap_weight(s);
    case BJ_SYNTH:return sf_synth_weight(s,SF_BUILD_W(s->bdrift));
    }
    return 0u;
}
#define SF_BLUR_COST(bw) (8u*31u+SF_FFT_COST(4u)+SF_UNITS(SF_SPLIT_CHUNK)*28u+SF_LOCK_COST+SF_SYNTH_COST(bw))

/* One block of Blur in place (the same mono wash on both outputs). */
static inline void sf_blur_process(struct spectral_freeze *s,float *l,float *r,unsigned n) {
    if(n>SF_BLOCK)n=SF_BLOCK;
    if(n>SF_H-s->hop_pos)n=SF_H-s->hop_pos;
    if(!n || !l)return;
    if(!r)r=l;
    const uint32_t t0=SF_NOW();
    float in=0.f;
    for(unsigned i=0;i<n;i++) {
        float x=.5f*(l[i]+r[i]);s->ring[(s->wpos+i)&(SF_RING-1u)]=x;
        float a=x<0.f?-x:x;if(a>in)in=a;
    }
    s->wpos+=n;
    sf_tilt_step(s,64u);
    /* idle (nothing in and the tail gone): only the ring runs */
    if(s->bidle && in<1e-5f) {
        for(unsigned i=0;i<n;i++)l[i]=0.f;
        if(r!=l)for(unsigned i=0;i<n;i++)r[i]=0.f;
        s->hop_pos=(s->hop_pos+n)&(SF_H-1u);
        return;
    }
    if(s->bidle){s->bidle=0;s->valid=0;s->gain=0.f;}
    {
        /* the cost left over the blocks left in the hop; a unit runs while
         * at least half of it fits in this block's share (and always one) */
        unsigned left_blocks=(SF_H-s->hop_pos+SF_BLOCK-1u)/SF_BLOCK,cost=SF_BLUR_COST(SF_BUILD_W(s->bdrift)),left=s->bdone<cost?cost-s->bdone:1u;
        unsigned budget=sf_share(left,left_blocks),spent=0;
        const int capped=left_blocks>SF_CAP_GRACE;
        while(s->bjob!=BJ_IDLE) {
            unsigned w=sf_blur_weight(s);
            if(spent && (spent+w/2u>budget || (capped && SF_NOW()-t0>SF_CYC_CAP)))break;
            sf_blur_chunk(s);spent+=w;s->bdone+=w;
        }
    }
    const float *pf=s->frame[s->prev]+SF_H,*cf=s->frame[s->cur];
    const float wp=s->valid>=2u?1.f:0.f,wc=s->valid>=1u?1.f:0.f;
    const float lev=(float)s->blevel*(1.f/250.f),step=1.f/(.02f*48000.f);
    float g=s->gain;unsigned pos=s->hop_pos;
    for(unsigned i=0;i<n;i++) {
        if(g<1.f){g+=step;if(g>1.f)g=1.f;}
        l[i]=(wp*pf[pos+i]+wc*cf[pos+i])*(g*lev);
    }
    sf_wander(s,l,n,s->bdrift);
    for(unsigned i=0;i<n;i++){float y=sf_knee(l[i]);l[i]=y;if(r!=l)r[i]=y;}
    s->gain=g;s->hop_pos=pos+n;
    if(s->hop_pos>=SF_H) {
        /* hop boundary: finish late work, rotate frames, start the next hop */
        if(s->bjob!=BJ_IDLE){s->late++;while(s->bjob!=BJ_IDLE)sf_blur_chunk(s);}
        unsigned old=s->prev;s->prev=s->cur;s->cur=s->work;s->work=old;
        if(s->valid<2u)s->valid++;
        s->hop_pos=0;s->hops++;
        s->bpeak=s->bpeak_run;s->bpeak_run=0.f;
        /* the tail is gone and nothing comes in: stop */
        if(s->bpeak<1e-3f && in<1e-5f && s->blur<1000){s->bidle=1;s->valid=0;return;}
        s->bend=s->wpos;s->bjob=BJ_LOAD;s->sidx=0;s->bdone=0;
    }
}
#endif
