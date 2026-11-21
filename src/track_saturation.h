/* Per-track Soft saturation: the master Soft curve and level reference
 * (soft_sat.h), one instance per track. Drive is level-compensated, so it adds
 * harmonics rather than volume. Production DSP is also compiled for host tests. */
#ifndef BLUEBOX_TRACK_SATURATION_H
#define BLUEBOX_TRACK_SATURATION_H
#include <stdint.h>
#include "soft_sat.h"
#define TS_ID 0x1bau
struct track_sat {void *owner;int32_t drive;float g,mix;};
static void ts_reset(struct track_sat *t,void *owner) {
    t->owner=owner;t->drive=0;t->g=1.f;t->mix=0.f;
}
static void ts_event(struct track_sat *t,int value) {
    t->drive=value<0?0:value>1000?1000:value;
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
    float d=(float)t->drive*.001f,g1=exp2f_(6.f*d),mix1=d*20.f;
    if(mix1>1.f)mix1=1.f;
    float g0=t->g,mix0=t->mix;t->g=g1;t->mix=mix1;
    if(g0==g1 && mix0==mix1) {
        float k=SAT_REF/sat(g1*SAT_REF);
        for(unsigned i=0;i<n;i++) {
            float x=ts_finite(l[i]);l[i]=x+mix1*(sat(x*g1)*k-x);
            if(r){x=ts_finite(r[i]);r[i]=x+mix1*(sat(x*g1)*k-x);}
        }
        return;
    }
    float inv=1.f/(float)n,dg=(g1-g0)*inv,dm=(mix1-mix0)*inv,g=g0,mix=mix0;
    for(unsigned i=0;i<n;i++) {
        g+=dg;mix+=dm;
        float k=SAT_REF/sat(g*SAT_REF);
        float x=ts_finite(l[i]);l[i]=x+mix*(sat(x*g)*k-x);
        if(r){x=ts_finite(r[i]);r[i]=x+mix*(sat(x*g)*k-x);}
    }
}
#endif
