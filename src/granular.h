/* Original bounded stereo granular delay. No Clouds code is included.
 * This header is also compiled unchanged into the host audio test harness. */
#ifndef BLUEBOX_GRANULAR_H
#define BLUEBOX_GRANULAR_H
#include <stdint.h>
#include "granular_params.h"
#define G_CAP 96000u
#define G_BYTES (G_CAP * 2u * sizeof(float))
#define G_MAGIC 0x47524e36u
/* +3 dB wet makeup. Averaging decorrelated grains (jitter, pitch) costs about
 * 4-7 dB against the input; coherent grains stay near 0 dB. Applied to the
 * output only, after the feedback tap, so loop gain is unchanged. */
#define G_MAKEUP 1.41253754f
/* Overlapping grains are averaged by the sum of their weights. With jitter or
 * pitch the grains are independent, so their power (not amplitude) adds and
 * that average loses up to ~6 dB at density 4. Their output is normalized by
 * sqrt(sum w^2) instead (only above 1, so single-grain windows keep their
 * shape). Coherent grains (no jitter, no pitch) keep the amplitude average.
 * The blend glides over 20 ms. Feedback always uses the amplitude average,
 * so loop gain and repeat decay are unchanged. */
#define G_DECOR_STEP (1.f/960.f)
static inline __attribute__((always_inline)) float g_rsqrt(float x) {
    union{float f;uint32_t u;} v={x};v.u=0x5f3759dfu-(v.u>>1);
    float r=v.f;r*=1.5f-.5f*x*r*r;r*=1.5f-.5f*x*r*r;r*=1.5f-.5f*x*r*r;
    return r;
}
struct grain { float pos, step, invlen; uint32_t age, len; };
struct granular {
    uint32_t magic;
    void *owner;
    float *buffer;
    int32_t mode, position, size, density, jitter, freeze, reverse, pitch, feedback;
    uint32_t wr, valid, rng, countdown, peak, spawned;
    float fade, fb_l, fb_r, fb_gain;
    struct grain grains[4];
    float decor; /* 0..1 blend toward power normalization (jitter/pitch set) */
};
static inline int g_clampi(int x,int lo,int hi) {return x<lo?lo:x>hi?hi:x;}
static inline float g_abs(float x) {return __builtin_fabsf(x);}
static inline float g_limit(float x) {return x>=-4.f&&x<=4.f?x:x>0.f?4.f:x<0.f?-4.f:0.f;}
static inline void g_reset(struct granular *g,void *owner,float *buffer) {
    uint32_t *p=(uint32_t *)g;
    for(unsigned i=0;i<sizeof(*g)/4;i++)p[i]=0;
    g->owner=owner;g->buffer=buffer;g->size=80;g->density=4;
    g->position=250;g->feedback=400;g->rng=0x63a41d7u;g->magic=G_MAGIC;
}
static inline void g_event(struct granular *g,unsigned id,int value) {
    switch(id) {
    case G_MODE:g->mode=value==1;break;
    case G_POS:g->position=g_clampi(value,0,1000);break;
    case G_SIZE:g->size=g_clampi(value,20,250);break;
    case G_DENSITY:g->density=g_clampi(value,1,4);break;
    case G_JITTER:g->jitter=g_clampi(value,0,1000);break;
    case G_FREEZE:g->freeze=value!=0;break;
    case G_REVERSE:g->reverse=value!=0;break;
    case 0x4a:g->pitch=g_clampi(value,-12,12);break;
    case 0x39:g->feedback=g_clampi(value,0,950);break;
    default:break;
    }
}
static inline float g_ratio(int semitones) {
    /* Tiny exact lookup: one octave each way, avoiding transcendental work. */
    static const float ratios[25]={.5f,.529731547f,.561231024f,.594603558f,.629960525f,.667419927f,.707106781f,.749153538f,.793700526f,.840896415f,.890898718f,.943874313f,1.f,1.059463094f,1.122462048f,1.189207115f,1.25992105f,1.334839854f,1.414213562f,1.498307077f,1.587401052f,1.681792831f,1.781797436f,1.887748625f,2.f};
    return ratios[g_clampi(semitones,-12,12)+12];
}
static inline unsigned g_spawn(struct granular *g,struct grain *grains) {
    struct grain *p=0;
    for(unsigned j=0;j<4;j++)if(!grains[j].len){p=&grains[j];break;}
    if(!p || g->valid<128)return 1;
    float ratio=g_ratio(g->pitch);
    unsigned len=(unsigned)g->size*48u;
    /* Keep the entire grain away from both history boundaries, even when the
     * writer moves and a reversed grain moves the other way. No unwritten read. */
    unsigned maxlen=(unsigned)((float)(g->valid-16u)/(2.f*(ratio+1.f)));
    if(len>maxlen)len=maxlen;
    if(len<16)return 1;
    float travel=(ratio+1.f)*(float)len+4.f;
    float hi=(float)g->valid-travel;
    if(hi<travel)return 1;
    g->rng=g->rng*1664525u+1013904223u;
    float random=(float)(int32_t)g->rng*(1.f/2147483648.f);
    float frac=(float)g->position*.001f+random*(float)g->jitter*.0005f;
    if(frac<0.f)frac=0.f;
    if(frac>1.f)frac=1.f;
    float back=travel+(hi-travel)*frac;
    p->pos=(float)g->wr-back;
    if(p->pos<0.f)p->pos+=(float)G_CAP;
    if(p->pos>=(float)G_CAP)p->pos-=(float)G_CAP;
    p->step=g->reverse?-ratio:ratio;p->age=0;p->len=len;p->invlen=1.f/(float)len;
    g->spawned++;
    unsigned interval=len/(unsigned)g->density;
    return interval?interval:1;
}
/* Call with input copied BEFORE the normal delay mutates its bus. Normal is
 * always computed too; crossfading keeps its native tails and event handling.
 * Hot state is block-local: history/output stores cannot alias it, so it stays
 * in registers. The capture writer and grains are published before a spawn. */
/* Rendered by g_render below with blend as a compile-time constant, so the
 * coherent copy carries none of the power-normalization work. */
static inline __attribute__((always_inline)) void g_render_body(struct granular *g,const float *in_l,const float *in_r,float *out_l,float *out_r,unsigned n,const int blend) {
    float *buffer=g->buffer;
    if(!buffer)return;
    float target=g->mode?1.f:0.f;
    float target_fb=(float)g->feedback*.001f;
    float dfb=n?(target_fb-g->fb_gain)/(float)n:0.f;
    uint32_t wr=g->wr,valid=g->valid,countdown=g->countdown,peak=g->peak;
    int freeze=g->freeze;
    float fade=g->fade,fb_gain=g->fb_gain,fb_l=g->fb_l,fb_r=g->fb_r;
    struct grain grains[4];
    for(unsigned j=0;j<4;j++)grains[j]=g->grains[j];
    float decor=g->decor,decor_target=g->jitter>0 || g->pitch!=0?1.f:0.f;
    if(!(decor>=0.f && decor<=1.f))decor=0.f;
    for(unsigned i=0;i<n;i++) {
        int active=g->mode||fade>0.f;
        fb_gain+=dfb;
        if(!active||!freeze) {
            if(active) {
                /* Bounded soft feedback x/(1+|x|), one shared reciprocal:
                 * both denominators are at least 1. */
                float fbl=fb_l*fb_gain,fbr=fb_r*fb_gain;
                float dl=1.f+g_abs(fbl),dr=1.f+g_abs(fbr),inv=1.f/(dl*dr);
                buffer[2*wr]=g_limit(in_l[i]+fbl*dr*inv);
                buffer[2*wr+1]=g_limit(in_r[i]+fbr*dl*inv);
            } else {
                /* Normal priming: zero feedback, so 0/(1+0) adds exactly +0. */
                buffer[2*wr]=g_limit(in_l[i]+0.f);
                buffer[2*wr+1]=g_limit(in_r[i]+0.f);
            }
            if(++wr==G_CAP)wr=0;
            if(valid<G_CAP)valid++;
        }
        if(!active) {
            for(unsigned j=0;j<4;j++)grains[j].len=0;
            countdown=0;fb_l=fb_r=0.f;
            continue;
        }
        if(!countdown){g->wr=wr;g->valid=valid;countdown=g_spawn(g,grains);}
        countdown--;
        float l=0.f,r=0.f,weights=0.f,power=0.f;
        unsigned alive=0;
        for(unsigned j=0;j<4;j++) {
            struct grain *p=&grains[j];
            if(!p->len)continue;
            alive++;
            unsigned a=(unsigned)p->pos,b=a+1u;
            if(a>=G_CAP){p->len=0;continue;}
            if(b==G_CAP)b=0;
            unsigned aa=wr>a?wr-a:wr+G_CAP-a;
            unsigned ab=wr>b?wr-b:wr+G_CAP-b;
            if(aa>valid||ab>valid){p->len=0;continue;}
            float f=p->pos-(float)a,t=(float)p->age*p->invlen;
            float w=4.f*t*(1.f-t); /* smooth, zero-ended parabolic window */
            const float *pa=buffer+2*a,*pb=buffer+2*b;
            l+=(pa[0]+(pb[0]-pa[0])*f)*w;
            r+=(pa[1]+(pb[1]-pa[1])*f)*w;
            weights+=w;
            if(blend)power+=w*w;
            p->pos+=p->step;
            if(p->pos>=(float)G_CAP)p->pos-=(float)G_CAP;
            if(p->pos<0.f)p->pos+=(float)G_CAP;
            if(++p->age>=p->len)p->len=0;
        }
        if(alive>peak)peak=alive;
        if(weights>1.f){float iw=1.f/weights;l*=iw;r*=iw;}
        fb_l=l;fb_r=r;
        if(blend) {
            if(decor<decor_target){decor+=G_DECOR_STEP;if(decor>decor_target)decor=decor_target;}
            else if(decor>decor_target){decor-=G_DECOR_STEP;if(decor<decor_target)decor=decor_target;}
            if(decor>0.f && weights>1.f) {
                /* l and r already carry 1/weights. Rescale toward the power
                 * normalization 1/max(1,sqrt(power)): no per-sample division. */
                float e=power>1.f?g_rsqrt(power):1.f;
                float gain=1.f+decor*(e*weights-1.f);
                l*=gain;r*=gain;
            }
        }
        l*=G_MAKEUP;r*=G_MAKEUP;
        if(fade<target){fade+=1.f/240.f;if(fade>target)fade=target;}
        else if(fade>target){fade-=1.f/240.f;if(fade<target)fade=target;}
        if(fade==1.f){out_l[i]=l;out_r[i]=r;}
        else {out_l[i]+=(l-out_l[i])*fade;out_r[i]+=(r-out_r[i])*fade;}
    }
    g->wr=wr;g->valid=valid;g->countdown=countdown;g->peak=peak;
    g->fade=fade;g->fb_gain=fb_gain;g->fb_l=fb_l;g->fb_r=fb_r;
    for(unsigned j=0;j<4;j++)g->grains[j]=grains[j];
    g->decor=decor;
}
static inline void g_render(struct granular *g,const float *in_l,const float *in_r,float *out_l,float *out_r,unsigned n) {
    /* Coherent settings with the blend at rest (decor 0, no jitter, no
     * pitch) take a copy without the power work: exp.6 cost at jitter 0. */
    float decor=g->decor;
    if((decor>0.f && decor<=1.f) || g->jitter>0 || g->pitch!=0)g_render_body(g,in_l,in_r,out_l,out_r,n,1);
    else g_render_body(g,in_l,in_r,out_l,out_r,n,0);
}
#endif
