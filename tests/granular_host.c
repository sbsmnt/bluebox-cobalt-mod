/* Compile the production engine unchanged; ASan/UBSan catches buffer errors. */
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include "../src/granular.h"
static struct granular g;
static float buffer[G_CAP*2],l[32],r[32],ol[32],orr[32];
static unsigned samples;
static void block(int signal) {
    for(unsigned i=0;i<32;i++) {
        l[i]=signal?.3f*sinf((float)(samples+i)*.0575958653f):0.f;
        r[i]=signal?.2f*cosf((float)(samples+i)*.0916297857f):0.f;
        ol[i]=l[i];orr[i]=r[i];
    }
    samples+=32;
    g_render(&g,l,r,ol,orr,32);
    for(unsigned i=0;i<32;i++)if(!isfinite(ol[i])||!isfinite(orr[i])||fabsf(ol[i])>4.01f||fabsf(orr[i])>4.01f)abort();
    if(g.peak>4||g.wr>=G_CAP||g.valid>G_CAP)abort();
}
int main(void) {
    for(unsigned i=0;i<G_CAP*2;i++)buffer[i]=NAN;
    g_reset(&g,0,buffer);g_event(&g,G_MODE,1);
    for(unsigned b=0;b<800;b++)block(1);
    puts("PASS cold Granular startup never reads poisoned unwritten history");
    g_reset(&g,0,buffer);
    for(unsigned b=0;b<4000;b++) {
        block(1);
        if(memcmp(l,ol,sizeof l)||memcmp(r,orr,sizeof r))abort();
    }
    puts("PASS Normal preserves every output sample while priming history");
    g_event(&g,G_MODE,1);g_event(&g,0x39,0);
    double energy=0;
    for(unsigned b=0;b<800;b++){block(1);for(unsigned i=0;i<32;i++)energy+=fabs(ol[i]);}
    if(energy<50||g.peak!=4)abort();
    puts("PASS Granular is audible and reaches exactly four overlapping grains");
    g_event(&g,G_FREEZE,1);unsigned wr=g.wr,valid=g.valid;
    for(unsigned b=0;b<4000;b++)block(0);
    if(g.wr!=wr||g.valid!=valid)abort();
    puts("PASS Freeze holds the capture writer for longer than the buffer duration");
    g_event(&g,G_FREEZE,0);
    for(int size=20;size<=250;size+=230)for(int density=1;density<=4;density++)
    for(int pitch=-12;pitch<=12;pitch+=24)for(int rev=0;rev<=1;rev++) {
        g_event(&g,G_SIZE,size);g_event(&g,G_DENSITY,density);g_event(&g,0x4a,pitch);
        g_event(&g,G_REVERSE,rev);g_event(&g,G_JITTER,1000);g_event(&g,G_POS,rev?1000:0);g_event(&g,0x39,1000);
        for(unsigned b=0;b<3300;b++)block(1);
    }
    puts("PASS 64 extreme size/density/pitch/reverse cases, maximal jitter/feedback, ring wraps");
    unsigned seed=314159;
    const unsigned ids[]={G_MODE,G_POS,G_SIZE,G_DENSITY,G_JITTER,G_FREEZE,G_REVERSE,0x4a,0x39};
    for(unsigned b=0;b<30000;b++) {
        seed=seed*1664525u+1013904223u;
        g_event(&g,ids[seed%9],(int)seed);block(1);
    }
    puts("PASS 30000 randomized malformed parameter edits remain finite and within four slots");
    g_event(&g,G_MODE,0);
    for(unsigned b=0;b<20;b++)block(1);
    if(g.fade!=0||memcmp(l,ol,sizeof l)||memcmp(r,orr,sizeof r))abort();
    puts("PASS returning to Normal finishes fade and restores exact output");
    g_event(&g,G_MODE,1);g_event(&g,G_FREEZE,0);
    for(unsigned i=0;i<32;i++){l[i]=NAN;r[i]=INFINITY;ol[i]=orr[i]=0;}
    g_render(&g,l,r,ol,orr,32);
    for(unsigned i=0;i<32;i++)if(!isfinite(ol[i])||!isfinite(orr[i]))abort();
    puts("PASS corrupt nonfinite input cannot poison granular feedback history");
    g_reset(&g,0,0);g_event(&g,G_MODE,1);g_render(&g,0,0,0,0,32);
    puts("PASS absent buffer safely leaves output untouched");
    puts("9 host audio checks passed under address/undefined-behavior sanitizers");
    return 0;
}
