/* Desktop BL 0x08137496 -> stock flip 0x0810134c.
 * Draw two 3x14 bars to the right of the narrowed clock label, before flipping.
 * Stock back-buffer getter 0x0810139a remains in use; neither UI layout nor
 * framebuffer addresses are assumed beyond the checked descriptor format.
 */
#include "shared.h"
typedef void (*flip_fn)(void *,int,int,int);
typedef const uint8_t *(*buffer_fn)(void *,int);
#define STOCK_FLIP ((flip_fn)0x0810134du)
#define STOCK_BACKBUFFER ((buffer_fn)0x0810139bu)
static int valid_buffer(uint32_t a, uint32_t size) {
    return (a >= 0xc0000000u && a <= 0xc4000000u-size) ||
           (a >= 0x24000000u && a <= 0x24080000u-size) ||
           (a >= 0x30000000u && a <= 0x30048000u-size);
}
static void pixel(uint8_t *p, unsigned fmt, uint32_t c) {
    unsigned r=(c>>16)&255u, g=(c>>8)&255u, b=c&255u;
    switch(fmt) {
    case 1: *(uint32_t *)p=0xff000000u|c; break;
    case 2: p[0]=(uint8_t)b; p[1]=(uint8_t)g; p[2]=(uint8_t)r; break;
    case 3: *(uint16_t *)p=(uint16_t)(0x8000u|((r>>3)<<10)|((g>>3)<<5)|(b>>3)); break;
    case 4: *(uint16_t *)p=(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)); break;
    }
}
#include "gr_m4.h"
void gate_ux_draw(uint8_t *px,unsigned stride,unsigned fmt,unsigned bpp);

static void draw(const uint8_t *d) {
    if (!d) return;
    uint8_t *px=*(uint8_t *const *)d;
    unsigned w=*(const uint16_t *)(d+4), h=*(const uint16_t *)(d+6);
    unsigned fmt=d[8], bpp=*(const uint16_t *)(d+10);
    if (w!=320u || h!=240u) return;
    if (!((fmt==1u && bpp==4u)||(fmt==2u && bpp==3u)||
          ((fmt==3u||fmt==4u) && bpp==2u))) return;
    if (!valid_buffer((uint32_t)px,w*h*bpp)) return;
    backup_enable();
    /* One snapshot per presented frame keeps both GR displays in agreement
     * even if the audio core publishes another report while we draw the row. */
    unsigned gr_valid=GR_REPORT->magic==GR_MAGIC;
    unsigned gr_on=GR_REPORT->on,gr_value=GR_REPORT->gr;
    gr_draw(px,w,fmt,bpp,gr_valid,gr_on,gr_value);
    gr_small_draw(px,w,fmt,bpp,gr_valid,gr_on,gr_value);
    gate_ux_draw(px,w,fmt,bpp);
    if (REPORT->magic!=REPORT_MAGIC) return;
    unsigned avg=REPORT->avg, peak=REPORT->peak;
    if (avg>1000u) avg=1000u;
    if (peak>1000u) peak=1000u;
    unsigned fill=(avg*14u+500u)/1000u, prow=(peak*14u+500u)/1000u;
    uint32_t on=avg>900u ? 0xff2828u : avg>700u ? 0xffb000u : 0x00c850u;
    for(unsigned k=0;k<14u;k++) {
        uint32_t c=k<fill ? on : 0x303030u;
        if(prow && k==prow-1u && prow>fill) c=0xffffffu;
        uint8_t *p=px+((16u-k)*w+314u)*bpp;
        for(unsigned x=0;x<3u;x++,p+=bpp) pixel(p,fmt,c);
    }
}
void cpu_present(void *display,int layer,int a2,int a3) {
    draw(STOCK_BACKBUFFER(display,layer));
    STOCK_FLIP(display,layer,a2,a3);
}

#include "sat_m4.h"

#include "delay_m4.h"
#include "knob_theme.h"

#include "gate_m4.h"

#include "pcm24_m4.h"
