/* Original bounded extensions, compiled unchanged for ARM and host checks.
 * Reverse shares the existing capture ring, but has only two playback heads.
 * Delay/Granular retain their original renderer and mode values 0/1. */
#ifndef BLUEBOX_DELAY_TYPES_H
#define BLUEBOX_DELAY_TYPES_H
#define X_MAGIC 0x4454593cu
struct delay_types {
    uint32_t magic;void *owner;
    int mode,drive,duck,release,segment,tone;
    float tape0,tape1,drive0,drive1,drive_sm;
    float tape_lp[6];uint32_t tape_valid;
    float duck_mix,envelope,rev_mix,rev_lp[2];
    uint32_t countdown;
    struct grain heads[2];
    float previous[2][32];
    float *capture_l,*capture_r;uint32_t capture_n;
    uint32_t reverse_frames; /* native clock-resolved segment; zero selects free ms */
    int32_t cutoff;float cutoff_sm,cutoff0,cutoff1;
    int32_t wild;float wild0,wild1;
    float wild_lp[6],wild_post[6];uint32_t wild_valid;
};
/* exp.8 Tape Lo C: one-pole high-pass per ring, after saturation, Hi Cut and
 * WILD, inside the feedback path (each repeat loses a little more low end).
 * Coefficient f/(f+7639.437) as Hi Cut, smoothed with the same block ramp.
 * 20 Hz is Off: the filter's contribution fades out over 240 samples and is
 * then skipped, so existing Tape projects are bit-identical. State does not
 * fit the 496-byte delay-types block; the firmware places it in free backup
 * SRAM (XL_STATE), hosts use a static instance. */
struct x_locut {uint32_t magic;int32_t hz;float b0,b1,m0,m1;float hp[6];uint32_t valid;};
#define XL_MAGIC 0x584c4331u
#ifndef XL_STATE
static struct x_locut x_locut_host;
#define XL_STATE (&x_locut_host)
#endif
static inline void x_locut_reset(void) {
    struct x_locut *l=XL_STATE;
    l->hz=20;l->b0=l->b1=20.f/(20.f+7639.437f);l->m0=l->m1=0.f;
    for(unsigned i=0;i<6;i++)l->hp[i]=0.f;
    l->valid=0;l->magic=XL_MAGIC;
}
static inline void x_reset(struct delay_types *x,void *owner) {
    uint32_t *p=(uint32_t *)x;
    for(unsigned i=0;i<sizeof(*x)/4;i++)p[i]=0;
    x->owner=owner;x->drive=350;x->duck=700;x->release=300;
    x->segment=400;x->tone=650;x->drive_sm=.35f;
    x->cutoff=6000;x->cutoff_sm=x->cutoff0=x->cutoff1=6000.f/(6000.f+7639.437f);
    x->magic=X_MAGIC;
    x_locut_reset();
}
static inline void x_event(struct delay_types *x,unsigned id,int value) {
    switch(id) {
    case G_MODE:x->mode=value>=0&&value<=5?value:0;break;
    case T_DRIVE:x->drive=g_clampi(value,0,1000);break;
    case T_DUCK:x->duck=g_clampi(value,0,1000);break;
    case T_RELEASE:x->release=g_clampi(value,20,2000);break;
    case T_SEGMENT:x->segment=g_clampi(value,40,600);break;
    case T_TONE:x->tone=g_clampi(value,0,1000);break;
    case T_HICUT:x->cutoff=g_clampi(value,500,16000);break;
    case T_WILD:x->wild=g_clampi(value,0,1);break;
    case T_LOCUT:if(XL_STATE->magic!=XL_MAGIC)x_locut_reset();XL_STATE->hz=g_clampi(value,20,2000);break;
    default:break;
    }
}
static inline float x_move(float value,float target,float step) {
    if(value<target){value+=step;if(value>target)value=target;}
    else if(value>target){value-=step;if(value<target)value=target;}
    return value;
}
static inline void x_prepare(struct delay_types *x,unsigned n) {
    x->wild0=x->wild1;
    x->wild1=x_move(x->wild1,x->wild?1.f:0.f,(float)n/240.f);
    if(!x->wild0 && !x->wild1)x->wild_valid=0;
    x->tape0=x->tape1;
    x->tape1=x_move(x->tape1,x->mode==2?1.f:0.f,(float)n/240.f);
    x->drive0=x->drive_sm;
    float amount=(float)x->drive*.001f,step=(float)n/960.f;
    if(step>1.f)step=1.f;
    x->drive1=x->drive_sm+=(amount-x->drive_sm)*step;
    x->cutoff0=x->cutoff_sm;
    float hz=(float)x->cutoff,alpha=hz/(hz+7639.437f);
    x->cutoff1=x->cutoff_sm+=(alpha-x->cutoff_sm)*step;
    if(x->tape0==0.f && x->tape1==0.f){x->tape_valid=0;x->wild_valid=0;}
    struct x_locut *l=XL_STATE;
    if(l->magic!=XL_MAGIC)x_locut_reset();
    /* Only Tape uses Lo C: other modes skip its coefficient work. */
    if(x->tape0>0.f || x->tape1>0.f || l->m1>0.f) {
        float lhz=(float)l->hz;
        l->b0=l->b1;l->b1+=(lhz/(lhz+7639.437f)-l->b1)*step;
        l->m0=l->m1;l->m1=x_move(l->m1,l->hz>20 && (x->tape0>0.f || x->tape1>0.f)?1.f:0.f,(float)n/240.f);
    } else l->m0=0.f;
    if((x->tape0==0.f && x->tape1==0.f) || (l->m0==0.f && l->m1==0.f))l->valid=0;
}
/* Runs after the native read/resampling, before native feedback writes. Each
 * ring owns one low-pass state. All reader calls use the same block ramp.
 * PING runs two serial stages on the left (lines0/1), and with QUAD also
 * on the right (lines2/3). Split the rational curve over those stages so
 * f_half(f_half(x))=f_full(x) at DC, instead of doubling regenerative gain. */
static inline float x_sqrt_positive(float v) {
#if defined(__arm__) || defined(__thumb__)
    float r;__asm__("vsqrt.f32 %0,%1":"=t"(r):"t"(v));return r;
#else
    return sqrtf(v);
#endif
}
/* Positive-domain rational-drive factorization. No sample/channel mixing. */
static inline void x_tape(struct delay_types *x,float *out,unsigned n,unsigned line,int split) {
    if(line>=6 || (!x->tape0 && !x->tape1) || !n)return;
    float lp=x->tape_lp[line];
    if(!(x->tape_valid&(1u<<line))){lp=g_limit(out[0]);x->tape_valid|=1u<<line;}
    float dm=(x->tape1-x->tape0)/(float)n,dd=(x->drive1-x->drive0)/(float)n;
    float mix=x->tape0,drive=x->drive0,alpha=x->cutoff0;
    float da=(x->cutoff1-x->cutoff0)/(float)n;
    int wild=x->wild0>0.f || x->wild1>0.f;
    float wm=x->wild0,dw=(x->wild1-x->wild0)/(float)n;
    float wl=x->wild_lp[line],wp=x->wild_post[line];
    struct x_locut *lc=XL_STATE;
    int locut=lc->m0>0.f || lc->m1>0.f;
    float hl=lc->hp[line],hb=lc->b0,dhb=(lc->b1-lc->b0)/(float)n;
    float hm=lc->m0,dhm=(lc->m1-lc->m0)/(float)n;
    if(locut && !(lc->valid&(1u<<line))){hl=0.f;lc->valid|=1u<<line;}
    if(wild && !(x->wild_valid&(1u<<line))) {
        wl=g_limit(out[0]);
        float k=6.f*x->drive0;
        if(split) {
            float gain=x_sqrt_positive(.9440608763f*(1.f+.2f*k));
            float dk=k/(1.f+gain);
            wp=wl*gain/(1.f+dk*g_abs(wl));
        } else wp=.9440608763f*wl*(1.f+.2f*k)/(1.f+k*g_abs(wl));
        x->wild_valid|=1u<<line;
    }
    for(unsigned i=0;i<n;i++) {
        mix+=dm;drive+=dd;float a=g_limit(out[i]);
        /* No makeup gain inside the feedback loop: the saturation slope at
         * zero is unity, so quiet tails do not acquire extra regeneration. */
        float k=drive*6.f;
        float saturated=a/(1.f+k*g_abs(a));
        alpha+=da;lp+=(saturated-lp)*alpha;
        float colored=lp;
        if(wild) {
            /* Original Tape pre-drive damping and regenerative makeup,
             * trimmed0.5dB in-loop. Keep the adjustable post-drive Hi Cut. */
            float v;
            if(split) {
                /* Two half-stages compose to G*x/(1+k*abs(x)). Halve the
                 * filter time constant too; the two-pole spectrum differs. */
                wl+=(a-wl)*.6864f; /* 1-(1-.44)^2 */
                float gain=x_sqrt_positive(.9440608763f*(1.f+.2f*k));
                float dk=k/(1.f+gain);
                v=wl*gain/(1.f+dk*g_abs(wl));
                wp+=(v-wp)*(alpha*(2.f-alpha));
            } else {
                wl+=(a-wl)*.44f;
                v=.9440608763f*wl*(1.f+.2f*k)/(1.f+k*g_abs(wl));
                wp+=(v-wp)*alpha;
            }
            wm+=dw;
            colored+=(wp-colored)*wm;
        }
        if(locut){hb+=dhb;hm+=dhm;hl+=(colored-hl)*hb;colored-=hl*hm;}
        out[i]=a+(colored-a)*mix;
    }
    x->tape_lp[line]=lp;if(locut)lc->hp[line]=hl;
    if(wild){x->wild_lp[line]=wl;x->wild_post[line]=wp;}
}
/* Off retains v0.9.3 return-only compensation after the feedback write.
 * WILD fades that gain to unity: its makeup already lives in the loop.
 * Blends/Drive use the same block ramps as the feedback-loop coloration. */
static inline void x_tape_return(struct delay_types *x,float *l,float *r,unsigned n) {
    if(!n || (!x->tape0 && !x->tape1))return;
    float mix=x->tape0,drive=x->drive0;
    int wild=x->wild0>0.f || x->wild1>0.f;
    float wm=x->wild0,dw=(x->wild1-x->wild0)/(float)n;
    float dm=(x->tape1-x->tape0)/(float)n,dd=(x->drive1-x->drive0)/(float)n;
    for(unsigned i=0;i<n;i++) {
        mix+=dm;drive+=dd;
        float delta=.9440608763f*(1.f+1.2f*drive)-1.f;
        if(wild){wm+=dw;delta*=1.f-wm;} /* makeup already in WILD feedback */
        float gain=1.f+delta*mix;
        l[i]*=gain;r[i]*=gain;
    }
}
/* The detector sees the FX1 input before the native delay mutates it. Only
 * the return is attenuated; the native feedback loop continues underneath. */
static inline void x_duck(struct delay_types *x,const float *il,const float *ir,float *l,float *r,unsigned n) {
    if(x->mode!=3 && x->duck_mix==0.f)return;
    float depth=(float)x->duck*.001f,release=1.f/((float)x->release*48.f);
    for(unsigned i=0;i<n;i++) {
        float a=g_abs(g_limit(il[i])),b=g_abs(g_limit(ir[i]));if(b>a)a=b;
        float c=a>x->envelope?.01f:release;
        x->envelope+=(a-x->envelope)*c;
        x->duck_mix=x_move(x->duck_mix,x->mode==3?1.f:0.f,1.f/240.f);
        float gain=1.f/(1.f+32.f*depth*x->envelope);
        gain=1.f+(gain-1.f)*x->duck_mix;
        l[i]*=gain;r[i]*=gain;
    }
}
/* One-block feedback stash avoids a second writer into the shared ring.
 * g_render's Normal branch performs the sole capture write for Reverse. */
static inline void x_reverse_input(struct delay_types *x,struct granular *g,float *l,float *r,unsigned n) {
    if(x->mode!=4 && x->rev_mix==0.f)return;
    float gain=(float)g->feedback*.001f*x->rev_mix;
    for(unsigned i=0;i<n;i++) {
        /* x/(1+|x|) for both channels from one reciprocal (exp.8). */
        float a=x->previous[0][i]*gain,b=x->previous[1][i]*gain;
        float da=1.f+g_abs(a),db=1.f+g_abs(b),inv=1.f/(da*db);
        l[i]=g_limit(l[i]+a*db*inv);r[i]=g_limit(r[i]+b*da*inv);
    }
}
static inline unsigned x_spawn(struct delay_types *x,struct granular *g,unsigned wr,unsigned valid,unsigned len,float ratio) {
    if(valid<(unsigned)(ratio*(float)len)+16u)return 1;
    struct grain *p=0;
    for(unsigned j=0;j<2;j++)if(!x->heads[j].len){p=&x->heads[j];break;}
    if(!p)return 1;
    p->pos=(float)wr-4.f;if(p->pos<0.f)p->pos+=(float)G_CAP;
    p->step=-ratio;p->age=0;p->len=len;p->invlen=1.f/(float)len;
    (void)g;
    return len/2u;
}
/* Two 50%-overlapped, zero-ended reverse windows. At maximum segment and
 * +12 semitones, read age stays below 3*28800+4 < the 96000-frame ring.
 * wr0/valid0 describe the block before g_render's sole capture write. */
/* exp.8: block-local heads, filter and blend state (output stores cannot
 * alias them), one reciprocal for the weight normalization; built at -O2.
 * Heads are published before a spawn, which reads and writes x->heads. */
static inline void x_reverse(struct delay_types *x,struct granular *g,float *out_l,float *out_r,unsigned n,unsigned wr0,unsigned valid0) {
    const float *buf=g->buffer;
    if(!buf)return;
    if(x->mode!=4 && x->rev_mix==0.f) {
        x->heads[0].len=x->heads[1].len=0;x->countdown=0;
        x->rev_lp[0]=x->rev_lp[1]=0.f;
        for(unsigned i=0;i<32;i++)x->previous[0][i]=x->previous[1][i]=0.f;
        return;
    }
    unsigned len=x->reverse_frames?x->reverse_frames:(unsigned)x->segment*48u,wr=wr0,valid=valid0;
    /* Writer and reverse head approach one another. Reserve block + interpolation
     * space at every pitch; long beat divisions saturate at this safe duration. */
    unsigned limit=(unsigned)((float)(G_CAP-128u)/(1.f+g_ratio(g->pitch))) & ~1u;
    if(len>limit)len=limit;
    if(len<256u)len=256u;
    /* Leaving frozen Granular can resume capture part-way through a block.
     * The legacy renderer then wrote only a trailing subset of its samples. */
    unsigned captured=g->wr>=wr0?g->wr-wr0:g->wr+G_CAP-wr0;
    if(captured>n)captured=n;
    unsigned skipped=n-captured;
    float ratio=g_ratio(g->pitch),freq=200.f+(float)x->tone*9.8f;
    float alpha=freq/(freq+7639.437f);
    struct grain heads[2]={x->heads[0],x->heads[1]};
    uint32_t countdown=x->countdown;
    float lp0=x->rev_lp[0],lp1=x->rev_lp[1],mix=x->rev_mix,target=x->mode==4?1.f:0.f;
    for(unsigned i=0;i<n;i++) {
        if(i>=skipped) {if(++wr==G_CAP)wr=0;if(valid<G_CAP)valid++;}
        if(!countdown) {
            x->heads[0]=heads[0];x->heads[1]=heads[1];
            countdown=x_spawn(x,g,wr,valid,len,ratio);
            heads[0]=x->heads[0];heads[1]=x->heads[1];
        }
        countdown--;
        float l=0.f,r=0.f,weights=0.f;
        for(unsigned j=0;j<2;j++) {
            struct grain *p=&heads[j];if(!p->len)continue;
            unsigned a=(unsigned)p->pos,b=a+1;if(b==G_CAP)b=0;
            if(a>=G_CAP){p->len=0;continue;}
            unsigned aa=wr>a?wr-a:wr+G_CAP-a,ab=wr>b?wr-b:wr+G_CAP-b;
            if(!aa||!ab||aa>valid||ab>valid||aa>=G_CAP-n-2u||ab>=G_CAP-n-2u){p->len=0;continue;}
            float f=p->pos-(float)a,t=(float)p->age*p->invlen,w=4.f*t*(1.f-t);
            const float *pa=buf+2*a,*pb=buf+2*b;
            l+=(pa[0]+(pb[0]-pa[0])*f)*w;
            r+=(pa[1]+(pb[1]-pa[1])*f)*w;
            weights+=w;p->pos+=p->step;
            if(p->pos<0.f)p->pos+=(float)G_CAP;
            if(++p->age>=p->len)p->len=0;
        }
        if(weights>1.f){float iw=1.f/weights;l*=iw;r*=iw;}
        lp0+=(g_limit(l)-lp0)*alpha;
        lp1+=(g_limit(r)-lp1)*alpha;
        l=lp0;r=lp1;
        x->previous[0][i]=l;x->previous[1][i]=r;
        mix=x_move(mix,target,1.f/240.f);
        if(mix==1.f){out_l[i]=l;out_r[i]=r;}
        else {out_l[i]+=(l-out_l[i])*mix;out_r[i]+=(r-out_r[i])*mix;}
    }
    x->heads[0]=heads[0];x->heads[1]=heads[1];x->countdown=countdown;
    x->rev_lp[0]=lp0;x->rev_lp[1]=lp1;x->rev_mix=mix;
    for(unsigned i=n;i<32;i++)x->previous[0][i]=x->previous[1][i]=0.f;
    if(x->mode!=4 && x->rev_mix==0.f) {
        x->heads[0].len=x->heads[1].len=0;x->countdown=0;
        x->rev_lp[0]=x->rev_lp[1]=0.f;
        for(unsigned i=0;i<32;i++)x->previous[0][i]=x->previous[1][i]=0.f;
    }
}
#endif
