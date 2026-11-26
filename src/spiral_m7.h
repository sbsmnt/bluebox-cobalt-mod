/* G30: FX1 mode 8, Spiral. The stock delay (Time, FEEDB, FILT, BEAT, PING,
 * QUAD, To Rev unchanged) with a single-sideband frequency shifter on each
 * line's read (dly_read), before the native feedback write: every repeat is
 * shifted by Shift Hz again, so the echoes climb or fall in an endless
 * barber-pole spiral. The shifter is the SPEC insert's (insert_freqshift.h:
 * Hilbert allpass pair, no FFT): fsh_channel with dry 0, wet 1 and no
 * feedback of its own (the delay's feedback is the loop).
 *
 * State: after the Freeze/Blur state in the same SDRAM allocation (the two
 * never run together); the Shift value is kept with the Freeze controls
 * (FRZ_CTL), so a cleared state is set up again with it. */
#ifndef COBALT_SPIRAL_M7_H
#define COBALT_SPIRAL_M7_H
#include "insert_freqshift.h"

#define SPI_LINES 6u
#define SPI_MAGIC 0x53504931u
struct spiral_line {
    float h[2][2][4];   /* chain, parity bank, section */
    float q1,prev,dc[2];
    uint32_t parity,pad[3];
};
struct spiral_state {
    uint32_t magic,on;
    float c,s,hz;       /* oscillator phasor, smoothed signed shift */
    uint32_t n,pad[2];  /* frames this block (cv/sv valid) */
    float cv[32],sv[32];
    struct spiral_line line[SPI_LINES];
};
_Static_assert(sizeof(struct spiral_state)%32u==0u,"Spiral state in whole cache lines");

static inline void spi_reset(struct spiral_state *p) {
    uint32_t *w=(uint32_t *)p;
    for(unsigned i=0;i<sizeof(*p)/4u;i++)w[i]=0;
    p->c=1.f;p->magic=SPI_MAGIC;
}
/* Once per block, before the native body: glide the shift (~15 ms, as the
 * insert) and make this block's rotation. Leaving Spiral clears the lines,
 * so coming back never replays an old allpass state. */
static inline void spi_prepare(struct spiral_state *p,int on,int knob,unsigned n) {
    if(p->magic!=SPI_MAGIC)spi_reset(p);
    if(!on) {
        if(p->on)spi_reset(p);
        p->n=0;return;
    }
    if(!p->on){p->hz=fsh_knob_hz(knob);p->on=1;}
    if(n>32u)n=32u;
    p->hz+=(fsh_knob_hz(knob)-p->hz)*.045f;
    float w=p->hz*(6.28318530718f/48000.f),ww=w*w;
    float rs=w*(1.f-ww*(1.f/6.f)*(1.f-ww*(1.f/20.f)*(1.f-ww*(1.f/42.f))));
    float rc=1.f-ww*.5f*(1.f-ww*(1.f/12.f)*(1.f-ww*(1.f/30.f)));
    float c=p->c,sn=p->s,g=1.5f-.5f*(c*c+sn*sn);c*=g;sn*=g;
    for(unsigned i=0;i<n;i++){p->cv[i]=c;p->sv[i]=sn;float nc=c*rc-sn*rs;sn=c*rs+sn*rc;c=nc;}
    p->c=c;p->s=sn;p->n=n;
}
/* G32: Spiral's own shifter loop, built at -O2 (dsp_fast_m7.c): the SPEC
 * insert's Hilbert pair and DC blocker with wet 1 and no dry, feedback or
 * ramps, one chain at a time so each pass keeps its states in registers. */
void spi_run_o2(struct spiral_line *L,float *x,unsigned n,const float *cv,const float *sv,float sign);
static inline void spi_run(struct spiral_line *L,float *__restrict x,unsigned n,const float *cv,const float *sv,float sign) {
    float v[32],ya[33];
    unsigned par=L->parity&1u;
    n&=~1u;   /* pairs (the bus block is 32); an odd last sample stays dry */
    /* Pass 1: DC blocker and chain 0 (the extra one-sample delay: ya[i]
     * is chain 0's output for sample i-1). */
    {
        float *e=L->h[0][par],*o=L->h[0][par^1u];
        float a0=e[0],a1=e[1],a2=e[2],a3=e[3],A0=o[0],A1=o[1],A2=o[2],A3=o[3];
        float dx=L->dc[0],dy=L->dc[1];
        ya[0]=L->q1;
        for(unsigned i=0;i<n;i+=2) {
            float in=x[i],y;
            dy=in-dx+FSH_DC_R*dy;dx=in;v[i]=dy;
            FSH_CHAIN(0,a0,a1,a2,a3,dy,y);ya[i+1]=y;
            in=x[i+1];
            dy=in-dx+FSH_DC_R*dy;dx=in;v[i+1]=dy;
            FSH_CHAIN(0,A0,A1,A2,A3,dy,y);ya[i+2]=y;
        }
        L->dc[0]=dx;L->dc[1]=fsh_flushf(dy);
        e[0]=a0;e[1]=a1;e[2]=a2;e[3]=a3;o[0]=A0;o[1]=A1;o[2]=A2;o[3]=A3;
    }
    /* Pass 2: chain 1 and the rotation, out = I cos + sign Q sin. */
    float y=L->prev;
    {
        float *e=L->h[1][par],*o=L->h[1][par^1u];
        float b0=e[0],b1=e[1],b2=e[2],b3=e[3],B0=o[0],B1=o[1],B2=o[2],B3=o[3];
        for(unsigned i=0;i<n;i+=2) {
            float yb;
            FSH_CHAIN(1,b0,b1,b2,b3,v[i],yb);
            y=ya[i]*cv[i]+sign*yb*sv[i];x[i]=fsh_knee(y);
            FSH_CHAIN(1,B0,B1,B2,B3,v[i+1],yb);
            y=ya[i+1]*cv[i+1]+sign*yb*sv[i+1];x[i+1]=fsh_knee(y);
        }
        e[0]=b0;e[1]=b1;e[2]=b2;e[3]=b3;o[0]=B0;o[1]=B1;o[2]=B2;o[3]=B3;
    }
    float q=ya[n];
    float m=(q<0.f?-q:q)+(y<0.f?-y:y);
    if(m<1e-12f || !(m<1e6f)) {
        float *h=&L->h[0][0][0];
        for(unsigned i=0;i<16u;i++)h[i]=0.f;
        q=y=0.f;
    }
    L->q1=q;L->prev=y;
}
/* One line's read, in place. sign -1: the other sideband (SPRD on the right
 * channel). */
static inline void spi_line(struct spiral_state *p,float *x,unsigned n,unsigned li,float sign) {
    if(!p || p->magic!=SPI_MAGIC || !p->on || li>=SPI_LINES || !n)return;
    if(n>p->n)n=p->n;
    spi_run_o2(&p->line[li],x,n,p->cv,p->sv,sign);
}
#endif
