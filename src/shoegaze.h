/* Original experimental DSP. Stereo allpass diffusion -> ADAA distortion ->
 * one four-line, modulated, damped feedback delay network. 48 kHz only.
 * No allocation, transcendental library calls, or full-buffer clears in run.
 */
#ifndef BLUEBOX_SHOEGAZE_H
#define BLUEBOX_SHOEGAZE_H
#include <stdint.h>
#include "shoegaze_params.h"
#define SG_RING 8192u
#define SG_AP 2048u
#define SG_LOOP_AP 512u
#define SG_FLOATS (4u*SG_RING+8u*SG_AP+4u*SG_LOOP_AP)
#define SG_MAGIC 0x53474732u
struct shoegaze {
    uint32_t magic,style,clearing,clear_pos,wr;
    uint32_t ap_pos[8];
    int32_t raw[7];
    float ctl[7],lp[4],hp[4],input_lp[2],input_hp[2],output_hp[2];
    float prev[2],delay[4],phase,fade,level;
    uint32_t loop_pos[4];
    float filter[4][2];
    float memory[SG_FLOATS];
};
#define SG_BYTES ((unsigned)sizeof(struct shoegaze))
static inline float sg_abs(float v){return __builtin_fabsf(v);}
static inline float sg_clamp(float x,float a,float b){return x<a?a:x>b?b:x;}
static inline float sg_finite(float x){return x>=-16.f && x<=16.f?x:0.f;}
static inline float sg_exp2(float x) {
    int e=(int)x;if((float)e>x)e--;
    float f=x-(float)e;
    float p=1.f+f*(.69314718f+f*(.24022651f+f*(.05550411f+f*(.00961813f+f*.00133336f))));
    union {float f;uint32_t u;} v={p};v.u+=(uint32_t)e<<23;return v.f;
}
static inline void sg_defaults(struct shoegaze *s) {
    static const int32_t values[]={350,600,750,250,200,5500,120};
    for(unsigned i=0;i<7;i++){s->raw[i]=values[i];s->ctl[i]=(float)values[i];}
}
static inline int sg_id(unsigned id){return id>=SG_SAT && id<=SG_LOCUT;}
static inline void sg_event(struct shoegaze *s,unsigned id,int value) {
    if(id==0x155){s->style=(uint32_t)(value>=0 && value<=SG_STYLE?value:0);return;}
    if(!sg_id(id))return;
    int lo=id==SG_RATE?10:id==SG_HICUT?500:id==SG_LOCUT?20:0;
    int hi=id==SG_RATE?2000:id==SG_HICUT?16000:id==SG_LOCUT?2000:1000;
    s->raw[id-SG_SAT]=value<lo?lo:value>hi?hi:value;
}
static inline void sg_reset_tail(struct shoegaze *s) {
    s->clearing=1;s->clear_pos=0;s->wr=0;s->fade=0.f;s->phase=0.f;
    for(unsigned i=0;i<8;i++)s->ap_pos[i]=0;
    for(unsigned i=0;i<4;i++){
        s->lp[i]=s->hp[i]=0.f;s->delay[i]=0.f;s->loop_pos[i]=0;
        s->filter[i][0]=s->filter[i][1]=0.f;
    }
    for(unsigned i=0;i<2;i++)s->input_lp[i]=s->input_hp[i]=s->output_hp[i]=s->prev[i]=0.f;
    for(unsigned i=0;i<7;i++)s->ctl[i]=(float)s->raw[i];
}
static inline void sg_init(struct shoegaze *s) {
    uint32_t *p=(uint32_t *)s;
    for(unsigned i=0;i<SG_BYTES/4;i++)p[i]=0;
    sg_defaults(s);s->level=1.f;s->magic=SG_MAGIC;
}
/* Rounded cubic clip, with its antiderivative for first-order ADAA. */
static inline float sg_curve(float x) {
    if(x>1.f)return 2.f/3.f;
    if(x< -1.f)return -2.f/3.f;
    return x-x*x*x*(1.f/3.f);
}
static inline float sg_integral(float x) {
    float a=sg_abs(x);
    return a>1.f?a*(2.f/3.f)-.25f:x*x*(.5f-x*x*(1.f/12.f));
}
static inline float sg_distort(float x,float *prev,float amount) {
    /* The upper half now reaches pedal-style fuzz even for quiet FX sends:
     * 0 dB at zero, about 7 dB at 35%, 30 dB at 70%, 60 dB at full. */
    float drive=sg_exp2(10.f*amount*amount);
    float v=x*drive,delta=v-*prev;
    float y;
    /* Same clipped branch is exactly constant. Avoid subtracting large,
     * nearly equal integrals at full fuzz drive. */
    if(v>=1.f && *prev>=1.f)y=2.f/3.f;
    else if(v<=-1.f && *prev<=-1.f)y=-2.f/3.f;
    else y=sg_abs(delta)>0.002f?(sg_integral(v)-sg_integral(*prev))/delta:sg_curve((v+*prev)*.5f);
    *prev=v;
    /* Fixed conservative makeup: increased drive changes texture without
     * multiplying the reverb loop gain. Zero is exact distortion bypass. */
    return x+amount*(y/(1.f+2.f*amount)-x);
}
/* Topology-preserving two-pole SVF. Q=1.05 gives a gentle resonant shoulder.
 * Resonance is outside the feedback loop, so it cannot raise loop gain. */
static inline float sg_filter_g(float hz) {
    float x=3.14159265f*hz/48000.f,z=x*x;
    float sn=x*(1.f+z*(-1.f/6.f+z*(1.f/120.f+z*(-1.f/5040.f+z/362880.f))));
    float cs=1.f+z*(-.5f+z*(1.f/24.f+z*(-1.f/720.f+z*(1.f/40320.f-z/3628800.f))));
    return sn/cs;
}
static inline float sg_filter(float *state,float x,float g,float a1,int high) {
    const float k=.95238095f;
    float v1=a1*(state[0]+g*(x-state[1])),v2=state[1]+g*v1;
    state[0]=2.f*v1-state[0];state[1]=2.f*v2-state[1];
    return high?x-k*v1-v2:v2;
}
static inline float sg_lfo(float p) {
    /* Smooth periodic parabola; range -1..1. No trigonometry per sample. */
    if(p>=1.f)p-=1.f;
    float t=p<.5f?p*4.f-1.f:3.f-p*4.f;
    return t*(1.5f-.5f*t*t);
}
/* Stereo ADAA after drive. When both channels need the antiderivative
 * quotient, one shared reciprocal 1/(dl*dr) replaces two divisions. */
static inline __attribute__((always_inline)) float sg_adaa1(float v,float old) {
    if(__builtin_fminf(v,old)>=1.f)return 2.f/3.f;
    if(__builtin_fmaxf(v,old)<=-1.f)return -2.f/3.f;
    float delta=v-old;
    return sg_abs(delta)>0.002f?(sg_integral(v)-sg_integral(old))/delta:sg_curve((v+old)*.5f);
}
static inline __attribute__((always_inline)) void sg_adaa2(float *yl,float *yr,float vl,float vr,float ol,float or_) {
    float dl=vl-ol,dr=vr-or_;
    unsigned ql=__builtin_fminf(vl,ol)<1.f && __builtin_fmaxf(vl,ol)>-1.f && sg_abs(dl)>0.002f;
    unsigned qr=__builtin_fminf(vr,or_)<1.f && __builtin_fmaxf(vr,or_)>-1.f && sg_abs(dr)>0.002f;
    if(ql && qr) {
        float inv=1.f/(dl*dr);
        *yl=(sg_integral(vl)-sg_integral(ol))*dr*inv;
        *yr=(sg_integral(vr)-sg_integral(or_))*dl*inv;
    } else {*yl=sg_adaa1(vl,ol);*yr=sg_adaa1(vr,or_);}
}
static inline __attribute__((always_inline)) float sg_ap(float *buf,uint32_t *pos,unsigned len,float x,float g) {
    unsigned p=*pos;
    float delayed=buf[p],y=delayed-g*x;
    buf[p]=x+g*y;*pos=p+1u==len?0u:p+1u;return y;
}
static inline __attribute__((always_inline)) float sg_tap_at(const float *buf,unsigned wr,float delay) {
    float p=(float)wr-delay;if(p<0.f)p+=(float)SG_RING;
    unsigned i=(unsigned)p;float f=p-(float)i;
    return buf[i]+(buf[(i+1u)&(SG_RING-1u)]-buf[i])*f;
}
/* Control smoothing keeps the per-sample one-pole time constant, evaluated in
 * closed form per block. Control-derived terms (drive, makeup, diffusion,
 * depth, input filter, rate) ramp linearly between the block's start and end
 * values, so no exp2 or division is needed per sample for them. */
static inline void sg_run(struct shoegaze *s,float *l,float *r,unsigned n,float level) {
    if(n>32u)n=32u;
    if(s->clearing) {
        unsigned end=s->clear_pos+1024u;if(end>SG_FLOATS)end=SG_FLOATS;
        for(unsigned i=s->clear_pos;i<end;i++)s->memory[i]=0.f;
        s->clear_pos=end;if(end==SG_FLOATS)s->clearing=0;
        for(unsigned i=0;i<n;i++)l[i]=r[i]=0.f;
        return;
    }
    if(!n)return;
    float c0[7],c1[7],decay=1.f;
    for(unsigned i=0;i<n;i++)decay*=1.f-.001041124f;
    for(unsigned k=0;k<7;k++) {
        float target=(float)s->raw[k];
        c0[k]=s->ctl[k];c1[k]=target+(c0[k]-target)*decay;
        if(sg_abs(c1[k]-target)<.01f)c1[k]=target;
        s->ctl[k]=c1[k];
    }
    float size=c0[1]*.001f,scale=.75f+.65f*size;
    float rt=3.f+8.f*size+29.f*size*size;
    static const float lengths[]={2137.f,3181.f,4001.f,4861.f};
    static const unsigned looplen[4]={173,257,349,431};
    float gh=sg_filter_g(c0[5]),gl=sg_filter_g(c0[6]);
    float ah=1.f/(1.f+gh*(gh+.95238095f)),al=1.f/(1.f+gl*(gl+.95238095f));
    float gain[4],dtarget[4];
    for(unsigned k=0;k<4;k++) {
        dtarget[k]=lengths[k]*scale;
        if(s->delay[k]==0.f)s->delay[k]=dtarget[k];
        gain[k]=sg_exp2(-9.9657843f*(s->delay[k]+(float)looplen[k])/(48000.f*rt));
    }
    static const unsigned aps[8]={887,1217,1543,1999,941,1297,1601,2039};
    /* Ramps: value for sample i is start+(i+1)*step, matching the per-sample
     * smoother, which updated before use. */
    float inv_n=1.f/(float)n,v0,v1;
    float sat0=c0[0]*.001f,sat1=c1[0]*.001f;
    float sat=sat0,dsat=(sat1-sat0)*inv_n;
    v0=sg_exp2(10.f*sat0*sat0);v1=sg_exp2(10.f*sat1*sat1);
    float drive=v0,ddrive=(v1-v0)*inv_n;
    v0=1.f/(1.f+2.f*sat0);v1=1.f/(1.f+2.f*sat1);
    float makeup=v0,dmakeup=(v1-v0)*inv_n;
    float diff=c0[2]*.001f,ddiff=(c1[2]-c0[2])*.001f*inv_n;
    float m0=c0[3]*.001f,m1=c1[3]*.001f;
    v0=72.f*m0+12.f*m0*m0*m0;v1=72.f*m1+12.f*m1*m1*m1;
    float depth=v0,ddepth=(v1-v0)*inv_n;
    v0=6.2831853f*c0[5]/48000.f;v0=v0/(1.f+v0);
    v1=6.2831853f*c1[5]/48000.f;v1=v1/(1.f+v1);
    float hi=v0,dhi=(v1-v0)*inv_n;
    float rate=c0[4]/48000000.f,drate=(c1[4]-c0[4])/48000000.f*inv_n;
    unsigned fuzz=sat0!=0.f || sat1!=0.f;
    /* Block-local state: tank/history stores cannot alias it. */
    float *mem=s->memory;
    uint32_t wr=s->wr,ap_pos[8],loop_pos[4];
    float lp[4],hp[4],delay[4],filter[4][2],input_lp[2],output_hp[2],prev[2];
    for(unsigned k=0;k<8;k++)ap_pos[k]=s->ap_pos[k];
    for(unsigned k=0;k<4;k++) {
        loop_pos[k]=s->loop_pos[k];lp[k]=s->lp[k];hp[k]=s->hp[k];delay[k]=s->delay[k];
        filter[k][0]=s->filter[k][0];filter[k][1]=s->filter[k][1];
    }
    for(unsigned c=0;c<2;c++){input_lp[c]=s->input_lp[c];output_hp[c]=s->output_hp[c];prev[c]=s->prev[c];}
    float phase=s->phase,fade=s->fade,lvl=s->level,level_target=sg_clamp(level,0.f,2.f);
    float *ap_mem=mem+4*SG_RING,*loop_mem=ap_mem+8*SG_AP;
    for(unsigned i=0;i<n;i++) {
        sat+=dsat;drive+=ddrive;makeup+=dmakeup;diff+=ddiff;depth+=ddepth;hi+=dhi;rate+=drate;
        float in[2]={sg_finite(l[i]),sg_finite(r[i])},x[2];
        for(unsigned c=0;c<2;c++) {
            float v=sg_filter(filter[c],in[c],gl,al,1);
            float d=v;
            for(unsigned k=0;k<4;k++)d=sg_ap(ap_mem+(c*4u+k)*SG_AP,ap_pos+c*4u+k,aps[c*4u+k],d,.78f);
            x[c]=v+diff*(d-v);
        }
        if(fuzz) {
            /* Zero is exact bypass, as in sg_distort; skipped entirely. */
            float vl=x[0]*drive,vr=x[1]*drive,yl,yr;
            sg_adaa2(&yl,&yr,vl,vr,prev[0],prev[1]);
            prev[0]=vl;prev[1]=vr;
            x[0]+=sat*(yl*makeup-x[0]);x[1]+=sat*(yr*makeup-x[1]);
        } else {prev[0]=x[0];prev[1]=x[1];}
        for(unsigned c=0;c<2;c++){input_lp[c]+=hi*(x[c]-input_lp[c]);in[c]=input_lp[c];}
        float v[4];
        for(unsigned k=0;k<4;k++) {
            delay[k]+=sg_clamp(dtarget[k]-delay[k],-.02f,.02f);
            float d=delay[k]+depth*sg_lfo(phase+.25f*(float)k);
            float y=sg_tap_at(mem+k*SG_RING,wr,d);
            y=sg_ap(loop_mem+k*SG_LOOP_AP,loop_pos+k,looplen[k],y,.35f+.4f*diff);
            /* Fixed gentle loss and DC removal: adjustable Hi/Lo Cut no
             * longer re-filter the tail on every trip through the tank. */
            lp[k]+=.82f*(y-lp[k]);
            hp[k]+=.00195965f*(lp[k]-hp[k]);v[k]=lp[k]-hp[k];
        }
        float a=v[0]+v[1],b=v[0]-v[1],c=v[2]+v[3],d=v[2]-v[3];
        float fb[4]={(a+c)*.5f,(b+d)*.5f,(a-c)*.5f,(b-d)*.5f};
        float feed[4]={in[0],in[1],-in[1],in[0]};
        for(unsigned k=0;k<4;k++) {
            float y=feed[k]*.35f+gain[k]*fb[k];
            mem[k*SG_RING+wr]=sg_clamp(sg_finite(y),-8.f,8.f);
        }
        lvl+=(level_target-lvl)*.001041124f;
        fade=sg_clamp(fade+1.f/240.f,0.f,1.f);
        float out[2]={(v[0]+v[1]-v[2]+v[3])*.5f,(v[0]-v[1]+v[2]+v[3])*.5f};
        for(unsigned ch=0;ch<2;ch++) {
            out[ch]=sg_filter(filter[2u+ch],out[ch],gh,ah,0);
            output_hp[ch]+=.00261116f*(out[ch]-output_hp[ch]);
            out[ch]=(out[ch]-output_hp[ch])*.7f*lvl*fade;
            float q=sg_abs(out[ch]);if(q>1.f)out[ch]=(out[ch]<0.f?-1.f:1.f)*(2.f-1.f/q);
        }
        l[i]=out[0];r[i]=out[1];wr=(wr+1u)&(SG_RING-1u);
        phase+=rate;if(phase>=1.f)phase-=1.f;
    }
    s->wr=wr;
    for(unsigned k=0;k<8;k++)s->ap_pos[k]=ap_pos[k];
    for(unsigned k=0;k<4;k++) {
        s->loop_pos[k]=loop_pos[k];s->lp[k]=lp[k];s->hp[k]=hp[k];s->delay[k]=delay[k];
        s->filter[k][0]=filter[k][0];s->filter[k][1]=filter[k][1];
    }
    for(unsigned c=0;c<2;c++){s->input_lp[c]=input_lp[c];s->output_hp[c]=output_hp[c];s->prev[c]=prev[c];}
    s->phase=phase;s->fade=fade;s->level=lvl;
}
#endif
