/* Insert filter: resonant state-variable filter (LP / BP / HP / DJ) with an
 * envelope follower. State only, no delay lines, no allocation or library
 * calls in the sample loop. Its 12 state words overlay the drive's private
 * words in the 88-byte insert sidecar (see insert_drive.h); the fields both
 * families share (enable, running_mode, dirty) are reserved here.
 * Prototype and previews: Bluebox-CFW/cobalt-insert-filter. */
#ifndef COBALT_INSERT_FILTER_H
#define COBALT_INSERT_FILTER_H
#include <stdint.h>

enum {FLT_LP=0,FLT_BP=1,FLT_HP=2,FLT_DJ=3};
#define FLT_STEP 16u         /* samples per cutoff coefficient update (0.33 ms) */
#define FLT_DJ_DEAD 0.02f    /* DJ centre dead zone (fraction of half travel) */
#define FLT_DJ_FADE 0.05f    /* dry-to-filter fade just outside the dead zone */

/* Offsets 24..87 of the sidecar. reserved_* are the drive's enable (32),
 * running_mode (64) and dirty (68): never touched through this struct. */
struct flt_state {
    float a[2];              /* 24: SVF first integrators (L, R) */
    float reserved_enable;   /* 32 */
    float b[2];              /* 36: SVF second integrators (L, R) */
    float follow;            /* 44: linked stereo envelope, linear */
    float base;              /* 48: smoothed cutoff position (knob units) */
    float g,h,k;             /* 52: cutoff coefficients and damping */
    int32_t reserved_running;/* 64 */
    uint32_t reserved_dirty; /* 68 */
    float kb;                /* 72: band-pass gain sqrt(k) */
    float level;             /* 76: smoothed output gain */
    uint32_t phase;          /* 80: samples until the next coefficient update */
    uint32_t spare;          /* 84 */
};
_Static_assert(sizeof(struct flt_state)==64,"filter state fills the drive's private words");

static inline float flt_exp2(float x) {
    int e=(int)x;if((float)e>x)e--;
    float f=x-(float)e;
    float p=1.f+f*(.69314718f+f*(.24022651f+f*(.05550411f+f*(.00961813f+f*.00133336f))));
    union{float f;uint32_t u;} v={p};v.u+=(uint32_t)e<<23;return v.f;
}
static inline float flt_clampf(float x,float lo,float hi){return x<lo?lo:x>hi?hi:x;}

/* Resonance 0..1000 -> damping k from 2.0 (Q 0.5) down to 0.04 (Q 25).
 * Band-pass gain sqrt(k) sits between equal-peak and equal-skirt levels. */
static inline void flt_resonance(struct flt_state *f,int resonance) {
    float k=2.f*flt_exp2(-5.643856f*(float)resonance*.001f);
    float kb=k*.5f+.5f;
    kb=.5f*(kb+k/kb);kb=.5f*(kb+k/kb);kb=.5f*(kb+k/kb);
    f->k=k;f->kb=kb;
}

/* DJ: side and dry/filter fade for a smoothed knob position. */
static inline float flt_dj_wet(float position,unsigned *high) {
    float d=(position-500.f)*(1.f/500.f),u=(d<0.f?-d:d)-FLT_DJ_DEAD;
    *high=d>0.f;
    if(u<=0.f)return 0.f;
    u*=1.f/(1.f-FLT_DJ_DEAD);
    return u<FLT_DJ_FADE?u*(1.f/FLT_DJ_FADE):1.f;
}

/* Cutoff for the smoothed position plus follower octaves;
 * g = tan(pi*hz/48000) via the synth's rational prewarp. */
static inline void flt_cutoff(struct flt_state *f,unsigned type,float octaves) {
    float position=f->base,hz;
    if(type!=FLT_DJ) {
        hz=20.f*flt_exp2(position*.009965784f+octaves); /* 20 Hz * 1000^(p/1000) */
    } else {
        float d=(position-500.f)*(1.f/500.f),u=(d<0.f?-d:d)-FLT_DJ_DEAD;
        if(u<=0.f) hz=d>0.f?20.f:20000.f;
        else {
            u*=1.f/(1.f-FLT_DJ_DEAD);
            /* Left: low-pass 20 kHz -> 50 Hz. Right: high-pass 20 Hz -> 10 kHz. */
            hz=d<0.f?20000.f*flt_exp2(u*-8.643856f+octaves):20.f*flt_exp2(u*8.965784f+octaves);
        }
    }
    hz=flt_clampf(hz,20.f,20000.f);
    float x=hz*(3.14159265358979f/48000.f),xx=x*x;
    float g=x*(105.f-10.f*xx)/(105.f-45.f*xx+xx*xx);
    f->g=g;f->h=1.f/(1.f+f->k*g+g*g);
}

/* Fresh filter state, already at the requested cutoff and output level. */
static inline void flt_init(struct flt_state *f,unsigned type,int cutoff,int resonance,int output) {
    f->a[0]=f->a[1]=f->b[0]=f->b[1]=0.f;f->follow=0.f;
    f->base=(float)cutoff;flt_resonance(f,resonance);flt_cutoff(f,type,0.f);
    f->level=flt_exp2((float)output/6020.599913f);f->phase=FLT_STEP;f->spare=0;
}
static inline void flt_quiet(struct flt_state *f) {
    f->a[0]=f->a[1]=f->b[0]=f->b[1]=0.f;f->follow=0.f;
}

/* Soft knee above full scale: unity below 1, approaching 2 at extreme peaks. */
static inline float flt_knee(float y) {
    float a=y<0.f?-y:y;
    if(a<=1.f)return y;
    float over=a-1.f,v=1.f+over/(1.f+over);
    return y<0.f?-v:v;
}

/* One channel through the TPT SVF (Zavalishin); out selects LP, BP or HP. */
#define FLT_TICK(x,a,b,y) do { \
    float hp_=((x)-(k+g)*(a)-(b))*h,bp_=g*hp_+(a); \
    (a)=g*hp_+bp_; \
    float lp_=g*bp_+(b); \
    (b)=g*bp_+lp_; \
    (y)=out==FLT_BP?bp_*kb:out==FLT_HP?hp_:lp_; \
} while(0)

/* enable/enable_step: the family crossfade (dry <-> filter), ramped per sample.
 * Constant type/stereo per call: the compiler specialises each loop. */
static inline __attribute__((always_inline)) void flt_loop(struct flt_state *f,float *l,float *r,unsigned n,
        unsigned type,unsigned stereo,int cutoff,int env,int output,float enable,float enable_step) {
    const float attack=1.f-.993079f,release=1.f-.999826f; /* ~3 ms / ~120 ms */
    float depth=(float)env*.006f,target=(float)cutoff;    /* +-6 octaves */
    float target_level=flt_exp2((float)output/6020.599913f);
    float follow=f->follow,level=f->level;
    float a0=f->a[0],b0=f->b[0],a1=f->a[1],b1=f->b[1];
    float g=f->g,h=f->h,k=f->k,kb=f->kb;
    unsigned phase=f->phase,high=0,dj=type==FLT_DJ;
    float wet=dj?flt_dj_wet(f->base,&high):1.f;
    unsigned out=dj?(high?FLT_HP:FLT_LP):type;
    for(unsigned i=0;i<n;i++) {
        float xl=l[i],xr=stereo?r[i]:xl;
        float peak=xl<0.f?-xl:xl;
        if(stereo){float pr=xr<0.f?-xr:xr;if(pr>peak)peak=pr;}
        follow+=(peak-follow)*(peak>follow?attack:release);
        if(!phase) {
            /* Knob smoothing (~5 ms) and follower move the cutoff together.
             * Full follower depth at about -12 dBFS, typical track level. */
            f->base+=(target-f->base)*.16f;
            float sense=follow*4.f;
            flt_cutoff(f,type,depth*(sense>1.f?1.f:sense));
            g=f->g;h=f->h;
            if(dj){wet=flt_dj_wet(f->base,&high);out=high?FLT_HP:FLT_LP;}
            phase=FLT_STEP;
        }
        phase--;
        level+=(target_level-level)*.004f;
        enable+=enable_step;
        float mixl=wet*enable;
        float yl;FLT_TICK(xl,a0,b0,yl);
        l[i]=flt_knee(xl+(yl*level-xl)*mixl);
        if(stereo) {
            float yr;FLT_TICK(xr,a1,b1,yr);
            r[i]=flt_knee(xr+(yr*level-xr)*mixl);
        }
    }
    /* Flush denormal-range state after long silence. */
    if(a0>-1e-20f && a0<1e-20f)a0=0.f;
    if(b0>-1e-20f && b0<1e-20f)b0=0.f;
    if(a1>-1e-20f && a1<1e-20f)a1=0.f;
    if(b1>-1e-20f && b1<1e-20f)b1=0.f;
    f->a[0]=a0;f->b[0]=b0;
    if(stereo){f->a[1]=a1;f->b[1]=b1;}else{f->a[1]=a0;f->b[1]=b0;}
    f->follow=follow;f->level=level;f->phase=phase;
}

static inline void flt_process(struct flt_state *f,float *l,float *r,unsigned n,unsigned type,
        int cutoff,int env,int output,float enable,float enable_to) {
    unsigned stereo=r && r!=l;
    float step=(enable_to-enable)/(float)n;
    switch(type*2u+stereo) {
    case FLT_LP*2:flt_loop(f,l,r,n,FLT_LP,0,cutoff,env,output,enable,step);break;
    case FLT_LP*2+1:flt_loop(f,l,r,n,FLT_LP,1,cutoff,env,output,enable,step);break;
    case FLT_BP*2:flt_loop(f,l,r,n,FLT_BP,0,cutoff,env,output,enable,step);break;
    case FLT_BP*2+1:flt_loop(f,l,r,n,FLT_BP,1,cutoff,env,output,enable,step);break;
    case FLT_HP*2:flt_loop(f,l,r,n,FLT_HP,0,cutoff,env,output,enable,step);break;
    case FLT_HP*2+1:flt_loop(f,l,r,n,FLT_HP,1,cutoff,env,output,enable,step);break;
    case FLT_DJ*2:flt_loop(f,l,r,n,FLT_DJ,0,cutoff,env,output,enable,step);break;
    default:flt_loop(f,l,r,n,FLT_DJ,1,cutoff,env,output,enable,step);break;
    }
}
#endif
