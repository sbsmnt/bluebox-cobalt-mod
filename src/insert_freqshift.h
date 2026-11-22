/* Insert frequency shifter (single sideband, "Bode" style): SHFT, SPRD, RING.
 * Every partial moves by the same number of Hz, so harmonic sounds turn
 * inharmonic / metallic; small shifts swirl, and feedback gives a barber-pole
 * rise or fall. Hilbert pair: two chains of four (a^2 - z^-2)/(1 - a^2 z^-2)
 * allpasses (Olli Niemitalo's polyphase design), the first chain one sample
 * later; out = I cos wt +/- Q sin wt. No FFT, no delay lines, no latency
 * beyond the allpass group delay. Prototype, checks and previews:
 * Bluebox-CFW/cobalt-insert-freqshift. */
#ifndef COBALT_INSERT_FREQSHIFT_H
#define COBALT_INSERT_FREQSHIFT_H
#include <stdint.h>

enum {FSH_SHIFT=0,  /* single sideband, direction from the knob */
      FSH_SPREAD=1, /* stereo: left shifts one way, right the other */
      FSH_RING=2};  /* ring modulation (both sidebands) */
#define FSH_DEAD 0.02f        /* Shift dead zone around centre (fraction of half travel) */
#define FSH_MIN_HZ 0.1f       /* shift just outside the dead zone */
#define FSH_OCTAVES 14.6f     /* 0.1 Hz * 2^14.6 = ~2.5 kHz at full travel */
#define FSH_FB_MAX 0.95f

struct fsh_state {
    /* h[ch][chain][bank][section]: direct-form-II section states. The
     * sections run on z^-2, so even and odd samples use separate banks. */
    float h[2][2][2][4];     /* 0 */
    float q1[2];             /* 128: chain 0's extra one-sample delay */
    float fb[2];             /* 136: previous wet output per channel */
    float c,s;               /* 144: oscillator phasor */
    float hz;                /* 152: smoothed signed shift */
    float level,wet,fbg;     /* 156: smoothed output gain, mix, feedback */
    uint32_t parity;         /* 168: bank of the next block's first sample */
    float dcx[2],dcy[2];     /* 172: F2 wet-input DC blocker (previous in / out) */
    uint32_t spare[1];       /* 188 */
};
_Static_assert(sizeof(struct fsh_state)==192,"shifter state ABI");

/* F3: the twelve tracks' shifter states (F4: or resonator states, the same
 * 192 bytes; one SPEC type runs per track) live in one SDRAM block from the
 * boot-time audio arena (dly_construct, as Granular and Shoegaze), NOT in
 * the EQ objects: growing those by 13 x 192 bytes exhausted the 250,000-byte
 * M7 heap at boot (F1/F2 did not start). A free backup-SRAM gap holds the
 * link; without a block the shifter types play dry. */
struct fsh_link {uint32_t magic;struct fsh_state *bank;uint32_t tracks,spare;};
#define FSH_LINK ((volatile struct fsh_link *)0x38800ce0u)
#define FSH_LINK_MAGIC 0x46534831u
#define FSH_TRACKS 12u
#define FSH_BANK_BYTES (FSH_TRACKS*sizeof(struct fsh_state))
static inline struct fsh_state *fsh_bank_state(unsigned ch) {
    if(ch>=FSH_TRACKS || FSH_LINK->magic!=FSH_LINK_MAGIC || FSH_LINK->tracks!=FSH_TRACKS)return 0;
    struct fsh_state *bank=FSH_LINK->bank;
    return bank?bank+ch:0;
}
/* ~8 Hz one-pole DC blocker on the wet input: a shifted DC offset (VA's
 * default patch has one) would become a steady tone at the shift. */
#define FSH_DC_R 0.99895f

/* Niemitalo's a values, squared. Phase difference -90 deg within ~0.7 deg
 * from ~20 Hz to ~20 kHz at 48 kHz; unwanted sideband 44-73 dB down. */
static const float fsh_coef[2][4]={
    {0.479400866f,0.876218494f,0.976597590f,0.997499256f},
    {0.161758498f,0.733028932f,0.945349700f,0.990599157f}};

static inline float fsh_exp2(float x) {
    int e=(int)x;if((float)e>x)e--;
    float f=x-(float)e;
    float p=1.f+f*(.69314718f+f*(.24022651f+f*(.05550411f+f*(.00961813f+f*.00133336f))));
    union{float f;uint32_t u;} v={p};v.u+=(uint32_t)e<<23;return v.f;
}
/* Soft knee above full scale, as the filter's: unity below 1. */
static inline float fsh_knee(float y) {
    float a=y<0.f?-y:y;
    if(a<=1.f)return y;
    float over=a-1.f,v=1.f+over/(1.f+over);
    return y<0.f?-v:v;
}
static inline float fsh_flushf(float v){return v>-1e-20f && v<1e-20f?0.f:v;}

/* Fresh state; the smoothers start at the current controls. */
static inline void fsh_init(struct fsh_state *f,int mix,int feedback,int output) {
    float *p=&f->h[0][0][0][0];
    for(unsigned i=0;i<sizeof f->h/sizeof(float);i++)p[i]=0.f;
    f->q1[0]=f->q1[1]=f->fb[0]=f->fb[1]=0.f;
    f->c=1.f;f->s=0.f;f->hz=0.f;
    f->level=fsh_exp2((float)output/6020.599913f);
    f->wet=(float)mix*.001f;f->fbg=(float)feedback*(.001f*FSH_FB_MAX);
    f->parity=0;f->spare[0]=0;
    f->dcx[0]=f->dcx[1]=f->dcy[0]=f->dcy[1]=0.f;
}
/* Clear the audio history but keep the smoothers (silent bus). */
static inline void fsh_quiet(struct fsh_state *f) {
    float *p=&f->h[0][0][0][0];
    for(unsigned i=0;i<sizeof f->h/sizeof(float);i++)p[i]=0.f;
    f->q1[0]=f->q1[1]=f->fb[0]=f->fb[1]=0.f;
    f->dcx[0]=f->dcx[1]=f->dcy[0]=f->dcy[1]=0.f;
}

/* Knob 0..1000 -> signed Hz. Centre dead zone is exactly 0 Hz. */
static inline float fsh_knob_hz(int knob) {
    float d=((float)knob-500.f)*(1.f/500.f),u=(d<0.f?-d:d)-FSH_DEAD;
    if(u<=0.f)return 0.f;
    float hz=FSH_MIN_HZ*fsh_exp2(u*(FSH_OCTAVES/(1.f-FSH_DEAD)));
    return d<0.f?-hz:hz;
}

/* One sample through one Hilbert chain (one parity bank, in registers). */
#define FSH_CHAIN(k,w0,w1,w2,w3,x,out) do { float t_,y_=(x); \
    t_=y_+fsh_coef[k][0]*w0;y_=fsh_coef[k][0]*t_-w0;w0=t_; \
    t_=y_+fsh_coef[k][1]*w1;y_=fsh_coef[k][1]*t_-w1;w1=t_; \
    t_=y_+fsh_coef[k][2]*w2;y_=fsh_coef[k][2]*t_-w2;w2=t_; \
    t_=y_+fsh_coef[k][3]*w3;y_=fsh_coef[k][3]*t_-w3;w3=t_;(out)=y_; } while(0)

/* Per-block controls: dry and wet output gains ramp across the block (they
 * include the family fade); feedback and its input compensation are held. */
struct fsh_ramp {float dry,ddry,wet,dwet,fb,comp;};

/* One out-of-line copy of each loop keeps the code small (the caller passes
 * type and stereo at run time). */
static __attribute__((noinline)) void fsh_channel(float *__restrict x,unsigned n,float (*h)[2][4],unsigned par,
        float *q1,float *fbp,float *dc,const float *cv,const float *sv,float sign,const struct fsh_ramp *rp) {
    float *ae=h[0][par],*ao=h[0][par^1u],*be=h[1][par],*bo=h[1][par^1u];
    float a0=ae[0],a1=ae[1],a2=ae[2],a3=ae[3];
    float A0=ao[0],A1=ao[1],A2=ao[2],A3=ao[3];
    float b0=be[0],b1=be[1],b2=be[2],b3=be[3];
    float B0=bo[0],B1=bo[1],B2=bo[2],B3=bo[3];
    float q=*q1,prev=*fbp,dry=rp->dry,wet=rp->wet,dx=dc[0],dy=dc[1];
    const float ddry=rp->ddry,dwet=rp->dwet,fb=rp->fb,comp=rp->comp;
#define FSH_STEP(i_,H0,H1,H2,H3,G0,G1,G2,G3) do { \
        float in=x[i_],ya,yb; \
        dy=in-dx+FSH_DC_R*dy;dx=in; \
        float v=dy*comp+fb*prev; \
        FSH_CHAIN(0,H0,H1,H2,H3,v,ya);FSH_CHAIN(1,G0,G1,G2,G3,v,yb); \
        float y=q*cv[i_]+sign*yb*sv[i_]; \
        q=ya;prev=y; \
        x[i_]=fsh_knee(in*dry+y*wet); \
        dry+=ddry;wet+=dwet; \
    } while(0)
    unsigned i=0;
    for(;i+1<n;i+=2) {
        FSH_STEP(i,a0,a1,a2,a3,b0,b1,b2,b3);
        FSH_STEP(i+1,A0,A1,A2,A3,B0,B1,B2,B3);
    }
    if(i<n)FSH_STEP(i,a0,a1,a2,a3,b0,b1,b2,b3);
#undef FSH_STEP
    /* After near-silence zero everything so the decaying allpass states
     * can't reach the denormal range. Bounded: a NaN/huge state resets. */
    float m=(q<0.f?-q:q)+(prev<0.f?-prev:prev);
    if(m<1e-12f || !(m<1e6f)){a0=a1=a2=a3=A0=A1=A2=A3=b0=b1=b2=b3=B0=B1=B2=B3=q=prev=0.f;}
    dc[0]=dx;dc[1]=fsh_flushf(dy);
    ae[0]=a0;ae[1]=a1;ae[2]=a2;ae[3]=a3;ao[0]=A0;ao[1]=A1;ao[2]=A2;ao[3]=A3;
    be[0]=b0;be[1]=b1;be[2]=b2;be[3]=b3;bo[0]=B0;bo[1]=B1;bo[2]=B2;bo[3]=B3;
    *q1=q;*fbp=prev;
}

static __attribute__((noinline)) void fsh_ring(float *__restrict x,unsigned n,float *fbp,float *dc,const float *cv,const struct fsh_ramp *rp) {
    float prev=*fbp,dry=rp->dry,wet=rp->wet,dx=dc[0],dy=dc[1];
    const float ddry=rp->ddry,dwet=rp->dwet,fb=rp->fb,comp=rp->comp;
    for(unsigned i=0;i<n;i++) {
        float in=x[i];
        dy=in-dx+FSH_DC_R*dy;dx=in;
        float y=(dy*comp+fb*prev)*cv[i];
        prev=y;
        x[i]=fsh_knee(in*dry+y*wet);
        dry+=ddry;wet+=dwet;
    }
    *fbp=(prev<0.f?-prev:prev)<1e6f?fsh_flushf(prev):0.f;
    dc[0]=dx;dc[1]=(dy<0.f?-dy:dy)<1e6f?fsh_flushf(dy):0.f;
}

/* Feedback loop-gain compensation: the wet input is scaled by ~sqrt(1-fb^2)
 * so broadband loudness stays roughly level as feedback rises. */
static inline float fsh_comp(float fb){return 1.f-fb*fb*(.5f+.5f*fb*fb);}

/* One block. e0 -> e1 is the insert's fade (0 = dry, 1 = full effect),
 * ramped across the block. Fully faded out (both 0) leaves the audio as is. */
static inline void fsh_loop(struct fsh_state *f,float *l,float *r,unsigned n,
        unsigned type,unsigned stereo,int shift,int feedback,int mix,int output,float e0,float e1) {
    /* Glide the shift (~15 ms) once per block; per-sample rotation from it. */
    float target=fsh_knob_hz(shift);
    if(type==FSH_RING && target<0.f)target=-target;
    f->hz+=(target-f->hz)*.045f;
    float w=f->hz*(6.28318530718f/48000.f),ww=w*w;
    float rs=w*(1.f-ww*(1.f/6.f)*(1.f-ww*(1.f/20.f)*(1.f-ww*(1.f/42.f))));
    float rc=1.f-ww*.5f*(1.f-ww*(1.f/12.f)*(1.f-ww*(1.f/30.f)));
    float c=f->c,sn=f->s;
    float g=1.5f-.5f*(c*c+sn*sn);c*=g;sn*=g;   /* renormalise once per block */
    float cv[32],sv[32];
    for(unsigned i=0;i<n;i++){cv[i]=c;sv[i]=sn;float nc=c*rc-sn*rs;sn=c*rs+sn*rc;c=nc;}
    f->c=c;f->s=sn;

    /* Controls: one-pole per block (~5 ms), ramped linearly inside it. */
    float k=.12f*(float)n*(1.f/32.f),inv=1.f/(float)n;
    float wet1=f->wet+((float)mix*.001f-f->wet)*k;
    float fb1=f->fbg+((float)feedback*(.001f*FSH_FB_MAX)-f->fbg)*k;
    float lev1=f->level+(fsh_exp2((float)output/6020.599913f)-f->level)*k;
    float dry0=1.f-e0+e0*f->level*(1.f-f->wet),dry1=1.f-e1+e1*lev1*(1.f-wet1);
    float wg0=e0*f->level*f->wet,wg1=e1*lev1*wet1;
    struct fsh_ramp rp={dry0,(dry1-dry0)*inv,wg0,(wg1-wg0)*inv,fb1,fsh_comp(fb1)};
    f->wet=wet1;f->fbg=fb1;f->level=lev1;

    unsigned par=f->parity;
    if(type==FSH_RING) {
        float dl[2]={f->dcx[0],f->dcy[0]},dr[2]={f->dcx[1],f->dcy[1]};
        fsh_ring(l,n,&f->fb[0],dl,cv,&rp);
        if(stereo)fsh_ring(r,n,&f->fb[1],dr,cv,&rp);
        f->dcx[0]=dl[0];f->dcy[0]=dl[1];f->dcx[1]=dr[0];f->dcy[1]=dr[1];
    } else {
        float dl[2]={f->dcx[0],f->dcy[0]},dr[2]={f->dcx[1],f->dcy[1]};
        fsh_channel(l,n,f->h[0],par,&f->q1[0],&f->fb[0],dl,cv,sv,1.f,&rp);
        if(stereo)fsh_channel(r,n,f->h[1],par,&f->q1[1],&f->fb[1],dr,cv,sv,type==FSH_SPREAD?-1.f:1.f,&rp);
        f->dcx[0]=dl[0];f->dcy[0]=dl[1];f->dcx[1]=dr[0];f->dcy[1]=dr[1];
    }
    f->parity=par^(n&1u);
}

static inline void fsh_process(struct fsh_state *f,float *l,float *r,unsigned n,unsigned type,
        int shift,int feedback,int mix,int output,float e0,float e1) {
    if(n>32u)n=32u;
    if(!n || !l)return;
    if(e0==0.f && e1==0.f){fsh_quiet(f);return;}
    unsigned stereo=r && r!=l;
    if(!stereo)f->q1[1]=f->fb[1]=0.f;
    if(type>FSH_RING)type=FSH_SHIFT;
    if(type==FSH_SPREAD && !stereo)type=FSH_SHIFT; /* mono: plain shift */
    fsh_loop(f,l,r,n,type,stereo,shift,feedback,mix,output,e0,e1);
}
#endif
