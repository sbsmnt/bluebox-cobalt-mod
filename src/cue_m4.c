/* Track header cue state, drawn over the native track tiles before the flip:
 * - a headphone in the top-right corner of every track whose cue is on, in
 *   the theme's active button fill with a background-colour outline;
 * - tracks without an input are greyed out with a background-colour stipple,
 *   so unconfigured tracks stand out (Tools "Dim No Input", S21: can be Off);
 * - S21: on the MIXER sends page (S23: and the TRACK page OUT2/OUT3 knobs),
 *   a track knob showing an Out 2 send that a
 *   Clk Out row has taken over is greyed: S22, its knob colour (the knob
 *   theme entry, which S21 missed), active fill and text all become the idle
 *   button colour, as a muted track's meter.
 * Reads the masks that cue_m7.c publishes. Track tiles are the native widgets
 * (class 0x0814af64) that every page with the track strip embeds at
 * page+0x14d0, one per 0xd98 bytes: tracks 0..11, then the master at 12.
 * Their own rectangles follow the track count, Single/Double Row and the
 * page, so nothing here assumes a layout. */
#include "shared.h"
#define APP_POINTER (*(volatile uint32_t *)0x3000d9fcu)
#define TILE_VT 0x0814af64u
/* Each track's strip block starts at page+0xbe0 and holds its tile at +0x8f0.
 * Pages hide whole strips (e.g. EQ full screen) with the block's own flag. */
#define STRIP0 0x0be0u
#define TILE0 0x14d0u
#define TILE_STRIDE 0xd98u
#define CUE_MAGIC 0x43554534u
struct cue_state {uint32_t magic;int32_t src;uint32_t hold,mixer,shown,inputs,seq;};
#define CUE ((volatile struct cue_state *)0x38800660u)
/* extra: what else the overlay shows (cue_extra), so a change redraws. */
struct cue_view {uint32_t magic,shown,inputs,extra;};
/* M4-only: after the synth resync state (0xc20..0xc2f); 0x1e0 is the insert UI. */
#define VIEW ((volatile struct cue_view *)0x38800c30u)
_Static_assert(0xc30+sizeof(struct cue_view)<=0xc40,"cue view in its slot after the synth resync state");
#define VIEW_MAGIC 0x43554957u
/* Native palette, rewritten by the patcher's theme: 2 background, 5 active fill. */
#define PALETTE ((const volatile uint32_t *)0x0814d230u)
/* Native palette: 1 idle button fill (muted track meters), 3 text. Knobs
 * draw in the knob theme colour, entry 34 of the CLUT in cm4 (knob_theme.h). */
extern const volatile uint32_t theme_knob_palette[35];
#define APP_MODEL ((uint8_t *)0x300173d0u)
#define fw_get ((int (*)(void *,unsigned))0x08120235u)
/* Two of the stock settings sets (0x08121cf4: cells 12-18 from +0xe78, cells
 * 20-22 from +0xe48): OUT2 Mode is in cell 12's (+0xe78), the PROJ B rows
 * with Clk Out L/R in cell 22's, the kind-8 project set (+0xe68). S21/S22
 * read the clock rows from +0xe78, where they are not, so the cue never
 * showed. */
#define OUTPUT_SET (APP_MODEL+0xe78u)
#define PROJECT_SET (APP_MODEL+0xe68u)
#define SETTINGS_SET (APP_MODEL+0x737cu)
#define DIM_SETTING 0x290u  /* Tools "Dim No Input" (pc_m4.c): 0 On, 1 Off */
#define OUT2_SEND 0x15eu
#define OUT3_SEND 0x15fu
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
/* Bit 0: Clk Out L on, 1: Clk Out R on, 2: OUT2 Mode Mono, 3: dimming Off. */
static unsigned cue_extra(void) {
    return (fw_get(PROJECT_SET,0x110u)!=0) | (unsigned)(fw_get(PROJECT_SET,0x111u)!=0)<<1 |
           (unsigned)(fw_get(OUTPUT_SET,0x18cu)==0)<<2 | (unsigned)(fw_get(SETTINGS_SET,DIM_SETTING)!=0)<<3;
}
/* Whether a MIXER tile showing send id carries a clock: Mono has OUT2 (Out 2
 * left, Clk Out L) and OUT3 (right, Clk Out R); Stereo one OUT2 for both. */
static int cue_clocked(unsigned id,unsigned extra) {
    if(id==OUT2_SEND)return extra&4u ? extra&1u : extra&3u;
    return id==OUT3_SEND && (extra&4u) && (extra&2u);
}
/* Grey a widget's rectangle: the knob colour, active fill and text become
 * the idle button colour. Exact colour swaps, so a second pass over an
 * unrepainted buffer changes nothing. */
static void cue_grey(uint8_t *px,unsigned fmt,unsigned bpp,const uint8_t *t) {
    int x=*(const int *)(t+4),bottom=*(const int *)(t+8),tw=*(const int *)(t+12),th=*(const int *)(t+16);
    if(tw<8 || th<8 || x<0 || x>320-tw || bottom<0 || bottom>240-th)return;
    int top=240-bottom-th;
    /* S25: colours encoded once per widget, compared as framebuffer words
     * (S23 encoded three colours per pixel; MIXER send switches lagged). */
    uint8_t k[4],f[4],t3[4],q[4];
    pixel(k,fmt,theme_knob_palette[34]&0xffffffu);pixel(f,fmt,PALETTE[5]&0xffffffu);
    pixel(t3,fmt,PALETTE[3]&0xffffffu);pixel(q,fmt,PALETTE[1]&0xffffffu);
    unsigned n=bpp>3u?3u:bpp;
    uint32_t mask=n==3u?0xffffffu:0xffffu;
    uint32_t K=(k[0]|k[1]<<8|(uint32_t)k[2]<<16)&mask,F=(f[0]|f[1]<<8|(uint32_t)f[2]<<16)&mask,T=(t3[0]|t3[1]<<8|(uint32_t)t3[2]<<16)&mask;
    for(int r=0;r<th;r++) {
        uint8_t *p=px+((unsigned)(top+r)*320u+(unsigned)x)*bpp;
        for(int c=0;c<tw;c++,p+=bpp) {
            uint32_t v=(p[0]|p[1]<<8|(n==3u?(uint32_t)p[2]<<16:0u))&mask;
            if(v==K || v==F || v==T)for(unsigned i=0;i<n;i++)p[i]=q[i];
        }
    }
}
/* TRACK page knobs (insert_m4.h layout): OUT2 is the seventh, OUT3 (when
 * shown) the eighth; any knob of the knob class bound to OUT2/OUT3 counts. */
static const uint16_t cue_track_knobs[8]={0xc398,0xcb38,0xc768,0xd6a8,0xcf08,0xd2d8,0xda78,0xde48};
static void cue_track_grey(uint8_t *px,unsigned fmt,unsigned bpp,uint32_t page,unsigned extra) {
    if(!(extra&3u) || !ram(page,0xde48u+0x100u))return;
    for(unsigned i=0;i<8u;i++) {
        const uint8_t *k=(const uint8_t *)(page+cue_track_knobs[i]);
        if(*(const uint32_t *)k==TILE_VT && *(const uint16_t *)(k+0x32)<12u && !k[0x30] &&
           cue_clocked(*(const uint16_t *)(k+0x94),extra))cue_grey(px,fmt,bpp,k);
    }
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
    unsigned extra=cue_extra();
    for(unsigned ch=0;ch<12u;ch++) {
        uint8_t *t=(uint8_t *)(page+TILE0+ch*TILE_STRIDE),*strip=(uint8_t *)(page+STRIP0+ch*TILE_STRIDE);
        unsigned on=cued>>ch&1u,dim=!(inputs>>ch&1u) && !(extra&8u);
        /* Only the MIXER sends page binds a track's strip knob to OUT2/OUT3. */
        unsigned clocked=*(uint32_t *)t==TILE_VT && cue_clocked(*(uint16_t *)(t+0x94),extra);
        if(!on && !dim && !clocked)continue;
        if(*(uint32_t *)t!=TILE_VT || *(uint16_t *)(t+0x32)!=ch || t[0x30] || strip[0x30])continue;
        int x=*(int *)(t+4),bottom=*(int *)(t+8),tw=*(int *)(t+12),th=*(int *)(t+16);
        if(tw<14 || th<12 || x<0 || x>320-tw || bottom<0 || bottom>240-th)continue;
        /* Native rectangles have a bottom-left origin; the framebuffer is top-down. */
        int top=240-bottom-th;
        if(clocked)cue_grey(px,fmt,bpp,t);   /* clocked send: grey */
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
    cue_track_grey(px,fmt,bpp,page,extra);
}
/* Present chain: cue state, then Scenes feedback, then Cobalt's meters and flip. */
void cue_present(void *display,int layer,int a2,int a3) {
    cue_draw(((const uint8_t *(*)(void *,int))0x0810139bu)(display,layer));
    pc_present(display,layer,a2,a3);
}
/* Redraw both pages once when a mask changes, so overlays appear and vanish. */
void cue_ui_tick(uint8_t *pages,uint8_t *flags) {
    unsigned cued=0,inputs=0xfffu,extra=cue_extra();
    (void)cue_masks(&cued,&inputs);
    PWR_CR1|=1u<<8;
    if(VIEW->magic!=VIEW_MAGIC){VIEW->magic=VIEW_MAGIC;VIEW->shown=cued;VIEW->inputs=inputs;VIEW->extra=extra;}
    if(VIEW->shown!=cued || VIEW->inputs!=inputs || VIEW->extra!=extra) {
        VIEW->shown=cued;VIEW->inputs=inputs;VIEW->extra=extra;pages[0x38]=pages[0x39]=1;
    }
    gate_ui_tick(pages,flags);
}
