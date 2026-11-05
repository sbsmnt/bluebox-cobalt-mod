/* Track header cue state, drawn over the native track tiles before the flip:
 * - a headphone in the top-right corner of every track whose cue is on, in
 *   the theme's active button fill with a background-colour outline;
 * - tracks without an input are greyed out with a background-colour stipple,
 *   so unconfigured tracks stand out.
 * Reads the masks that cue_m7.c publishes. Track tiles are the native widgets
 * (class 0x0814af64) that every page with the track strip embeds at
 * page+0x14d0, one per 0xd98 bytes: tracks 0..11, then the master at 12.
 * Their own rectangles follow the track count, Single/Double Row and the
 * page, so nothing here assumes a layout. */
#include "shared.h"
#define APP_POINTER (*(volatile uint32_t *)0x3000d9fcu)
#define TILE_VT 0x0814af64u
#define TILE0 0x14d0u
#define TILE_STRIDE 0xd98u
#define CUE_MAGIC 0x43554534u
struct cue_state {uint32_t magic;int32_t src;uint32_t hold,mixer,shown,inputs,seq;};
#define CUE ((volatile struct cue_state *)0x38800660u)
struct cue_view {uint32_t magic,shown,inputs;};
/* M4-only: after the Gate accel (0x1c0..0x1cf), before delay types at 0x200. */
#define VIEW ((volatile struct cue_view *)0x388001e0u)
#define VIEW_MAGIC 0x43554957u
/* Native palette, rewritten by the patcher's theme: 2 background, 5 active fill. */
#define PALETTE ((const volatile uint32_t *)0x0814d230u)
void pc_present(void *display,int layer,int a2,int a3);
void gate_ui_tick(uint8_t *pages,uint8_t *flags);

static int ram(uint32_t a,uint32_t n) {
    return (a>=0x24000000u && n<=0x80000u && a<=0x24080000u-n) ||
           (a>=0x30000000u && n<=0x48000u && a<=0x30048000u-n) ||
           (a>=0xc0000000u && n<=0x4000000u && a<=0xc4000000u-n);
}
static void pixel(uint8_t *p,unsigned fmt,uint32_t c) {
    unsigned r=(c>>16)&255u,g=(c>>8)&255u,b=c&255u;
    switch(fmt) {
    case 1: *(uint32_t *)p=0xff000000u|c; break;
    case 2: p[0]=(uint8_t)b; p[1]=(uint8_t)g; p[2]=(uint8_t)r; break;
    case 3: *(uint16_t *)p=(uint16_t)(0x8000u|((r>>3)<<10)|((g>>3)<<5)|(b>>3)); break;
    case 4: *(uint16_t *)p=(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)); break;
    }
}
static int cue_masks(unsigned *cued,unsigned *inputs) {
    backup_enable();
    if(CUE->magic!=CUE_MAGIC)return 0;
    *cued=CUE->shown&0xfffu;*inputs=CUE->inputs&0xfffu;
    return 1;
}
/* The current page's track tiles, or 0 when it has no track strip. */
static uint32_t cue_page(void) {
    uint32_t app=APP_POINTER;
    if(!ram(app,0x2b8u))return 0;
    uint32_t page=*(volatile uint32_t *)(app+0x2b4u);
    if(!ram(page,TILE0+13u*TILE_STRIDE) || *(volatile uint32_t *)(page+TILE0)!=TILE_VT)return 0;
    return page;
}
/* 9x7 headphones: band and two ear cups. */
static const uint16_t cue_icon[7]={0x07c,0x082,0x101,0x101,0x183,0x183,0x183};
static int icon_bit(int r,int c) {return r>=0 && r<7 && c>=0 && c<9 && (cue_icon[r]>>(8-c)&1u);}
static void cue_draw(const uint8_t *d) {
    if(!d)return;
    uint8_t *px=*(uint8_t *const *)d;
    unsigned w=*(const uint16_t *)(d+4),h=*(const uint16_t *)(d+6),fmt=d[8],bpp=*(const uint16_t *)(d+10);
    if(w!=320u || h!=240u || !ram((uint32_t)px,w*h*bpp))return;
    if(!((fmt==1u && bpp==4u)||(fmt==2u && bpp==3u)||((fmt==3u||fmt==4u) && bpp==2u)))return;
    unsigned cued,inputs;
    if(!cue_masks(&cued,&inputs))return;
    uint32_t page=cue_page();
    if(!page)return;
    uint32_t bg=PALETTE[2]&0xffffffu,fill=PALETTE[5]&0xffffffu;
    for(unsigned ch=0;ch<12u;ch++) {
        unsigned on=cued>>ch&1u,dim=!(inputs>>ch&1u);
        if(!on && !dim)continue;
        uint8_t *t=(uint8_t *)(page+TILE0+ch*TILE_STRIDE);
        if(*(uint32_t *)t!=TILE_VT || *(uint16_t *)(t+0x32)!=ch || t[0x30])continue;
        int x=*(int *)(t+4),bottom=*(int *)(t+8),tw=*(int *)(t+12),th=*(int *)(t+16);
        if(tw<14 || th<12 || x<0 || x>320-tw || bottom<0 || bottom>240-th)continue;
        /* Native rectangles have a bottom-left origin; the framebuffer is top-down. */
        int top=240-bottom-th;
        /* A fixed 50% stipple, not a blend: drawing it again on a buffer the
         * UI did not repaint leaves it unchanged. */
        if(dim)
            for(int r=0;r<th;r++) {
                uint8_t *p=px+((unsigned)(top+r)*w+(unsigned)(x+((x+top+r)&1)))*bpp;
                for(int c=(x+top+r)&1;c<tw;c+=2,p+=2u*bpp)pixel(p,fmt,bg);
            }
        if(on) {
            /* Icon plus a 1px outline: 11x9 cell, 1px in from the tile's corner. */
            int x0=x+tw-11,y0=top+2;
            for(int r=-1;r<8;r++)for(int c=-1;c<10;c++) {
                int body=icon_bit(r,c),edge=0;
                if(!body)for(int dr=-1;dr<=1 && !edge;dr++)for(int dc=-1;dc<=1;dc++)if(icon_bit(r+dr,c+dc)){edge=1;break;}
                if(body || edge)pixel(px+((unsigned)(y0+r)*w+(unsigned)(x0+c))*bpp,fmt,body ? fill : bg);
            }
        }
    }
}
/* Present chain: cue state, then Scenes feedback, then Cobalt's meters and flip. */
void cue_present(void *display,int layer,int a2,int a3) {
    cue_draw(((const uint8_t *(*)(void *,int))0x0810139bu)(display,layer));
    pc_present(display,layer,a2,a3);
}
/* Redraw both pages once when a mask changes, so overlays appear and vanish. */
void cue_ui_tick(uint8_t *pages,uint8_t *flags) {
    unsigned cued=0,inputs=0xfffu;
    (void)cue_masks(&cued,&inputs);
    PWR_CR1|=1u<<8;
    if(VIEW->magic!=VIEW_MAGIC){VIEW->magic=VIEW_MAGIC;VIEW->shown=cued;VIEW->inputs=inputs;}
    if(VIEW->shown!=cued || VIEW->inputs!=inputs) {
        VIEW->shown=cued;VIEW->inputs=inputs;pages[0x38]=pages[0x39]=1;
    }
    gate_ui_tick(pages,flags);
}
