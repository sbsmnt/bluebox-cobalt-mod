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
 * Cost: an LR4 split is two SVFs (the second-order allpass falls out of the
 * first, and HP4 = AP2 - LP4), so the ISO tree is eight SVFs per channel;
 * the per-sample loops keep every state in registers. */
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
static void dj_coefficients(struct dj_track *t,const struct dj_settings *s) {
    int iso=s->curve==DJ_CURVE_ISO;
    if(t->cur.curve==s->curve && t->cur.bands==s->bands && t->cur.x_lo==s->x_lo && t->cur.x_mid==s->x_mid && t->cur.x_hi==s->x_hi)return;
    float k=iso ? 1.4142136f : 2.f;
    dj_svf_set(&t->c_lo,(float)s->x_lo,k);dj_svf_set(&t->c_mid,(float)s->x_mid,k);dj_svf_set(&t->c_hi,(float)s->x_hi,k);
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

/* One channel of the band tree. Gains ramp linearly over the block; the
 * dry/processed crossfade moves by step per sample towards fade_in. */
static float dj_eq_channel(const struct dj_track *t,int iso,struct dj_z *z,float *x,unsigned n,
                           const float *g0,const float *dg,float w,int fade_in) {
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
        gh+=dgh;ghm+=dghm;glm+=dglm;gl+=dgl;
        float y=gh*b_high+ghm*b_himid+glm*b_lomid+gl*b_low;
        if(fading) {
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
static float dj_eq3_channel(const struct dj_track *t,int iso,struct dj_z *z,float *x,unsigned n,
                            const float *g0,const float *dg,float w,int fade_in) {
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
        gh+=dgh;gm+=dgm;gl+=dgl;
        float y=gh*high+gm*mid+gl*low;
        if(fading) {
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
static float dj_filter_channel(const struct dj_svf *c,const struct dj_svf *c2,struct dj_z *z,struct dj_z *z2,
                               float *x,unsigned n,int hp,int off,float w,int fade_in) {
    const float a1=c->a1,a2=c->a2,a3=c->a3,k=c->k,step=1.f/(float)DJ_FADE;
    const float b1=c2?c2->a1:0.f,b2=c2?c2->a2:0.f,b3=c2?c2->a3:0.f,k2=c2?c2->k:0.f;
    float s1=z->ic1,s2=z->ic2,t1=z2->ic1,t2=z2->ic2;
    int fading=fade_in ? w<1.f : w>0.f;
    for(unsigned i=0;i<n;i++) {
        float in=x[i],lp,bp;
        DJ_SVF(a1,a2,a3,s1,s2,in,lp,bp);
        float y=off ? in : hp ? in-k*bp-lp : lp;
        if(c2 && !off){float y1=y;DJ_SVF(b1,b2,b3,t1,t2,y1,lp,bp);y=hp ? y1-k2*bp-lp : lp;}
        if(fading) {
            if(fade_in){w+=step;if(w>=1.f){w=1.f;fading=0;}}
            else{w-=step;if(w<=0.f){w=0.f;fading=0;}}
            x[i]=in+(y-in)*w;
        } else x[i]=fade_in ? y : in;
    }
    z->ic1=s1;z->ic2=s2;z2->ic1=t1;z2->ic2=t2;
    return w;
}

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
        float w=three ? dj_eq3_channel(t,iso,t->z[0],l,n,g0,dg,t->wet,fade_in)
                      : dj_eq_channel(t,iso,t->z[0],l,n,g0,dg,t->wet,fade_in);
        if(r)(void)(three ? dj_eq3_channel(t,iso,t->z[1],r,n,g0,dg,t->wet,fade_in)
                          : dj_eq_channel(t,iso,t->z[1],r,n,g0,dg,t->wet,fade_in));
        t->wet=w;
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
        float w=dj_filter_channel(&t->c_f,c2,&t->zf[0],&t->zf2[0],l,n,hp,0,t->fwet,fade_in);
        if(r)(void)dj_filter_channel(&t->c_f,c2,&t->zf[1],&t->zf2[1],r,n,hp,0,t->fwet,fade_in);
        t->fwet=w;
        if(!fade_in && t->fwet<=0.f){t->zf[0].ic1=t->zf[0].ic2=t->zf[1].ic1=t->zf[1].ic2=0.f;
            t->zf2[0].ic1=t->zf2[0].ic2=t->zf2[1].ic1=t->zf2[1].ic2=0.f;}
    }
}
#endif
