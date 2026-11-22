#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <vector>
#include "../src/synth_voice.h"
static unsigned checks;
static void check(bool ok) {if(!ok)std::fprintf(stderr,"Failed synth assertion %u\n",checks+1);assert(ok);checks++;}
static void midi(synth_voice& s,unsigned status,unsigned a,unsigned b) {
    uint8_t m[5]={(uint8_t)status,(uint8_t)a,(uint8_t)b,0,3};synth_midi(&s,m);
}
int main() {
    synth_voice s;synth_init(&s);synth_event(&s,SYN_ON,1);
    midi(s,0x90,60,100);midi(s,0x90,64,80);midi(s,0x90,67,60);
    check(s.count==3 && s.note==67 && s.velocity==60);
    midi(s,0x80,67,0);check(s.count==2 && s.note==64 && s.velocity==80);
    midi(s,0x80,60,0);check(s.count==1 && s.note==64);
    midi(s,0xb0,64,127);midi(s,0x80,64,0);check(s.count==1 && s.note==64);
    midi(s,0x90,72,127);midi(s,0x80,72,0);check(s.count==2 && s.note==72);
    midi(s,0xb0,64,0);check(s.count==0);
    for(unsigned n=0;n<128;n++)midi(s,0x90,n,1+n%127);
    check(s.count==128 && s.note==127);
    for(unsigned n=0;n<128;n++)midi(s,0x90,n,100);
    check(s.count==128);
    for(unsigned n=128;n;n--)midi(s,0x80,n-1,0);
    check(s.count==0);
    // Model last-note priority, velocities, and sustained notes independently
    // across random insertion/removal orders and the packed stack boundaries.
    std::vector<unsigned> held;
    unsigned velocity[128]={};bool down[128]={},pedal=false;
    uint32_t rng=0x62e7u;
    for(unsigned step=0;step<20000;step++) {
        rng=rng*1664525u+1013904223u;
        unsigned key=(rng>>16)&127u,operation=rng&7u;
        if(operation<4) {
            unsigned v=1+(rng>>24)%127;
            for(auto i=held.begin();i!=held.end();++i)if(*i==key){held.erase(i);break;}
            held.push_back(key);velocity[key]=v;down[key]=true;
            midi(s,0x90,key,v);
        } else if(operation<7) {
            down[key]=false;
            if(!pedal)for(auto i=held.begin();i!=held.end();++i)if(*i==key){held.erase(i);break;}
            midi(s,0x80,key,0);
        } else {
            pedal=!pedal;
            if(!pedal)for(auto i=held.begin();i!=held.end();)if(!down[*i])i=held.erase(i);else ++i;
            midi(s,0xb0,64,pedal?127:0);
        }
        check(s.count==held.size());
        if(!held.empty())check(s.note==held.back() && s.velocity==velocity[held.back()]);
    }
    midi(s,0xb0,120,0);
    for(unsigned ch=1;ch<=16;ch++) {
        synth_event(&s,SYN_CHANNEL,ch);
        midi(s,0x90|((ch+1)%16),69,127);check(s.count==0);
        midi(s,0x90|(ch-1),69,127);check(s.count==1 && s.note==69);
    }
    synth_event(&s,SYN_CHANNEL,1);
    midi(s,0xe0,0,0);check(s.bend==-2.f);
    midi(s,0xe0,0,64);check(s.bend==0.f);
    midi(s,0xe0,127,127);check(s.bend<2.f && s.bend>1.99f);
    midi(s,0xb0,121,0);check(s.bend==0.f);
    synth_event(&s,SYN_ATTACK,-2147483647);synth_event(&s,SYN_RELEASE,2147483647);
    check(s.value[SYN_INDEX(SYN_ATTACK)]==1 && s.value[SYN_INDEX(SYN_RELEASE)]==5000);
    unsigned random=9177;
    for(unsigned engine=0;engine<SYN_ENGINE_COUNT;engine++)for(unsigned note: {0u,21u,48u,69u,96u,127u}) {
        synth_init(&s);synth_event(&s,SYN_ON,1);synth_event(&s,SYN_ENGINE,engine);midi(s,0x90,note,127);
        for(unsigned block=0;block<160;block++) {
            random=random*1664525u+1013904223u;synth_event(&s,SYN_HARMONICS,random%1001);
            random=random*1664525u+1013904223u;synth_event(&s,SYN_TIMBRE,random%1001);
            random=random*1664525u+1013904223u;synth_event(&s,SYN_MORPH,random%1001);
            synth_render(&s,32);
            for(unsigned i=0;i<32;i++)check(std::isfinite(s.left[i]) && std::fabs(s.left[i])<=.5f && s.left[i]==s.right[i]);
        }
        midi(s,0xb0,123,0);for(unsigned b=0;b<240;b++)synth_render(&s,32);
        check(s.envelope==0.f && s.count==0);
        for(float x:s.left)check(x==0.f);
    }
    synth_init(&s);synth_event(&s,SYN_ON,1);synth_event(&s,SYN_DRONE,1);synth_render(&s,32);
    midi(s,0xb0,120,0);synth_render(&s,32);check(s.envelope==0.f && s.panic==1);
    for(float x:s.left)check(x==0.f);
    midi(s,0x90,69,127);synth_render(&s,32);check(s.envelope>0.f && s.panic==0);
    /* Tune test: original Plaits hardware runs at 47872.34 Hz. Our one-line
     * clock correction must produce A4 at 440 Hz on Bluebox's 48 kHz path. */
    synth_init(&s);synth_event(&s,SYN_ON,1);synth_event(&s,SYN_TIMBRE,0);midi(s,0x90,69,127);
    for(unsigned b=0;b<100;b++)synth_render(&s,32);
    unsigned crossings=0;float prev=s.left[31];
    for(unsigned b=0;b<750;b++) {
        synth_render(&s,32);
        for(float x:s.left){if(prev<=0 && x>0)crossings++;prev=x;}
    }
    check(crossings>=219 && crossings<=221);
    /* Compare the wrapper's primary audio to a separate original engine
     * instance, including the 24+8 split and defined envelope/output gain. */
    for(unsigned engine=0;engine<SYN_ENGINE_COUNT;engine++) {
        synth_init(&s);synth_event(&s,SYN_ENGINE,engine);synth_event(&s,SYN_DRONE,1);
        s.note=69;s.envelope=1.f;
        alignas(plaits::VirtualAnalogEngine) unsigned char analog[sizeof(plaits::VirtualAnalogEngine)]={};
        alignas(void*) unsigned char scratch[256*sizeof(void*)];stmlib::BufferAllocator allocator(&scratch,sizeof(scratch));
        alignas(cobalt::ClusterEngine) unsigned char wave[sizeof(cobalt::ClusterEngine)]={};
        /* F2: engine 0 is VA, 1 is Cluster (FM and Shape removed). */
        plaits::Engine* reference=engine==1?(plaits::Engine*)new(wave) cobalt::ClusterEngine:(plaits::Engine*)new(analog) plaits::VirtualAnalogEngine;
        reference->Init(engine==1?nullptr:&allocator);reference->Reset();
        if(engine==1)((cobalt::ClusterEngine*)reference)->set_seed(1); // wrapper seed 0 = Seed 1
        // Independent RBJ biquad reference, using the exact math-library cutoff.
        double omega=2.0*M_PI*20000.0/48000.0,co=std::cos(omega),al=std::sin(omega)/std::sqrt(2.0);
        double b0=(1.0-co)*.5/(1.0+al),b1=2.0*b0,b2=b0,a1=-2.0*co/(1.0+al),a2=(1.0-al)/(1.0+al);
        double x1=0,x2=0,y1=0,y2=0;
        plaits::EngineParameters p={plaits::TRIGGER_HIGH,69,350.f*.001f,500.f*.001f,500.f*.001f,100.f*(1.f/127.f)};
        for(unsigned b=0;b<100;b++) {
            float out[32],aux[32];bool enveloped=false;
            reference->Render(p,out,aux,24,&enveloped);reference->Render(p,out+24,aux+24,8,&enveloped);
            synth_render(&s,32);
            for(unsigned i=0;i<32;i++) {
                double x=synth_bounded(out[i]),y=b0*x+b1*x1+b2*x2-a1*y1-a2*y2;
                x2=x1;x1=x;y2=y1;y1=y;
                check(std::fabs(s.left[i]-synth_bounded((float)y)*.25f)<.0002f);
            }
        }
    }
    // LFO period, three independent routes, clamp and parameter immutability.
    for(unsigned dest=0;dest<4;dest++) {
        synth_init(&s);synth_event(&s,SYN_LFO_DEST,dest);synth_event(&s,SYN_LFO_AMOUNT,1000);
        float modulation[4];s.lfo_phase=.25f;synth_modulate(&s,24,false,modulation);
        for(unsigned i=0;i<3;i++)check(std::fabs(modulation[i]-(dest==i+1?1.f:i==0?.35f:.5f))<1e-6f);
        check(s.value[SYN_INDEX(SYN_TIMBRE)]==350 && s.value[SYN_INDEX(SYN_HARMONICS)]==500);
    }
    synth_init(&s);synth_event(&s,SYN_LFO_RATE,20000);
    for(unsigned i=0;i<150;i++)synth_modulate(&s,16,false);
    check(s.lfo_phase<.0001f || s.lfo_phase>.9999f); // 20 Hz period = 2400 samples
    for(unsigned dest=1;dest<4;dest++)for(int amount: {-100,100}) {
        synth_init(&s);synth_event(&s,SYN_ENV_DEST,dest);synth_event(&s,SYN_ENV_AMOUNT,amount);synth_event(&s,SYN_ENV_DECAY,100);
        float modulation[4];synth_modulate(&s,24,true,modulation);check(modulation[dest-1]==(amount>0?1.f:0.f));
        for(unsigned i=0;i<200;i++)synth_modulate(&s,24,false);
        check(s.mod_envelope==0.f);
        synth_modulate(&s,24,true);check(s.mod_envelope>.99f); // retrigger
    }
    for(unsigned engine=0;engine<SYN_ENGINE_COUNT;engine++)for(unsigned cutoff: {20u,1000u,20000u}) {
        synth_init(&s);synth_event(&s,SYN_ON,1);synth_event(&s,SYN_ENGINE,engine);synth_event(&s,SYN_CUTOFF,cutoff);
        synth_event(&s,SYN_LFO_DEST,1);synth_event(&s,SYN_LFO_AMOUNT,1000);synth_event(&s,SYN_LFO_RATE,20000);
        synth_event(&s,SYN_ENV_DEST,1);synth_event(&s,SYN_ENV_AMOUNT,-100);synth_event(&s,SYN_ENV_DECAY,5);
        midi(s,0x90,48,127);
        for(unsigned block=0;block<300;block++) {
            synth_render(&s,32);
            for(float x:s.left)check(std::isfinite(x) && std::fabs(x)<=.25f);
        }
    }
    // Cutoff modulation is in octaves, sums both routes, clamps to the
    // musical range and never overwrites the stored base setting.
    for(unsigned dest=0;dest<SYN_DEST_COUNT;dest++) {
        synth_init(&s);synth_event(&s,SYN_CUTOFF,1000);synth_event(&s,SYN_LFO_DEST,dest);
        synth_event(&s,SYN_LFO_AMOUNT,500);s.lfo_phase=.25f;
        float mod[4];synth_modulate(&s,24,false,mod);
        float expected=dest==4?1000.f*std::sqrt(1000.f):1000.f;
        if(expected>20000.f)expected=20000.f;
        check(std::fabs(mod[3]-expected)<expected*.001f);
        check(s.value[SYN_INDEX(SYN_CUTOFF)]==1000);
    }
    for(float phase: {.25f,.75f})for(int env_amount: {-100,-25,0,25,100}) {
        synth_init(&s);synth_event(&s,SYN_CUTOFF,1000);synth_event(&s,SYN_LFO_DEST,4);
        synth_event(&s,SYN_ENV_DEST,4);synth_event(&s,SYN_LFO_AMOUNT,250);
        synth_event(&s,SYN_ENV_AMOUNT,env_amount);s.lfo_phase=phase;
        float mod[4];synth_modulate(&s,24,true,mod);
        float depth=(phase==.25f?.25f:-.25f)+env_amount*.01f;
        double expected=std::fmax(20.,std::fmin(20000.,1000.*std::pow(1000.,depth)));
        check(std::fabs(mod[3]-(float)expected)<expected*.001f);
        check(std::fabs(mod[0]-.35f)<1e-6f && mod[1]==.5f && mod[2]==.5f);
    }
    // Analytical linear AD envelope: attack rises for 100 ms, then a
    // 200 ms decay. Check real time through the 24/8 native adapter.
    synth_init(&s);synth_event(&s,SYN_ENV_ATTACK,100);synth_event(&s,SYN_ENV_DECAY,200);
    synth_event(&s,SYN_ENV_DEST,1);synth_event(&s,SYN_ENV_AMOUNT,50);
    unsigned elapsed=0;
    for(unsigned block=0;block<500;block++)for(unsigned sub: {24u,8u}) {
        float mod[4];synth_modulate(&s,sub,elapsed==0,mod);elapsed+=sub;
        double reference=elapsed<4800?elapsed/4800.:std::fmax(0.,1.-(elapsed-4800)/9600.);
        check(std::fabs(s.mod_envelope-reference)<.0001);
        check(s.mod_envelope>=0.f && s.mod_envelope<=1.f);
    }
    check(s.mod_envelope==0.f && !s.mod_attacking);
    float mod[4];synth_modulate(&s,24,true,mod);
    check(std::fabs(mod[0]-.35f)<1e-6f && s.mod_envelope>0.f && s.mod_envelope<.01f);
    synth_event(&s,SYN_ENV_ATTACK,0);synth_modulate(&s,24,true,mod);
    check(mod[0]>.84f && !s.mod_attacking); // zero attack retains the old instant peak
    synth_event(&s,SYN_ENV_ATTACK,-1000);check(s.value[SYN_INDEX(SYN_ENV_ATTACK)]==0);
    synth_event(&s,SYN_ENV_ATTACK,99999);check(s.value[SYN_INDEX(SYN_ENV_ATTACK)]==5000);
    // Exercise cutoff routing on every actual engine, with both modulation
    // polarities and a nonzero attack, including fast LFO/extreme depth.
    for(unsigned engine=0;engine<SYN_ENGINE_COUNT;engine++)for(int amount: {-100,100}) {
        synth_init(&s);synth_event(&s,SYN_ENGINE,engine);synth_event(&s,SYN_ON,1);
        synth_event(&s,SYN_CUTOFF,1000);synth_event(&s,SYN_LFO_DEST,4);
        synth_event(&s,SYN_LFO_AMOUNT,1000);synth_event(&s,SYN_LFO_RATE,20000);
        synth_event(&s,SYN_ENV_DEST,4);synth_event(&s,SYN_ENV_AMOUNT,amount);
        synth_event(&s,SYN_ENV_ATTACK,30);synth_event(&s,SYN_ENV_DECAY,100);
        midi(s,0x90,72,127);
        for(unsigned block=0;block<1500;block++) {
            if(block%200==0)synth_event(&s,SYN_LFO_SHAPE,(block/200)%4);
            synth_render(&s,32);
            check(std::isfinite(s.cutoff) && s.cutoff>=19.99f && s.cutoff<=20000.01f);
            for(float x:s.left)check(std::isfinite(x) && std::fabs(x)<=.25f);
        }
    }
    // All four LFOs use bounded bipolar values. Sine is compared with
    // independent math, and Random must hold until its own cycle wraps.
    for(unsigned shape=0;shape<4;shape++) {
        synth_init(&s);synth_event(&s,SYN_LFO_SHAPE,shape);
        for(unsigned point=0;point<1000;point++) {
            s.lfo_phase=point*.001f;float value=synth_lfo_value(&s);
            check(std::isfinite(value) && value>=-1.f && value<=1.f);
            if(shape==1)check(std::fabs(value-std::sin(2.*M_PI*s.lfo_phase))<.000025);
            if(shape==2)check(value==(s.lfo_phase<.5f?1.f:-1.f));
        }
    }
    synth_init(&s);synth_event(&s,SYN_LFO_SHAPE,3);synth_event(&s,SYN_LFO_RATE,20000);
    float held_lfo=synth_lfo_value(&s);uint32_t state=s.lfo_random;
    for(unsigned block=0;block<64;block++){synth_modulate(&s,32,false);check(synth_lfo_value(&s)==held_lfo);}
    check(s.lfo_random==state);
    for(unsigned block=0;block<32;block++)synth_modulate(&s,32,false);
    check(s.lfo_random!=state && synth_lfo_value(&s)!=held_lfo);
    float next=synth_lfo_value(&s);stmlib::Random::Seed(9241);
    for(unsigned i=0;i<100;i++)stmlib::Random::GetWord();
    check(synth_lfo_value(&s)==next); // independent from the stmlib RNG
    const double beats[]={1./16,1./8,1./4,1./3,3./8,1./2,2./3,3./4,1,4./3,2,4};
    for(float bpm: {60.f,120.f,180.f})for(unsigned choice=1;choice<=12;choice++) {
        synth_init(&s);synth_event(&s,SYN_LFO_SYNC,choice);
        check(std::fabs(synth_lfo_hz(&s,bpm)-bpm/(60.*beats[choice-1]))<.0001);
    }
    synth_event(&s,SYN_LFO_SYNC,0);synth_event(&s,SYN_LFO_RATE,1234);
    check(std::fabs(synth_lfo_hz(&s,60)-1.234f)<1e-6f && synth_lfo_hz(&s,60)==synth_lfo_hz(&s,180));
    // Reproduce a long release, then require the next note's complete attack.
    synth_init(&s);synth_event(&s,SYN_ON,1);synth_event(&s,SYN_ATTACK,1000);
    synth_event(&s,SYN_RELEASE,5000);synth_event(&s,SYN_ENV_ATTACK,1000);synth_event(&s,SYN_ENV_DECAY,1000);
    midi(s,0x90,60,127);
    for(unsigned block=0;block<1600;block++)synth_render(&s,32);
    midi(s,0x80,60,0);for(unsigned block=0;block<150;block++)synth_render(&s,32);
    check(s.envelope>.95f); // most of the old release remains
    midi(s,0x90,67,127);check(s.retrigger==48);
    synth_render(&s,32);synth_render(&s,32);
    check(s.retrigger==0 && s.envelope<.001f && s.mod_envelope<.002f && s.mod_attacking);
    for(unsigned block=0;block<750;block++)synth_render(&s,32);
    check(s.envelope>.49f && s.envelope<.51f && s.mod_envelope>.49f && s.mod_envelope<.51f);
    // A second legato Note On during attack restarts both envelopes too.
    midi(s,0x90,72,127);synth_render(&s,32);synth_render(&s,32);
    check(s.envelope<.001f && s.mod_envelope<.002f && s.mod_attacking);
    synth_event(&s,SYN_LEVEL,1000);check(s.value[SYN_INDEX(SYN_DRONE)]==0 && s.value[SYN_INDEX(SYN_LFO_SHAPE)]==0); // ignored legacy key
    // Cutoff must audibly attenuate the upper spectrum, using the same source.
    // F2: the default engine is VA, whose default patch carries a DC offset
    // that any low-pass passes; compare the AC energy.
    float energy[2]={};
    for(unsigned low=0;low<2;low++) {
        synth_init(&s);synth_event(&s,SYN_ON,1);synth_event(&s,SYN_CUTOFF,low?100:20000);midi(s,0x90,96,127);
        double sum=0,sq=0;unsigned n=0;
        for(unsigned b=0;b<1000;b++) {synth_render(&s,32);if(b>500)for(float x:s.left){sum+=x;sq+=(double)x*x;n++;}}
        energy[low]=(float)(sq-sum*sum/n);
    }
    check(energy[1]<energy[0]*.01f);
    synth_init(&s);synth_event(&s,SYN_ON,1);synth_event(&s,SYN_ENV_AMOUNT,-99999);synth_event(&s,SYN_LFO_RATE,99999);synth_event(&s,SYN_ENGINE,99);
    check(s.value[SYN_INDEX(SYN_ENV_AMOUNT)]==-100 && s.value[SYN_INDEX(SYN_LFO_RATE)]==20000 && s.value[SYN_INDEX(SYN_ENGINE)]==1); // F2: out-of-range ordinals play Cluster
    {   // F2: older projects' ordinals: 0 FM and 2 VA -> VA, 1 Shape, 3 Cluster and 4 Swarm -> Cluster.
        const int want[5]={0,1,0,1,1};
        for(int v=0;v<5;v++){synth_voice e;synth_init(&e);synth_event(&e,SYN_ENGINE,v);check(e.value[SYN_INDEX(SYN_ENGINE)]==want[v]);}
        synth_voice e;synth_init(&e);synth_event(&e,SYN_ENGINE,-5);check(e.value[SYN_INDEX(SYN_ENGINE)]==0);
    }
    // Measured filter response against the analytical bilinear Butterworth
    // response, including -3 dB at cutoff and a flat low-frequency passband.
    for(float cutoff: {20.f,100.f,1000.f,8000.f,12000.f,20000.f})for(float fraction: {.125f,.5f,1.f}) {
        synth_init(&s);auto c=synth_lowpass_coefficients(cutoff);
        double e_in=0,e_out=0,freq=(double)cutoff*fraction;
        for(unsigned i=0;i<144000;i++) {
            float input=(float)std::sin(2.0*M_PI*freq*(double)i/48000.0)*.5f;
            float output=synth_lowpass(&s,input,c);
            if(i>=48000){e_in+=(double)input*input;e_out+=(double)output*output;}
        }
        double measured=std::sqrt(e_out/e_in),ratio=std::tan(M_PI*freq/48000.0)/std::tan(M_PI*cutoff/48000.0);
        double expected=1.0/std::sqrt(1.0+ratio*ratio*ratio*ratio);
        check(std::fabs(measured-expected)<.004);
    }
    // Smooth full-range sweeps and impulse transients must keep finite state.
    synth_init(&s);synth_event(&s,SYN_ON,1);midi(s,0x90,60,127);
    for(unsigned b=0;b<1200;b++) {
        synth_event(&s,SYN_CUTOFF,(b%100)<50?20:20000);synth_render(&s,32);
        check(std::isfinite(s.filter1) && std::isfinite(s.filter2) && s.cutoff>=20.f && s.cutoff<=20000.f);
        for(float x:s.left)check(std::isfinite(x) && std::fabs(x)<=.25f);
    }
    // All algorithms remain selectable after repeated switches and extremes.
    for(unsigned repeat=0;repeat<25;repeat++)for(unsigned engine=0;engine<SYN_ENGINE_COUNT;engine++) {
        synth_event(&s,SYN_ENGINE,engine);synth_event(&s,SYN_ON,1);midi(s,0x90,48+repeat%36,127);
        for(unsigned b=0;b<20;b++)synth_render(&s,32);
        for(float x:s.left)check(std::isfinite(x) && std::fabs(x)<=.25f);
    }
    // S18: Reso. 0 keeps the Butterworth damping exactly; 1000 lifts the
    // response at the cutoff by roughly +8 dB (Q 2.5), stays stable and
    // never self-oscillates.
    {
        synth_voice r;synth_init(&r);check(synth_reso_damping(&r)==1.41421356237f);
        synth_event(&r,SYN_RESO,5000);check(r.reso==1000 && std::fabs(synth_reso_damping(&r)-.4f)<.001f);
        double gain[2];
        for(unsigned q=0;q<2;q++) {
            synth_voice f;synth_init(&f);synth_event(&f,SYN_RESO,q?1000:0);
            auto c=synth_lowpass_coefficients(1000.f,synth_reso_damping(&f));
            double e_in=0,e_out=0;
            for(unsigned n=0;n<48000;n++) {
                float x=(float)std::sin(2.0*M_PI*1000.0*n/48000.0),y=synth_lowpass(&f,x,c);
                if(n>4800){e_in+=x*x;e_out+=(double)y*y;}
            }
            gain[q]=10.0*std::log10(e_out/e_in);
        }
        check(gain[0]<-2.5 && gain[0]>-3.5 && gain[1]>gain[0]+6.0 && gain[1]<gain[0]+12.0);
        synth_voice f;synth_init(&f);synth_event(&f,SYN_RESO,1000);float peak=0.f;
        auto c=synth_lowpass_coefficients(20000.f,synth_reso_damping(&f));
        for(unsigned n=0;n<48000;n++){float y=synth_lowpass(&f,n?0.f:1.f,c);if(std::fabs(y)>peak)peak=std::fabs(y);}
        check(std::isfinite(peak) && std::fabs(f.filter1)<1e-6f && std::fabs(f.filter2)<1e-6f);
    }
    // S19: Reso through the real render loop (S18 only changed the separate
    // filter function). A drone at A4 with Cutoff at the note: Reso 1000
    // lifts it; with Cutoff high, Reso changes the level only a little and
    // the output stays finite (S18 muted and glitched there).
    {
        double energy[2][2];
        for(unsigned high=0;high<2;high++)for(unsigned q=0;q<2;q++) {
            synth_voice v;synth_init(&v);synth_event(&v,SYN_ON,1);synth_event(&v,SYN_ENGINE,2);
            synth_event(&v,SYN_DRONE,1);synth_event(&v,SYN_CUTOFF,high?12000:440);synth_event(&v,SYN_RESO,q?1000:0);
            v.note=69;double e=0;bool finite=true;
            for(unsigned b=0;b<1500;b++) {
                synth_render(&v,32);
                for(unsigned i=0;i<32;i++){finite=finite && std::isfinite(v.left[i]);if(b>500)e+=(double)v.left[i]*v.left[i];}
            }
            check(finite && e>0);
            energy[high][q]=10.0*std::log10(e);
        }
        check(energy[0][1]>energy[0][0]+3.0);                         // audible peak at the cutoff
        check(std::fabs(energy[1][1]-energy[1][0])<3.0);              // no mute or blow-up up high
    }
    // S15: the output stage applies the track Gain (pregain, mdB) and the
    // Cluster boost; at 0 dB on other engines it leaves samples untouched.
    {
        synth_voice g;synth_init(&g);
        for(unsigned i=0;i<32;i++){g.left[i]=.25f;g.right[i]=-.25f;}
        synth_output_gain(&g,32);check(g.left[31]==.25f && g.right[0]==-.25f);
        synth_event(&g,SYN_PREGAIN_ID,-6021);
        for(unsigned i=0;i<32;i++){g.left[i]=.25f;g.right[i]=-.25f;}
        synth_output_gain(&g,32);check(g.left[0]<.25f && g.left[0]>.24f && std::fabs(g.left[31]-.125f)<.0005f);
        for(unsigned i=0;i<32;i++){g.left[i]=.25f;g.right[i]=-.25f;}
        synth_output_gain(&g,32);check(std::fabs(g.left[0]-.125f)<.0005f && std::fabs(g.right[31]+.125f)<.0005f);
        synth_event(&g,SYN_PREGAIN_ID,0);synth_event(&g,SYN_ENGINE,3);
        for(unsigned b=0;b<2;b++){for(unsigned i=0;i<32;i++){g.left[i]=.25f;g.right[i]=-.25f;}synth_output_gain(&g,32);}
        check(std::fabs(g.left[5]-.25f*SYN_CLUSTER_GAIN)<.0001f);
        synth_event(&g,SYN_PREGAIN_ID,99999);check(std::fabs(g.out_target-10.f)<.02f);
    }
    // S14: Swarm is removed; a project saved with it (engine 4) plays Cluster,
    // sample for sample the same as engine 3.
    synth_voice old_swarm,cluster;synth_init(&old_swarm);synth_event(&old_swarm,SYN_ENGINE,4);synth_event(&old_swarm,SYN_DRONE,1);
    synth_init(&cluster);synth_event(&cluster,SYN_ENGINE,3);synth_event(&cluster,SYN_DRONE,1);
    check(old_swarm.value[SYN_INDEX(SYN_ENGINE)]==1);
    float drone_peak=0.f;
    for(unsigned block=0;block<300;block++) {
        synth_render(&old_swarm,32);synth_render(&cluster,32);
        check(std::memcmp(old_swarm.left,cluster.left,sizeof(cluster.left))==0);
        for(float x:cluster.left)if(std::fabs(x)>drone_peak)drone_peak=std::fabs(x);
    }
    check(drone_peak>.01f);
    // Cluster: deterministic per seed, distinct across seeds, AUX optional,
    // bounded and finite over sweeping macros, notes and both Warp sides.
    {
        cobalt::ClusterEngine a,b,c;a.Init(nullptr);b.Init(nullptr);c.Init(nullptr);
        a.Reset();b.Reset();c.Reset();a.set_seed(5);b.set_seed(5);c.set_seed(6);
        bool differs=false;float peak=0.f;
        for(unsigned block=0;block<4096;block++) {
            plaits::EngineParameters params={plaits::TRIGGER_HIGH,24.f+(block%97),(block%41)*.025f,(block%101)*.01f,(block%67)*(1.f/66.f),1};
            float oa[24],ob[24],oc[24],aux[24];bool env=false;
            a.Render(params,oa,nullptr,24,&env);b.Render(params,ob,aux,24,&env);c.Render(params,oc,nullptr,24,&env);
            check(std::memcmp(oa,ob,sizeof(oa))==0 && std::memcmp(ob,aux,sizeof(ob))==0);
            for(unsigned i=0;i<24;i++) {
                check(std::isfinite(oa[i]) && std::fabs(oa[i])<=2.f);
                if(oa[i]!=oc[i])differs=true;
                if(std::fabs(oa[i])>peak)peak=std::fabs(oa[i]);
            }
        }
        check(differs && peak>.05f);
        // Seed events reach the voice, survive engine switches and only
        // reseed Cluster.
        synth_voice v;synth_init(&v);synth_event(&v,SYN_ENGINE,3);synth_event(&v,SYN_SEED,40);
        check(v.seed==SYN_SEED_COUNT-1);
        synth_event(&v,SYN_SEED,7);synth_event(&v,SYN_ENGINE,0);synth_event(&v,SYN_ENGINE,3);
        check(v.seed==7);
        synth_voice w;synth_init(&w);synth_event(&w,SYN_SEED,7);synth_event(&w,SYN_ENGINE,3);
        synth_event(&v,SYN_DRONE,1);synth_event(&w,SYN_DRONE,1);
        for(unsigned block=0;block<64;block++) {
            synth_render(&v,32);synth_render(&w,32);
            check(std::memcmp(v.left,w.left,sizeof(v.left))==0);
        }
    }
    // S7a resync: repeating every stored value must not retrigger, clear held
    // notes or reinitialise the engine of a sounding voice.
    {
        synth_voice v,w;synth_init(&v);synth_init(&w);
        const unsigned ids[]={SYN_ON,SYN_ENGINE,SYN_CHANNEL,SYN_HARMONICS,SYN_TIMBRE,SYN_MORPH,SYN_ATTACK,SYN_RELEASE,
            SYN_DRONE,SYN_CUTOFF,SYN_LFO_RATE,SYN_LFO_DEST,SYN_LFO_AMOUNT,SYN_ENV_DEST,SYN_ENV_AMOUNT,SYN_ENV_DECAY,
            SYN_ENV_ATTACK,SYN_LFO_SHAPE,SYN_LFO_SYNC,SYN_SEED};
        const int values[]={1,3,1,700,300,400,5,150,0,3000,1000,1,300,0,0,500,0,1,0,9};
        for(synth_voice* x:{&v,&w}) {
            for(unsigned i=0;i<20;i++)synth_event(x,ids[i],values[i]);
            x->channel_index=0;
            uint8_t on[5]={0x90,60,100,0,3};synth_midi(x,on);
        }
        for(unsigned block=0;block<200;block++) {
            if(block%7==0)for(unsigned i=0;i<20;i++)synth_event(&v,ids[i],values[i]);
            synth_render(&v,32);synth_render(&w,32);
            check(std::memcmp(v.left,w.left,sizeof(v.left))==0 && v.count==1);
        }
    }
    // S10: second LFO. Defaults leave LFO1 output unchanged; its own routes,
    // sum with LFO1 on a shared destination, independent rate/sync/random,
    // clamps, and the phase reset on engine change.
    {
        synth_init(&s);
        check(s.lfo2[SYN_LFO2_INDEX(SYN_LFO2_RATE)]==1000 && s.lfo2[SYN_LFO2_INDEX(SYN_LFO2_DEST)]==0 && s.lfo2[SYN_LFO2_INDEX(SYN_LFO2_AMOUNT)]==0);
        synth_voice a,b;
        for(synth_voice* x:{&a,&b}) {
            synth_init(x);synth_event(x,SYN_ON,1);synth_event(x,SYN_LFO_DEST,1);synth_event(x,SYN_LFO_AMOUNT,700);
            synth_event(x,SYN_LFO_RATE,3000);synth_event(x,SYN_LFO_SHAPE,1);x->channel_index=0;
            uint8_t on[5]={0x90,60,100,0,3};synth_midi(x,on);
        }
        synth_event(&b,SYN_LFO2_RATE,7000);synth_event(&b,SYN_LFO2_AMOUNT,900); // Dest still Off
        for(unsigned block=0;block<400;block++) {
            synth_render(&a,32);synth_render(&b,32);
            check(std::memcmp(a.left,b.left,sizeof(a.left))==0); // LFO2 Off: bit-identical to LFO1 alone
        }
        for(unsigned dest=1;dest<4;dest++) {
            synth_init(&s);synth_event(&s,SYN_LFO2_DEST,dest);synth_event(&s,SYN_LFO2_AMOUNT,500);
            float modulation[4];s.lfo2_phase=.25f;synth_modulate(&s,24,false,modulation);
            for(unsigned i=0;i<3;i++)check(std::fabs(modulation[i]-((i==0?.35f:.5f)+(dest==i+1?.5f:0.f)))<1e-6f);
            check(s.lfo_phase==0.f || s.lfo_phase<.001f);
        }
        synth_init(&s);synth_event(&s,SYN_LFO_DEST,1);synth_event(&s,SYN_LFO_AMOUNT,200);
        synth_event(&s,SYN_LFO2_DEST,1);synth_event(&s,SYN_LFO2_AMOUNT,300);
        {float m[4];s.lfo_phase=.25f;s.lfo2_phase=.25f;synth_modulate(&s,24,false,m);check(std::fabs(m[0]-(.35f+.2f+.3f))<1e-6f);}
        {float m[4];s.lfo_phase=.25f;s.lfo2_phase=.75f;synth_modulate(&s,24,false,m);check(std::fabs(m[0]-(.35f+.2f-.3f))<1e-6f);} // triangle at 3/4 = -1
        synth_init(&s);synth_event(&s,SYN_CUTOFF,1000);synth_event(&s,SYN_LFO2_DEST,4);synth_event(&s,SYN_LFO2_AMOUNT,100);
        {float m[4];s.lfo2_phase=.25f;synth_modulate(&s,24,false,m);check(std::fabs(m[3]/(1000.f*std::pow(2.f,.1f*119.5894114f/12.f))-1.f)<.01f);}
        synth_init(&s);synth_event(&s,SYN_LFO_RATE,1000);synth_event(&s,SYN_LFO2_RATE,20000);
        for(unsigned i=0;i<150;i++)synth_modulate(&s,16,false);
        check(s.lfo2_phase<.0001f || s.lfo2_phase>.9999f); // 20 Hz: whole periods
        check(std::fabs(s.lfo_phase-.05f)<.0001f); // LFO1 at 1 Hz, 2400 samples
        for(float bpm: {60.f,120.f})for(unsigned choice=1;choice<=12;choice++) {
            synth_init(&s);synth_event(&s,SYN_LFO2_SYNC,choice);
            check(std::fabs(synth_lfo2_hz(&s,bpm)-bpm/(60.*beats[choice-1]))<.0001 && synth_lfo_hz(&s,bpm)==1.f);
        }
        synth_event(&s,SYN_LFO2_SYNC,13+4);check(synth_lfo2_hz(&s,120)==1.f); // Free remembering a division
        synth_init(&s);synth_event(&s,SYN_LFO2_RATE,99999);synth_event(&s,SYN_LFO2_DEST,9);synth_event(&s,SYN_LFO2_AMOUNT,-5);
        synth_event(&s,SYN_LFO2_SHAPE,9);synth_event(&s,SYN_LFO2_SYNC,99);
        check(s.lfo2[0]==20000 && s.lfo2[1]==4 && s.lfo2[2]==0 && s.lfo2[3]==3 && s.lfo2[4]==24);
        synth_event(&s,SYN_LFO2_RATE,1);check(s.lfo2[0]==10);
        // Random shape: its own generator, stepping on its own wraps.
        synth_init(&s);synth_event(&s,SYN_LFO2_SHAPE,3);synth_event(&s,SYN_LFO_SHAPE,3);synth_event(&s,SYN_LFO2_RATE,20000);synth_event(&s,SYN_LFO_RATE,10);
        uint32_t r1=s.lfo_random,r2=s.lfo2_random;float held2=synth_lfo2_value(&s);
        check(held2!=synth_lfo_value(&s));
        for(unsigned i=0;i<200;i++)synth_modulate(&s,32,false);
        check(s.lfo_random==r1 && s.lfo2_random!=r2 && synth_lfo2_value(&s)!=held2);
        synth_event(&s,SYN_ON,1);s.lfo2_phase=.5f;synth_event(&s,SYN_ENGINE,1);check(s.lfo2_phase==0.f);
        // Unrelated ids never land in lfo2[].
        synth_init(&s);synth_event(&s,SYN_LFO2_MODE,1);synth_event(&s,0x1d6,5);synth_event(&s,0x1de,5);
        check(s.lfo2[0]==1000 && s.lfo2[1]==0 && s.lfo2[2]==0 && s.lfo2[3]==0 && s.lfo2[4]==0);
    }
    std::printf("PASS %u synth host assertions; 48kHz A4 crossings=%u; voice bytes=%zu\n",checks,crossings,sizeof(synth_voice));
}
