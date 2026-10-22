/* Granular and Shoegaze render loops only, built at -O2. Kept apart from
 * delay_m7.c so native delay wrappers keep their -Os code generation. */
#include "granular.h"
#include "shoegaze.h"
#include "ring_clear.h"
#define XL_STATE ((struct x_locut *)0x388000b0u) /* same as delay_m7.c */
#include "delay_types.h"
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
void x_reverse_input_o2(struct delay_types *x,struct granular *g,float *l,float *r,unsigned n) {
    x_reverse_input(x,g,l,r,n);
}
