#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/granular.h"
#include "../src/delay_types.h"
static struct granular g;static struct delay_types x;
static float memory[G_CAP*2+8];
static unsigned checks;
#define CHECK(c,m) do{assert(c);checks++;puts("PASS " m);}while(0)
static void reset(void) {
    for(unsigned i=0;i<G_CAP*2;i++)memory[i]=NAN;
    for(unsigned i=G_CAP*2;i<G_CAP*2+8;i++)memory[i]=12345.f;
    g_reset(&g,0,memory);x_reset(&x,0);
}
static void mode(int value){g_event(&g,G_MODE,value);x_event(&x,G_MODE,value);}
static unsigned sample,peak_heads;
static void block(unsigned n,float amplitude,float *l,float *r) {
    float il[32]={0},ir[32]={0};
    for(unsigned i=0;i<n;i++) {
        il[i]=amplitude*sinf((float)sample*.071f);ir[i]=amplitude*cosf((float)sample*.113f);
        l[i]=.125f;r[i]=-.25f;sample++;
    }
    x_prepare(&x,n);x_tape(&x,l,n,0,(sample/37)&1);x_tape(&x,r,n,1,(sample/37)&1);x_tape_return(&x,l,r,n);x_reverse_input(&x,&g,il,ir,n);
    unsigned wr=g.wr,valid=g.valid;g_render(&g,il,ir,l,r,n);
    x_reverse(&x,&g,l,r,n,wr,valid);x_duck(&x,il,ir,l,r,n);
    for(unsigned i=0;i<n;i++)assert(isfinite(l[i])&&isfinite(r[i])&&fabsf(l[i])<=4.01f&&fabsf(r[i])<=4.01f);
    for(unsigned i=G_CAP*2;i<G_CAP*2+8;i++)assert(memory[i]==12345.f);
    unsigned heads=0;for(unsigned j=0;j<2;j++)if(x.heads[j].len){heads++;assert(x.heads[j].pos>=0.f&&x.heads[j].pos<(float)G_CAP);}
    if(heads>peak_heads)peak_heads=heads;assert(heads<=2);
}
int main(void) {
    float l[32],r[32];reset();
    CHECK(x.mode==0&&x.drive==350&&x.duck==700&&x.release==300&&x.segment==400&&x.tone==650,"new modes reset to documented safe defaults");
    for(int v=-100;v<1100;v+=13){x_event(&x,T_DRIVE,v);x_event(&x,T_DUCK,v);x_event(&x,T_RELEASE,v);x_event(&x,T_SEGMENT,v);x_event(&x,T_TONE,v);}
    CHECK(x.drive<=1000&&x.duck<=1000&&x.release>=20&&x.release<=2000&&x.segment>=40&&x.segment<=600&&x.tone<=1000,"all new event values clamp to their defined ranges");
    mode(999);CHECK(x.mode==0&&g.mode==0,"malformed mode selects original Delay, never a new effect");
    reset();for(unsigned b=0;b<3500;b++)block(32,.3f,l,r);
    CHECK(l[0]==.125f&&r[0]==-.25f&&g.valid==G_CAP,"Normal output remains exact while shared history primes and wraps");
    mode(4);peak_heads=0;unsigned active=0;float energy=0.f;
    for(unsigned b=0;b<3200;b++){block(32,.3f,l,r);for(unsigned i=0;i<32;i++)energy+=fabsf(l[i]);for(unsigned j=0;j<2;j++)if(x.heads[j].len)active++;}
    CHECK(active>100&&energy>1.f&&x.rev_mix==1.f&&peak_heads==2,"reverse plays audible finite stereo windows after history and ring wraps");
    CHECK((!x.heads[0].len||x.heads[0].step<0.f)&&(!x.heads[1].len||x.heads[1].step<0.f),"reverse playback heads advance backwards");
    /* Known monotonic history isolates playback direction from input timing. */
    reset();for(unsigned i=0;i<G_CAP;i++)memory[2*i]=memory[2*i+1]=(float)i/(float)G_CAP;
    g.wr=80000;g.valid=G_CAP;mode(4);x.rev_mix=1.f;x.countdown=9999;
    x.heads[0]=(struct grain){60000.f,-1.f,.0001f,5000,10000};x.rev_lp[0]=x.rev_lp[1]=.625f;
    for(unsigned i=0;i<32;i++)l[i]=r[i]=0.f;
    x_reverse(&x,&g,l,r,32,g.wr,g.valid);
    CHECK(l[31]<l[0]&&r[31]<r[0],"reverse audio reads a monotonic source in descending order");
    mode(0);for(unsigned b=0;b<20;b++)block(32,.1f,l,r);
    CHECK(x.rev_mix==0.f&&x.heads[0].len==0&&x.heads[1].len==0&&l[0]==.125f,"leaving reverse fades to exact Normal and retires its heads");
    reset();mode(3);x_event(&x,T_DUCK,0);for(unsigned b=0;b<100;b++)block(32,.5f,l,r);
    CHECK(l[0]==.125f&&r[0]==-.25f,"duck amount zero preserves wet samples exactly");
    x_event(&x,T_DUCK,1000);for(unsigned b=0;b<200;b++)block(32,.5f,l,r);
    float ducked=l[31];CHECK(ducked<.03f,"input activity attenuates the ducked return");
    for(unsigned b=0;b<6000;b++)block(32,0.f,l,r);
    CHECK(l[31]>.124f&&l[31]>ducked*4,"ducked return recovers after the input becomes silent");
    float recovery[2];
    for(unsigned j=0;j<2;j++) {
        reset();mode(3);x.duck_mix=1.f;x.envelope=.5f;x.release=j?1000:100;
        for(unsigned b=0;b<1500;b++)block(32,0.f,l,r);
        recovery[j]=l[31];
    }
    CHECK(recovery[0]>recovery[1]*4.f,"shorter Release restores the ducked return measurably faster");
    reset();mode(2);float high[32],low[32];for(unsigned b=0;b<100;b++) {
        x_prepare(&x,32);for(unsigned i=0;i<32;i++){high[i]=(i&1)?.3f:-.3f;low[i]=.3f;}
        x_tape(&x,high,32,0,0);x_tape(&x,low,32,1,0);
    }
    CHECK(fabsf(high[31])<fabsf(low[31])*.5f,"tape damping reduces high-frequency repeats");
    CHECK(fabsf(low[31]-.3f)>.001f&&fabsf(low[31])<.3f,"tape drive adds bounded compensated saturation");
    mode(0);for(unsigned b=0;b<12;b++)block(32,.3f,l,r);
    CHECK(x.tape1==0.f&&l[0]==.125f&&r[0]==-.25f,"Tape exits through its ramp and restores the exact unprocessed reader path");
    mode(2);x_prepare(&x,32);
    high[0]=NAN;high[1]=INFINITY;high[2]=-INFINITY;x_tape(&x,high,32,0,0);
    CHECK(isfinite(high[0])&&isfinite(high[1])&&isfinite(high[2]),"tape state cannot be poisoned by nonfinite reader samples");
    /* Quiet tails must not acquire makeup gain inside native feedback. */
    for(unsigned amount=0;amount<=1000;amount+=100) {
        reset();mode(2);x.drive=(int)amount;
        for(unsigned b=0;b<120;b++) {
            x_prepare(&x,32);for(unsigned i=0;i<32;i++)high[i]=.00001f;
            x_tape(&x,high,32,0,0);
        }
        for(unsigned i=0;i<32;i++)assert(high[i]>=0.f && high[i]<=.000010001f);
    }
    CHECK(1,"all drive amounts have no quiet-signal makeup gain inside feedback");
    /* Post-drive filter gets darker when its nominal frequency is lowered. */
    float band[2];
    for(unsigned j=0;j<2;j++) {
        reset();mode(2);x.cutoff=j?16000:500;
        float energy=0.f;
        for(unsigned b=0;b<200;b++) {
            x_prepare(&x,32);
            for(unsigned i=0;i<32;i++)high[i]=.3f*sinf((float)(b*32+i)*.85f);
            x_tape(&x,high,32,0,0);
            if(b>100)for(unsigned i=0;i<32;i++)energy+=high[i]*high[i];
        }
        band[j]=energy;
    }
    CHECK(band[0]<band[1]*.03f,"post-drive Hi Cut suppresses high-frequency energy");
    x_event(&x,T_HICUT,-100);assert(x.cutoff==500);
    x_event(&x,T_HICUT,999999);assert(x.cutoff==16000);
    CHECK(1,"cutoff parameter clamps at both safe endpoints");
    /* Match the first Tape static drive transfer minus0.5dB at the return.
     * The feedback coloration tested above remains non-boosting. */
    for(unsigned j=0;j<3;j++) {
        float amount=j==0?0.f:j==1?.35f:1.f;
        reset();x.tape0=x.tape1=1.f;x.drive0=x.drive1=amount;
        float a=.2f/(1.f+6.f*amount*.2f);
        for(unsigned i=0;i<32;i++){l[i]=a;r[i]=-a;}
        x_tape_return(&x,l,r,32);
        float original=.2f*(1.f+1.2f*amount)/(1.f+6.f*amount*.2f);
        assert(fabsf(l[31]-original*powf(10.f,-.5f/20.f))<.000001f);
        assert(l[31]==-r[31]);
        CHECK(1,"return transfer matches original makeup minus0.5dB at a tested Drive");
    }
    /* WILD restores the original regenerative transfer with half-dB trim,
     * retains Hi Cut, and never applies the return makeup a second time. */
    for(unsigned j=0;j<3;j++) {
        reset();mode(2);x.wild=1;x.drive=j==0?0:j==1?350:1000;
        for(unsigned b=0;b<400;b++) {
            x_prepare(&x,32);for(unsigned i=0;i<32;i++)l[i]=.00001f;
            x_tape(&x,l,32,0,0);
        }
        float k=6.f*(float)x.drive*.001f;
        float expected=.9440608763f*.00001f*(1.f+.2f*k)/(1.f+k*.00001f);
        CHECK(fabsf(l[31]-expected)<1e-10f,"WILD settled loop matches original drive curve minus0.5dB");
        memcpy(r,l,sizeof l);x_tape_return(&x,l,r,32);
        CHECK(fabsf(l[31]-expected)<1e-10f,"WILD return does not apply duplicate makeup");
    }
    float wb[2];
    for(unsigned j=0;j<2;j++) {
        reset();mode(2);x.wild=1;x.cutoff=j?16000:500;float e=0.f;
        for(unsigned b=0;b<300;b++) {
            x_prepare(&x,32);
            for(unsigned i=0;i<32;i++)l[i]=.3f*sinf((float)(b*32+i)*.85f);
            x_tape(&x,l,32,0,0);
            if(b>100)for(unsigned i=0;i<32;i++)e+=l[i]*l[i];
        }
        wb[j]=e;
    }
    CHECK(wb[0]<wb[1]*.03f,"WILD retains effective post-drive Hi Cut");
    reset();mode(2);x_event(&x,T_WILD,1);
    for(unsigned b=0;b<8;b++)x_prepare(&x,32);
    CHECK(x.wild1==1.f,"WILD reaches on within eight32-sample blocks");
    x_event(&x,T_WILD,-100);for(unsigned b=0;b<8;b++)x_prepare(&x,32);
    CHECK(x.wild==0&&x.wild1==0.f,"WILD malformed negative event turns off and completes its fade");
    /* PING's two serial stages share one complete rational curve. */
    for(unsigned amount=0;amount<=1000;amount+=100) {
        float k=6.f*(float)amount*.001f,G=.9440608763f*(1.f+.2f*k);
        float q=x_sqrt_positive(G),ks=k/(1.f+q);
        for(unsigned j=0;j<5;j++) {
            float a=j==0?-.7f:j==1?-.01f:j==2?0.f:j==3?.01f:.7f;
            float first=q*a/(1.f+ks*fabsf(a));
            float second=q*first/(1.f+ks*fabsf(first));
            float full=G*a/(1.f+k*fabsf(a));
            assert(fabsf(second-full)<2e-7f);
        }
    }
    CHECK(1,"two split PING drive stages compose to the original full curve over all Drive settings");
    for(unsigned j=0;j<3;j++) {
        struct delay_types right;
        reset();mode(2);x.wild=1;x.drive=j==0?0:j==1?350:1000;
        right=x;
        for(unsigned b=0;b<400;b++) {
            x_prepare(&x,32);x_prepare(&right,32);
            for(unsigned i=0;i<32;i++)l[i]=r[i]=.01f;
            x_tape(&x,l,32,0,1);x_tape(&x,l,32,1,1);
            x_tape(&right,r,32,2,0);
        }
        CHECK(fabsf(l[31]-r[31])<1e-7f,"production split WILD filters settle to the same DC curve as an unsplit channel");
    }
    /* Exercise short blocks, history ownership, freeze transitions, every mode,
     * extreme pitch/feedback and malformed edits across many ring wraps. */
    reset();uint32_t rng=1234567;
    for(unsigned b=0;b<50000;b++) {
        rng=rng*1664525u+1013904223u;
        if(!(b%37)){mode((int)(rng%6));g_event(&g,G_FREEZE,(int)(rng&1));g_event(&g,0x4a,(int)(rng%60)-30);g_event(&g,0x39,1500);x_event(&x,T_WILD,(int)(rng%4)-1);x_event(&x,T_SEGMENT,(int)(rng%1000));x_event(&x,T_HICUT,(int)(rng%40000)-10000);x.reverse_frames=(rng&256)?rng%250000u:0;x_event(&x,T_TONE,(int)(rng%1600)-300);x_event(&x,T_RELEASE,(int)(rng%4000)-200);}
        block(1u+(rng%32u),.8f,l,r);
    }
    CHECK(g.feedback<=950,"50000 randomized transition/extreme/short-block/free-or-synced cases stay finite, bounded and within two reverse heads");
    puts("Host new-delay checks execute the production header under ASan/UBSan.");
    printf("%u new-delay host checks passed.\n",checks);return 0;
}
