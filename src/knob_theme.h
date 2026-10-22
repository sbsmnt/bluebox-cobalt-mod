/* Scoped knob geometry recoloring. Extend native indexed-surface conversion and
 * native RGB pixel orientation; never rewrite shared palette slot15. */
/* Dedicated DMA2D conversion CLUT: preserve all34 stock entries and append
 * knob-only entry34. This lives in flash and does not overwrite native SRAM. */
const volatile uint32_t theme_knob_palette[35]={
    0x00000000u,0xff56565au,0xff141414u,0xfffcfcfcu,0xff9455fau,0xff09d7f5u,
    0xffff0000u,0xff09d7f5u,0xff141414u,0xff666666u,0xff09d7f5u,0xff22bb22u,
    0xffbb2222u,0xff141414u,0xff000000u,0xffffffffu,0xff444444u,0xff707070u,
    0xff22bb22u,0xffbb2222u,0xffffff00u,0xffbb2222u,0xffaaaaaau,0xff00ffccu,
    0xffba00ffu,0xff2b2b2bu,0xff046b7au,0xff09d7f5u,0xff777100u,0xff09d7f5u,
    0xffffffffu,0xff09d7f5u,0xff9455fau,0xffffffffu,0xff09d7f5u,
};
__asm__(".global theme_knob_rgb\n.set theme_knob_rgb, theme_knob_palette + 136");
unsigned theme_knob_clut(void *handle,const uint32_t *palette,unsigned start,unsigned count,unsigned layer) {
    if(palette==(const uint32_t *)0x30000000u && start==0 && count==33 && layer<2) {
        palette=(const uint32_t *)theme_knob_palette;count=34;
    }
    return ((unsigned (*)(void *,const uint32_t *,unsigned,unsigned,unsigned))0x0811085du)(handle,palette,start,count,layer);
}
void theme_knob_pixel(uint8_t *surface,unsigned x,unsigned y,unsigned index) {
    if(index==15 && surface[8]==1) {
        ((void (*)(void *,unsigned,unsigned,uint32_t))0x081371d1u)(surface,x,y,theme_knob_palette[34]);
    } else ((void (*)(void *,unsigned,unsigned,unsigned))0x0813723du)(surface,x,y,index==15?34:index);
}
/* Native line primitive uses signed16.16 DDA with truncation toward zero.
 * Keep its endpoints and inclusive raster mask; redirect only the knob call. */
void theme_knob_line(const int *a,const int *b,unsigned index,uint8_t *surface) {
    if(index!=15 || surface[8]!=1) {
        ((void (*)(const int *,const int *,unsigned,void *))0x081364dbu)(a,b,index==15?34:index,surface);return;
    }
    int dx=b[0]-a[0],dy=b[1]-a[1];
    int nx=dx<0?-dx:dx,ny=dy<0?-dy:dy,n=nx>ny?nx:ny;
    if(!n){theme_knob_pixel(surface,(unsigned)a[0],(unsigned)a[1],index);return;}
    int x=a[0]*65536,y=a[1]*65536,sx=dx*65536/n,sy=dy*65536/n;
    for(int i=0;i<=n;i++) {
        theme_knob_pixel(surface,(unsigned)(x/65536),(unsigned)(y/65536),index);
        x+=sx;y+=sy;
    }
}
