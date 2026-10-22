#include "shoegaze.h"
/* The free backup-SRAM gap between granular state (ends 0xa8) and delay
 * modes (starts 0x200). Actual audio state lives in its own SDRAM block. */
struct sg_link {uint32_t magic;struct shoegaze *state;uint8_t *owner;uint32_t active;};
#define SL ((struct sg_link *)0x38800100u)
#define SL_MAGIC 0x53474c32u
#define SG_ALLOC ((SG_BYTES+31u)&~31u)
_Static_assert(0x100+sizeof(struct sg_link)<=0x200,"Shoegaze link gap");
void sg_allocate(void) {
    SL->magic=SL_MAGIC;SL->state=0;SL->owner=0;SL->active=0;
    uint32_t cursor=*(volatile uint32_t *)0x24000004u;
    uint32_t limit=*(volatile uint32_t *)0x24000000u;
    if(cursor>=0xc0000000u && cursor<=limit && SG_ALLOC<=limit-cursor &&
       cursor<=0xc3400000u-SG_ALLOC) {
        struct shoegaze *s=((void *(*)(unsigned))0x080412a5u)(SG_ALLOC);
        sg_init(s);SL->state=s;
    }
}
static unsigned sg_chain(uint8_t *obj,void *ctx) {
    uint8_t *next=*(uint8_t **)(obj+8);
    return next?(*(process_fn **)next)[3](next,ctx):1;
}
unsigned sg_reverb_process(uint8_t *obj,void *ctx) {
    struct shoegaze *s=SL->magic==SL_MAGIC?SL->state:0;
    void *q=fw_queue(ctx,*(uint32_t *)(obj+0x18));
    struct fw_ev ev;unsigned want=s?s->style:0;
    if(s && SL->owner!=obj){SL->owner=obj;SL->active=0;s->style=0;sg_defaults(s);want=0;}
    for(unsigned i=0;fw_next_ev(q,i,&ev);i++)if(ev.type==0x39) {
        if(ev.id==0x155)want=ev.value>=0 && ev.value<=SG_STYLE?(unsigned)ev.value:0;
        if(s)sg_event(s,ev.id,ev.value);
    }
    if(want!=SG_STYLE) {
        if(s)SL->active=0;
        return ((process_fn)0x080489a1u)(obj,ctx);
    }
    /* Custom style never reaches the stock preset table. Native params and
     * bypass still use the actual desktop setter; queue is inspected once
     * more, as in the established delayed-send bridge. It is not consumed. */
    for(unsigned i=0;fw_next_ev(q,i,&ev);i++)if(ev.type==0x39 && ev.id!=0x155 && !sg_id(ev.id))
        ((void (*)(float,uint8_t *,unsigned))0x08044da5u)((float)ev.value,obj,ev.id);
    if(!s) {
        ((void (*)(float,uint8_t *,unsigned))0x08044da5u)(0.f,obj,0x155);
        uint32_t msg[6]={0x39,0,*(uint32_t *)(obj+0x18),0x155,0,0};
        ((void (*)(void *,void *))0x0804e1e9u)((uint8_t *)ctx+0x1529c,msg);
        /* Do not replay the out-of-range style via the native event loop. */
        port_fn port=(*(port_fn **)obj)[0x54/4];
        void *bus=fw_bus(ctx,port(obj));unsigned n=fw_frames(bus);if(n>32u)n=32u;
        if(!obj[0x6e] && n){float *l,*r;fw_stereo(bus,&l,&r);for(unsigned i=0;i<n;i++)l[i]=r[i]=0.f;}
        return sg_chain(obj,ctx);
    }
    /* Native bypass is dry passthrough, matching stock. Re-entry starts from
     * cleared custom history, not a paused tail. Stock tank is never touched. */
    if(obj[0x6e]){SL->active=0;return sg_chain(obj,ctx);}
    if(!SL->active){sg_reset_tail(s);SL->active=1;}
    port_fn port=(*(port_fn **)obj)[0x54/4];
    void *bus=fw_bus(ctx,port(obj));unsigned n=fw_frames(bus);if(n>32u)n=32u;
    if(n) {
        float *l,*r;fw_stereo(bus,&l,&r);
        /* Native Level is normalized at +0xc4 by the verified setter. */
        float level=*(float *)(obj+0xc4);
        if(!(level>=0.f && level<=2.f))level=1.f;
        sg_run_o2(s,l,r,n,level);
    }
    return sg_chain(obj,ctx);
}
