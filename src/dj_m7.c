/* DJ EQ on the M7: per-track 4-band isolator/EQ and filter after the native
 * channel EQ (and so after the gate, which runs before it).
 * - The channel EQ process pointer runs dj_process, which reads this block's
 *   DJ parameter events and calls the gate wrapper. The native EQ ends by
 *   tail-calling the next node through obj+8; for that call obj+8 points at
 *   a per-track proxy node that processes the track bus and then calls the
 *   real next node.
 * - State lives in an SDRAM block taken once per boot from the stock bump
 *   allocator, chained after Cobalt's delay constructor. */
#include "shared.h"
#include "dj_eq.h"
#define DJ_MAGIC 0x444a4531u
#define DJ_ID0 0x1bau         /* per track: High, Hi Mid, Lo Mid, Low, Filter, Res */
#define DJ_ID_CURVE 0x1c0u    /* global (Main table): curve, crossovers, bands, presets, filter */
typedef unsigned (process_fn)(uint8_t *,void *);
struct dj_proxy {const void *vtable;uint8_t pad[4];uint8_t *next;uint8_t *owner;unsigned port;struct dj_track *track;};
struct dj_bank {uint32_t magic,table_tried;struct dj_settings settings;struct dj_track track[12];struct dj_proxy proxy[12];};
struct dj_link {uint32_t magic;struct dj_bank *bank;uint32_t pad[2];};
/* Free backup-SRAM gap between the Shoegaze link (0x100..0x10f) and ring clear (0x120). */
#define DJ_LINK ((struct dj_link *)0x38800110u)
_Static_assert(0x110+sizeof(struct dj_link)<=0x120,"DJ link gap");
#define DJ_ALLOC ((sizeof(struct dj_bank)+31u)&~31u)
struct dj_event {uint8_t type,pad[7];uint32_t target;uint16_t id,pad2;int32_t value;uint32_t pad3;};
#define dj_queue ((void *(*)(void *,unsigned))0x080532d5u)
#define dj_next ((int (*)(void *,unsigned,void *))0x0804e1b9u)
#define dj_bus_at ((uint8_t *(*)(void *,unsigned))0x0805331du)
unsigned dly_construct(uint8_t *obj);
unsigned gate_process(uint8_t *obj,void *ctx);

static unsigned dj_proxy_process(uint8_t *self,void *ctx);
/* Slot 3 (+0xc) is the process call; the others are never used on a proxy. */
static process_fn *const dj_proxy_vtable[4]={0,0,0,dj_proxy_process};

static struct dj_bank *dj_bank(void) {
    backup_enable();PWR_CR1|=1u<<8;
    struct dj_link *k=DJ_LINK;
    struct dj_bank *b=k->magic==DJ_MAGIC ? k->bank : 0;
    return b && b->magic==DJ_MAGIC ? b : 0;
}
/* Delay construction happens once per boot, before audio starts. */
unsigned dj_construct(uint8_t *obj) {
    unsigned result=dly_construct(obj);
    backup_enable();PWR_CR1|=1u<<8;
    struct dj_link *k=DJ_LINK;
    k->magic=0;k->bank=0;
    uint32_t cursor=*(volatile uint32_t *)0x24000004u;
    uint32_t limit=*(volatile uint32_t *)0x24000000u;
    if(cursor>=0xc0000000u && cursor<=limit && DJ_ALLOC<=limit-cursor &&
       cursor<=0xc3400000u-DJ_ALLOC) {
        struct dj_bank *b=((void *(*)(unsigned))0x080412a5u)(DJ_ALLOC);
        dj_settings_default(&b->settings);
        for(unsigned i=0;i<12;i++)dj_reset(&b->track[i]);
        b->table_tried=0;b->magic=DJ_MAGIC;
        k->bank=b;k->magic=DJ_MAGIC;
    }
    return result;
}

static unsigned dj_proxy_process(uint8_t *self,void *ctx) {
    struct dj_proxy *p=(struct dj_proxy *)self;
    struct dj_bank *b=dj_bank();
    uint8_t *bus=dj_bus_at(ctx,p->port);
    unsigned n=*(uint32_t *)(bus+4);if(!n)n=*(uint32_t *)bus;
    if(b && !bus[0x10] && n<=1024u)
        dj_process(p->track,&b->settings,*(float **)(bus+8),bus[0x11]?*(float **)(bus+12):0,n);
    uint8_t *next=p->next;
    return next ? (*(process_fn ***)next)[3](next,ctx) : 1;
}

/* MIDI learn scales a CC by the M7's own parameter table (28-byte records
 * by id, built when the first mapping is made). It has no records for
 * Cobalt's ids, so their range was 0..0: add the DJ bands, Filter, Res and
 * the Gate threshold (ranges as defined on the M4). */
#define M7_PARAMS (*(uint8_t *volatile *)0x24008e50u)
static void dj_param_record(uint8_t *tab,unsigned id,unsigned type,int min,int max) {
    uint8_t *r=tab+id*28u;
    if(*(uint16_t *)r)return;
    *(int32_t *)(r+0x10)=min;*(int32_t *)(r+0x14)=max;r[2]=(uint8_t)type;
    *(uint16_t *)r=(uint16_t)id;
}
static void dj_param_table(struct dj_bank *b) {
    uint8_t *tab=M7_PARAMS;
    /* Build it now (the stock builder, as the first mapping would), so a
     * mapping made or loaded later already finds the records. Once per
     * boot: it allocates. */
    if(!tab && !b->table_tried){b->table_tried=1;((void (*)(void))0x0805342du)();tab=M7_PARAMS;}
    if(!tab || *(uint16_t *)(tab+(DJ_ID0+5u)*28u))return;
    for(unsigned i=0;i<5;i++)dj_param_record(tab,DJ_ID0+i,1,-100,100);
    dj_param_record(tab,0x1aeu,7,-96000,-12000);
    dj_param_record(tab,DJ_ID0+5u,8,0,1000);   /* last: marks the table done */
}
unsigned dj_process_node(uint8_t *obj,void *ctx) {
    struct dj_bank *b=dj_bank();
    if(b)dj_param_table(b);
    unsigned ch=*(uint32_t *)(obj+0x18);
    if(b) {
        void *q=dj_queue(ctx,ch);
        struct dj_event ev;
        for(unsigned i=0;dj_next(q,i,&ev);i++) {
            if(ev.type!=0x39)continue;
            if(ev.target!=ch && !(ch==12 && ev.target<12))continue;
            /* Filter is -100..100 on the knob, -1000..1000 in the DSP. */
            if(ch<12 && ev.id>=DJ_ID0 && ev.id<DJ_ID0+6u)dj_param(&b->track[ch],ev.id-DJ_ID0,ev.id==DJ_ID0+4u?ev.value*10:ev.value);
            /* MIDI-learned CCs for parameters without a native per-track
             * route arrive on Main's queue, addressed to the track. */
            if(ch==12 && ev.target<12 && ev.id>=DJ_ID0 && ev.id<DJ_ID0+6u)
                dj_param(&b->track[ev.target],ev.id-DJ_ID0,ev.id==DJ_ID0+4u?ev.value*10:ev.value);
            if(ch==12 && ev.target==12) {
                if(ev.id==DJ_ID_CURVE)b->settings.curve=ev.value?DJ_CURVE_EQ:DJ_CURVE_ISO;
                if(ev.id==DJ_ID_CURVE+1u && ev.value>=40 && ev.value<=1000)b->settings.x_lo=ev.value;
                if(ev.id==DJ_ID_CURVE+2u && ev.value>=300 && ev.value<=3000)b->settings.x_mid=ev.value;
                if(ev.id==DJ_ID_CURVE+3u && ev.value>=1500 && ev.value<=12000)b->settings.x_hi=ev.value;
                /* 0x1c4 Bands (0: 4, 1: 3), 0x1c7 Filter Slope (0: 12, 1: 24 dB), 0x1c8 Res Max. */
                if(ev.id==DJ_ID_CURVE+4u)b->settings.bands=ev.value ? 3 : 4;
                if(ev.id==DJ_ID_CURVE+7u)b->settings.slope=ev.value ? 24 : 12;
                if(ev.id==DJ_ID_CURVE+8u && ev.value>=0 && ev.value<=1000)b->settings.res_max=ev.value;
            }
        }
    }
    if(!b || ch>=12)return gate_process(obj,ctx);
    struct dj_proxy *p=&b->proxy[ch];
    p->vtable=dj_proxy_vtable;p->owner=obj;p->track=&b->track[ch];
    p->port=(*(unsigned (**)(void *))(*(uint32_t *)obj+0x54))(obj);
    uint8_t **link=(uint8_t **)(obj+8);
    p->next=*link;*link=(uint8_t *)p;
    unsigned result=gate_process(obj,ctx);
    *link=p->next;
    return result;
}
