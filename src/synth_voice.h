#ifndef COBALT_SYNTH_VOICE_H
#define COBALT_SYNTH_VOICE_H
#include <stdint.h>
#include <stddef.h>
#include <new>
#include <algorithm>
#include "synth_params.h"
#include "plaits/dsp/engine/fm_engine.h"          // F2: only for the reserved layout below
#include "plaits/dsp/engine/waveshaping_engine.h"  // F2: only for the reserved layout below
#include "plaits/dsp/engine/virtual_analog_engine.h"
#include "cluster_engine.h"
#include "plaits/dsp/engine/swarm_engine.h" // S14: only for the reserved layout below

/* The original engines are vendored, not approximations of their sound.
 * This wrapper provides one last-note-priority voice per input track. */
struct synth_voice {
    uint32_t magic;
    uint8_t channel_index; // 0..11; 255 means unregistered
    uint8_t seed; // Cluster seed 0..31, in the former alignment byte
    int16_t value[SYN_RUNTIME_PARAMS];
    union Engines {
        /* Spectral F2: FM and Shape are removed (never constructed or
         * rendered, their code is not built). Their space stays reserved so
         * the voice layout is unchanged. */
        plaits::FMEngine fm_reserved;
        plaits::WaveshapingEngine shape_reserved;
        struct Analog {
            plaits::VirtualAnalogEngine engine;
            float scratch[plaits::kMaxBlockSize];
        } analog;
        cobalt::ClusterEngine cluster; // S7: replaces Plaits Wavetable
        /* S14: Swarm is removed (never constructed or rendered). Its space
         * stays reserved so the voice layout, and every offset after it
         * (LFO2 at 856), is unchanged. */
        struct SwarmReserved {
            plaits::SwarmEngine engine;
            plaits::SwarmVoice scratch[plaits::kNumSwarmVoices];
        } swarm_reserved;
        Engines() {} ~Engines() {}
    } engines;
    uint8_t down[128],order[112]; // 128 seven-bit MIDI note numbers
    uint32_t count:8,note:7,velocity:7,sustain:1,trigger:1,panic:1,mod_attacking:1,retrigger:6;
    float envelope,bend;
    uint32_t random_state,lfo_random;
    float lfo_phase,mod_envelope,cutoff,filter1,filter2;
    int16_t lfo2[SYN_LFO2_PARAMS]; // S10: second LFO, rate..sync
    float lfo2_phase;
    uint32_t lfo2_random;
    float left[32],right[32];
    float out_gain,out_target; // S15: track Gain (pregain) and the Cluster boost
    int16_t reso,reserved;     // S18: filter resonance 0..1000
};
/* S5 keeps both random generators and retrigger state in the same reservation.
 * Reject all older retained graph pointers before MIDI dereferences them. */
#define SYN_MAGIC SYN_VOICE_MAGIC /* S15: output gain state (synth_params.h) */
#define SYN_PREGAIN_ID 3u      /* native track Gain (pregaindb, -20000..20000 mdB) */
#define SYN_CLUSTER_GAIN 1.5848932f /* S15: Cluster +4 dB */
static_assert(sizeof(void*)!=4 || sizeof(synth_voice)<=SYN_SOURCE_BYTES-SYN_SOURCE_OFFSET,"32-bit target synth source sidecar bounds");
static_assert(SYN_SOURCE_BYTES==0xa20u,"0xa00 boot-tested through S9; S10 adds 0x20 for LFO2");
static inline int synth_clamp(int v,int lo,int hi) {return v<lo?lo:v>hi?hi:v;}
static inline void synth_notes_clear(synth_voice* s,bool immediate) {
    for(unsigned i=0;i<128;i++)s->down[i]=0;
    s->count=s->sustain=s->trigger=s->panic=0;
    if(immediate){s->envelope=s->mod_envelope=0.f;s->mod_attacking=s->retrigger=0;s->filter1=s->filter2=0.f;}
}
static inline void synth_engine_init(synth_voice* s) {
    uint8_t* storage=(uint8_t*)&s->engines;
    for(unsigned i=0;i<sizeof(s->engines);i++)storage[i]=0;
    if(!SYN_ENGINE_CLUSTER(s->value[SYN_INDEX(SYN_ENGINE)])) {
        new (&s->engines.analog) synth_voice::Engines::Analog;
        stmlib::BufferAllocator allocator(s->engines.analog.scratch,sizeof(s->engines.analog.scratch));
        s->engines.analog.engine.Init(&allocator);
    } else {
        new (&s->engines.cluster) cobalt::ClusterEngine;
        s->engines.cluster.Init(nullptr);
        s->engines.cluster.set_seed(s->seed+1u);
    }
}
static inline void synth_init(synth_voice* s) {
    uint8_t* p=(uint8_t*)s;
    for(unsigned i=0;i<sizeof(*s);i++)p[i]=0;
    s->channel_index=0xffu;
    s->lfo_random=0x6d2b79f5u;
    s->lfo2_random=0x2545f491u;
    s->lfo2[SYN_LFO2_INDEX(SYN_LFO2_RATE)]=1000; // mHz
    s->out_gain=s->out_target=1.f;
    s->value[SYN_INDEX(SYN_CHANNEL)]=1;
    s->value[SYN_INDEX(SYN_HARMONICS)]=500;
    s->value[SYN_INDEX(SYN_TIMBRE)]=350;
    s->value[SYN_INDEX(SYN_MORPH)]=500;
    s->value[SYN_INDEX(SYN_ATTACK)]=5;
    s->value[SYN_INDEX(SYN_RELEASE)]=150;
    s->value[SYN_INDEX(SYN_CUTOFF)]=20000;
    s->value[SYN_INDEX(SYN_LFO_RATE)]=1000; // mHz
    s->value[SYN_INDEX(SYN_ENV_DECAY)]=500; // ms
    /* Start open without new lookup-table arithmetic during graph creation.
     * Exact coefficient follows the smoothed runtime cutoff path. */
    s->cutoff=20000.f;
    s->note=60;s->velocity=100;
    synth_engine_init(s);s->magic=SYN_MAGIC;
}
static inline void synth_event(synth_voice* s,unsigned id,int value) {
    if(id==SYN_PREGAIN_ID) {
        /* S15: the native Gain acts on the physical input, before the synth
         * replaces it, so the synth applies the track's Gain itself. */
        s->out_target=stmlib::SemitonesToRatio((float)synth_clamp(value,-20000,20000)*(12.f/6020.6f));
        return;
    }
    if(id==SYN_RESO){s->reso=(int16_t)synth_clamp(value,0,1000);return;}
    if(id==SYN_SEED) {
        /* Seed lives outside the parameter array; it only reseeds Cluster. */
        s->seed=(uint8_t)synth_clamp(value,0,SYN_SEED_COUNT-1);
        if(SYN_ENGINE_CLUSTER(s->value[SYN_INDEX(SYN_ENGINE)]))s->engines.cluster.set_seed(s->seed+1u);
        return;
    }
    if(id>=SYN_LFO2_FIRST && id<=SYN_LFO2_LAST) {
        int lo=0,hi=1000;
        if(id==SYN_LFO2_RATE){lo=10;hi=20000;}
        if(id==SYN_LFO2_DEST)hi=SYN_DEST_COUNT-1;
        if(id==SYN_LFO2_SHAPE)hi=SYN_LFO_SHAPE_COUNT-1;
        if(id==SYN_LFO2_SYNC)hi=SYN_LFO_SYNC_MAX;
        s->lfo2[SYN_LFO2_INDEX(id)]=(int16_t)synth_clamp(value,lo,hi);
        return;
    }
    if(id<SYN_ON || id>SYN_LAST || id==SYN_LEVEL)return;
    int lo=0,hi=1000;
    if(id==SYN_ON || id==SYN_DRONE)hi=1;
    if(id==SYN_ENGINE)value=SYN_ENGINE_CLUSTER(synth_clamp(value,0,SYN_ENGINE_STORED_MAX))?1:0; /* F2: 0 VA, 1 Cluster */
    if(id==SYN_LFO_SHAPE)hi=SYN_LFO_SHAPE_COUNT-1;
    if(id==SYN_LFO_SYNC)hi=SYN_LFO_SYNC_MAX;
    if(id==SYN_LFO_DEST || id==SYN_ENV_DEST)hi=SYN_DEST_COUNT-1;
    if(id==SYN_LFO_RATE){lo=10;hi=20000;}
    if(id==SYN_CUTOFF){lo=20;hi=20000;}
    if(id==SYN_ENV_AMOUNT){lo=-100;hi=100;}
    if(id==SYN_ENV_DECAY){lo=5;hi=5000;}
    if(id==SYN_ENV_ATTACK)hi=5000;
    if(id==SYN_CHANNEL){lo=1;hi=16;}
    if(id==SYN_ATTACK){lo=1;hi=2000;}
    if(id==SYN_RELEASE){lo=5;hi=5000;}
    value=synth_clamp(value,lo,hi);
    int16_t* slot=&s->value[SYN_INDEX(id)];
    if(*slot==value)return;
    *slot=value;
    if(id==SYN_DRONE)s->panic=0;
    if(id==SYN_ON || id==SYN_CHANNEL || id==SYN_ENGINE) {
        synth_notes_clear(s,true);s->bend=0.f;s->lfo_phase=s->lfo2_phase=0.f;
        if(id==SYN_ENGINE)synth_engine_init(s);
    }
}
/* Seven-bit packing retains all 128 notes and full velocity/held state. */
static inline unsigned synth_order_get(const synth_voice* s,unsigned index) {
    unsigned bit=index*7u,byte=bit>>3u,shift=bit&7u;
    unsigned word=s->order[byte];
    if(shift>1u)word|=(unsigned)s->order[byte+1u]<<8u;
    return (word>>shift)&127u;
}
static inline void synth_order_set(synth_voice* s,unsigned index,unsigned note) {
    unsigned bit=index*7u,byte=bit>>3u,shift=bit&7u;
    unsigned mask=127u<<shift,word=s->order[byte];
    if(shift>1u)word|=(unsigned)s->order[byte+1u]<<8u;
    word=(word&~mask)|((note&127u)<<shift);
    s->order[byte]=(uint8_t)word;
    if(shift>1u)s->order[byte+1u]=(uint8_t)(word>>8u);
}
static inline void synth_remove(synth_voice* s,unsigned note) {
    for(unsigned i=0;i<s->count;i++)if(synth_order_get(s,i)==note) {
        for(unsigned j=i+1;j<s->count;j++)synth_order_set(s,j-1,synth_order_get(s,j));
        s->count--;break;
    }
}
static inline void synth_select_note(synth_voice* s) {
    if(s->count) {
        s->note=synth_order_get(s,s->count-1);
        if(s->down[s->note]&127u)s->velocity=s->down[s->note]&127u;
    }
}
static inline void synth_midi(synth_voice* s,const uint8_t* m) {
    if(s->magic!=SYN_MAGIC || !s->value[0] || m[4]<3 || m[1]>127 || m[2]>127)return;
    unsigned status=m[0]&0xf0u;
    if((m[0]&15u)+1u!=(unsigned)s->value[SYN_INDEX(SYN_CHANNEL)])return;
    unsigned note=m[1];
    if(status==0x90u && m[2]) {
        s->panic=0;synth_remove(s,note);s->down[note]=m[2]|128u;
        if(s->count<128)synth_order_set(s,s->count++,note);
        synth_select_note(s);s->trigger=1;
        // A 1 ms fade clears a remaining release/legato level before a full
        // new amplitude attack. The independent parameter AD restarts too.
        s->retrigger=s->envelope>0.f?48u:0u;
    } else if(status==0x80u || status==0x90u) {
        s->down[note]&=127u;if(!s->sustain)synth_remove(s,note);
        synth_select_note(s);
    } else if(status==0xe0u) {
        s->bend=((float)((int)(m[1]|(m[2]<<7))-8192))*(2.f/8192.f);
    } else if(status==0xb0u) {
        if(note==64) {
            s->sustain=m[2]>=64;
            if(!s->sustain) {
                for(unsigned i=s->count;i;i--)if(!(s->down[synth_order_get(s,i-1)]&128u))synth_remove(s,synth_order_get(s,i-1));
                synth_select_note(s);
            }
        } else if(note==120 || note==123){synth_notes_clear(s,note==120);if(note==120)s->panic=1;}
        else if(note==121) {
            s->bend=0;s->sustain=0;
            for(unsigned i=s->count;i;i--)if(!(s->down[synth_order_get(s,i-1)]&128u))synth_remove(s,synth_order_get(s,i-1));
            synth_select_note(s);
        }
    }
}
static inline float synth_bounded(float x) {return x>=-1.f&&x<=1.f?x:x>1.f?1.f:x< -1.f?-1.f:0.f;}
static inline float synth_unit(float x) {return x<0.f?0.f:x>1.f?1.f:x;}
static inline float synth_lfo_shape_value(float phase,unsigned shape,uint32_t random) {
    if(shape==1)return plaits::SineNoWrap(phase);
    if(shape==2)return phase<.5f?1.f:-1.f;
    if(shape==3)return (float)(random>>8)*(1.f/8388608.f)-1.f;
    return phase<.25f?phase*4.f:phase<.75f?2.f-phase*4.f:phase*4.f-4.f;
}
static inline float synth_lfo_value(const synth_voice* s) {
    return synth_lfo_shape_value(s->lfo_phase,(unsigned)s->value[SYN_INDEX(SYN_LFO_SHAPE)],s->lfo_random);
}
static inline float synth_lfo2_value(const synth_voice* s) {
    return synth_lfo_shape_value(s->lfo2_phase,(unsigned)s->lfo2[SYN_LFO2_INDEX(SYN_LFO2_SHAPE)],s->lfo2_random);
}
static inline float synth_lfo_rate_hz(unsigned sync,int rate,float bpm) {
    if(sync && sync<SYN_LFO_SYNC_COUNT) {
        // Exactly the stock delay's twelve divisions, in quarter-note beats.
        static const float cycles_per_beat[]={16.f,8.f,4.f,3.f,8.f/3.f,2.f,1.5f,4.f/3.f,1.f,.75f,.5f,.25f};
        if(!(bpm>=1.f && bpm<=1000.f))bpm=120.f;
        return bpm*(1.f/60.f)*cycles_per_beat[sync-1];
    }
    return (float)rate*.001f;
}
static inline float synth_lfo_hz(const synth_voice* s,float bpm) {
    return synth_lfo_rate_hz((unsigned)s->value[SYN_INDEX(SYN_LFO_SYNC)],s->value[SYN_INDEX(SYN_LFO_RATE)],bpm);
}
static inline float synth_lfo2_hz(const synth_voice* s,float bpm) {
    return synth_lfo_rate_hz((unsigned)s->lfo2[SYN_LFO2_INDEX(SYN_LFO2_SYNC)],s->lfo2[SYN_LFO2_INDEX(SYN_LFO2_RATE)],bpm);
}
/* Free-running phase; a wrap draws the next Random value. */
static inline void synth_lfo_advance(float* phase,uint32_t* random,unsigned n,float hz) {
    float p=*phase+(float)n*hz*(1.f/48000.f);
    if(p>=1.f) {
        p-=1.f;
        *random=*random*1664525u+1013904223u;
    }
    *phase=p;
}
static inline void synth_modulate(synth_voice* s,unsigned n,bool trigger,float* modulation=nullptr,float bpm=120.f) {
    if(trigger) {
        s->mod_attacking=s->value[SYN_INDEX(SYN_ENV_ATTACK)]>0;
        s->mod_envelope=s->mod_attacking?0.f:1.f;
    }
    /* Free-running phases, with optional native-tempo divisions. */
    float lfo=synth_lfo_value(s)*(float)s->value[SYN_INDEX(SYN_LFO_AMOUNT)]*.001f;
    float env=s->mod_envelope*(float)s->value[SYN_INDEX(SYN_ENV_AMOUNT)]*.01f;
    /* S10: LFO2 adds to whatever it shares a destination with. */
    int dest2=s->lfo2[SYN_LFO2_INDEX(SYN_LFO2_DEST)];
    float lfo2=dest2?synth_lfo2_value(s)*(float)s->lfo2[SYN_LFO2_INDEX(SYN_LFO2_AMOUNT)]*.001f:0.f;
    const unsigned ids[]={SYN_TIMBRE,SYN_HARMONICS,SYN_MORPH};
    for(unsigned i=0;i<3;i++) {
        float target=(float)s->value[SYN_INDEX(ids[i])]*.001f;
        if(s->value[SYN_INDEX(SYN_LFO_DEST)]==(int)i+1)target+=lfo;
        if(dest2==(int)i+1)target+=lfo2;
        if(s->value[SYN_INDEX(SYN_ENV_DEST)]==(int)i+1)target+=env;
        if(modulation)modulation[i]=synth_unit(target);
    }
    /* Cutoff depth is logarithmic: 100% spans the 20 Hz..20 kHz range.
     * Reuse the engine pitch lookup; no new tables or runtime allocation. */
    if(modulation) {
        float depth=0.f;
        if(s->value[SYN_INDEX(SYN_LFO_DEST)]==4)depth+=lfo;
        if(dest2==4)depth+=lfo2;
        if(s->value[SYN_INDEX(SYN_ENV_DEST)]==4)depth+=env;
        depth=synth_bounded(depth);
        float hz=(float)s->value[SYN_INDEX(SYN_CUTOFF)];
        if(depth!=0.f)hz*=stmlib::SemitonesToRatio(depth*119.5894114f);
        modulation[3]=hz<20.f?20.f:hz>20000.f?20000.f:hz;
    }
    float remaining=(float)n;
    if(s->mod_attacking) {
        float duration=48.f*(float)s->value[SYN_INDEX(SYN_ENV_ATTACK)];
        float until_peak=(1.f-s->mod_envelope)*duration;
        if(remaining<until_peak) {
            s->mod_envelope+=remaining/duration;remaining=0.f;
        } else {
            s->mod_envelope=1.f;s->mod_attacking=0;remaining-=until_peak;
        }
    }
    s->mod_envelope-=remaining/(48.f*(float)s->value[SYN_INDEX(SYN_ENV_DECAY)]);
    if(s->mod_envelope<0.f)s->mod_envelope=0.f;
    synth_lfo_advance(&s->lfo_phase,&s->lfo_random,n,synth_lfo_hz(s,bpm));
    synth_lfo_advance(&s->lfo2_phase,&s->lfo2_random,n,synth_lfo2_hz(s,bpm));
}
struct synth_lowpass_coeff {float g,h,k;};
/* S18: damping k = 1/Q. Reso 0 keeps the Butterworth k = sqrt(2) exactly;
 * 1000 gives k = 0.4 (Q 2.5, about +8 dB at the cutoff): gentle, not
 * self-oscillating like the insert filter. */
static inline float synth_reso_damping(const synth_voice* s) {
    return s->reso>0?1.41421356237f-(float)s->reso*0.00101421356f:1.41421356237f;
}
static inline synth_lowpass_coeff synth_lowpass_coefficients(float hz,float k=1.41421356237f) {
    /* Bilinear/TPT Butterworth low-pass, Q=1/sqrt(2). A rational tangent
     * prewarp stays accurate up to 20 kHz without a runtime math library. */
    if(hz<20.f)hz=20.f;
    if(hz>20000.f)hz=20000.f;
    float x=hz*(3.14159265358979323846f/48000.f),xx=x*x;
    float g=x*(105.f-10.f*xx)/(105.f-45.f*xx+xx*xx);
    synth_lowpass_coeff c={g,1.f/(1.f+k*g+g*g),k};
    return c;
}
static inline float synth_lowpass(synth_voice* s,float in,synth_lowpass_coeff c) {
    float hp=(in-(c.k+c.g)*s->filter1-s->filter2)*c.h;
    float bp=c.g*hp+s->filter1;
    s->filter1=c.g*hp+bp;
    float lp=c.g*bp+s->filter2;
    s->filter2=c.g*bp+lp;
    return lp;
}
static inline void synth_render(synth_voice* s,unsigned n,float bpm=120.f) {
    if(n>32)n=32;
    if(!n)return;
    bool drone=s->value[SYN_INDEX(SYN_DRONE)]!=0 && !s->panic;
    if(!s->count && !drone && s->envelope==0.f) {
        synth_modulate(s,n,false,nullptr,bpm);
        s->filter1=s->filter2=0.f;
        s->cutoff=(float)s->value[SYN_INDEX(SYN_CUTOFF)];
        for(unsigned i=0;i<n;i++)s->left[i]=s->right[i]=0.f;
        return;
    }
    float release=1.f/(48.f*(float)s->value[SYN_INDEX(SYN_RELEASE)]);
    float attack=1.f/(48.f*(float)s->value[SYN_INDEX(SYN_ATTACK)]);
    synth_lowpass_coeff filters[2];
    bool enveloped=false;
    for(unsigned at=0;at<n;) {
        unsigned size=n-at;if(size>plaits::kMaxBlockSize)size=plaits::kMaxBlockSize;
        float modulation[4];
        synth_modulate(s,size,s->trigger && !at,modulation,bpm);
        // Smooth the combined base + LFO + envelope target over about 8 ms.
        float previous_cutoff=s->cutoff;
        s->cutoff+=(modulation[3]-s->cutoff)*((float)size/384.f);
        filters[at/plaits::kMaxBlockSize]=at && s->cutoff==previous_cutoff
            ?filters[0]:synth_lowpass_coefficients(s->cutoff,synth_reso_damping(s));
        plaits::EngineParameters p={s->trigger && !at?plaits::TRIGGER_RISING_EDGE:plaits::TRIGGER_HIGH,
            (float)s->note+s->bend,modulation[0],modulation[2],modulation[1],
            (float)s->velocity*(1.f/127.f)};
        if(!s->value[SYN_INDEX(SYN_ENGINE)])s->engines.analog.engine.Render(p,s->left+at,nullptr,size,&enveloped);
        else s->engines.cluster.Render(p,s->left+at,nullptr,size,&enveloped);
        at+=size;
    }
    s->trigger=0;
    float vel=drone?1.f:(float)s->velocity*(1.f/127.f);
    /* Same arithmetic as S5, with envelope and filter state in registers.
     * Stores to left/right no longer force reloads of the voice fields. */
    float envelope=s->envelope,f1=s->filter1,f2=s->filter2;
    unsigned retrigger=s->retrigger;
    bool held=s->count || drone;
    float* left=s->left;float* right=s->right;
    for(unsigned at=0,sub=0;at<n;sub++) {
        unsigned end=n-at>plaits::kMaxBlockSize?at+plaits::kMaxBlockSize:n;
        /* S19: the damping comes with the coefficients (S18 left sqrt(2) here,
         * so Reso only rescaled the filter: no peak, and gain errors up high). */
        float g=filters[sub].g,h=filters[sub].h,k=filters[sub].k+g;
        for(unsigned i=at;i<end;i++) {
            if(retrigger) {
                envelope-=envelope/(float)retrigger;
                if(!--retrigger)envelope=0.f;
            }
            else if(held){envelope+=attack;if(envelope>1.f)envelope=1.f;}
            else {envelope-=release;if(envelope<0.f)envelope=0.f;}
            float in=synth_bounded(left[i]);
            float hp=(in-k*f1-f2)*h;
            float bp=g*hp+f1;
            f1=g*hp+bp;
            float lp=g*bp+f2;
            f2=g*bp+lp;
            float x=synth_bounded(lp)*(.25f*envelope*vel);
            left[i]=right[i]=x;
        }
        at=end;
    }
    s->envelope=envelope;s->filter1=f1;s->filter2=f2;s->retrigger=retrigger;
}
/* S15: track Gain and the Cluster boost, ramped over the block so a change
 * does not click. At 0 dB on FM/Shape/VA the output is untouched. */
static inline void synth_output_gain(synth_voice* s,unsigned n) {
    float target=s->out_target*(s->value[SYN_INDEX(SYN_ENGINE)]?SYN_CLUSTER_GAIN:1.f);
    float g=s->out_gain;
    if(g==1.f && target==1.f)return;
    if(!n){s->out_gain=target;return;}
    float step=(target-g)/(float)n;
    for(unsigned i=0;i<n;i++){g+=step;s->left[i]*=g;s->right[i]*=g;}
    s->out_gain=target;
}

#endif
