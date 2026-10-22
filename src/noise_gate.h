/* Original per-track linked gate. Production DSP is also compiled for host tests. */
#ifndef BB_NOISE_GATE_H
#define BB_NOISE_GATE_H
#include <stdint.h>
#define NG_ID 0x1aeu
#define NG_OFF (-96000)
struct noise_gate {
    void *owner;
    int32_t threshold;
    float level,env,fall,gain,release_ms,step;
    uint32_t hold,open;
};
static void ng_reset(struct noise_gate *g,void *owner) {
    g->owner=owner;g->threshold=NG_OFF;g->level=0.f;
    g->env=g->fall=0.f;g->gain=1.f;g->release_ms=80.f;
    g->step=1.f/(80.f*48.f);g->hold=0;g->open=0;
}
static float ng_exp2(float x) {
    int fl=(int)x;if((float)fl>x)fl--;
    float f=x-(float)fl;
    float p=1.f+f*(.6931472f+f*(.2402265f+f*(.0555041f+f*(.0096181f+f*.0013334f))));
    union {float f;uint32_t u;} v={p};v.u+=(uint32_t)fl<<23;return v.f;
}
static void ng_event(struct noise_gate *g,int value) {
    if(value<NG_OFF)value=NG_OFF;
    if(value> -12000)value=-12000;
    if(g->threshold==value)return;
    g->threshold=value;
    /* Convert only on an edit; threshold's unit is millidecibels. */
    g->level=ng_exp2((float)value*(1.f/6020.599913f));
}
static float ng_abs(float x) {return __builtin_fabsf(x);}
static float ng_peak(float x) {
    /* Bad samples cannot poison envelope/adaptive state. Leave dry samples
     * unmodified at unity; this gate is not a global corruption limiter. */
    if(!(x>=-1e12f && x<=1e12f))return 0.f;
    return ng_abs(x);
}
/* Block peak of ng_peak(x): VMAXNM over |x| ignores NaN, and any value above
 * 1e12 (or inf) falls back to the exact per-sample definition. */
static float ng_scan(const float *x,unsigned n) {
    float m=0.f;
    for(unsigned i=0;i<n;i++)m=__builtin_fmaxf(m,__builtin_fabsf(x[i]));
    if(!(m<=1e12f)) {
        m=0.f;
        for(unsigned i=0;i<n;i++){float p=ng_peak(x[i]);if(p>m)m=p;}
    }
    return m;
}
static void ng_process_peak(struct noise_gate *g,float *l,float *r,unsigned n,float peak) {
    if(!n)return;
    if(g->threshold==NG_OFF && g->gain==1.f) {
        g->env=g->fall=0.f;g->hold=g->open=0;return;
    }
    float old=g->env,c=(float)n/960.f;if(c>1.f)c=1.f;
    float env=old*(1.f-c);if(peak>env)env=peak;g->env=env;
    float dt=(float)n/48000.f;
    if(old>g->level*.25f && env<old) {
        float f=(old-env)/old;
        float smooth=(float)n/240.f;if(smooth>1.f)smooth=1.f;
        g->fall+=(f-g->fall)*smooth;
    } else if(env>old)g->fall*=.5f;
    int off=g->threshold==NG_OFF;
    if(off || env>=g->level) {g->open=1;g->hold=960u;}
    else if(g->open && env<g->level*.501187234f) {
        if(g->hold>n)g->hold-=n;
        else {
            g->hold=0;g->open=0;
            /* Relative envelope decay estimates a time constant. Abrupt endings
             * get a short VCA fade; gradual tails lengthen it, bounded60..2500ms.
             * Freeze this estimate during closing rather than following noise. */
            float f=g->fall;if(f<.00001f)f=.00001f;
            float ms=dt*1000.f/f*1.5f;
            if(ms<60.f)ms=60.f;
            if(ms>2500.f)ms=2500.f;
            g->release_ms=ms;g->step=g->gain/(ms*48.f);
        }
    } else if(g->open)g->hold=960u;
    /* Fully open at unity leaves audio untouched for the whole block. Fully
     * closed stays at zero (gain-step clamps back to 0) and writes silence. */
    float gain=g->gain;
    if(g->open && gain==1.f)return;
    if(r==l)r=0;
    if(!g->open && gain==0.f) {
        for(unsigned i=0;i<n;i++){if(l)l[i]=0.f;if(r)r[i]=0.f;}
        return;
    }
    float step=g->step;unsigned open=g->open;
    for(unsigned i=0;i<n;i++) {
        if(open) {gain+=1.f/24.f;if(gain>1.f)gain=1.f;}
        else {gain-=step;if(gain<.0000001f)gain=0.f;}
        if(gain!=1.f) {
            if(l)l[i]=gain==0.f?0.f:l[i]*gain;
            if(r)r[i]=gain==0.f?0.f:r[i]*gain;
        }
    }
    g->gain=gain;
}
/* Host/reference entry computes the same linked block peak. The M7 bridge
 * shares its pre-gate meter scan, avoiding a second active audio pass. */
static __attribute__((unused)) void ng_process(struct noise_gate *g,float *l,float *r,unsigned n) {
    float peak=0.f;
    if(!(g->threshold==NG_OFF && g->gain==1.f)) {
        float a=l?ng_scan(l,n):0.f,b=r?ng_scan(r,n):0.f;
        peak=a>b?a:b;
    }
    ng_process_peak(g,l,r,n,peak);
}
#endif
