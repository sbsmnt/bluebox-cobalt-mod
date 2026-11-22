#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "../src/insert_drive.h"
static unsigned checks;
#define CHECK(x,msg) do {if(!(x)){fprintf(stderr,"FAIL %s\n",msg);return 1;}checks++;printf("PASS %s\n",msg);} while(0)
#define PI 3.14159265358979323846
static void configure(struct insert_drive *s,int drive,int tone,int mix,int output) {
    ins_reset(s);ins_event(s,INS_MODE,1);ins_event(s,INS_DRIVE,drive);
    ins_event(s,INS_TONE,tone);ins_event(s,INS_MIX,mix);ins_event(s,INS_OUTPUT,output);
}
static double rms_at(int drive,int tone,int mix,int output,double frequency,double *third) {
    struct insert_drive s;configure(&s,drive,tone,mix,output);
    double power=0,re=0,im=0;unsigned count=0;float l[32];
    for(unsigned block=0;block<1500;block++) {
        for(unsigned i=0;i<32;i++)l[i]=(float)(.25*sin(2*PI*frequency*(block*32+i)/48000.));
        ins_run(&s,l,0,32);
        if(block>=750)for(unsigned i=0;i<32;i++) {
            double t=2*PI*frequency*(block*32+i)/48000.;
            power+=(double)l[i]*l[i];re+=l[i]*cos(3*t);im-=l[i]*sin(3*t);count++;
        }
    }
    if(third)*third=2*sqrt(re*re+im*im)/count;
    return sqrt(power/count);
}
int main(void) {
    struct insert_drive s,a,b;float l[34],r[34],dry[32];
    struct fsh_state fsb;fsh_init(&fsb,500,0,0); /* F3: the track's shifter state (SDRAM bank on the device) */
    ins_reset(&s);for(unsigned i=0;i<32;i++)dry[i]=(float)i*.01f;
    memcpy(l+1,dry,sizeof(dry));l[0]=12.3f;l[33]=-45.6f;
    ins_run(&s,l+1,0,32);
    CHECK(!memcmp(l+1,dry,sizeof(dry)) && l[0]==12.3f && l[33]==-45.6f,"Off is exact dry with memory guards");
    configure(&s,1000,1000,1000,0);
    ins_event(&s,INS_DRIVE,-99999);ins_event(&s,INS_TONE,99999);ins_event(&s,INS_OUTPUT,-99999);
    CHECK(s.drive==0 && s.tone==1000 && s.output==-18000,"control inputs clamp to registered ranges");
    ins_event(&s,INS_MODE,99);CHECK(s.mode==0,"unknown mode falls back to Off");
    configure(&a,750,650,1000,0);b=a;
    for(unsigned block=0;block<500;block++) {
        for(unsigned i=0;i<32;i++)l[i]=(float)(.4*sin((block*32+i)*.063));
        memcpy(r,l,128);ins_run(&a,l,l,32);ins_run(&b,r,0,32);
    }
    CHECK(!memcmp(l,r,128),"aliased stereo input is processed once");
    configure(&s,1000,1000,1000,0);
    for(unsigned block=0;block<1000;block++) {
        for(unsigned i=0;i<32;i++){l[i]=.25f;r[i]=0.f;}
        ins_run(&s,l,r,32);
    }
    double silence=0;for(unsigned i=0;i<32;i++)silence+=fabs(r[i]);
    CHECK(silence==0,"left distortion/filter state does not leak into right channel");
    double dc=0;for(unsigned i=0;i<32;i++)dc+=fabs(l[i]);
    CHECK(dc<.0001,"DC input is removed after settling");
    ins_event(&s,INS_MODE,0);
    for(unsigned block=0;block<120;block++){memcpy(l,dry,128);ins_run(&s,l,0,32);}
    CHECK(s.enable==0.f && !memcmp(l,dry,128),"Off transition settles to exact dry");
    configure(&s,800,300,0,6000);memcpy(l,dry,128);ins_run(&s,l,0,32);
    CHECK(!memcmp(l,dry,128),"Mix zero is exact dry including nonzero Output");
    configure(&s,0,0,1000,6000);memcpy(l,dry,128);ins_run(&s,l,0,32);
    CHECK(!memcmp(l,dry,128),"Drive zero is exact dry");
    configure(&s,1000,300,1000,6000);s.enable=1.f;
    for(unsigned i=0;i<32;i++)l[i]=(i%3)==0?NAN:(i%3)==1?INFINITY:-INFINITY;
    ins_run(&s,l,0,32);int finite=1;for(unsigned i=0;i<32;i++)finite &= isfinite(l[i]);
    CHECK(finite,"active nonfinite input cannot poison filters");
    uint32_t rng=42;int bounded=1;
    for(unsigned block=0;block<20000;block++) {
        if(!(block%17)) {
            ins_event(&s,INS_DRIVE,(int)(block%1001));ins_event(&s,INS_TONE,(int)((block*7)%1001));
            ins_event(&s,INS_MIX,(int)((block*13)%1001));ins_event(&s,INS_OUTPUT,(int)(block%24001)-18000);
        }
        for(unsigned i=0;i<32;i++){rng=rng*1664525u+1013904223u;l[i]=(float)(int32_t)rng*(16.f/2147483648.f);r[i]=-l[i];}
        ins_run(&s,l,r,32);
        for(unsigned i=0;i<32;i++)bounded &= isfinite(l[i]) && isfinite(r[i]) && fabs(l[i])<40.f && fabs(r[i])<40.f;
    }
    CHECK(bounded,"640000 stereo samples remain finite during parameter sweeps and overload");
    double hlow,hhigh,low=rms_at(50,1000,1000,0,1000,&hlow),high=rms_at(1000,1000,1000,0,1000,&hhigh);
    CHECK(hhigh>hlow*8 && high>0.05,"Drive adds substantial third-harmonic distortion");
    double dark=rms_at(700,0,1000,0,6000,0),bright=rms_at(700,1000,1000,0,6000,0);
    CHECK(dark<bright*.5,"Tone darkens high-frequency output");
    double quiet=rms_at(700,650,1000,-12000,1000,0),unity=rms_at(700,650,1000,0,1000,0);
    CHECK(fabs(20*log10(quiet/unity)+12)<.02,"Output trim follows decibels after wet/dry mix");
    /* Independent mathematical reference over normal and overloaded input.
     * No division by the periodic transfer function in normalization. */
    int shapes=1,periodic=1;
    for(int i=-65536;i<=65536;i++) {
        float x=(float)i/1024.f;
        float f=ins_fold(x),w=ins_wrap(x);
        shapes &= isfinite(f) && fabsf(f)<=1 && isfinite(w) && fabsf(w)<=1;
        periodic &= fabsf(f-ins_fold(x+4))<.00001f && fabsf(w-ins_wrap(x+2))<.00001f;
    }
    CHECK(shapes && periodic,"FOLD/WRAP remain bounded and match their four/two-unit periods across 131073 inputs");
    CHECK(ins_fold(1.25f)==.75f && ins_fold(2.5f)==-.5f && ins_wrap(1.25f)==-.75f && ins_wrap(-1.25f)==.75f,"FOLD reflects and WRAP restarts beyond the unit ceiling");
    CHECK(ins_fold(1e30f)==0 && ins_wrap(-1e30f)==0,"periodic range reduction guards invalid and extreme input before integer conversion");
    /* S8f: WRAP ADAA. Between jumps it is the two-sample mean; across a jump
     * it is the exact mean of wrap() over the step; it stays bounded. */
    {
        float px=0,pw=0;int mean=1,bounded=1;
        ins_wrap_aa(.2f,&px,&pw);mean &= fabsf(ins_wrap_aa(.6f,&px,&pw)-.4f)<1e-6f;
        /* .6 -> 1.4 crosses +1: wrap runs .6..1 then -1..-.6, mean 0 */
        mean &= fabsf(ins_wrap_aa(1.4f,&px,&pw))<1e-5f;
        px=0;pw=0;
        for(int i=0;i<200000;i++){
            rng=rng*1664525u+1013904223u;float x=(float)(int32_t)rng*(40.f/2147483648.f);
            if(i&1)x=px+(float)(int32_t)(rng>>7)*(1e-6f/16777216.f);
            float y=ins_wrap_aa(x,&px,&pw);bounded &= isfinite(y) && fabsf(y)<=1.f;
        }
        CHECK(mean && bounded,"WRAP ADAA is the mean of wrap over each step and bounded for random and near-zero steps");
        /* Harmonic share of a driven sine, naive wrap vs ADAA (Drive 850). */
        static double yn[48000],ya[48000];double ratio[2][2];const double freqs[2]={109,3313};
        float g=1.f+31.f*.85f*.85f;
        for(int fi=0;fi<2;fi++){
            px=0;pw=0;double tn=0,ta=0;
            for(int i=0;i<48000;i++){float x=(float)(.3*sin(2*M_PI*freqs[fi]*i/48000.))*g;yn[i]=ins_wrap(x);ya[i]=ins_wrap_aa(x,&px,&pw);tn+=yn[i]*yn[i];ta+=ya[i]*ya[i];}
            double hn=0,ha=0;
            for(int k=1;k*freqs[fi]<24000;k++){double w=2*M_PI*k*freqs[fi]/48000.,cn=0,sn=0,ca=0,sa=0;
                for(int i=0;i<48000;i++){double c=cos(w*i),q=sin(w*i);cn+=yn[i]*c;sn+=yn[i]*q;ca+=ya[i]*c;sa+=ya[i]*q;}
                hn+=2*(cn*cn+sn*sn)/(48000.*48000.);ha+=2*(ca*ca+sa*sa)/(48000.*48000.);}
            tn/=48000;ta/=48000;
            ratio[fi][0]=10*log10((tn-hn)/hn);ratio[fi][1]=10*log10((ta-ha)/ha);
        }
        printf("INFO WRAP inharmonic/harmonic dB at Drive 850: 109 Hz naive %.1f ADAA %.1f; 3313 Hz naive %.1f ADAA %.1f\n",ratio[0][0],ratio[0][1],ratio[1][0],ratio[1][1]);
        CHECK(ratio[1][0]>0 && ratio[1][1]<-10,"WRAP ADAA turns a 3.3 kHz sine from mostly aliasing into mostly harmonics (>10 dB)");
        CHECK(ratio[0][1]<ratio[0][0]-6,"WRAP ADAA also lowers low-frequency aliasing");
    }
    float signature[3][512];static const int drives[3]={1,3,4};
    for(int d=0;d<3;d++) {
        int mode=drives[d];
        configure(&s,850,850,1000,0);ins_event(&s,INS_MODE,mode);
        int safe=1;
        for(unsigned block=0;block<1000;block++) {
            for(unsigned i=0;i<32;i++){float t=(float)(block*32+i);l[i]=.31f*sinf(t*.093f);r[i]=0;}
            ins_run(&s,l,r,32);
            for(unsigned i=0;i<32;i++)safe &= isfinite(l[i]) && fabsf(l[i])<1.f && r[i]==0;
            if(block>=984)memcpy(signature[d]+(block-984)*32,l,128);
        }
        CHECK(safe && s.running_mode==mode && s.enable==1.f,"all modes settle, remain bounded and keep stereo channels independent");
        ins_event(&s,INS_MODE,0);
        for(unsigned block=0;block<120;block++){memcpy(l,dry,128);ins_run(&s,l,0,32);}
        CHECK(s.enable==0 && !memcmp(l,dry,128),"every mode transitions to exact dry Off");
    }
    int distinct=1;
    for(int i=0;i<3;i++)for(int j=i+1;j<3;j++){
        double difference=0;for(int k=0;k<512;k++)difference+=fabs(signature[i][k]-signature[j][k]);
        distinct &= difference/512>.005;
    }
    CHECK(distinct,"three drives produce distinct audio at matched drive/tone/mix settings");
    configure(&s,750,650,1000,0);
    for(unsigned block=0;block<120;block++){for(int i=0;i<32;i++)l[i]=.1f;ins_run(&s,l,0,32);}
    ins_event(&s,INS_MODE,4);int transitioned=0,smooth=1;
    for(unsigned block=0;block<240;block++) {
        int old_mode=s.running_mode;float old_enable=s.enable;
        for(int i=0;i<32;i++)l[i]=.1f;ins_run(&s,l,0,32);
        if(s.running_mode!=old_mode){transitioned++;smooth &= old_enable==0.f;}
    }
    CHECK(transitioned==1 && smooth && s.running_mode==4 && s.enable==1,"algorithm switches only at exact dry bypass then fades back in");
    int swept=1;
    for(int d=0;d<3;d++) {
        int mode=drives[d];
        configure(&s,1000,1000,1000,6000);ins_event(&s,INS_MODE,mode);
        for(unsigned block=0;block<2000;block++) {
            if(!(block%17)){ins_event(&s,INS_DRIVE,(block*31)%1001);ins_event(&s,INS_TONE,(block*11)%1001);}
            for(int i=0;i<32;i++){rng=rng*1664525u+1013904223u;l[i]=(float)(int32_t)rng*(16.f/2147483648.f);r[i]=-l[i];}
            ins_run(&s,l,r,32);
            for(int i=0;i<32;i++)swept &= isfinite(l[i]) && isfinite(r[i]) && fabsf(l[i])<40 && fabsf(r[i])<40;
        }
    }
    CHECK(swept,"192000 additional stereo samples cover overload/control sweeps in all three shapers");
    printf("INFO cubic harmonic amplitude low %.8f high %.8f, tone dark/bright %.5f, output trim %.5f dB, RMS low/high %.5f/%.5f\n",hlow,hhigh,dark/bright,20*log10(quiet/unity),low,high);
    /* S8 filter types through ins_process: random control sweeps, overload,
     * mono/stereo, type and family changes; finite and bounded throughout. */
    int filtered=1;
    for(int mode=5;mode<=8;mode++) {
        configure(&s,500,500,500,0);ins_event(&s,INS_MODE,mode);
        for(unsigned block=0;block<3000;block++) {
            if(!(block%13)){ins_event(&s,INS_DRIVE,(block*37)%1001);ins_event(&s,INS_TONE,(block*7)%1001);ins_event(&s,INS_MIX,(block*53)%1001);}
            if(block==1500)ins_event(&s,INS_MODE,mode==8?1:mode+1);
            if(block==2200)ins_event(&s,INS_MODE,mode);
            for(int i=0;i<32;i++){rng=rng*1664525u+1013904223u;l[i]=(float)(int32_t)rng*(4.f/2147483648.f);r[i]=.5f*l[i];}
            ins_process(&s,&fsb,l,(block/700)%2?r:0,32);
            for(int i=0;i<32;i++)filtered &= isfinite(l[i]) && fabsf(l[i])<8 && isfinite(r[i]) && fabsf(r[i])<8;
        }
    }
    CHECK(filtered,"384000 samples of LP/BP/HP/DJ sweeps, overload and family changes stay finite and bounded");
    configure(&s,500,350,500,0);ins_event(&s,INS_MODE,8);
    for(unsigned block=0;block<200;block++){memcpy(l,dry,128);ins_process(&s,&fsb,l,0,32);}
    CHECK(!memcmp(l,dry,128) && s.running_mode==8,"DJ centre is exact dry after the fade-in");
    /* Spectral F1 shifter types: random Shift/Feedback/Mix/Output sweeps,
     * overload, odd block sizes, mono/stereo, type and family changes. */
    int shifted=1;
    for(int mode=9;mode<=14;mode++) { /* F4: + resonator HARM / CHRD / BELL */
        configure(&s,500,500,500,0);ins_event(&s,INS_MODE,mode);
        for(unsigned block=0;block<3000;block++) {
            if(!(block%11)){ins_event(&s,INS_DRIVE,(block*37)%1001);ins_event(&s,INS_TONE,(block*7)%1001);ins_event(&s,INS_MIX,(block*53)%1001);ins_event(&s,INS_OUTPUT,(int)((block*977)%24001)-18000);}
            if(block==1000)ins_event(&s,INS_MODE,mode==14?5:mode+1);
            if(block==1700)ins_event(&s,INS_MODE,1);
            if(block==2200)ins_event(&s,INS_MODE,mode);
            unsigned n=block%5==4?1u+block%31u:32u;
            for(unsigned i=0;i<n;i++){rng=rng*1664525u+1013904223u;l[i]=(float)(int32_t)rng*(4.f/2147483648.f);r[i]=.5f*l[i];}
            ins_process(&s,&fsb,l,(block/700)%2?r:0,n);
            for(unsigned i=0;i<n;i++)shifted &= isfinite(l[i]) && fabsf(l[i])<8 && isfinite(r[i]) && fabsf(r[i])<8;
        }
    }
    CHECK(shifted,"SHFT/SPRD/RING/HARM/CHRD/BELL sweeps, overload, odd blocks and family changes stay finite and bounded");
    configure(&s,500,0,1000,0);ins_event(&s,INS_MODE,9);
    for(unsigned block=0;block<200;block++){memcpy(l,dry,128);ins_process(&s,&fsb,l,0,32);}
    ins_event(&s,INS_MODE,0);
    for(unsigned block=0;block<200;block++){memcpy(l,dry,128);ins_process(&s,&fsb,l,0,32);}
    CHECK(!memcmp(l,dry,128) && s.running_mode==0,"shifter switched Off hands back and ends exact dry");
    /* SD4: SPEC sleeps on silence only after its own tail has decayed below
     * -120 dB. A reference that never sleeps (quiet forced to 0) gets the same
     * blocks: burst, ~33 s of silence with a control change, then sound. */
    {
        int modes[3][3]={{13,600,1000},{9,700,1000},{11,800,900}}; /* CHRD 12 s decay; SHFT and RING, feedback */
        for(unsigned m=0;m<3;m++) {
            static struct insert_drive a,b;static struct fsh_state fa,fb2;
            configure(&a,modes[m][1],modes[m][2],1000,0);configure(&b,modes[m][1],modes[m][2],1000,0);
            ins_event(&a,INS_MODE,modes[m][0]);ins_event(&b,INS_MODE,modes[m][0]);
            if(modes[m][0]==13){ins_event(&a,INS_CHORD,3);ins_event(&b,INS_CHORD,3);}
            double worst_tail=0,worst_wake=0,ph=0,ea=0,eb=0;unsigned slept=0,first_sleep=0,heard_late=0;
            uint32_t rng=99;
            for(unsigned block=0;block<51000;block++) {
                int sound=block<300 || block>=50300;
                if(block==20000){ins_event(&a,INS_DRIVE,300);ins_event(&b,INS_DRIVE,300);ins_event(&a,INS_MIX,700);ins_event(&b,INS_MIX,700);}
                float al[32],ar[32],bl[32],br[32];
                for(unsigned i=0;i<32;i++){rng=rng*1664525u+1013904223u;
                    float v=sound?(float)(.3*sin(ph)+.1*(float)(int32_t)rng/2147483648.f):0.f;ph+=6.283185307*220./48000.;
                    al[i]=bl[i]=v;ar[i]=br[i]=-.7f*v;}
                b.quiet=0;
                ins_process(&a,&fa,al,ar,32);ins_process(&b,&fb2,bl,br,32);
                if(a.quiet>=INS_QUIET_BLOCKS){slept++;if(!first_sleep)first_sleep=block;}
                for(unsigned i=0;i<32;i++) {
                    double d=fabs((double)al[i]-bl[i])+fabs((double)ar[i]-br[i]);
                    if(block<50300){if(d>worst_tail)worst_tail=d;}else{if(d>worst_wake)worst_wake=d;if(block>=50400){ea+=(double)al[i]*al[i];eb+=(double)bl[i]*bl[i];}}
                    if(block==300+2250 && fabsf(al[i])>1e-4f)heard_late=1;
                }
            }
            printf("INFO SPEC mode %d: asleep from block %u (%.1f s after the input stopped), %u blocks asleep, tail diff %.3g, wake diff %.3g, wake level %+.3f dB\n",
                   modes[m][0],first_sleep,(first_sleep-300)*32/48000.,slept,worst_tail,worst_wake,10*log10(ea/eb));
            CHECK(slept>10000 && first_sleep>300,"SPEC sleeps on long silence");
            CHECK(worst_tail<2e-5,"SPEC tail plays out: the output matches a never-sleeping insert within -94 dB until and through sleep");
            /* The shifter's oscillator phase is not advanced while asleep (any
             * phase is as good), so its samples differ; its level must not. */
            if(modes[m][0]==13)CHECK(worst_wake<1e-4,"CHRD wakes on sound from clear state on the current settings, matching the reference");
            else CHECK(fabs(10*log10(ea/eb))<.2,"SHFT/RING wake on sound at the reference's level (oscillator phase may differ)");
            if(modes[m][0]==13)CHECK(heard_late,"CHRD with 12 s decay still rings 1.5 s after the input stopped");
        }
    }
    printf("%u insert audio/sanitizer checks passed; no oversampling or hardware-cycle claim.\n",checks);
    return 0;
}
