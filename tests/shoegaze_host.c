#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include "../src/shoegaze.h"
static struct shoegaze a,b;
static unsigned checks;
#define CHECK(x,msg) do {assert(x);checks++;printf("PASS %s\n",msg);}while(0)
static void ready(struct shoegaze *s) {
    sg_init(s);sg_reset_tail(s);float l[32]={0},r[32]={0};
    while(s->clearing)sg_run(s,l,r,32,1.f);
}
static unsigned rng=312443u;
static float noise(void){rng=rng*1664525u+1013904223u;return (float)(int32_t)rng/2147483648.f;}
static double impulse(struct shoegaze *s,int size) {
    ready(s);sg_event(s,SG_SIZE,size);s->ctl[1]=(float)size;
    sg_event(s,SG_SAT,0);s->ctl[0]=0;
    double late=0;float peak=0;
    for(unsigned n=0;n<48000u*16u;n+=32) {
        float l[32]={0},r[32]={0};if(n==0)l[0]=r[0]=.5f;
        sg_run(s,l,r,32,1.f);
        for(unsigned k=0;k<32;k++) {
            assert(isfinite(l[k])&&isfinite(r[k]));
            if(fabsf(l[k])>peak)peak=fabsf(l[k]);
            if(n>=48000u*5u)late+=(double)l[k]*l[k]+(double)r[k]*r[k];
        }
    }
    printf("INFO size %d: late impulse energy %.9g peak %.6f\n",size,late,peak);
    assert(peak>.00001f && peak<1.f);return late;
}
static double tone_energy(float hz,unsigned id,int value) {
    ready(&a);sg_event(&a,id,value);a.ctl[id-SG_SAT]=(float)a.raw[id-SG_SAT];
    sg_event(&a,SG_MOD,0);a.ctl[3]=0.f;double energy=0;
    for(unsigned n=0;n<48000u*3u;n+=32) {
        float x[32],y[32];for(unsigned i=0;i<32;i++)x[i]=y[i]=.15f*sinf((n+i)*6.2831853f*hz/48000.f);
        sg_run(&a,x,y,32,1.f);
        if(n>=48000u)for(unsigned i=0;i<32;i++)energy+=(double)x[i]*x[i]+(double)y[i]*y[i];
    }
    return energy;
}
static double distortion_thd(float amount) {
    float prev=0;double ss=0,si=0,co=0;
    for(unsigned n=0;n<48000;n++) {
        float phase=(float)n*6.2831853f*500.f/48000.f;
        float y=sg_distort(.005f*sinf(phase),&prev,amount);
        ss+=(double)y*y;si+=y*sinf(phase);co+=y*cosf(phase);
    }
    double fp=2.*(si*si+co*co)/(48000.*48000.);
    double hp=ss/48000.-fp;if(hp<0)hp=0;
    return sqrt(hp/fp);
}
static double filter_gain(unsigned high) {
    float state[2]={0,0},g=sg_filter_g(1000.f),a1=1.f/(1.f+g*(g+.95238095f));
    float hz=high?1333.333f:750.f;double input=0,output=0;
    for(unsigned n=0;n<48000;n++) {
        float x=sinf(n*6.2831853f*hz/48000.f),y=sg_filter(state,x,g,a1,(int)high);
        if(n>1000){input+=(double)x*x;output+=(double)y*y;}
    }
    return sqrt(output/input);
}
int main(void) {
    printf("INFO Shoegaze SDRAM bytes %u; rounded allocation %u\n",SG_BYTES,(SG_BYTES+31u)&~31u);
    ready(&a);float l[32]={0},r[32]={0};
    for(unsigned n=0;n<3000;n++) {sg_run(&a,l,r,32,1.f);for(unsigned k=0;k<32;k++)assert(l[k]==0.f&&r[k]==0.f);}
    CHECK(1,"cold silence remains exactly silent");
    for(int id=SG_SAT;id<=SG_LOCUT;id++) {
        sg_event(&a,id,-2147483647);sg_event(&a,id,2147483647);
        assert(a.raw[id-SG_SAT]>=0 && a.raw[id-SG_SAT]<=16000);
    }
    CHECK(1,"all seven malformed controls are bounded");
    float p=0.f;CHECK(sg_distort(.123f,&p,0.f)==.123f,"zero saturation is exact distortion bypass");
    CHECK(fabsf(sg_curve(1.f)-2.f/3.f)<1e-6f && fabsf(sg_integral(1.f)-5.f/12.f)<1e-6f,"rounded distortion and antiderivative meet continuously at the clip corner");
    double clean=distortion_thd(0.f),mild=distortion_thd(.35f),fuzz=distortion_thd(1.f);
    printf("INFO 500Hz -46dBFS distortion THD clean %.6f mild %.6f fuzz %.6f\n",clean,mild,fuzz);
    CHECK(clean<.001 && mild<.02 && fuzz>.30,"full Saturate produces heavy fuzz harmonics even on a quiet source");
    double lp=filter_gain(0),hp=filter_gain(1);
    printf("INFO resonant filter shoulders LP %.4f HP %.4f\n",lp,hp);
    CHECK(lp>1.1 && lp<1.5 && hp>1.1 && hp<1.5,"Hi and Lo Cut have a bounded resonant shoulder outside feedback");
    double shorttail=impulse(&a,0),longtail=impulse(&b,1000);
    CHECK(longtail>shorttail*1000. && longtail>1e-9,"Size lengthens measured decay without adding a second reverb");
    ready(&a);ready(&b);sg_event(&a,SG_DIFF,0);a.ctl[2]=0;
    double diff=0;for(unsigned n=0;n<48000;n+=32) {
        float x[32]={0},y[32]={0},xx[32]={0},yy[32]={0};if(!n)x[0]=y[0]=xx[0]=yy[0]=.5f;
        sg_run(&a,x,y,32,1.f);sg_run(&b,xx,yy,32,1.f);
        for(unsigned k=0;k<32;k++)diff+=fabsf(x[k]-xx[k]);
    }
    CHECK(diff>.01,"Diffusion changes the wet response");
    ready(&a);ready(&b);sg_event(&a,SG_MOD,0);a.ctl[3]=0;
    diff=0;for(unsigned n=0;n<48000u*3u;n+=32) {
        float x[32],y[32],xx[32],yy[32];for(unsigned k=0;k<32;k++)x[k]=y[k]=xx[k]=yy[k]=.1f*sinf((n+k)*.04f);
        sg_run(&a,x,y,32,1.f);sg_run(&b,xx,yy,32,1.f);for(unsigned k=0;k<32;k++)diff+=fabsf(x[k]-xx[k]);
    }
    CHECK(diff>.01,"Mod Depth changes the reverb tank response");
    ready(&a);ready(&b);sg_event(&a,SG_RATE,10);a.ctl[4]=10;
    sg_event(&b,SG_RATE,2000);b.ctl[4]=2000;
    for(unsigned n=0;n<48000/32;n++){float x[32]={0},y[32]={0};sg_run(&a,x,y,32,1.f);sg_run(&b,x,y,32,1.f);}
    CHECK(fabsf(a.phase-.01f)<.0001f && (b.phase<.001f || b.phase>.999f),"Rate covers 0.01 to 2 Hz independently of Mod Depth");
    double bright=tone_energy(6000.f,SG_HICUT,16000),dark=tone_energy(6000.f,SG_HICUT,500);
    printf("INFO 6 kHz Hi Cut energy open %.6g closed %.6g\n",bright,dark);
    CHECK(bright>dark*100.,"Hi Cut audibly attenuates upper-frequency wet energy");
    double bass=tone_energy(110.f,SG_LOCUT,20),cut=tone_energy(110.f,SG_LOCUT,2000);
    printf("INFO 110 Hz Lo Cut energy open %.6g closed %.6g\n",bass,cut);
    CHECK(bass>cut*100.,"Lo Cut audibly attenuates low-frequency wet energy");
    ready(&a);double le=0,re=0;rng=312443;
    for(unsigned n=0;n<48000u*10u;n+=32) {
        float x[32],y[32];for(unsigned i=0;i<32;i++)x[i]=y[i]=n<240000u?noise()*.2f:0.f;
        sg_run(&a,x,y,32,1.f);
        if(n>=48000u)for(unsigned i=0;i<32;i++){le+=(double)x[i]*x[i];re+=(double)y[i]*y[i];}
    }
    printf("INFO centered broadband stereo difference %.3f dB\n",10*log10(le/re));
    CHECK(le>0 && re>0 && fabs(10*log10(le/re))<1.5,"centered broadband source has balanced stereo wet energy");
    ready(&a);float worst=0;
    for(unsigned n=0;n<48000u*40u;n+=32) {
        if(n%24000u==0)for(unsigned id=SG_SAT;id<=SG_LOCUT;id++)sg_event(&a,id,(n/24000u)&1u?2147483647:0);
        float x[32],y[32];for(unsigned k=0;k<32;k++){x[k]=noise()*2.f;y[k]=noise()*2.f;}
        if(n==48000){x[3]=NAN;y[7]=INFINITY;}
        sg_run(&a,x,y,32,2.f);
        for(unsigned k=0;k<32;k++){assert(isfinite(x[k])&&isfinite(y[k])&&fabsf(x[k])<2.001f&&fabsf(y[k])<2.001f);if(fabsf(x[k])>worst)worst=fabsf(x[k]);}
    }
    printf("INFO forty-second randomized control/overload peak %.6f\n",worst);
    CHECK(1,"rapid extremes, overdrive and nonfinite input remain finite and bounded under sanitizers");
    float x[32],y[32];for(unsigned k=0;k<32;k++)x[k]=y[k]=.25f;
    sg_run(&a,x,y,1,1.f);CHECK(x[1]==.25f&&y[31]==.25f,"one-frame block leaves unused output untouched");
    sg_run(&a,x,y,0,1.f);CHECK(x[1]==.25f&&y[31]==.25f,"empty block leaves output untouched");
    sg_reset_tail(&a);unsigned blocks=0;do{sg_run(&a,x,y,32,1.f);blocks++;}while(a.clearing);
    CHECK(blocks==50 && a.fade==0.f,"history clears in exactly fifty bounded blocks before fade-in");
    printf("%u Shoegaze host checks passed (ASan/UBSan run required).\n",checks);
}
