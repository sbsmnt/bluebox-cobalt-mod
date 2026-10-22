/* Original lightweight, tape-inspired master voicing. Not a physical tape
 * calibration. Stereo-linked treble compression, per-channel filters/ADAA.
 * Fixed 48kHz; no allocation or library transcendentals in the audio path. */
#ifndef BLUEBOX_MASTER_TAPE_H
#define BLUEBOX_MASTER_TAPE_H
#include <stdint.h>
#include "tape_params.h"
#define MT_MAGIC 0x54505832u
struct master_tape {
    uint32_t magic;
    int32_t mode,tone,output;
    float blend,tone_ctl,output_gain,drive_ctl,envelope;
    float channel[2][8];
    float hf_inv; /* last 1/(.1+hf_scale*envelope), refined per sample */
};
_Static_assert(sizeof(struct master_tape)<=128,"Tape fits isolated 0x980..0x9ff gap");
static inline float mt_abs(float x){return __builtin_fabsf(x);}
static inline float mt_exp2(float x) {
    int e=(int)x;if((float)e>x)e--;
    float f=x-(float)e;
    float p=1.f+f*(.69314718f+f*(.24022651f+f*(.05550411f+f*(.00961813f+f*.00133336f))));
    union{float f;uint32_t u;} v={p};v.u+=(uint32_t)e<<23;return v.f;
}
static inline void mt_init(struct master_tape *t) {
    t->mode=0;t->tone=500;t->output=0;t->blend=0.f;t->tone_ctl=.5f;
    t->output_gain=1.f;t->drive_ctl=0.f;t->envelope=0.f;t->hf_inv=10.f;
    for(unsigned c=0;c<2;c++)for(unsigned i=0;i<8;i++)t->channel[c][i]=0.f;
    t->magic=MT_MAGIC;
}
static inline void mt_event(struct master_tape *t,unsigned id,int value) {
    if(id==MT_MODE)t->mode=value==1?1:0;
    if(id==MT_TONE)t->tone=value<0?0:value>1000?1000:value;
    if(id==MT_OUTPUT)t->output=value<-12000?-12000:value>6000?6000:value;
}
static inline float mt_curve(float x) {
    if(x>1.f)return 2.f/3.f;
    if(x< -1.f)return -2.f/3.f;
    return x-x*x*x*(1.f/3.f);
}
static inline __attribute__((always_inline)) float mt_integral(float x) {
    float a=mt_abs(x);return a>1.f?a*(2.f/3.f)-.25f:x*x*(.5f-x*x*(1.f/12.f));
}
static inline __attribute__((always_inline)) float mt_adaa1(float x,float old) {
    float delta=x-old;
    if(x>=1.f && old>=1.f)return 2.f/3.f;
    if(x<=-1.f && old<=-1.f)return -2.f/3.f;
    if(mt_abs(delta)>.002f)return (mt_integral(x)-mt_integral(old))/delta;
    return mt_curve((x+old)*.5f);
}
static inline float mt_adaa(float x,float *previous) {
    float y=mt_adaa1(x,*previous);*previous=x;return y;
}
/* Smoothed block coefficients; render uses exactly the same production core
 * in host and ARM tests. The envelope is linked so stereo brightness moves
 * together, while no signal is mixed between channels. */
struct mt_block {float g,k,loss,emphasis,head,hf_scale,playback;};
static inline struct mt_block mt_prepare(struct master_tape *t,float d,unsigned n) {
    float smooth=(float)n*(1.f/960.f);if(smooth>1.f)smooth=1.f;
    t->drive_ctl+=(d-t->drive_ctl)*smooth;
    t->tone_ctl+=((float)t->tone*.001f-t->tone_ctl)*smooth;
    float g=mt_exp2(4.5f*t->drive_ctl);
    float k=.2f/mt_curve(g*.2f);
    float hz=5000.f+13000.f*t->tone_ctl;
    struct mt_block b={g,k,1.f-mt_exp2(-9.06472028f*hz/48000.f),.8f*t->drive_ctl,.25f*t->drive_ctl,1.f+2.2f*t->drive_ctl,1.f/(1.f+.8f*t->drive_ctl)};
    return b;
}
/* Record emphasis, scaled into the nonlinear stage. */
static inline __attribute__((always_inline)) float mt_pre(float *s,struct mt_block b,float x) {
    s[0]+=.279095f*(x-s[0]);float shaped=x+b.emphasis*(x-s[0]);
    return shaped*b.g;
}
/* Reciprocal playback emphasis, linked treble loss, bass lift and DC removal. */
static inline __attribute__((always_inline)) float mt_post(float *s,struct mt_block b,float y,float hf_gain) {
    y*=b.k;
    s[1]+=.279095f*(y-s[1]);y=s[1]+(y-s[1])*b.playback;
    s[2]+=b.loss*(y-s[2]);y=s[2]+(y-s[2])*hf_gain;
    /* Modest band-limited bass lift, not a claimed calibrated head model. */
    s[3]+=.015585f*(y-s[3]);s[4]+=.005222f*(y-s[4]);y+=b.head*(s[3]-s[4]);
    /* Remove DC below approximately 12Hz. */
    float dc=y-s[6]+.99843f*s[7];s[6]=y;s[7]=dc;
    return dc;
}
static inline __attribute__((always_inline)) float mt_channel(struct master_tape *t,struct mt_block b,unsigned c,float x,float hf_gain) {
    float *s=t->channel[c];
    if(!(x>=-16.f && x<=16.f))x=0.f;
    return mt_post(s,b,mt_adaa(mt_pre(s,b,x),s+5),hf_gain);
}
/* Stereo ADAA for the settled Tape loop. The previous sample's antiderivative
 * is carried in il and ir (computed once per block from the stored input), so
 * each sample evaluates one antiderivative per channel instead of two. When
 * both channels need the quotient, one reciprocal replaces two divisions:
 * 1/dl = dr/(dl*dr). Each |delta| > .002, so the product stays normal. */
static inline __attribute__((always_inline)) unsigned mt_adaa_kind(float x,float old,float delta,float *y) {
    if(__builtin_fminf(x,old)>=1.f){*y=2.f/3.f;return 0;}
    if(__builtin_fmaxf(x,old)<=-1.f){*y=-2.f/3.f;return 0;}
    if(mt_abs(delta)>.002f)return 1;
    *y=mt_curve((x+old)*.5f);return 0;
}
static inline __attribute__((always_inline)) void mt_adaa2(float *yl,float *yr,float xl,float xr,float *pl,float *pr,float *il,float *ir) {
    float dl=xl-*pl,dr=xr-*pr,nl=mt_integral(xl),nr=mt_integral(xr);
    unsigned ql=mt_adaa_kind(xl,*pl,dl,yl),qr=mt_adaa_kind(xr,*pr,dr,yr);
    if(ql && qr) {
        float inv=1.f/(dl*dr);
        *yl=(nl-*il)*dr*inv;*yr=(nr-*ir)*dl*inv;
    } else {
        if(ql)*yl=(nl-*il)/dl;
        if(qr)*yr=(nr-*ir)/dr;
    }
    *pl=xl;*pr=xr;*il=nl;*ir=nr;
}
static inline float mt_hf_gain(struct master_tape *t,struct mt_block b,float l,float r) {
    float peak=mt_abs(l);if(mt_abs(r)>peak)peak=mt_abs(r);
    if(!(peak>=0.f && peak<=16.f))peak=0.f;
    float a=peak>t->envelope?.00416f:.000416f;
    t->envelope+=a*(peak-t->envelope);
    return (.1f+t->envelope)/(.1f+b.hf_scale*t->envelope);
}
/* Same gain without a per-sample division. The denominator moves slowly, so
 * the previous reciprocal is refined with r*(1+e+e*e), e=1-den*r (relative
 * error e^3). Large jumps, cold state or a nonfinite value divide exactly. */
static inline __attribute__((always_inline)) float mt_hf_gain_fast(float *envelope,float *inv,struct mt_block b,float l,float r) {
    /* Callers pass inputs already limited to finite |x|<=16. */
    float peak=__builtin_fmaxf(mt_abs(l),mt_abs(r));
    float a=peak>*envelope?.00416f:.000416f;
    *envelope+=a*(peak-*envelope);
    float den=.1f+b.hf_scale**envelope,q=*inv,e=1.f-den*q;
    if(mt_abs(e)<=.015625f)q+=q*(e+e*e);
    else q=1.f/den;
    *inv=q;
    return (.1f+*envelope)*q;
}
/* Settled full-Tape block (blend==1). Block-local state: output stores cannot
 * alias it, so filter histories stay in registers instead of reloading every
 * sample. Shared by the firmware (-O2 unit) and host tests. */
static inline __attribute__((always_inline)) void mt_render_settled(struct master_tape *t,struct mt_block b,float *l,float *r,unsigned n,float mix,float dm,unsigned full_wet,unsigned unity_trim,unsigned drive,float trim,float off_trim_step) {
    float cl[8],cr[8],env=t->envelope,inv=t->hf_inv,gain=t->output_gain;
    for(unsigned j=0;j<8;j++){cl[j]=t->channel[0][j];cr[j]=t->channel[1][j];}
    float il=mt_integral(cl[5]),ir=mt_integral(cr[5]);
    for(unsigned i=0;i<n;i++) {
        float dl=l[i],dr=r[i];
        if(!(mt_abs(dl)<=16.f))dl=0.f; /* also rejects NaN/inf */
        if(!(mt_abs(dr)<=16.f))dr=0.f;
        float hf=mt_hf_gain_fast(&env,&inv,b,dl,dr),yl,yr;
        mt_adaa2(&yl,&yr,mt_pre(cl,b,dl),mt_pre(cr,b,dr),cl+5,cr+5,&il,&ir);
        float tl=mt_post(cl,b,yl,hf),tr=mt_post(cr,b,yr,hf);
        if(!full_wet){mix+=dm;tl=dl+mix*(tl-dl);tr=dr+mix*(tr-dr);}
        if(!unity_trim) {
            gain+=drive?(trim-gain)*(1.f/960.f):off_trim_step;
            if(mt_abs(trim-gain)<.000001f)gain=trim;
            tl*=gain;tr*=gain;
        }
        l[i]=tl;if(r!=l)r[i]=tr;
    }
    for(unsigned j=0;j<8;j++){t->channel[0][j]=cl[j];t->channel[1][j]=cr[j];}
    t->envelope=env;t->hf_inv=inv;t->output_gain=gain;
}
#endif
