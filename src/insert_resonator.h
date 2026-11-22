/* Insert resonator bank (SPEC group): HARM, CHRD, BELL. Eight tuned
 * two-pole resonators fed by the track's mid signal ring at a root note
 * times a ratio set; even ones sit 70/30 left, odd ones 30/70 right.
 *   Drive = Pitch: root C1..C7 in semitone steps (glides ~25 ms)
 *   Tone  = Decay: T60 50 ms .. 12 s (log); higher partials decay sooner
 *   Mix   = dry/wet
 * Resonator gain halfway (geometric mean) between unit white-noise and unit
 * resonance-peak gain with a decay make-up (broadband input stays near its
 * dry loudness, a sustained tone on a partial is not tens of dB hotter),
 * 1/sqrt(8) for the bank, and a smooth peak-hold wet limiter at 0.5 with a
 * soft saturator: no gain steps. F5: F4's instant-attack limiter worked
 * 30-46 dB deep on sustained or distorted low input and crackled at full
 * wet. No FFT, no delay lines. State: one 192-byte slot of the SDRAM
 * bank (shared with the shifter: one type runs per track). Prototype,
 * checks and previews: Bluebox-CFW/cobalt-insert-resonator. */
#ifndef COBALT_INSERT_RESONATOR_H
#define COBALT_INSERT_RESONATOR_H
#include <stdint.h>

enum {RSN_HARM=0,RSN_CHRD=1,RSN_BELL=2};
#define RSN_N 8
#define RSN_LOW_NOTE 24.f      /* C1 */
#define RSN_SEMIS 72.f         /* to C7 */

struct rsn_state {
    float y1[RSN_N],y2[RSN_N];             /* 0, 32: resonator states */
    float a1[RSN_N],a2[RSN_N],g[RSN_N];    /* 64: coefficients for coef_* */
    float note;                            /* 160: smoothed root (MIDI) */
    float wet,level;                       /* 164: mix, output gain */
    float env,gain;                        /* 172: wet limiter peak envelope and smoothed gain */
    int32_t coef_key;                      /* 180: type << 16 | decay the coefficients are for */
    float coef_note;                       /* 184 */
    uint32_t hold;                         /* 188: limiter peak-hold samples left */
};
_Static_assert(sizeof(struct rsn_state)==192,"resonator state fits a shifter bank slot");

/* Rows 0 HARM, 1 CHRD (MIN9), 2 BELL; F6: 3..9 CHRD's other chords
 * (rsn_row). Chord rows are semitones over the root: 2^(n/12). */
#define RSN_ROWS 10u

static const float rsn_ratio[RSN_ROWS][RSN_N]={
    {1.f,2.f,3.f,4.f,5.f,6.f,7.f,8.f},
    {1.f,1.189207f,1.498307f,1.781797f,2.f,2.244924f,2.996614f,4.f},       /* MIN9: 0 3 7 10 12 14 19 24 */
    {.5f,1.f,1.189207f,1.498307f,2.f,2.514f,2.662f,3.011f},                /* hum prime m3 5 nominal ... */
    {1.f,1.259921f,1.498307f,1.887749f,2.f,2.244924f,2.996614f,4.f},       /* MAJ9: 0 4 7 11 12 14 19 24 */
    {1.f,1.189207f,1.498307f,1.781797f,2.f,2.378414f,2.996614f,3.563595f}, /* MIN7: 0 3 7 10 12 15 19 22 */
    {1.f,1.259921f,1.498307f,1.887749f,2.f,2.519842f,2.996614f,3.775497f}, /* MAJ7: 0 4 7 11 12 16 19 23 */
    {1.f,1.122462f,1.334840f,1.498307f,2.f,2.244924f,2.669680f,2.996614f}, /* SUS: 0 2 5 7 12 14 17 19 */
    {1.f,1.498307f,2.f,2.996614f,4.f,5.993228f,8.f,11.986456f},             /* 5THS: 0 7 12 19 24 31 36 43 */
    {1.f,1.334840f,1.781797f,2.378414f,3.174802f,4.237852f,5.656854f,7.550995f}, /* 4THS: 0 5 10 .. 35 */
    {1.f,1.189207f,1.414214f,1.681793f,2.f,2.378414f,2.828427f,3.363586f}}; /* DIM: 0 3 6 9 12 15 18 21 */

/* The ratio row for a resonator type (0 HARM, 1 CHRD, 2 BELL) and chord. */
static inline unsigned rsn_row(unsigned type,unsigned chord) {
    return type==1u && chord>0u && chord<8u?2u+chord:type;
}

static inline float rsn_exp2(float x) {
    int e=(int)x;if((float)e>x)e--;
    float f=x-(float)e;
    float p=1.f+f*(.69314718f+f*(.24022651f+f*(.05550411f+f*(.00961813f+f*.00133336f))));
    union{float f;uint32_t u;} v={p};v.u+=(uint32_t)e<<23;return v.f;
}
static inline float rsn_log2(float x) {   /* x > 0 */
    union{float f;uint32_t u;} v={x};float e=(float)(int)((v.u>>23)&255u)-127.f;
    v.u=(v.u&0x007fffffu)|0x3f800000u;float m=v.f-1.f;
    return e+m*(1.4425449f+m*(-.7181452f+m*(.4575485f+m*(-.2779042f+m*.0958096f))));
}
static inline float rsn_cos(float x) {    /* 0 <= x <= pi */
    float s=0.f;if(x>1.5707963f){x=3.14159265f-x;s=1.f;}
    float xx=x*x,c=1.f-xx*.5f*(1.f-xx*(1.f/12.f)*(1.f-xx*(1.f/30.f)*(1.f-xx*(1.f/56.f))));
    return s?-c:c;
}
static inline float rsn_rsqrt(float x) {
    union{float f;uint32_t u;} v={x};v.u=0x5f3759dfu-(v.u>>1);
    float y=v.f;y*=1.5f-.5f*x*y*y;y*=1.5f-.5f*x*y*y;y*=1.5f-.5f*x*y*y;return y;
}
/* sqrt without libm: the FPU instruction on the device (the code caves
 * link no sqrtf), the compiler's elsewhere. */
static inline float rsn_sqrt(float x) {
#if defined(__arm__) || defined(__thumb__)
    __asm__("vsqrt.f32 %0, %1" : "=t"(x) : "t"(x));return x;
#else
    return __builtin_sqrtf(x);
#endif
}
/* Wet soft saturation: linear to 0.6, then smoothly towards 1.0 (C1 at the
 * corner). Normal operation stays below 0.6 (limiter target 0.5); it only
 * rounds the overshoot of a sudden hit before the limiter has pulled down. */
static inline float rsn_wet_sat(float y) {
    float a=__builtin_fabsf(y);
    if(a<=.6f)return y;
    float o=a-.6f,v=.6f+o/(1.f+o*2.5f);
    return y<0.f?-v:v;
}
static inline float rsn_knee(float y) {
    float a=__builtin_fabsf(y);
    if(a<=1.f)return y;
    float over=a-1.f,v=1.f+over/(1.f+over);
    return y<0.f?-v:v;
}
/* Pitch knob -> root note, semitone steps. */
static inline float rsn_knob_note(int pitch) {
    int semi=(int)((float)pitch*(RSN_SEMIS/1000.f)+.5f);
    return RSN_LOW_NOTE+(float)semi;
}
/* Decay knob -> T60 seconds, 0.05 * 240^(d/1000). */
static inline float rsn_knob_t60(int decay) {return .05f*rsn_exp2((float)decay*(7.9068906f*.001f));}

static inline void rsn_quiet(struct rsn_state *s) {
    for(int i=0;i<RSN_N;i++)s->y1[i]=s->y2[i]=0.f;
    s->env=0.f;s->gain=1.f;s->hold=0;
}
static inline void rsn_init(struct rsn_state *s,int pitch,int mix,int output) {
    rsn_quiet(s);
    for(int i=0;i<RSN_N;i++)s->a1[i]=s->a2[i]=s->g[i]=0.f;
    s->note=rsn_knob_note(pitch);
    s->wet=(float)mix*.001f;s->level=rsn_exp2((float)output/6020.599913f);
    s->coef_key=-1;s->coef_note=-1.f;
}

static __attribute__((noinline)) void rsn_coefficients(struct rsn_state *s,unsigned type,int decay) {
    float t60=rsn_knob_t60(decay);
    float root=440.f*rsn_exp2((s->note-69.f)*(1.f/12.f));
    for(int i=0;i<RSN_N;i++) {
        float ratio=rsn_ratio[type][i],hz=root*ratio;
        if(hz>21000.f){s->g[i]=s->a1[i]=s->a2[i]=0.f;continue;}
        float t=t60/(1.f+.12f*(ratio-1.f));
        /* r = 10^(-3/(t*48000)) = e^y, |y| < 3e-3: a short series is exact. */
        float y=-6.9077553f/(t*48000.f),r=1.f+y*(1.f+y*(.5f+y*(1.f/6.f)));
        float c=rsn_cos(hz*(6.28318531f/48000.f)),rr=r*r,c2=2.f*c*c-1.f;
        s->a1[i]=2.f*r*c;s->a2[i]=-rr;
        float d=(1.f+rr)*(1.f+rr)-2.f*rr*(1.f+c2);
        /* Gain: geometric mean of unit white-noise gain and unit gain at
         * the resonance, so broadband input still rings while a sustained
         * tone landing on a partial is not tens of dB hotter. */
        float gn=rsn_rsqrt((1.f+rr)/((1.f-rr)*d));
        float s1=rsn_sqrt(__builtin_fmaxf(1.f-c*c,0.f)),cw2=c2,sw2=2.f*s1*c;
        float re=1.f-s->a1[i]*c-s->a2[i]*cw2,im=s->a1[i]*s1+s->a2[i]*sw2;
        float gp=rsn_sqrt(re*re+im*im);
        /* Make-up so broadband input stays near its dry loudness: the mean
         * loses ~11 dB at 50 ms and ~23 dB at 12 s, i.e. 3.55 * (T60/50 ms)^0.253. */
        s->g[i]=rsn_sqrt(gn*gp)*3.55f*rsn_exp2(.253f*rsn_log2(t*20.f));
    }
    s->coef_key=(int32_t)(type<<16|(unsigned)decay);s->coef_note=s->note;
}

/* One block. e0 -> e1 is the insert's fade (0 dry, 1 full effect). */
static __attribute__((noinline)) void rsn_process(struct rsn_state *s,float *l,float *r,unsigned n,unsigned type,
        int pitch,int decay,int mix,int output,float e0,float e1) {
    if(n>32u)n=32u;
    if(!n || !l)return;
    if(e0==0.f && e1==0.f){rsn_quiet(s);return;}
    if(type>=RSN_ROWS)type=RSN_HARM;
    unsigned stereo=r && r!=l;
    float target=rsn_knob_note(pitch);
    s->note+=(target-s->note)*.08f;
    if(s->note-target<.001f && s->note-target>-.001f)s->note=target;
    if(s->coef_key!=(int32_t)(type<<16|(unsigned)decay) || s->coef_note!=s->note)rsn_coefficients(s,type,decay);
    float k=.12f*(float)n*(1.f/32.f),inv=1.f/(float)n;
    float wet1=s->wet+((float)mix*.001f-s->wet)*k;
    float lev1=s->level+(rsn_exp2((float)output/6020.599913f)-s->level)*k;
    float dry0=1.f-e0+e0*s->level*(1.f-s->wet),dry1=1.f-e1+e1*lev1*(1.f-wet1);
    float wg0=e0*s->level*s->wet,wg1=e1*lev1*wet1;
    float dry=dry0,ddry=(dry1-dry0)*inv,wg=wg0,dwg=(wg1-wg0)*inv;
    s->wet=wet1;s->level=lev1;
    float mid[32],ev[32],od[32],env=s->env,gain=s->gain;uint32_t hold=s->hold,peaked=0;float bp=0.f;
    float want=env>.5f?.5f/env:1.f;
    if(stereo)for(unsigned i=0;i<n;i++)mid[i]=.5f*(l[i]+r[i]);
    const float *in=stereo?mid:l;
    for(int j=0;j<RSN_N;j+=4) {
        float a1=s->y1[j],a2=s->y2[j],b1=s->y1[j+1],b2=s->y2[j+1];
        float c1=s->y1[j+2],c2=s->y2[j+2],d1=s->y1[j+3],d2=s->y2[j+3];
        const float kk=.5f; /* 1/sqrt(8) bank scale times sqrt(2) pan compensation */
        const float aa=s->a1[j],ab=s->a2[j],ag=s->g[j]*kk,ba=s->a1[j+1],bb=s->a2[j+1],bg=s->g[j+1]*kk;
        const float ca=s->a1[j+2],cb=s->a2[j+2],cg=s->g[j+2]*kk,da=s->a1[j+3],db=s->a2[j+3],dg=s->g[j+3]*kk;
        /* SD5: two samples per pass, the newest output written over the
         * oldest state, so no register moves (same arithmetic as one step:
         * y = g x + a1 y[n-1] + a2 y[n-2]). */
        unsigned i=0;
        for(;i+1<n;i+=2) {
            float x0=in[i],x1=in[i+1];
            a2=ag*x0+aa*a1+ab*a2;b2=bg*x0+ba*b1+bb*b2;
            c2=cg*x0+ca*c1+cb*c2;d2=dg*x0+da*d1+db*d2;
            float e0=a2+c2,o0=b2+d2;
            a1=ag*x1+aa*a2+ab*a1;b1=bg*x1+ba*b2+bb*b1;
            c1=cg*x1+ca*c2+cb*c1;d1=dg*x1+da*d2+db*d1;
            float e1=a1+c1,o1=b1+d1;
            if(j){ev[i]+=e0;od[i]+=o0;ev[i+1]+=e1;od[i+1]+=o1;}
            else{ev[i]=e0;od[i]=o0;ev[i+1]=e1;od[i+1]=o1;}
        }
        if(i<n) {
            float x=in[i];
            float ya=ag*x+aa*a1+ab*a2;a2=a1;a1=ya;
            float yb=bg*x+ba*b1+bb*b2;b2=b1;b1=yb;
            float yc=cg*x+ca*c1+cb*c2;c2=c1;c1=yc;
            float yd=dg*x+da*d1+db*d2;d2=d1;d1=yd;
            if(j){ev[i]+=ya+yc;od[i]+=yb+yd;}else{ev[i]=ya+yc;od[i]=yb+yd;}
        }
#define RSN_FLUSH(p,q) if(((p)>-1e-10f && (p)<1e-10f && (q)>-1e-10f && (q)<1e-10f) || !((p)<1e6f && (p)>-1e6f))(p)=(q)=0.f /* -200 dB */
        RSN_FLUSH(a1,a2);RSN_FLUSH(b1,b2);RSN_FLUSH(c1,c2);RSN_FLUSH(d1,d2);
#undef RSN_FLUSH
        s->y1[j]=a1;s->y2[j]=a2;s->y1[j+1]=b1;s->y2[j+1]=b2;
        s->y1[j+2]=c1;s->y2[j+2]=c2;s->y1[j+3]=d1;s->y2[j+3]=d2;
    }
    /* SD5: pan first (in place), with the block's wet peak. */
    float mx=0.f;
    for(unsigned i=0;i<n;i++) {
        float ol=.7f*ev[i]+.3f*od[i],or_=.3f*ev[i]+.7f*od[i];
        ev[i]=ol;od[i]=or_;
        mx=__builtin_fmaxf(mx,__builtin_fmaxf(__builtin_fabsf(ol),__builtin_fabsf(or_)));
    }
    if(gain==1.f && env<=.5f && mx<=.5f) {
        /* Not limiting: the general loop below would keep want = gain = 1
         * and leave the wet peaks under the saturator's 0.6 corner, so it
         * reduces exactly to this. */
        bp=mx;if(mx>env){env=mx;peaked=1u;}
        if(stereo)for(unsigned i=0;i<n;i++) {
            l[i]=rsn_knee(l[i]*dry+ev[i]*wg);r[i]=rsn_knee(r[i]*dry+od[i]*wg);
            dry+=ddry;wg+=dwg;
        }
        else for(unsigned i=0;i<n;i++){l[i]=rsn_knee(l[i]*dry+ev[i]*wg);dry+=ddry;wg+=dwg;}
    }
    else for(unsigned i=0;i<n;i++) {
        float ol=ev[i],or_=od[i];
        /* Wet limiter: the peak envelope rises at once to a new peak, holds
         * 60 ms (longer than the lowest period, so it does not ripple at the
         * note), then releases over ~300 ms (per block, below); the gain
         * follows it smoothly (~0.7 ms down, the release up), so it never
         * steps. The target needs a division only when a new peak arrives.
         * A soft saturator rounds a sudden hit's overshoot. */
        float pk=__builtin_fmaxf(__builtin_fabsf(ol),__builtin_fabsf(or_));
        bp=__builtin_fmaxf(bp,pk);
        if(pk>env){env=pk;want=env>.5f?.5f/env:1.f;peaked=1u;}
        gain+=(want-gain)*(want<gain?.03f:.0000694f);
        ol=rsn_wet_sat(ol*gain);or_=rsn_wet_sat(or_*gain);
        float xl=l[i];
        l[i]=rsn_knee(xl*dry+ol*wg);
        if(stereo)r[i]=rsn_knee(r[i]*dry+or_*wg);
        dry+=ddry;wg+=dwg;
    }
    /* Hold 60 ms after the last new peak, then release: the envelope moves
     * toward this block's peak by (1-1/14400)^n (~300 ms). */
    if(peaked)hold=2880u;
    else if(hold>n)hold-=n;
    else {
        hold=0;env=bp+(env-bp)*(1.f-.0000694f*(float)n);
    }
    s->env=env<1e-10f || !(env<1e6f)?0.f:env;s->hold=hold;
    /* SD5: the release towards unity stalls about 0.004 dB short in float;
     * close that inaudible gap so the not-limiting path can take over. */
    if(env<=.5f && gain>.9995f)gain=1.f;
    s->gain=gain>1.f || !(gain>0.f)?1.f:gain;
}
#endif
