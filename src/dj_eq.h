/* DJ EQ: per-track 4-band isolator/EQ and a bipolar filter, after the native
 * EQ and the gate. Portable C: built for the M7 and run by the host tests.
 *
 * Bands: High, Hi Mid, Lo Mid, Low, split at three crossovers. Each band
 * knob runs -100..100 with 0 dB at the centre: left down to -26 dB (the
 * ISO curve kills the band at -100), right up to +6 dB.
 * - Curve ISO: Linkwitz-Riley 4th order, phase compensated; the four bands
 *   at 0 dB sum to a pure allpass.
 * - Curve EQ: Linkwitz-Riley 2nd order, gentler and cheaper; no kill.
 * Filter: one knob, left low-pass, right high-pass, centre off; Res sets Q.
 * All filters are TPT state-variable filters, stable under fast sweeps.
 *
 * Neutral (all bands 0 dB, filter centred) is an exact bypass. Leaving or
 * returning to neutral crossfades dry and processed over 10 ms, so there is
 * no click although the processed path is an allpass, not the identity.
 *
 * Cost: an LR4 split is an SVF plus a low-pass biquad (the second-order
 * allpass falls out of the SVF, and HP4 = AP2 - LP4); the two compensating
 * allpasses are biquads. Settled blocks (no gain ramp, no crossfade) run
 * specialised loops without the per-sample fade logic. */
#ifndef BB_DJ_EQ_H
#define BB_DJ_EQ_H
#include <stdint.h>

#define DJ_SR 48000.f
#define DJ_BAND_MAX 100       /* knob -100 (-26 dB / kill) .. 0 (0 dB) .. 100 (+6 dB) */
#define DJ_FILT_MAX 1000      /* filter -1000 (LP) .. 0 (off) .. 1000 (HP) */
#define DJ_FILT_DEAD 20       /* +-2 % centre detent */
#define DJ_RES_MAX 1000
#define DJ_FADE 480u          /* 10 ms dry/processed crossfade */
#define DJ_GAIN_RATE (1.f/480.f)   /* band gain change per sample: kill <-> 0 dB in 10 ms */
#define DJ_HOLD 4800u         /* stay processing 100 ms after neutral */
#define DJ_CURVE_ISO 0
#define DJ_CURVE_EQ 1

struct dj_svf {float a1,a2,a3,k;};   /* coefficients */
struct dj_z {float ic1,ic2;};        /* state */
/* bands 4 or 3 (3: x_lo and x_hi split Low | Mid | High, the Lo Mid knob is
 * unused); slope 12 or 24 dB/oct for the filter; res_max scales Res, 0..1000. */
struct dj_settings {int32_t curve,x_lo,x_mid,x_hi,bands,slope,res_max;};  /* Hz */
/* Per channel: mid split (2), allpasses (2), low split (2), high split (2). */
enum {DJ_Z_MID=0,DJ_Z_AP_HI=2,DJ_Z_AP_LO=3,DJ_Z_LO=4,DJ_Z_HI=6,DJ_Z_N=8};
struct dj_track {
    int32_t band[4],filt,res;          /* parameters: High, HiMid, LoMid, Low */
    float gain[4];                     /* applied band gains, ramped */
    float wet,fwet;                    /* EQ / filter crossfade 0..1 */
    float fpos;                        /* smoothed filter position -1..1 */
    uint32_t hold,fhold;
    int32_t fhp;                       /* filter side last used: 1 high-pass */
    int32_t fslope;                    /* slope the filter states were made for */
    struct dj_settings cur;            /* settings the coefficients were made for */
    struct dj_svf c_mid,c_lo,c_hi,c_f;
    struct dj_bq {float d1,d2;} q_mid,q_lo,q_hi;   /* ISO: biquad denominators of the crossovers */
    struct dj_svf c_f2;                /* second filter stage (24 dB) */
    struct dj_z z[2][DJ_Z_N],zf[2],zf2[2];
};

static float dj_exp2(float x) {
    int fl=(int)x;if((float)fl>x)fl--;
    float f=x-(float)fl;
    float p=1.f+f*(.6931472f+f*(.2402265f+f*(.0555041f+f*(.0096181f+f*.0013334f))));
    union {float f;uint32_t u;} v={p};v.u+=(uint32_t)fl<<23;return v.f;
}
/* tan(x) for 0 <= x < pi/2 from odd/even polynomials on [0, pi/4]. */
static float dj_tan(float x) {
    int flip=x>.7853982f;if(flip)x=1.5707963f-x;
    float x2=x*x;
    float s=x*(1.f+x2*(-.16666667f+x2*(.0083333310f+x2*-.00019840874f)));
    float c=1.f+x2*(-.5f+x2*(.041666418f+x2*(-.0013888397f+x2*.000024760495f)));
    return flip ? c/s : s/c;
}
static void dj_svf_set(struct dj_svf *c,float hz,float k) {
    if(hz<10.f)hz=10.f;
    if(hz>.45f*DJ_SR)hz=.45f*DJ_SR;
    float g=dj_tan(3.14159265f*hz/DJ_SR);
    c->k=k;c->a1=1.f/(1.f+g*(g+k));c->a2=g*c->a1;c->a3=g*c->a2;
}
/* One SVF step on register copies: low-pass into lp, band-pass into bp. */
#define DJ_SVF(a1,a2,a3,s1,s2,in,lp,bp) do{float v3_=(in)-(s2),v1_=(a1)*(s1)+(a2)*v3_,v2_=(s2)+(a2)*(s1)+(a3)*v3_; \
    (s1)=2.f*v1_-(s1);(s2)=2.f*v2_-(s2);(lp)=v2_;(bp)=v1_;}while(0)

static void dj_reset(struct dj_track *t) {
    uint8_t *p=(uint8_t *)t;for(unsigned i=0;i<sizeof *t;i++)p[i]=0;
    for(unsigned b=0;b<4;b++)t->gain[b]=1.f;
    t->cur.curve=-1;
}
static void dj_settings_default(struct dj_settings *s) {
    s->curve=DJ_CURVE_ISO;s->x_lo=200;s->x_mid=1000;s->x_hi=5000;s->bands=4;s->slope=12;s->res_max=1000;
}
static int dj_eq_neutral(const struct dj_track *t,int bands) {
    return !t->band[0] && !t->band[1] && (bands==3 || !t->band[2]) && !t->band[3];
}
/* Knob -> gain: left half -26..0 dB (ISO kill at -100), right half 0..+6 dB. */
static float dj_band_gain(int32_t v,int curve) {
    if(!v)return 1.f;
    if(v<=-DJ_BAND_MAX && curve==DJ_CURVE_ISO)return 0.f;
    float db=v<0 ? (float)v*.26f : (float)v*.06f;
    return dj_exp2(db*.16609640f);   /* 2^(dB/6.0206) */
}
/* Parameter ids are mapped by the caller: 0..3 bands, 4 filter, 5 res. */
static void dj_param(struct dj_track *t,unsigned which,int32_t v) {
    if(which<4){t->band[which]=v<-DJ_BAND_MAX?-DJ_BAND_MAX:v>DJ_BAND_MAX?DJ_BAND_MAX:v;return;}
    if(which==4){t->filt=v<-DJ_FILT_MAX?-DJ_FILT_MAX:v>DJ_FILT_MAX?DJ_FILT_MAX:v;return;}
    if(which==5)t->res=v<0?0:v>DJ_RES_MAX?DJ_RES_MAX:v;
}
/* The bilinear biquad with the SVF's g and k: 1 + d1 z^-1 + d2 z^-2 over
 * a0 = 1 + g(g + k). Its low-pass numerator is a3 (1 + 2 z^-1 + z^-2), its
 * allpass numerator d2 + d1 z^-1 + z^-2: the same responses as the SVF. */
static void dj_bq_set(struct dj_bq *q,const struct dj_svf *c) {
    float g=c->a2/c->a1;
    q->d1=2.f*(g*g-1.f)*c->a1;q->d2=(1.f-c->k*g+g*g)*c->a1;
}
static void dj_coefficients(struct dj_track *t,const struct dj_settings *s) {
    int iso=s->curve==DJ_CURVE_ISO;
    if(t->cur.curve==s->curve && t->cur.bands==s->bands && t->cur.x_lo==s->x_lo && t->cur.x_mid==s->x_mid && t->cur.x_hi==s->x_hi)return;
    float k=iso ? 1.4142136f : 2.f;
    dj_svf_set(&t->c_lo,(float)s->x_lo,k);dj_svf_set(&t->c_mid,(float)s->x_mid,k);dj_svf_set(&t->c_hi,(float)s->x_hi,k);
    dj_bq_set(&t->q_lo,&t->c_lo);dj_bq_set(&t->q_mid,&t->c_mid);dj_bq_set(&t->q_hi,&t->c_hi);
    if(t->cur.curve!=s->curve || t->cur.bands!=s->bands)for(unsigned c=0;c<2;c++)for(unsigned i=0;i<DJ_Z_N;i++)t->z[c][i].ic1=t->z[c][i].ic2=0.f;
    t->cur=*s;
}
/* Filter position -> SVF: left 20 kHz -> 40 Hz low-pass, right 20 Hz -> 10 kHz
 * high-pass, exponential travel. Resonance is eased in near the detent so
 * the corner never rings at the edges of the spectrum. */
static void dj_filter_coeffs(struct dj_track *t,const struct dj_settings *s) {
    float p=t->fpos,a=p<0.f?-p:p;
    float r=(float)t->res*(float)s->res_max*(1.f/((float)DJ_RES_MAX*1000.f)),ease=a*8.f;if(ease>1.f)ease=1.f;
    float hz=p<0.f ? 20000.f*dj_exp2(-8.966f*a) : 20.f*dj_exp2(8.966f*a);
    if(s->slope==24) {
        /* 4th-order Butterworth as two stages; Res lowers the first one's damping. */
        dj_svf_set(&t->c_f,hz,.76536686f-.70f*r*ease);
        dj_svf_set(&t->c_f2,hz,1.8477591f);
    } else dj_svf_set(&t->c_f,hz,1.4142136f-1.3f*r*ease);
}

/* The channel loops are written once and specialised: steady (gains settled,
 * fully wet: the usual case while a band is turned) drops the gain ramps and
 * the crossfade from the per-sample loop. Its arithmetic is the general
 * loop's with zero ramps and w = 1, so the output is bit-identical. */
#define DJ_INLINE static inline __attribute__((always_inline))
#define DJ_NOINLINE static __attribute__((noinline))
/* One channel of the band tree. Gains ramp linearly over the block; the
 * dry/processed crossfade moves by step per sample towards fade_in. */
DJ_INLINE float dj_eq_body(const struct dj_track *t,int iso,struct dj_z *z,float *x,unsigned n,
                           const float *g0,const float *dg,float w,int fade_in,int steady) {
    const float ma1=t->c_mid.a1,ma2=t->c_mid.a2,ma3=t->c_mid.a3;
    const float la1=t->c_lo.a1,la2=t->c_lo.a2,la3=t->c_lo.a3;
    const float ha1=t->c_hi.a1,ha2=t->c_hi.a2,ha3=t->c_hi.a3;
    float m1=z[0].ic1,m2=z[0].ic2,m3=z[1].ic1,m4=z[1].ic2;
    float p1=z[DJ_Z_AP_HI].ic1,p2=z[DJ_Z_AP_HI].ic2,q1=z[DJ_Z_AP_LO].ic1,q2=z[DJ_Z_AP_LO].ic2;
    float l1=z[DJ_Z_LO].ic1,l2=z[DJ_Z_LO].ic2,l3=z[DJ_Z_LO+1].ic1,l4=z[DJ_Z_LO+1].ic2;
    float h1=z[DJ_Z_HI].ic1,h2=z[DJ_Z_HI].ic2,h3=z[DJ_Z_HI+1].ic1,h4=z[DJ_Z_HI+1].ic2;
    float gh=g0[0],ghm=g0[1],glm=g0[2],gl=g0[3];
    const float dgh=dg[0],dghm=dg[1],dglm=dg[2],dgl=dg[3];
    const float step=1.f/(float)DJ_FADE;
    int fading=fade_in ? w<1.f : w>0.f;
    for(unsigned i=0;i<n;i++) {
        float in=x[i],lp,bp,lo,hi,b_low,b_lomid,b_himid,b_high,d;   /* d: unused SVF output */
        if(iso) {
            /* LR4 split: LP4 = LP2(LP2(x)); AP2 = x - 2k*bp; HP4 = AP2 - LP4. */
            DJ_SVF(ma1,ma2,ma3,m1,m2,in,lp,bp);DJ_SVF(ma1,ma2,ma3,m3,m4,lp,lo,d);
            hi=in-2.8284271f*bp-lo;
            DJ_SVF(ha1,ha2,ha3,p1,p2,lo,d,bp);lo-=2.8284271f*bp;   /* AP at x_hi */
            DJ_SVF(la1,la2,la3,q1,q2,hi,d,bp);hi-=2.8284271f*bp;   /* AP at x_lo */
            DJ_SVF(la1,la2,la3,l1,l2,lo,lp,bp);DJ_SVF(la1,la2,la3,l3,l4,lp,b_low,d);
            b_lomid=lo-2.8284271f*bp-b_low;
            DJ_SVF(ha1,ha2,ha3,h1,h2,hi,lp,bp);DJ_SVF(ha1,ha2,ha3,h3,h4,lp,b_himid,d);
            b_high=hi-2.8284271f*bp-b_himid;
        } else {
            /* LR2 split: low = LP2, high = -HP2 (k = 2); LP2 - HP2 is AP1. */
            DJ_SVF(ma1,ma2,ma3,m1,m2,in,lo,bp);hi=lo+2.f*bp-in;
            float ap;
            DJ_SVF(ha1,ha2,ha3,p1,p2,lo,ap,bp);lo=2.f*(ap+bp)-lo;   /* AP1 at x_hi */
            DJ_SVF(la1,la2,la3,q1,q2,hi,ap,bp);hi=2.f*(ap+bp)-hi;   /* AP1 at x_lo */
            DJ_SVF(la1,la2,la3,l1,l2,lo,b_low,bp);b_lomid=b_low+2.f*bp-lo;
            DJ_SVF(ha1,ha2,ha3,h1,h2,hi,b_himid,bp);b_high=b_himid+2.f*bp-hi;
        }
        (void)d;
        if(!steady){gh+=dgh;ghm+=dghm;glm+=dglm;gl+=dgl;}
        float y=gh*b_high+ghm*b_himid+glm*b_lomid+gl*b_low;
        if(steady)x[i]=y;
        else if(fading) {
            if(fade_in){w+=step;if(w>=1.f){w=1.f;fading=0;}}
            else{w-=step;if(w<=0.f){w=0.f;fading=0;}}
            x[i]=in+(y-in)*w;
        } else x[i]=fade_in ? y : in;
    }
    z[0].ic1=m1;z[0].ic2=m2;z[1].ic1=m3;z[1].ic2=m4;
    z[DJ_Z_AP_HI].ic1=p1;z[DJ_Z_AP_HI].ic2=p2;z[DJ_Z_AP_LO].ic1=q1;z[DJ_Z_AP_LO].ic2=q2;
    z[DJ_Z_LO].ic1=l1;z[DJ_Z_LO].ic2=l2;z[DJ_Z_LO+1].ic1=l3;z[DJ_Z_LO+1].ic2=l4;
    z[DJ_Z_HI].ic1=h1;z[DJ_Z_HI].ic2=h2;z[DJ_Z_HI+1].ic1=h3;z[DJ_Z_HI+1].ic2=h4;
    return w;
}
/* Three bands: split at x_lo (Low | rest), the rest at x_hi (Mid | High); the
 * Low branch takes the x_hi allpass. Knobs: High g0, Mid g1, Low g3. */
DJ_INLINE float dj_eq3_body(const struct dj_track *t,int iso,struct dj_z *z,float *x,unsigned n,
                            const float *g0,const float *dg,float w,int fade_in,int steady) {
    const float la1=t->c_lo.a1,la2=t->c_lo.a2,la3=t->c_lo.a3;
    const float ha1=t->c_hi.a1,ha2=t->c_hi.a2,ha3=t->c_hi.a3;
    float p1=z[DJ_Z_AP_HI].ic1,p2=z[DJ_Z_AP_HI].ic2;
    float l1=z[DJ_Z_LO].ic1,l2=z[DJ_Z_LO].ic2,l3=z[DJ_Z_LO+1].ic1,l4=z[DJ_Z_LO+1].ic2;
    float h1=z[DJ_Z_HI].ic1,h2=z[DJ_Z_HI].ic2,h3=z[DJ_Z_HI+1].ic1,h4=z[DJ_Z_HI+1].ic2;
    float gh=g0[0],gm=g0[1],gl=g0[3];
    const float dgh=dg[0],dgm=dg[1],dgl=dg[3];
    const float step=1.f/(float)DJ_FADE;
    int fading=fade_in ? w<1.f : w>0.f;
    for(unsigned i=0;i<n;i++) {
        float in=x[i],lp,bp,d,low,rest,mid,high;
        if(iso) {
            DJ_SVF(la1,la2,la3,l1,l2,in,lp,bp);DJ_SVF(la1,la2,la3,l3,l4,lp,low,d);
            rest=in-2.8284271f*bp-low;
            DJ_SVF(ha1,ha2,ha3,h1,h2,rest,lp,bp);DJ_SVF(ha1,ha2,ha3,h3,h4,lp,mid,d);
            high=rest-2.8284271f*bp-mid;
            DJ_SVF(ha1,ha2,ha3,p1,p2,low,d,bp);low-=2.8284271f*bp;   /* AP at x_hi */
        } else {
            DJ_SVF(la1,la2,la3,l1,l2,in,low,bp);rest=low+2.f*bp-in;
            DJ_SVF(ha1,ha2,ha3,h1,h2,rest,mid,bp);high=mid+2.f*bp-rest;
            float ap;DJ_SVF(ha1,ha2,ha3,p1,p2,low,ap,bp);low=2.f*(ap+bp)-low;   /* AP1 at x_hi */
            d=0.f;
        }
        (void)d;
        if(!steady){gh+=dgh;gm+=dgm;gl+=dgl;}
        float y=gh*high+gm*mid+gl*low;
        if(steady)x[i]=y;
        else if(fading) {
            if(fade_in){w+=step;if(w>=1.f){w=1.f;fading=0;}}
            else{w-=step;if(w<=0.f){w=0.f;fading=0;}}
            x[i]=in+(y-in)*w;
        } else x[i]=fade_in ? y : in;
    }
    z[DJ_Z_AP_HI].ic1=p1;z[DJ_Z_AP_HI].ic2=p2;
    z[DJ_Z_LO].ic1=l1;z[DJ_Z_LO].ic2=l2;z[DJ_Z_LO+1].ic1=l3;z[DJ_Z_LO+1].ic2=l4;
    z[DJ_Z_HI].ic1=h1;z[DJ_Z_HI].ic2=h2;z[DJ_Z_HI+1].ic1=h3;z[DJ_Z_HI+1].ic2=h4;
    return w;
}
DJ_INLINE float dj_filter_body(const struct dj_svf *c,const struct dj_svf *c2,struct dj_z *z,struct dj_z *z2,
                               float *x,unsigned n,int hp,int off,float w,int fade_in,int steady) {
    const float a1=c->a1,a2=c->a2,a3=c->a3,k=c->k,step=1.f/(float)DJ_FADE;
    const float b1=c2?c2->a1:0.f,b2=c2?c2->a2:0.f,b3=c2?c2->a3:0.f,k2=c2?c2->k:0.f;
    float s1=z->ic1,s2=z->ic2,t1=z2->ic1,t2=z2->ic2;
    int fading=fade_in ? w<1.f : w>0.f;
    for(unsigned i=0;i<n;i++) {
        float in=x[i],lp,bp;
        DJ_SVF(a1,a2,a3,s1,s2,in,lp,bp);
        float y=off ? in : hp ? in-k*bp-lp : lp;
        if(c2 && !off){float y1=y;DJ_SVF(b1,b2,b3,t1,t2,y1,lp,bp);y=hp ? y1-k2*bp-lp : lp;}
        if(steady)x[i]=y;
        else if(fading) {
            if(fade_in){w+=step;if(w>=1.f){w=1.f;fading=0;}}
            else{w-=step;if(w<=0.f){w=0.f;fading=0;}}
            x[i]=in+(y-in)*w;
        } else x[i]=fade_in ? y : in;
    }
    z->ic1=s1;z->ic2=s2;z2->ic1=t1;z2->ic2=t2;
    return w;
}

/* ISO (LR4) trees with the cheaper stage forms; the responses are unchanged:
 * - an LR4 split is one SVF (its low-pass and band-pass) then a Butterworth
 *   low-pass biquad: LP4 = LP2 LP2, HP4 = AP2 - LP4 with AP2 = x - 2k bp;
 * - the phase-compensating allpasses are allpass biquads.
 * Transposed direct form II; the crossovers are fixed while a track runs
 * (only the settings page moves them), so no SVF is needed for sweeps.
 * State slots: 0 mid SVF, 1 mid LP2, 2 AP at x_hi, 3 AP at x_lo, 4 lo SVF,
 * 5 lo LP2, 6 hi SVF, 7 hi LP2. */
#define DJ_LP2(b0,d1,d2,s1,s2,in,out) do{float t_=(b0)*(in);(out)=t_+(s1);(s1)=(s2)+2.f*t_-(d1)*(out);(s2)=t_-(d2)*(out);}while(0)
#define DJ_AP2(d1,d2,s1,s2,io) do{float x_=(io),y_=(d2)*x_+(s1);(s1)=(s2)+(d1)*(x_-y_);(s2)=x_-(d2)*y_;(io)=y_;}while(0)
DJ_INLINE float dj_iso4_body(const struct dj_track *t,struct dj_z *z,float *x,unsigned n,
                             const float *g0,const float *dg,float w,int fade_in,int steady) {
    const float ma1=t->c_mid.a1,ma2=t->c_mid.a2,ma3=t->c_mid.a3,md1=t->q_mid.d1,md2=t->q_mid.d2;
    const float la1=t->c_lo.a1,la2=t->c_lo.a2,la3=t->c_lo.a3,ld1=t->q_lo.d1,ld2=t->q_lo.d2;
    const float ha1=t->c_hi.a1,ha2=t->c_hi.a2,ha3=t->c_hi.a3,hd1=t->q_hi.d1,hd2=t->q_hi.d2;
    float m1=z[0].ic1,m2=z[0].ic2,m3=z[1].ic1,m4=z[1].ic2;
    float p1=z[DJ_Z_AP_HI].ic1,p2=z[DJ_Z_AP_HI].ic2,q1=z[DJ_Z_AP_LO].ic1,q2=z[DJ_Z_AP_LO].ic2;
    float l1=z[DJ_Z_LO].ic1,l2=z[DJ_Z_LO].ic2,l3=z[DJ_Z_LO+1].ic1,l4=z[DJ_Z_LO+1].ic2;
    float h1=z[DJ_Z_HI].ic1,h2=z[DJ_Z_HI].ic2,h3=z[DJ_Z_HI+1].ic1,h4=z[DJ_Z_HI+1].ic2;
    float gh=g0[0],ghm=g0[1],glm=g0[2],gl=g0[3];
    const float dgh=dg[0],dghm=dg[1],dglm=dg[2],dgl=dg[3];
    const float step=1.f/(float)DJ_FADE;
    int fading=fade_in ? w<1.f : w>0.f;
    for(unsigned i=0;i<n;i++) {
        float in=x[i],lp,bp,lo,hi,b_low,b_himid;
        DJ_SVF(ma1,ma2,ma3,m1,m2,in,lp,bp);DJ_LP2(ma3,md1,md2,m3,m4,lp,lo);
        hi=in-2.8284271f*bp-lo;
        DJ_AP2(hd1,hd2,p1,p2,lo);DJ_AP2(ld1,ld2,q1,q2,hi);
        DJ_SVF(la1,la2,la3,l1,l2,lo,lp,bp);DJ_LP2(la3,ld1,ld2,l3,l4,lp,b_low);
        float r_lo=lo-2.8284271f*bp;          /* = Lo Mid + Low */
        DJ_SVF(ha1,ha2,ha3,h1,h2,hi,lp,bp);DJ_LP2(ha3,hd1,hd2,h3,h4,lp,b_himid);
        float r_hi=hi-2.8284271f*bp;          /* = High + Hi Mid */
        if(!steady){gh+=dgh;ghm+=dghm;glm+=dglm;gl+=dgl;}
        float y=gh*r_hi+(ghm-gh)*b_himid+glm*r_lo+(gl-glm)*b_low;
        if(steady)x[i]=y;
        else if(fading) {
            if(fade_in){w+=step;if(w>=1.f){w=1.f;fading=0;}}
            else{w-=step;if(w<=0.f){w=0.f;fading=0;}}
            x[i]=in+(y-in)*w;
        } else x[i]=fade_in ? y : in;
    }
    z[0].ic1=m1;z[0].ic2=m2;z[1].ic1=m3;z[1].ic2=m4;
    z[DJ_Z_AP_HI].ic1=p1;z[DJ_Z_AP_HI].ic2=p2;z[DJ_Z_AP_LO].ic1=q1;z[DJ_Z_AP_LO].ic2=q2;
    z[DJ_Z_LO].ic1=l1;z[DJ_Z_LO].ic2=l2;z[DJ_Z_LO+1].ic1=l3;z[DJ_Z_LO+1].ic2=l4;
    z[DJ_Z_HI].ic1=h1;z[DJ_Z_HI].ic2=h2;z[DJ_Z_HI+1].ic1=h3;z[DJ_Z_HI+1].ic2=h4;
    return w;
}
/* Three bands: split at x_lo (Low | rest), the rest at x_hi (Mid | High); the
 * Low branch takes the x_hi allpass. */
DJ_INLINE float dj_iso3_body(const struct dj_track *t,struct dj_z *z,float *x,unsigned n,
                             const float *g0,const float *dg,float w,int fade_in,int steady) {
    const float la1=t->c_lo.a1,la2=t->c_lo.a2,la3=t->c_lo.a3,ld1=t->q_lo.d1,ld2=t->q_lo.d2;
    const float ha1=t->c_hi.a1,ha2=t->c_hi.a2,ha3=t->c_hi.a3,hd1=t->q_hi.d1,hd2=t->q_hi.d2;
    float p1=z[DJ_Z_AP_HI].ic1,p2=z[DJ_Z_AP_HI].ic2;
    float l1=z[DJ_Z_LO].ic1,l2=z[DJ_Z_LO].ic2,l3=z[DJ_Z_LO+1].ic1,l4=z[DJ_Z_LO+1].ic2;
    float h1=z[DJ_Z_HI].ic1,h2=z[DJ_Z_HI].ic2,h3=z[DJ_Z_HI+1].ic1,h4=z[DJ_Z_HI+1].ic2;
    float gh=g0[0],gm=g0[1],gl=g0[3];
    const float dgh=dg[0],dgm=dg[1],dgl=dg[3];
    const float step=1.f/(float)DJ_FADE;
    int fading=fade_in ? w<1.f : w>0.f;
    for(unsigned i=0;i<n;i++) {
        float in=x[i],lp,bp,low,rest,mid;
        DJ_SVF(la1,la2,la3,l1,l2,in,lp,bp);DJ_LP2(la3,ld1,ld2,l3,l4,lp,low);
        rest=in-2.8284271f*bp-low;
        DJ_SVF(ha1,ha2,ha3,h1,h2,rest,lp,bp);DJ_LP2(ha3,hd1,hd2,h3,h4,lp,mid);
        float r_hi=rest-2.8284271f*bp;        /* = High + Mid */
        DJ_AP2(hd1,hd2,p1,p2,low);
        if(!steady){gh+=dgh;gm+=dgm;gl+=dgl;}
        float y=gh*r_hi+(gm-gh)*mid+gl*low;
        if(steady)x[i]=y;
        else if(fading) {
            if(fade_in){w+=step;if(w>=1.f){w=1.f;fading=0;}}
            else{w-=step;if(w<=0.f){w=0.f;fading=0;}}
            x[i]=in+(y-in)*w;
        } else x[i]=fade_in ? y : in;
    }
    z[DJ_Z_AP_HI].ic1=p1;z[DJ_Z_AP_HI].ic2=p2;
    z[DJ_Z_LO].ic1=l1;z[DJ_Z_LO].ic2=l2;z[DJ_Z_LO+1].ic1=l3;z[DJ_Z_LO+1].ic2=l4;
    z[DJ_Z_HI].ic1=h1;z[DJ_Z_HI].ic2=h2;z[DJ_Z_HI+1].ic1=h3;z[DJ_Z_HI+1].ic2=h4;
    return w;
}
/* Settled 4-band ISO in two passes over chunks of up to 32 samples: the
 * merged loop needs about 37 live floats (16 states, 15 coefficients, gains)
 * and spills on a 32-register FPU; each pass fits. Same stages and
 * operation order as dj_iso4_body, so the two paths share their states. */
DJ_NOINLINE void dj_iso4_pass1(const struct dj_track *t,struct dj_z *z,const float *x,float *lo_o,float *hi_o,unsigned n) {
    const float ma1=t->c_mid.a1,ma2=t->c_mid.a2,ma3=t->c_mid.a3,md1=t->q_mid.d1,md2=t->q_mid.d2;
    const float ld1=t->q_lo.d1,ld2=t->q_lo.d2,hd1=t->q_hi.d1,hd2=t->q_hi.d2;
    float m1=z[0].ic1,m2=z[0].ic2,m3=z[1].ic1,m4=z[1].ic2;
    float p1=z[DJ_Z_AP_HI].ic1,p2=z[DJ_Z_AP_HI].ic2,q1=z[DJ_Z_AP_LO].ic1,q2=z[DJ_Z_AP_LO].ic2;
    for(unsigned i=0;i<n;i++) {
        float in=x[i],lp,bp,lo,hi;
        DJ_SVF(ma1,ma2,ma3,m1,m2,in,lp,bp);DJ_LP2(ma3,md1,md2,m3,m4,lp,lo);
        hi=in-2.8284271f*bp-lo;
        DJ_AP2(hd1,hd2,p1,p2,lo);DJ_AP2(ld1,ld2,q1,q2,hi);
        lo_o[i]=lo;hi_o[i]=hi;
    }
    z[0].ic1=m1;z[0].ic2=m2;z[1].ic1=m3;z[1].ic2=m4;
    z[DJ_Z_AP_HI].ic1=p1;z[DJ_Z_AP_HI].ic2=p2;z[DJ_Z_AP_LO].ic1=q1;z[DJ_Z_AP_LO].ic2=q2;
}
DJ_NOINLINE void dj_iso4_pass2(const struct dj_track *t,struct dj_z *z,const float *lo_i,const float *hi_i,float *x,unsigned n,const float *g) {
    const float la1=t->c_lo.a1,la2=t->c_lo.a2,la3=t->c_lo.a3,ld1=t->q_lo.d1,ld2=t->q_lo.d2;
    const float ha1=t->c_hi.a1,ha2=t->c_hi.a2,ha3=t->c_hi.a3,hd1=t->q_hi.d1,hd2=t->q_hi.d2;
    const float gh=g[0],dhm=g[1]-g[0],glm=g[2],dl=g[3]-g[2];
    float l1=z[DJ_Z_LO].ic1,l2=z[DJ_Z_LO].ic2,l3=z[DJ_Z_LO+1].ic1,l4=z[DJ_Z_LO+1].ic2;
    float h1=z[DJ_Z_HI].ic1,h2=z[DJ_Z_HI].ic2,h3=z[DJ_Z_HI+1].ic1,h4=z[DJ_Z_HI+1].ic2;
    for(unsigned i=0;i<n;i++) {
        float lo=lo_i[i],hi=hi_i[i],lp,bp,b_low,b_himid;
        DJ_SVF(la1,la2,la3,l1,l2,lo,lp,bp);DJ_LP2(la3,ld1,ld2,l3,l4,lp,b_low);
        float r_lo=lo-2.8284271f*bp;
        DJ_SVF(ha1,ha2,ha3,h1,h2,hi,lp,bp);DJ_LP2(ha3,hd1,hd2,h3,h4,lp,b_himid);
        float r_hi=hi-2.8284271f*bp;
        x[i]=gh*r_hi+dhm*b_himid+glm*r_lo+dl*b_low;
    }
    z[DJ_Z_LO].ic1=l1;z[DJ_Z_LO].ic2=l2;z[DJ_Z_LO+1].ic1=l3;z[DJ_Z_LO+1].ic2=l4;
    z[DJ_Z_HI].ic1=h1;z[DJ_Z_HI].ic2=h2;z[DJ_Z_HI+1].ic1=h3;z[DJ_Z_HI+1].ic2=h4;
}
static void dj_iso4_split(const struct dj_track *t,struct dj_z *z,float *x,unsigned n,const float *g) {
    float lo[32],hi[32];
    while(n) {
        unsigned k=n<32u?n:32u;
        dj_iso4_pass1(t,z,x,lo,hi,k);dj_iso4_pass2(t,z,lo,hi,x,k,g);
        x+=k;n-=k;
    }
}
/* Stereo: the same passes with both channels in one loop. The M7 is in
 * order and each filter is a serial chain (FMA latency ~5 cycles), so two
 * independent channels fill each other's stalls. Four short passes keep
 * both channels' states in registers. */
DJ_NOINLINE void dj_iso4s_mid(const struct dj_track *t,struct dj_z *zl,struct dj_z *zr,const float *xl,const float *xr,
                              float *lol,float *hil,float *lor,float *hir,unsigned n) {
    const float a1=t->c_mid.a1,a2=t->c_mid.a2,a3=t->c_mid.a3,d1=t->q_mid.d1,d2=t->q_mid.d2;
    float m1=zl[0].ic1,m2=zl[0].ic2,m3=zl[1].ic1,m4=zl[1].ic2;
    float n1=zr[0].ic1,n2=zr[0].ic2,n3=zr[1].ic1,n4=zr[1].ic2;
    for(unsigned i=0;i<n;i++) {
        float il=xl[i],ir=xr[i],lpl,bpl,lpr,bpr,lo_l,lo_r;
        DJ_SVF(a1,a2,a3,m1,m2,il,lpl,bpl);DJ_SVF(a1,a2,a3,n1,n2,ir,lpr,bpr);
        DJ_LP2(a3,d1,d2,m3,m4,lpl,lo_l);DJ_LP2(a3,d1,d2,n3,n4,lpr,lo_r);
        lol[i]=lo_l;lor[i]=lo_r;
        hil[i]=il-2.8284271f*bpl-lo_l;hir[i]=ir-2.8284271f*bpr-lo_r;
    }
    zl[0].ic1=m1;zl[0].ic2=m2;zl[1].ic1=m3;zl[1].ic2=m4;
    zr[0].ic1=n1;zr[0].ic2=n2;zr[1].ic1=n3;zr[1].ic2=n4;
}
DJ_NOINLINE void dj_iso4s_ap(const struct dj_track *t,struct dj_z *zl,struct dj_z *zr,float *lol,float *hil,float *lor,float *hir,unsigned n) {
    const float hd1=t->q_hi.d1,hd2=t->q_hi.d2,ld1=t->q_lo.d1,ld2=t->q_lo.d2;
    float p1=zl[DJ_Z_AP_HI].ic1,p2=zl[DJ_Z_AP_HI].ic2,q1=zl[DJ_Z_AP_LO].ic1,q2=zl[DJ_Z_AP_LO].ic2;
    float r1=zr[DJ_Z_AP_HI].ic1,r2=zr[DJ_Z_AP_HI].ic2,s1=zr[DJ_Z_AP_LO].ic1,s2=zr[DJ_Z_AP_LO].ic2;
    for(unsigned i=0;i<n;i++) {
        float a=lol[i],b=hil[i],c=lor[i],d=hir[i];
        DJ_AP2(hd1,hd2,p1,p2,a);DJ_AP2(ld1,ld2,q1,q2,b);DJ_AP2(hd1,hd2,r1,r2,c);DJ_AP2(ld1,ld2,s1,s2,d);
        lol[i]=a;hil[i]=b;lor[i]=c;hir[i]=d;
    }
    zl[DJ_Z_AP_HI].ic1=p1;zl[DJ_Z_AP_HI].ic2=p2;zl[DJ_Z_AP_LO].ic1=q1;zl[DJ_Z_AP_LO].ic2=q2;
    zr[DJ_Z_AP_HI].ic1=r1;zr[DJ_Z_AP_HI].ic2=r2;zr[DJ_Z_AP_LO].ic1=s1;zr[DJ_Z_AP_LO].ic2=s2;
}
/* One band split of both channels with its two gains: out = g_sum*(a+b)
 * band pair + g_diff*low part. add: accumulate into out. */
DJ_INLINE void dj_iso4s_split(const struct dj_svf *c,const struct dj_bq *q,struct dj_z *zl,struct dj_z *zr,
                              const float *il_,const float *ir_,float *ol,float *or_,unsigned n,float gs,float gd,int add) {
    const float a1=c->a1,a2=c->a2,a3=c->a3,d1=q->d1,d2=q->d2;
    float m1=zl[0].ic1,m2=zl[0].ic2,m3=zl[1].ic1,m4=zl[1].ic2;
    float n1=zr[0].ic1,n2=zr[0].ic2,n3=zr[1].ic1,n4=zr[1].ic2;
    for(unsigned i=0;i<n;i++) {
        float il=il_[i],ir=ir_[i],lpl,bpl,lpr,bpr,bl,br;
        DJ_SVF(a1,a2,a3,m1,m2,il,lpl,bpl);DJ_SVF(a1,a2,a3,n1,n2,ir,lpr,bpr);
        DJ_LP2(a3,d1,d2,m3,m4,lpl,bl);DJ_LP2(a3,d1,d2,n3,n4,lpr,br);
        float yl=gs*(il-2.8284271f*bpl)+gd*bl,yr=gs*(ir-2.8284271f*bpr)+gd*br;
        if(add){ol[i]+=yl;or_[i]+=yr;}else{ol[i]=yl;or_[i]=yr;}
    }
    zl[0].ic1=m1;zl[0].ic2=m2;zl[1].ic1=m3;zl[1].ic2=m4;
    zr[0].ic1=n1;zr[0].ic2=n2;zr[1].ic1=n3;zr[1].ic2=n4;
}
DJ_NOINLINE void dj_iso4s_lo(const struct dj_track *t,struct dj_z *zl,struct dj_z *zr,const float *lol,const float *lor,float *xl,float *xr,unsigned n,float gs,float gd)
{dj_iso4s_split(&t->c_lo,&t->q_lo,zl+DJ_Z_LO,zr+DJ_Z_LO,lol,lor,xl,xr,n,gs,gd,0);}
DJ_NOINLINE void dj_iso4s_hi(const struct dj_track *t,struct dj_z *zl,struct dj_z *zr,const float *hil,const float *hir,float *xl,float *xr,unsigned n,float gs,float gd)
{dj_iso4s_split(&t->c_hi,&t->q_hi,zl+DJ_Z_HI,zr+DJ_Z_HI,hil,hir,xl,xr,n,gs,gd,1);}
static void dj_iso4_stereo(struct dj_track *t,float *l,float *r,unsigned n,const float *g) {
    float lol[32],hil[32],lor[32],hir[32];
    struct dj_z *zl=t->z[0],*zr=t->z[1];
    while(n) {
        unsigned k=n<32u?n:32u;
        dj_iso4s_mid(t,zl,zr,l,r,lol,hil,lor,hir,k);
        dj_iso4s_ap(t,zl,zr,lol,hil,lor,hir,k);
        dj_iso4s_lo(t,zl,zr,lol,lor,l,r,k,g[2],g[3]-g[2]);
        dj_iso4s_hi(t,zl,zr,hil,hir,l,r,k,g[0],g[1]-g[0]);
        l+=k;r+=k;n-=k;
    }
}
/* 3-band stereo: Low split plus its x_hi allpass (written as gl * Low),
 * then the Mid/High split added on top. */
DJ_NOINLINE void dj_iso3s_lo(const struct dj_track *t,struct dj_z *zl,struct dj_z *zr,float *xl,float *xr,
                             float *restl,float *restr,unsigned n,float gl) {
    const float a1=t->c_lo.a1,a2=t->c_lo.a2,a3=t->c_lo.a3,d1=t->q_lo.d1,d2=t->q_lo.d2,hd1=t->q_hi.d1,hd2=t->q_hi.d2;
    float m1=zl[DJ_Z_LO].ic1,m2=zl[DJ_Z_LO].ic2,m3=zl[DJ_Z_LO+1].ic1,m4=zl[DJ_Z_LO+1].ic2,p1=zl[DJ_Z_AP_HI].ic1,p2=zl[DJ_Z_AP_HI].ic2;
    float n1=zr[DJ_Z_LO].ic1,n2=zr[DJ_Z_LO].ic2,n3=zr[DJ_Z_LO+1].ic1,n4=zr[DJ_Z_LO+1].ic2,r1=zr[DJ_Z_AP_HI].ic1,r2=zr[DJ_Z_AP_HI].ic2;
    for(unsigned i=0;i<n;i++) {
        float il=xl[i],ir=xr[i],lpl,bpl,lpr,bpr,lowl,lowr;
        DJ_SVF(a1,a2,a3,m1,m2,il,lpl,bpl);DJ_SVF(a1,a2,a3,n1,n2,ir,lpr,bpr);
        DJ_LP2(a3,d1,d2,m3,m4,lpl,lowl);DJ_LP2(a3,d1,d2,n3,n4,lpr,lowr);
        restl[i]=il-2.8284271f*bpl-lowl;restr[i]=ir-2.8284271f*bpr-lowr;
        DJ_AP2(hd1,hd2,p1,p2,lowl);DJ_AP2(hd1,hd2,r1,r2,lowr);
        xl[i]=gl*lowl;xr[i]=gl*lowr;
    }
    zl[DJ_Z_LO].ic1=m1;zl[DJ_Z_LO].ic2=m2;zl[DJ_Z_LO+1].ic1=m3;zl[DJ_Z_LO+1].ic2=m4;zl[DJ_Z_AP_HI].ic1=p1;zl[DJ_Z_AP_HI].ic2=p2;
    zr[DJ_Z_LO].ic1=n1;zr[DJ_Z_LO].ic2=n2;zr[DJ_Z_LO+1].ic1=n3;zr[DJ_Z_LO+1].ic2=n4;zr[DJ_Z_AP_HI].ic1=r1;zr[DJ_Z_AP_HI].ic2=r2;
}
static void dj_iso3_stereo(struct dj_track *t,float *l,float *r,unsigned n,const float *g) {
    float restl[32],restr[32];
    struct dj_z *zl=t->z[0],*zr=t->z[1];
    while(n) {
        unsigned k=n<32u?n:32u;
        dj_iso3s_lo(t,zl,zr,l,r,restl,restr,k,g[3]);
        dj_iso4s_hi(t,zl,zr,restl,restr,l,r,k,g[0],g[1]-g[0]);
        l+=k;r+=k;n-=k;
    }
}
/* Settled filter, both channels in one loop. */
DJ_INLINE void dj_filter_stereo_body(const struct dj_svf *c,const struct dj_svf *c2,struct dj_z *zl,struct dj_z *zr,
                                     struct dj_z *z2l,struct dj_z *z2r,float *xl,float *xr,unsigned n,int hp,int two) {
    const float a1=c->a1,a2=c->a2,a3=c->a3,k=c->k;
    const float b1=two?c2->a1:0.f,b2=two?c2->a2:0.f,b3=two?c2->a3:0.f,k2=two?c2->k:0.f;
    float s1=zl->ic1,s2=zl->ic2,u1=zr->ic1,u2=zr->ic2;
    float t1=two?z2l->ic1:0.f,t2=two?z2l->ic2:0.f,v1=two?z2r->ic1:0.f,v2=two?z2r->ic2:0.f;
    for(unsigned i=0;i<n;i++) {
        float il=xl[i],ir=xr[i],lpl,bpl,lpr,bpr;
        DJ_SVF(a1,a2,a3,s1,s2,il,lpl,bpl);DJ_SVF(a1,a2,a3,u1,u2,ir,lpr,bpr);
        float yl=hp ? il-k*bpl-lpl : lpl,yr=hp ? ir-k*bpr-lpr : lpr;
        if(two) {
            float y1l=yl,y1r=yr;
            DJ_SVF(b1,b2,b3,t1,t2,y1l,lpl,bpl);DJ_SVF(b1,b2,b3,v1,v2,y1r,lpr,bpr);
            yl=hp ? y1l-k2*bpl-lpl : lpl;yr=hp ? y1r-k2*bpr-lpr : lpr;
        }
        xl[i]=yl;xr[i]=yr;
    }
    zl->ic1=s1;zl->ic2=s2;zr->ic1=u1;zr->ic2=u2;
    if(two){z2l->ic1=t1;z2l->ic2=t2;z2r->ic1=v1;z2r->ic2=v2;}
}
DJ_NOINLINE void dj_filter_s_lp12(struct dj_track *t,float *l,float *r,unsigned n){dj_filter_stereo_body(&t->c_f,0,&t->zf[0],&t->zf[1],0,0,l,r,n,0,0);}
DJ_NOINLINE void dj_filter_s_hp12(struct dj_track *t,float *l,float *r,unsigned n){dj_filter_stereo_body(&t->c_f,0,&t->zf[0],&t->zf[1],0,0,l,r,n,1,0);}
DJ_NOINLINE void dj_filter_s_lp24(struct dj_track *t,float *l,float *r,unsigned n){dj_filter_stereo_body(&t->c_f,&t->c_f2,&t->zf[0],&t->zf[1],&t->zf2[0],&t->zf2[1],l,r,n,0,1);}
DJ_NOINLINE void dj_filter_s_hp24(struct dj_track *t,float *l,float *r,unsigned n){dj_filter_stereo_body(&t->c_f,&t->c_f2,&t->zf[0],&t->zf[1],&t->zf2[0],&t->zf2[1],l,r,n,1,1);}
/* Specialisations. The general loops (fades, ramps: a few blocks around each
 * change) keep the curve and filter side as run-time arguments. */
DJ_NOINLINE float dj_eq_channel(const struct dj_track *t,int iso,struct dj_z *z,float *x,unsigned n,
                                const float *g0,const float *dg,float w,int fade_in)
{return iso ? dj_iso4_body(t,z,x,n,g0,dg,w,fade_in,0) : dj_eq_body(t,0,z,x,n,g0,dg,w,fade_in,0);}
DJ_NOINLINE float dj_eq3_channel(const struct dj_track *t,int iso,struct dj_z *z,float *x,unsigned n,
                                 const float *g0,const float *dg,float w,int fade_in)
{return iso ? dj_iso3_body(t,z,x,n,g0,dg,w,fade_in,0) : dj_eq3_body(t,0,z,x,n,g0,dg,w,fade_in,0);}
DJ_NOINLINE void dj_eq4_iso_steady(const struct dj_track *t,struct dj_z *z,float *x,unsigned n,const float *g)
{dj_iso4_split(t,z,x,n,g);}
DJ_NOINLINE void dj_eq4_lr2_steady(const struct dj_track *t,struct dj_z *z,float *x,unsigned n,const float *g)
{(void)dj_eq_body(t,0,z,x,n,g,g,1.f,1,1);}
DJ_NOINLINE void dj_eq3_iso_steady(const struct dj_track *t,struct dj_z *z,float *x,unsigned n,const float *g)
{(void)dj_iso3_body(t,z,x,n,g,g,1.f,1,1);}
DJ_NOINLINE void dj_eq3_lr2_steady(const struct dj_track *t,struct dj_z *z,float *x,unsigned n,const float *g)
{(void)dj_eq3_body(t,0,z,x,n,g,g,1.f,1,1);}
DJ_NOINLINE float dj_filter_channel(const struct dj_svf *c,const struct dj_svf *c2,struct dj_z *z,struct dj_z *z2,
                                    float *x,unsigned n,int hp,int off,float w,int fade_in)
{return dj_filter_body(c,c2,z,z2,x,n,hp,off,w,fade_in,0);}
DJ_NOINLINE void dj_filter_lp12(const struct dj_svf *c,struct dj_z *z,float *x,unsigned n)
{struct dj_z unused={0.f,0.f};(void)dj_filter_body(c,0,z,&unused,x,n,0,0,1.f,1,1);}
DJ_NOINLINE void dj_filter_hp12(const struct dj_svf *c,struct dj_z *z,float *x,unsigned n)
{struct dj_z unused={0.f,0.f};(void)dj_filter_body(c,0,z,&unused,x,n,1,0,1.f,1,1);}
DJ_NOINLINE void dj_filter_lp24(const struct dj_svf *c,const struct dj_svf *c2,struct dj_z *z,struct dj_z *z2,float *x,unsigned n)
{(void)dj_filter_body(c,c2,z,z2,x,n,0,0,1.f,1,1);}
DJ_NOINLINE void dj_filter_hp24(const struct dj_svf *c,const struct dj_svf *c2,struct dj_z *z,struct dj_z *z2,float *x,unsigned n)
{(void)dj_filter_body(c,c2,z,z2,x,n,1,0,1.f,1,1);}
static void dj_filter_steady(const struct dj_track *t,const struct dj_svf *c2,int hp,unsigned ch,float *x,unsigned n) {
    struct dj_track *m=(struct dj_track *)t;
    if(c2){if(hp)dj_filter_hp24(&t->c_f,c2,&m->zf[ch],&m->zf2[ch],x,n);else dj_filter_lp24(&t->c_f,c2,&m->zf[ch],&m->zf2[ch],x,n);}
    else if(hp)dj_filter_hp12(&t->c_f,&m->zf[ch],x,n);else dj_filter_lp12(&t->c_f,&m->zf[ch],x,n);
}

/* One block, in place. r may be 0 for mono. */
/* Process one block in place. r may be 0 for mono. */
static void dj_process(struct dj_track *t,const struct dj_settings *s,float *l,float *r,unsigned n) {
    if(!n)return;
    int iso=s->curve==DJ_CURVE_ISO;
    float target[4];
    for(unsigned b=0;b<4;b++)target[b]=dj_band_gain(t->band[b],s->curve);
    int three=s->bands==3;
    if(three)target[2]=1.f;
    /* EQ path activity with hold and crossfade. */
    int eq_want=!dj_eq_neutral(t,s->bands);
    if(eq_want)t->hold=DJ_HOLD;else t->hold=t->hold>n?t->hold-n:0;
    int eq_run=eq_want || t->hold || t->wet>0.f;
    float fp_target=(t->filt>-DJ_FILT_DEAD && t->filt<DJ_FILT_DEAD)?0.f:(float)t->filt*(1.f/DJ_FILT_MAX);
    int f_want=fp_target!=0.f;
    if(f_want)t->fhold=DJ_HOLD;else t->fhold=t->fhold>n?t->fhold-n:0;
    int f_run=f_want || t->fhold || t->fwet>0.f;
    if(!eq_run && !f_run){t->fpos=0.f;return;}
    if(eq_run) {
        dj_coefficients(t,s);
        float g0[4],dg[4],inv=1.f/(float)n;
        /* Gains move at most DJ_GAIN_RATE per sample, so a kill button or a
         * MIDI jump fades over about 10 ms instead of one block. */
        float g1[4],lim=DJ_GAIN_RATE*(float)n;
        for(unsigned b=0;b<4;b++) {
            float d=target[b]-t->gain[b];
            if(d>lim)d=lim;
            if(d< -lim)d=-lim;
            g0[b]=t->gain[b];g1[b]=g0[b]+d;dg[b]=d*inv;
        }
        int fade_in=eq_want || t->hold;
        if(fade_in && t->wet>=1.f && !dg[0] && !dg[1] && !dg[2] && !dg[3]) {
            void (*f)(const struct dj_track *,struct dj_z *,float *,unsigned,const float *)=
                three ? (iso ? dj_eq3_iso_steady : dj_eq3_lr2_steady) : (iso ? dj_eq4_iso_steady : dj_eq4_lr2_steady);
            if(r && iso)(three ? dj_iso3_stereo : dj_iso4_stereo)(t,l,r,n,g0);
            else{f(t,t->z[0],l,n,g0);if(r)f(t,t->z[1],r,n,g0);}
        } else {
            float w=three ? dj_eq3_channel(t,iso,t->z[0],l,n,g0,dg,t->wet,fade_in)
                          : dj_eq_channel(t,iso,t->z[0],l,n,g0,dg,t->wet,fade_in);
            if(r)(void)(three ? dj_eq3_channel(t,iso,t->z[1],r,n,g0,dg,t->wet,fade_in)
                              : dj_eq_channel(t,iso,t->z[1],r,n,g0,dg,t->wet,fade_in));
            t->wet=w;
        }
        for(unsigned b=0;b<4;b++)t->gain[b]=g1[b];
        if(!fade_in && t->wet<=0.f)for(unsigned c=0;c<2;c++)for(unsigned i=0;i<DJ_Z_N;i++)t->z[c][i].ic1=t->z[c][i].ic2=0.f;
    }
    if(f_run) {
        /* One-pole smoothing of the knob per block (about 15 ms), so MIDI CC
         * steps sweep instead of jumping. Crossing the detent snaps over. */
        float p=t->fpos;
        if((p<0.f && fp_target>0.f) || (p>0.f && fp_target<0.f))p=0.f;
        p+=(fp_target-p)*((float)n/(720.f+(float)n));
        if(!f_want && p>-.001f && p<.001f)p=0.f;
        t->fpos=p;
        int fade_in=f_want || t->fhold;
        if(t->fslope!=s->slope){t->fslope=s->slope;t->zf2[0].ic1=t->zf2[0].ic2=t->zf2[1].ic1=t->zf2[1].ic2=0.f;}
        /* LP <-> HP while the filter is still heard: switching the output
         * tap would step. Fade to dry on the old setting first, then switch. */
        int flip=p!=0.f && (p>0.f)!=t->fhp && t->fwet>0.f;
        if(flip)fade_in=0;
        else if(p!=0.f)dj_filter_coeffs(t,s);
        const struct dj_svf *c2=s->slope==24 ? &t->c_f2 : 0;
        /* Back at the detent the filter keeps its last setting (nearly
         * transparent) while the 10 ms fade returns to the dry signal. */
        int hp=p!=0.f && !flip ? p>0.f : t->fhp;
        if(hp!=t->fhp){t->zf[0].ic1=t->zf[0].ic2=t->zf[1].ic1=t->zf[1].ic2=0.f;
            t->zf2[0].ic1=t->zf2[0].ic2=t->zf2[1].ic1=t->zf2[1].ic2=0.f;}
        t->fhp=hp;
        if(fade_in && t->fwet>=1.f) {
            if(r)(c2 ? (hp ? dj_filter_s_hp24 : dj_filter_s_lp24) : (hp ? dj_filter_s_hp12 : dj_filter_s_lp12))(t,l,r,n);
            else dj_filter_steady(t,c2,hp,0,l,n);
        } else {
            float w=dj_filter_channel(&t->c_f,c2,&t->zf[0],&t->zf2[0],l,n,hp,0,t->fwet,fade_in);
            if(r)(void)dj_filter_channel(&t->c_f,c2,&t->zf[1],&t->zf2[1],r,n,hp,0,t->fwet,fade_in);
            t->fwet=w;
        }
        if(!fade_in && t->fwet<=0.f){t->zf[0].ic1=t->zf[0].ic2=t->zf[1].ic1=t->zf[1].ic2=0.f;
            t->zf2[0].ic1=t->zf2[0].ic2=t->zf2[1].ic1=t->zf2[1].ic2=0.f;}
    }
}
#endif
