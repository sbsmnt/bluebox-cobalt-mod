/* Granular and Shoegaze render loops only, built at -O2. Kept apart from
 * delay_m7.c so native delay wrappers keep their -Os code generation. */
#include "granular.h"
#include "shoegaze.h"
#include "ring_clear.h"
#define XL_STATE ((struct x_locut *)0x388000b0u) /* same as delay_m7.c */
#include "delay_types.h"
/* G24 per-block cycle cap: DWT CYCCNT (the stock firmware enables it). */
#define SF_NOW() (*(volatile uint32_t *)0xe0001004u)
#include "spectral_freeze.h"
#include "spiral_m7.h"
void g_render_o2(struct granular *g,const float *in_l,const float *in_r,float *out_l,float *out_r,unsigned n) {
    g_render(g,in_l,in_r,out_l,out_r,n);
}
void sg_run_o2(struct shoegaze *s,float *l,float *r,unsigned n,float level) {
    sg_run(s,l,r,n,level);
}
void rc_step_o2(struct ring_clear *c,uint32_t *lines,unsigned skipping) {
    rc_step(c,lines,skipping);
}
void x_reverse_o2(struct delay_types *x,struct granular *g,float *l,float *r,unsigned n,unsigned wr0,unsigned valid0) {
    x_reverse(x,g,l,r,n,wr0,valid0);
}
/* FX1 Freeze: the spectral freeze, its FFT and loops at -O2. */
void sf_process_o2(struct spectral_freeze *s,float *l,float *r,unsigned n) {
    sf_process(s,l,r,n);
}
void sf_blur_process_o2(struct spectral_freeze *s,float *l,float *r,unsigned n) {
    sf_blur_process(s,l,r,n);
}
void sf_init_o2(struct spectral_freeze *s,struct sf_fast *fast) {
    sf_init_fast(s,fast);
}
void x_reverse_input_o2(struct delay_types *x,struct granular *g,float *l,float *r,unsigned n) {
    x_reverse_input(x,g,l,r,n);
}
/* G32: Spiral's per-line shifter at -O2. */
void spi_run_o2(struct spiral_line *L,float *x,unsigned n,const float *cv,const float *sv,float sign) {
    spi_run(L,x,n,cv,sv,sign);
}
