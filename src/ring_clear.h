/* Native delay ring clearing while the body is skipped (fully wet Granular).
 * Skipped rings stop moving, holding the delay's audio from before Granular.
 * Returning to Delay must not replay it, so the rings are zeroed in bounded
 * slices: RC_STEP samples per line per block, less work than the skipped
 * body, so a clearing block never costs more than exp.4. Each line starts
 * RC_MARGIN samples behind its read head (full Drift reads up to 768 back)
 * and moves forward, so the region read first after a return is cleared
 * first; at 8x the read speed it stays ahead of a resumed read head. While still skipped, a full lap clears the
 * whole ring. After a return, clearing stops at the frozen write head: native
 * writes from there on are fresh, and older positions are rewritten before
 * they can be read. Lines are the stock descriptors at obj+0x30, 0x1c apart:
 * [0] buffer, [1] write, [2] read, [5] size (floats). */
#ifndef BLUEBOX_RING_CLEAR_H
#define BLUEBOX_RING_CLEAR_H
#include <stdint.h>
#define RC_MAGIC 0x52434c31u
#define RC_LINES 6u
#define RC_STEP 256u
#define RC_MARGIN 1024u
struct ring_clear {
    uint32_t magic;void *owner;
    uint32_t active,resumed,clean;
    uint32_t cursor[RC_LINES],left[RC_LINES],keep[RC_LINES];
};
static inline void rc_reset(struct ring_clear *c,void *owner) {
    uint32_t *p=(uint32_t *)c;
    for(unsigned i=0;i<sizeof(*c)/4;i++)p[i]=0;
    c->owner=owner;c->magic=RC_MAGIC;
}
static inline void rc_step(struct ring_clear *c,uint32_t *lines,unsigned skipping) {
    if(!skipping){c->clean=0;if(!c->active)return;c->resumed=1;}
    /* Start on entry, or again if the delay ran (and wrote) since the last
     * start. Once fully clear, nothing writes the rings until the delay runs. */
    else if((!c->active && !c->clean) || (c->active && c->resumed)) {
        for(unsigned i=0;i<RC_LINES;i++) {
            uint32_t *line=lines+i*7u,size=line[5],wr=line[1],rd=line[2];
            c->left[i]=0;
            if(!line[0] || !size || wr>=size || rd>=size)continue;
            uint32_t dist=wr>=rd?wr-rd:wr+size-rd,margin=size-dist;
            if(margin>RC_MARGIN)margin=RC_MARGIN;
            uint32_t start=rd>=margin?rd-margin:rd+size-margin;
            c->cursor[i]=start;c->left[i]=size;
            /* Samples from the start up to the frozen write head. */
            c->keep[i]=wr>=start?wr-start:wr+size-start;
        }
        c->active=1;c->resumed=0;
    }
    if(!c->active)return;
    unsigned busy=0;
    for(unsigned i=0;i<RC_LINES;i++) {
        uint32_t left=c->left[i];
        if(!left)continue;
        uint32_t *line=lines+i*7u,size=line[5];
        float *buf=(float *)line[0];
        if(!buf || !size || c->cursor[i]>=size){c->left[i]=0;continue;}
        uint32_t done=size-left,amount=left<RC_STEP?left:RC_STEP;
        if(c->resumed) {
            if(done>=c->keep[i]){c->left[i]=0;continue;}
            if(amount>c->keep[i]-done)amount=c->keep[i]-done;
        }
        uint32_t at=c->cursor[i];
        while(amount) {
            uint32_t run=size-at<amount?size-at:amount;
            uint32_t *p=(uint32_t *)(buf+at),k=run;
            for(;k>=8;k-=8,p+=8){p[0]=0;p[1]=0;p[2]=0;p[3]=0;p[4]=0;p[5]=0;p[6]=0;p[7]=0;}
            for(;k;k--)*p++=0;
            at+=run;if(at==size)at=0;
            amount-=run;left-=run;
        }
        c->cursor[i]=at;c->left[i]=left;
        if(left && !(c->resumed && size-left>=c->keep[i]))busy=1;
    }
    if(!busy){c->active=0;c->clean=skipping;}
}
#endif
