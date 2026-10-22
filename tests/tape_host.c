/* Runs the production Tape core under ASan/UBSan. */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include "../src/tape_saturation.h"
static unsigned checks;
#define CHECK(x,s) do{if(!(x)){fprintf(stderr,"FAIL %s\n",s);exit(1);}puts("PASS " s);checks++;}while(0)
static unsigned seed=7421;
static float noise(void){seed=1664525u*seed+1013904223u;return (float)(seed>>8)*(1.f/8388608.f)-1.f;}
static double energy(float amp,float hz,int drive,int tone) {
 struct master_tape t;mt_init(&t);t.tone=tone;double en=0.;unsigned n=96000;
 for(unsigned i=0;i<n;i+=32){struct mt_block b=mt_prepare(&t,drive*.001f,32);
  for(unsigned j=0;j<32;j++){float x=amp*sinf(6.28318530718f*hz*(float)(i+j)/48000.f);float h=mt_hf_gain(&t,b,x,x);float y=mt_channel(&t,b,0,x,h);if(i>48000)en+=(double)y*y;}}
 return en;
}
int main(void){
 struct master_tape t;mt_init(&t);
 CHECK(t.mode==0&&t.tone==500&&t.output==0&&t.output_gain==1.f,"cold defaults preserve Soft and unity trim");
 float prev=-100.f,last=-1.f;
 for(int i=0;i<=20000;i++){float x=-4.f+i*.0004f,y=mt_curve(x);assert(y>=last-1e-6f);assert(fabsf(y+mt_curve(-x))<1e-6f);last=y;float z=mt_adaa(x,&prev);assert(isfinite(z)&&fabsf(z)<.667f);}
 CHECK(1,"rounded curve and antiderivative are finite, odd and monotonic");
 double maxrel=0;for(int i=-12000;i<=6000;i+=100){double z=mt_exp2((float)i/6020.599913f),ref=pow(10.,i/20000.);double e=fabs(z/ref-1);if(e>maxrel)maxrel=e;}
 CHECK(maxrel<.0002,"output dB approximation stays within 0.002dB");
 double dark=energy(.15f,10000,700,0),bright=energy(.15f,10000,700,1000);
 printf("INFO 10kHz energy dark %.6f bright %.6f delta %.3fdB\n",dark,bright,10*log10(dark/bright));
 CHECK(dark<bright*.7,"Tone darkens high-frequency Tape output");
 double low=energy(.02f,6000,700,500),high=energy(.5f,6000,700,500);
 CHECK(high/low<(.5/.02)*(.5/.02)*.15,"high-level Tape input compresses rather than scaling linearly");
 mt_init(&t);t.mode=1;t.tone=0;
 float peak=0.;for(unsigned i=0;i<2000000;i+=32){
  if(i%8000==0){mt_event(&t,MT_TONE,(int)(noise()*1800));mt_event(&t,MT_OUTPUT,(int)(noise()*25000));}
  struct mt_block b=mt_prepare(&t,(i%32000)* (1.f/32000.f),32);
  for(unsigned j=0;j<32;j++){float l=noise()*4.f,r=noise()*4.f;if(i%48000==0&&j==0)l=NAN;if(i%48000==0&&j==1)r=INFINITY;
   float hf=mt_hf_gain(&t,b,l,r);float a=mt_channel(&t,b,0,l,hf),c=mt_channel(&t,b,1,r,hf);
   assert(isfinite(a)&&isfinite(c));if(fabsf(a)>peak)peak=fabsf(a);if(fabsf(c)>peak)peak=fabsf(c);
  }
 }
 printf("INFO randomized overload peak %.5f\n",peak);CHECK(peak<1.5f,"two million stereo samples and nonfinite overloads remain bounded");
 mt_init(&t);t.mode=1;
 float tail=0;for(unsigned i=0;i<96000;i+=32){struct mt_block b=mt_prepare(&t,.7f,32);for(unsigned j=0;j<32;j++)tail=mt_channel(&t,b,0,.1f,1.f);}
 CHECK(fabsf(tail)<.00001f,"DC blocker rejects sustained DC");
 for(unsigned i=0;i<8;i++)assert(t.channel[1][i]==0.f);
 CHECK(1,"channel filter histories remain independent");
 /* exp.4 settled loop (shared ADAA reciprocal, carried antiderivative,
  * refined HF reciprocal) against the reference per-sample channel path. */
 struct master_tape a,c;mt_init(&a);mt_init(&c);a.mode=c.mode=1;
 double worst=0,sq=0;unsigned count=0;seed=9001;
 for(unsigned i=0;i<480000;i+=32){
  if(i%48000==0){int tone=(int)((i/48000)*111%1001);a.tone=c.tone=tone;}
  float d=(float)((i/24000)%11)*.1f,amp=(i/12000)%4==3?6.f:.05f+.3f*(float)((i/12000)%3);
  struct mt_block ba=mt_prepare(&a,d,32),bc=mt_prepare(&c,d,32);
  float l[32],r[32],rl[32],rr[32];
  for(unsigned j=0;j<32;j++){l[j]=rl[j]=amp*noise();r[j]=rr[j]=amp*.7f*noise();}
  if(i==96000){l[5]=rl[5]=NAN;r[9]=rr[9]=INFINITY;}
  mt_render_settled(&a,ba,l,r,32,1.f,0.f,1,1,1,1.f,0.f);
  for(unsigned j=0;j<32;j++){float h=mt_hf_gain(&c,bc,rl[j],rr[j]);float x=mt_channel(&c,bc,0,rl[j],h),y=mt_channel(&c,bc,1,rr[j],h);
   assert(isfinite(l[j])&&isfinite(r[j]));
   double e1=fabs((double)l[j]-x),e2=fabs((double)r[j]-y);if(e1>worst)worst=e1;if(e2>worst)worst=e2;sq+=e1*e1+e2*e2;count+=2;}
 }
 printf("INFO settled loop vs reference: peak %.3g RMS %.3g\n",worst,sqrt(sq/count));
 CHECK(worst<2e-5 && sqrt(sq/count)<2e-6,"settled exp.4 Tape loop matches the reference channel path, including overload and nonfinite input");
 printf("%u production Tape host checks passed; host execution is not device CPU timing.\n",checks);return 0;
}
