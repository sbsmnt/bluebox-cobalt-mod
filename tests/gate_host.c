#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "../src/noise_gate.h"
static unsigned checks;
#define CHECK(c,m) do{assert(c);checks++;puts("PASS " m);}while(0)
static struct noise_gate g;
static float l[40],r[40];
static void run(float a,float b) {
 for(unsigned i=0;i<32;i++){l[i]=a;r[i]=b;}
 for(unsigned i=32;i<40;i++)l[i]=r[i]=1234.f;
 ng_process(&g,l,r,32);
 for(unsigned i=0;i<32;i++)assert(isfinite(l[i])&&isfinite(r[i]));
 for(unsigned i=32;i<40;i++)assert(l[i]==1234.f&&r[i]==1234.f);
}
int main(void) {
 ng_reset(&g,0);CHECK(g.threshold==NG_OFF&&g.gain==1.f,"default is exact unity bypass");
 run(.123f,-.456f);CHECK(l[0]==.123f&&r[31]==-.456f,"Off leaves stereo samples exact");
 ng_event(&g,-40000);CHECK(fabsf(g.level-.01f)<1e-8f,"millidecibel threshold converts correctly");
 for(unsigned b=0;b<300;b++)run(.0001f,-.0002f);
 CHECK(g.gain==0.f&&l[31]==0.f&&r[31]==0.f,"idle noise fades to true zero on both channels");
 run(.1f,-.01f);CHECK(g.gain==1.f&&l[31]==.1f&&fabsf(l[0]-.1f/24.f)<1e-8f,"first loud block opens smoothly within24 samples");
 CHECK(fabsf(l[0]+r[0]*10.f)<1e-8f,"stereo pair uses identical VCA gain");
 for(unsigned b=0;b<200;b++)run(.007f,-.007f);
 CHECK(g.gain==1.f,"six-decibel hysteresis preserves open state below opening threshold");
 for(unsigned b=0;b<20;b++)run(0.f,0.f);
 CHECK(g.gain==1.f,"detector and hold avoid an abrupt cut at signal end");
 for(unsigned b=0;b<300;b++)run(0.f,0.f);
 CHECK(g.gain==0.f,"short ending eventually closes completely");
 ng_event(&g,NG_OFF);run(.1f,-.1f);
 CHECK(g.gain==1.f&&l[31]==.1f,"Off smoothly reopens and reaches exact unity");
 run(.123f,-.456f);CHECK(l[0]==.123f&&r[31]==-.456f,"settled bypass is bit exact again");
 float releases[3];
 for(unsigned j=0;j<3;j++) {
  ng_reset(&g,0);ng_event(&g,-40000);
  for(unsigned b=0;b<300;b++)run(.1f,-.03f);
  for(unsigned b=0;b<12000;b++) {
   float t=(float)b*32.f/48000.f;
   float tau=j==0?.01f:j==1?.25f:1.f;
   float a=.1f*expf(-t/tau);run(a,-.3f*a);
   if(!g.open){releases[j]=g.release_ms;break;}
  }
  assert(releases[j]>=60.f&&releases[j]<=2500.f);
 }
 printf("INFO automatic release ms: short=%g medium=%g long=%g\n",releases[0],releases[1],releases[2]);
 CHECK(releases[1]>releases[0]*3.f&&releases[2]>releases[1]*2.f,"automatic release lengthens for slower natural decay");
 ng_reset(&g,0);ng_event(&g,1000);CHECK(g.threshold==-12000,"upper malformed threshold clamps");
 ng_event(&g,-10000000);CHECK(g.threshold==NG_OFF,"lower malformed threshold becomes Off");
 ng_event(&g,-40000);for(unsigned b=0;b<300;b++)run(.00001f,0.f);
 for(unsigned i=0;i<32;i++)l[i]=.1f;
 ng_process(&g,l,l,32);CHECK(fabsf(l[0]-.1f/24.f)<1e-8f,"aliased stereo pointer is multiplied only once");
 float saved=g.gain;ng_process(&g,l,r,0);CHECK(g.gain==saved,"empty blocks preserve state and samples");
 uint32_t rng=7;
 for(unsigned b=0;b<100000;b++) {
  rng=rng*1664525u+1013904223u;
  if(!(b%29))ng_event(&g,(int)(rng%150000)-120000);
  unsigned n=1+(rng%32);
  for(unsigned i=0;i<32;i++){l[i]=(rng&1)?-.01f:.1f;r[i]=l[i]*.3f;}
  ng_process(&g,l,r,n);
  assert(isfinite(g.gain)&&g.gain>=0.f&&g.gain<=1.f&&isfinite(g.env)&&isfinite(g.fall));
 }
 CHECK(1,"100000 randomized threshold/block-size/level transitions remain finite and bounded");
 printf("%u gate host checks passed under ASan/UBSan.\n",checks);
}
