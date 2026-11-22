#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/dj_eq.h"
#define TAU 6.283185307179586
static unsigned checks;
#define CHECK(c,m) do{assert(c);checks++;puts("PASS " m);}while(0)
static struct dj_track t;
static struct dj_settings s;
static float l[40],r[40];
static double phase;
/* One 32-frame block of a sine, processed; while measuring, adds squared left
 * samples to *acc until *count reaches measure. Guard samples catch overruns. */
static void block(double hz,int measure,double *acc,unsigned *count) {
 for(unsigned i=0;i<32;i++){l[i]=r[i]=(float)(.5*sin(phase));phase+=TAU*hz/48000.;}
 for(unsigned i=32;i<40;i++)l[i]=r[i]=1234.f;
 dj_process(&t,&s,l,r,32);
 for(unsigned i=0;i<32;i++)assert(isfinite(l[i])&&isfinite(r[i])&&l[i]==r[i]);
 for(unsigned i=32;i<40;i++)assert(l[i]==1234.f&&r[i]==1234.f);
 if(measure)for(unsigned i=0;i<32 && *count<(unsigned)measure;i++){*acc+=(double)l[i]*l[i];(*count)++;}
}
/* RMS over whole periods only (every test frequency divides 48 kHz). */
static double level_db(double hz) {
 double acc=0;unsigned n=0,period=(unsigned)(48000./hz+.5),want=period*((4096+period-1)/period);
 for(unsigned b=0;b<120;b++)block(hz,0,0,0);
 while(n<want)block(hz,(int)want,&acc,&n);
 return 20*log10(sqrt(acc/n)/(.5/sqrt(2.)));
}
static void set(int hi,int himid,int lomid,int low,int filt,int res) {
 dj_param(&t,0,hi);dj_param(&t,1,himid);dj_param(&t,2,lomid);dj_param(&t,3,low);dj_param(&t,4,filt);dj_param(&t,5,res);
}
int main(void) {
 dj_reset(&t);dj_settings_default(&s);
 block(1000,0,0,0);
 float in[32];for(unsigned i=0;i<32;i++){in[i]=(float)i*.01f-.1f;l[i]=r[i]=in[i];}
 dj_process(&t,&s,l,r,32);
 CHECK(!memcmp(l,in,sizeof in)&&!memcmp(r,in,sizeof in),"neutral is an exact bypass");
 set(1,0,0,0,0,0);
 double f[4]={60,500,3000,12000};int flat=1;
 for(unsigned i=0;i<4;i++)flat&=fabs(level_db(f[i]))<.08;
 CHECK(flat,"ISO bands at ~0 dB sum to an allpass: flat within 0.08 dB (High +0.06 dB)");
 set(0,0,0,-DJ_BAND_MAX,0,0);
 CHECK(level_db(50)<-40&&fabs(level_db(3000))<.5,"ISO Low kill removes 50 Hz, 3 kHz untouched");
 set(0,0,0,100,0,0);
 CHECK(fabs(level_db(60)-6)<.3,"Low +6 dB boosts 60 Hz by 6 dB");
 s.curve=DJ_CURVE_EQ;set(0,0,0,-DJ_BAND_MAX,0,0);
 double eq=level_db(40);
 CHECK(eq>-30&&eq<-15,"EQ curve: Low at minimum attenuates without a kill");
 s.curve=DJ_CURVE_ISO;set(0,0,0,0,-1000,0);
 CHECK(level_db(8000)<-30,"filter full left is a low-pass");
 set(0,0,0,0,1000,0);
 CHECK(level_db(60)<-30,"filter full right is a high-pass");
 set(0,0,0,0,0,0);
 for(unsigned b=0;b<400;b++)block(200,0,0,0);
 for(unsigned i=0;i<32;i++){in[i]=(float)i*.01f-.1f;l[i]=r[i]=in[i];}
 dj_process(&t,&s,l,r,32);
 CHECK(!memcmp(l,in,sizeof in),"back at neutral after the hold and fade: exact bypass again");
 /* SD2: settled stereo blocks run interleaved kernels, mono and ramping
  * blocks the per-channel loops. All share the filter states, so a stereo
  * track's left channel must match a mono track fed the same signal, through
  * ramps, fades and every curve/band/filter mode. */
 {
  static struct dj_track ts,tm;double worst=0;
  int modes[6][3]={{DJ_CURVE_ISO,4,12},{DJ_CURVE_ISO,3,12},{DJ_CURVE_ISO,4,24},{DJ_CURVE_ISO,3,24},{DJ_CURVE_EQ,4,12},{DJ_CURVE_EQ,3,24}};
  for(unsigned m=0;m<6;m++) {
   struct dj_settings ss;dj_settings_default(&ss);ss.curve=modes[m][0];ss.bands=modes[m][1];ss.slope=modes[m][2];
   dj_reset(&ts);dj_reset(&tm);double ph=0;
   for(unsigned b=0;b<1500;b++) {
    if(b==10||b==400||b==900){int v=b==10?-40:b==400?-100:0;
     unsigned which=b==900?4:3;int fv=b==900?-500:v;
     dj_param(&ts,which,fv);dj_param(&tm,which,fv);dj_param(&ts,0,b==400?60:0);dj_param(&tm,0,b==400?60:0);}
    if(b==1200){dj_param(&ts,4,400);dj_param(&tm,4,400);}
    float a[32],c[32],d[32];
    for(unsigned i=0;i<32;i++){a[i]=c[i]=d[i]=(float)(.4*sin(ph)+.2*sin(ph*7.3));ph+=TAU*90./48000.;}
    dj_process(&ts,&ss,a,c,32);dj_process(&tm,&ss,d,0,32);
    for(unsigned i=0;i<32;i++){double e=fabs((double)a[i]-d[i]);if(e>worst)worst=e;assert(a[i]==c[i]);}
   }
  }
  printf("INFO stereo kernels vs per-channel path: worst difference %.3g\n",worst);
  CHECK(worst<1e-5,"stereo (interleaved) and mono (per-channel) paths agree within 1e-5 in all modes, through ramps, kills, fades and filter side changes");
 }
 /* Mono buses and zero-length blocks. */
 set(0,0,-25,0,0,0);for(unsigned i=0;i<32;i++)l[i]=.1f;
 dj_process(&t,&s,l,0,32);dj_process(&t,&s,l,0,0);
 CHECK(isfinite(l[0]),"mono bus (no right channel) and empty blocks are safe");
 /* Randomised parameters, settings, block sizes and levels stay finite. */
 uint32_t rng=7;
 for(unsigned b=0;b<100000;b++) {
  rng=rng*1664525u+1013904223u;
  if(!(b%23))dj_param(&t,rng%6,(int)((rng>>8)%2401)-1200);
  if(!(b%997)){s.curve=(rng>>3)&1;s.x_lo=40+(int)(rng%560);s.x_mid=300+(int)(rng%2700);s.x_hi=1500+(int)(rng%10500);}
  unsigned n=1+(rng%32);
  for(unsigned i=0;i<32;i++){l[i]=(rng&2)?-.9f:.9f;r[i]=l[i]*.5f;}
  dj_process(&t,&s,l,r,n);
  for(unsigned i=0;i<n;i++)assert(isfinite(l[i])&&isfinite(r[i])&&fabsf(l[i])<1e3f);
 }
 CHECK(1,"100000 randomized parameter/setting/block-size transitions remain finite and bounded");
 printf("%u DJ EQ host checks passed under ASan/UBSan.\n",checks);
}
