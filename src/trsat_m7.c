/* Per-track saturation (Soft morphing to hard clip) ahead of the per-track
 * gate and the native channel EQ: sat -> gate -> EQ, so the gate detects what
 * it gates. Main uses the same EQ class and is never saturated here. */
#include "shared.h"
#include "track_saturation.h"
#define TS_MAGIC 0x54534131u
struct tsat_bank {uint32_t magic;struct track_sat track[12];};
/* M7-only, in its own cache lines: after the M4 FX UI state (0xb00..0xb87),
 * before the extended delay state at 0xd00. */
#define TS_BANK ((struct tsat_bank *)0x38800c00u)
_Static_assert(sizeof(struct track_sat)==16,"track saturation ABI");
_Static_assert(0xc00+sizeof(struct tsat_bank)<=0xce0,"track saturation below the TRACK Sat section state (0xce0)");
struct ts_bus {uint32_t cap,n;float *l,*r;uint8_t silent,stereo;};
struct ts_ev {uint8_t type,pad[7];uint32_t target;uint16_t id,pad2;int32_t value;uint32_t pad3;};
/* Gate payload entry points (gate_m7.c), resolved by build.sh. */
unsigned gate_process(uint8_t *obj,void *ctx);
void *gate_construct(uint8_t *obj,unsigned bands);
static struct tsat_bank *ts_bank(void) {
    backup_enable();PWR_CR1|=1u<<8;
    if(TS_BANK->magic!=TS_MAGIC) {
        for(unsigned i=0;i<12;i++)ts_reset(&TS_BANK->track[i],0);
        TS_BANK->magic=TS_MAGIC;
    }
    return TS_BANK;
}
/* A reconstructed EQ owner, even at the same address, starts Off. */
void *tsat_construct(uint8_t *obj,unsigned bands) {
    void *result=gate_construct(obj,bands);
    struct tsat_bank *b=ts_bank();
    for(unsigned i=0;i<12;i++)if(b->track[i].owner==obj)ts_reset(&b->track[i],obj);
    return result;
}
unsigned tsat_process(uint8_t *obj,void *ctx) {
    unsigned ch=*(uint32_t *)(obj+0x18);
    if(ch>=12)return gate_process(obj,ctx);
    struct track_sat *t=&ts_bank()->track[ch];
    if(t->owner!=obj)ts_reset(t,obj);
    /* Inspect the queue without consuming it; the gate and EQ read it next. */
    void *q=((void *(*)(void *,unsigned))0x080532d5u)(ctx,ch);
    struct ts_ev ev;
    for(unsigned i=0;((int (*)(void *,unsigned,void *))0x0804e1b9u)(q,i,&ev);i++)
        if(ev.type==0x39 && ev.target==ch) {
            if(ev.id==TS_ID)ts_event(t,ev.value);
            else if(ev.id==TS_CURVE_ID)ts_curve(t,ev.value);
        }
    if(ts_active(t)) {
        unsigned port=(*(unsigned (**)(void *))(*(uint32_t *)obj+0x54))(obj);
        struct ts_bus *bus=((struct ts_bus *(*)(void *,unsigned))0x0805331du)(ctx,port);
        unsigned n=((unsigned (*)(void *))0x0804cd41u)(bus);if(n>32)n=32;
        if(!bus->silent && bus->n && bus->l)ts_process(t,bus->l,bus->stereo?bus->r:0,n);
    }
    return gate_process(obj,ctx);
}
