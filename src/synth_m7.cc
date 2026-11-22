#include <stdint.h>
#include "synth_voice.h"
/* M7-only pointer registry. RC occupies [0x120,0x17c); Gate acceleration
 * starts at 0x1c0. Voices and samples live in native owned heap objects. */
struct synth_bank {uint32_t magic;synth_voice* voice[12];};
#define SYN_BANK ((synth_bank*)SYN_BANK_ADDRESS)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
static_assert(offsetof(synth_voice,magic)==0 && offsetof(synth_voice,value)==SYN_VOICE_VALUES_OFFSET && offsetof(synth_voice,lfo2)==SYN_VOICE_LFO2_OFFSET,"voice fields read by the M7 MIDI map code");
#pragma GCC diagnostic pop
static_assert(sizeof(synth_bank)<=SYN_RNG_ADDRESS-0x38800180u,"synth registry before temporary RNG word");
static synth_bank* synth_registry() {
    *(volatile uint32_t*)0x580244e0u|=1u<<28;
    (void)*(volatile uint32_t*)0x580244e0u;
    *(volatile uint32_t*)0x58024800u|=1u<<8;
    if(SYN_BANK->magic!=SYN_MAGIC) {
        for(unsigned i=0;i<12;i++)SYN_BANK->voice[i]=nullptr;
        SYN_BANK->magic=SYN_MAGIC;
    }
    return SYN_BANK;
}
struct synth_bus {uint32_t cap,n;float *l,*r;uint8_t silent,stereo;uint16_t pad;};
struct synth_event_message {uint8_t type,pad[7];uint32_t target;uint16_t id,pad2;int32_t value;uint32_t pad3;};
static synth_voice* source_voice(uint8_t* obj) {return (synth_voice*)(obj+SYN_SOURCE_OFFSET);}
extern "C" void* synth_source_construct(uint8_t* obj) {
    void* result=((void*(*)(void*))0x08056d55u)(obj);
    synth_voice* s=source_voice(obj);
    synth_bank* bank=synth_registry();
    /* Backup SRAM survives resets. Invalidate old graph pointers whenever
     * a source is constructed; every live source re-registers next block. */
    for(unsigned i=0;i<12;i++)bank->voice[i]=nullptr;
    new (s) synth_voice;synth_init(s);return result;
}
extern "C" void* synth_source_destroy(uint8_t* obj) {
    synth_bank* bank=synth_registry();synth_voice* s=source_voice(obj);
    for(unsigned i=0;i<12;i++)if(bank->voice[i]==s)bank->voice[i]=nullptr;
    return ((void*(*)(void*))0x08056d3du)(obj);
}
extern "C" void* synth_source_delete(uint8_t* obj) {
    synth_source_destroy(obj);
    ((void(*)(void*,unsigned))0x0806899du)(obj,SYN_SOURCE_BYTES);return obj;
}
extern "C" unsigned synth_source_process(uint8_t* obj,void* ctx) {
    unsigned ch=*(uint32_t*)(obj+0x18);
    if(ch>=12)return ((unsigned(*)(void*,void*))0x08057209u)(obj,ctx);
    synth_voice* s=source_voice(obj);
    if(s->magic!=SYN_MAGIC)synth_init(s);
    if(s->channel_index!=ch) {
        synth_bank* b=synth_registry();
        for(unsigned i=0;i<12;i++)if(b->voice[i]==s)b->voice[i]=nullptr;
        synth_notes_clear(s,true);s->channel_index=ch;
        s->lfo_random=0x6d2b79f5u^(0x9e3779b9u*(ch+1u));
        s->lfo2_random=0x2545f491u^(0x85ebca6bu*(ch+1u));
    }
    synth_registry()->voice[ch]=s;
    void* q=((void*(*)(void*,unsigned))0x080532d5u)(ctx,ch);
    synth_event_message event;
    for(unsigned i=0;((int(*)(void*,unsigned,void*))0x0804e1b9u)(q,i,&event);i++)
        if(event.type==0x39 && event.target==ch)synth_event(s,event.id,event.value);
    if(!s->value[0])return ((unsigned(*)(void*,void*))0x08057209u)(obj,ctx);
    unsigned port=(*(unsigned(**)(void*))(*(uint32_t*)obj+0x54))(obj);
    synth_bus* bus=((synth_bus*(*)(void*,unsigned))0x0805331du)(ctx,port);
    unsigned n=((unsigned(*)(void*))0x0804cd41u)(bus);
    if(n>32)n=32;
    synth_bus saved=*bus;
    // The stock delay resolves beat periods from the same environment BPM.
    uint8_t* environment=*(uint8_t**)ctx;
    float bpm=environment?*(float*)(environment+0x18):120.f;
    synth_render(s,n,bpm);
    synth_output_gain(s,n);
    /* Private buffers prevent any edit of shared input/USB sample memory.
     * All downstream nodes execute synchronously in the native chain. */
    bus->cap=bus->n=n;bus->l=s->left;bus->r=s->right;bus->silent=0;bus->stereo=saved.stereo;
    unsigned result=((unsigned(*)(void*,void*))0x08057209u)(obj,ctx);
    *bus=saved;return result;
}
extern "C" void synth_midi_dispatch(void* mapper,const uint8_t* message,void* ctx) {
    synth_bank* b=synth_registry();
    for(unsigned i=0;i<12;i++)if(b->voice[i])synth_midi(b->voice[i],message);
    ((void(*)(void*,const uint8_t*,void*))0x0804f0c5u)(mapper,message,ctx);
}
