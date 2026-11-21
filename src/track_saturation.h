/* Per-track saturation: the master Soft curve and level reference
 * (soft_sat.h), one instance per track, morphing towards an increasingly
 * overdriven hard clip with Curve. Drive is level-compensated, so it adds harmonics rather than volume.
 * Production DSP is also compiled for host tests. */
#ifndef BLUEBOX_TRACK_SATURATION_H
#define BLUEBOX_TRACK_SATURATION_H
#include <stdint.h>
#include "soft_sat.h"
#define TS_ID 0x1bau
#define TS_CURVE_ID 0x1bbu
struct track_sat {void *owner;int16_t drive,curve;float g,mix;};
static void ts_reset(struct track_sat *t,void *owner) {
    t->owner=owner;t->drive=0;t->curve=0;t->g=1.f;t->mix=0.f;
}
static int16_t ts_clamp(int value) {return (int16_t)(value<0?0:value>1000?1000:value);}
static void ts_event(struct track_sat *t,int value) {t->drive=ts_clamp(value);}
static void ts_curve(struct track_sat *t,int value) {t->curve=ts_clamp(value);}
/* Curve m blends Soft (0) into a hard clip (1). The clip is fed up to 4x
 * the driven signal as m rises, so full Curve clips far more headroom than
 * a plain clip at the same drive. m=0 is the Soft curve. */
static float ts_shape(float y,float m) {
    float s=sat(y),c=y*(1.f+3.f*m);
    c=c>1.f?1.f:c<-1.f?-1.f:c;
    return s+m*(c-s);
}
/* Drive 0 with its fade finished is an exact bypass: samples are not touched. */
static int ts_active(const struct track_sat *t) {return t->drive!=0 || t->mix!=0.f;}
/* Bad samples cannot poison the curve; only applied while active. */
static float ts_finite(float x) {return x>=-16.f && x<=16.f?x:0.f;}
/* r is 0 for a mono track; an aliased r is processed once. Drive edits ramp
 * gain and blend across the block, as on the master. */
static void ts_process(struct track_sat *t,float *l,float *r,unsigned n) {
    if(!n || !ts_active(t))return;
    if(r==l)r=0;
    float d=(float)t->drive*.001f,m=(float)t->curve*.001f,g1=exp2f_(6.f*d),mix1=d*20.f;
    if(mix1>1.f)mix1=1.f;
    float g0=t->g,mix0=t->mix;t->g=g1;t->mix=mix1;
    if(g0==g1 && mix0==mix1) {
        float k=SAT_REF/ts_shape(g1*SAT_REF,m);
        for(unsigned i=0;i<n;i++) {
            float x=ts_finite(l[i]);l[i]=x+mix1*(ts_shape(x*g1,m)*k-x);
            if(r){x=ts_finite(r[i]);r[i]=x+mix1*(ts_shape(x*g1,m)*k-x);}
        }
        return;
    }
    float inv=1.f/(float)n,dg=(g1-g0)*inv,dm=(mix1-mix0)*inv,g=g0,mix=mix0;
    for(unsigned i=0;i<n;i++) {
        g+=dg;mix+=dm;
        float k=SAT_REF/ts_shape(g*SAT_REF,m);
        float x=ts_finite(l[i]);l[i]=x+mix*(ts_shape(x*g,m)*k-x);
        if(r){x=ts_finite(r[i]);r[i]=x+mix*(ts_shape(x*g,m)*k-x);}
    }
}
#endif
