#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "../src/track_saturation.h"
static unsigned checks;
#define CHECK(c,m) do{assert(c);checks++;puts("PASS " m);}while(0)
static struct track_sat t;
static float l[40],r[40];
/* Independent double-precision reference of the master Soft curve. */
static double curve(double x){return x>3?1:x<-3?-1:x*(27+x*x)/(27+9*x*x);}
static double reference(double x,int drive) {
 double d=drive/1000.,g=pow(2,6*d),mix=fmin(d*20,1),k=.2/curve(g*.2);
 return x+mix*(curve(x*g)*k-x);
}
static void fill(float a,float b){for(unsigned i=0;i<40;i++){l[i]=i<32?a:1234.f;r[i]=i<32?b:1234.f;}}
static void run(float a,float b,unsigned n) {
 fill(a,b);ts_process(&t,l,r,n);
 for(unsigned i=0;i<32;i++)assert(isfinite(l[i])&&isfinite(r[i]));
 for(unsigned i=32;i<40;i++)assert(l[i]==1234.f&&r[i]==1234.f);
}
int main(void) {
 ts_reset(&t,0);CHECK(t.drive==0&&!ts_active(&t),"default is Off");
 run(.123f,-.456f,32);CHECK(l[0]==.123f&&r[31]==-.456f,"Off leaves stereo samples exact");
 fill(NAN,INFINITY);ts_process(&t,l,r,32);CHECK(isnan(l[0])&&isinf(r[0]),"Off bypass does not even sanitize samples");
 static const int drives[]={1,10,50,300,700,1000};
 for(unsigned j=0;j<6;j++) {
  ts_reset(&t,0);ts_event(&t,drives[j]);
  for(unsigned b=0;b<3;b++)run(.8f,-.3f,32);
  assert(fabs(l[31]-reference(.8,drives[j]))<6e-5&&fabs(r[31]-reference(-.3,drives[j]))<6e-5);
 }
 CHECK(1,"steady stereo audio matches the Soft reference curve at six drives");
 ts_reset(&t,0);ts_event(&t,1000);for(unsigned b=0;b<3;b++)run(.2f,-.2f,32);
 CHECK(fabsf(l[0]-.2f)<1e-6f&&fabsf(r[31]+.2f)<1e-6f,"level reference +/-0.2 preserved at full drive");
 CHECK(fabsf(l[0]+r[0])<1e-7f,"curve is symmetric");
 ts_reset(&t,0);run(.7f,-.7f,32);ts_event(&t,1000);run(.7f,-.7f,32);
 CHECK(l[0]<.7f&&l[0]>.2f&&fabsf(l[31]-.2f)<1e-5f,"drive edit ramps across one block");
 ts_event(&t,0);run(.7f,-.7f,32);CHECK(fabsf(l[31]-.7f)<1e-5f,"drive down ramps back to dry");
 run(.7f,-.7f,32);CHECK(!ts_active(&t)&&l[0]==.7f&&r[31]==-.7f,"settled Off is bit-exact again");
 ts_event(&t,5000);CHECK(t.drive==1000,"upper malformed drive clamps");
 ts_event(&t,-5);CHECK(t.drive==0,"lower malformed drive clamps to Off");
 ts_reset(&t,0);ts_event(&t,600);run(.5f,0,32);
 float saved=t.g;fill(.5f,.5f);ts_process(&t,l,r,0);CHECK(t.g==saved&&l[0]==.5f,"empty block keeps state and samples");
 fill(.5f,.5f);ts_process(&t,l,l,32);CHECK(fabsf(l[3]-(float)reference(.5,600))<6e-5f,"aliased stereo pointer is processed once");
 fill(.5f,.9f);ts_process(&t,l,0,32);CHECK(r[0]==.9f&&fabsf(l[0]-(float)reference(.5,600))<6e-5f,"mono leaves right storage untouched");
 fill(NAN,40.f);ts_process(&t,l,r,32);CHECK(l[0]==0.f&&r[0]==0.f,"nonfinite and out-of-range samples become silence while active");
 uint32_t rng=7;
 for(unsigned b=0;b<100000;b++) {
  rng=rng*1664525u+1013904223u;
  if(!(b%17))ts_event(&t,(int)(rng%1400)-200);
  unsigned n=1+(rng%32);
  for(unsigned i=0;i<32;i++){l[i]=(float)((int)((rng>>(i%16))%2000u)-1000)*.004f;r[i]=-l[i]*.7f;}
  ts_process(&t,l,r,n);
  for(unsigned i=0;i<n;i++)assert(isfinite(l[i])&&fabsf(l[i])<=4.1f&&isfinite(r[i]));
  assert(isfinite(t.g)&&t.mix>=0.f&&t.mix<=1.f);
 }
 CHECK(1,"100000 randomized drive/block-size/level transitions stay finite and bounded");
 printf("%u track saturation host checks passed under ASan/UBSan.\n",checks);
}
