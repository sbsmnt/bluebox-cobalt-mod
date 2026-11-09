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
