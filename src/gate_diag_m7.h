#include "gate_diag_shared.h"
static void gd_audio_clean(unsigned ch) {
    __asm volatile("dmb" ::: "memory");
    SCB_DCCMVAC=(uint32_t)&GD_AUDIO[ch];
    __asm volatile("dsb" ::: "memory");
}
static void gd_audio_reset(void) {
    for(unsigned ch=0;ch<12;ch++) {
        volatile uint32_t *p=(volatile uint32_t *)&GD_AUDIO[ch];
        for(unsigned i=0;i<16;i++)p[i]=0;
        gd_audio_clean(ch);
    }
}
static void gd_audio_tick(unsigned ch,struct noise_gate *g,unsigned events,
                          float left,float right,int measured) {
    volatile struct gate_audio_diag *d=&GD_AUDIO[ch];
    if(d->owner!=(uint32_t)g->owner) {
        d->magic=0;d->seq=0;d->ticks=0;d->total_events=0;
        d->max_left=d->max_right=0.f;d->owner=(uint32_t)g->owner;
    }
    d->total_events+=events;
    if(measured) {
        if(left>d->max_left)d->max_left=left;
        if(right>d->max_right)d->max_right=right;
    }
    d->ticks++;
    if(!events && d->ticks<75u)return;
    /* Publish actual DSP state, not an echo written by the UI. */
    d->seq=(d->seq&~1u)+1u;
    __asm volatile("dmb" ::: "memory");
    d->magic=GD_AUDIO_MAGIC;d->threshold=g->threshold;
    d->left=measured?d->max_left:-1.f;d->right=measured?d->max_right:-1.f;
    d->env=g->env;d->gain=g->gain;d->events=d->total_events;
    __asm volatile("dmb" ::: "memory");
    d->seq++;gd_audio_clean(ch);
    d->ticks=0;d->max_left=d->max_right=0.f;
}
