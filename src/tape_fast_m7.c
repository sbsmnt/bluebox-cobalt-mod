/* Settled Tape loop only, built at -O2. Kept apart from gr_m7.c so the Soft
 * path keeps its -Os code generation and bit-identical PCM. */
#include "tape_saturation.h"
void mt_render_settled_o2(struct master_tape *t,struct mt_block b,float *l,float *r,unsigned n,float mix,float dm,unsigned full_wet,unsigned unity_trim,unsigned drive,float trim,float off_trim_step) {
    mt_render_settled(t,b,l,r,n,mix,dm,full_wet,unity_trim,drive,trim,off_trim_step);
}
