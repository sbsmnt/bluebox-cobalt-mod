/* Desktop-specific settings-list integration. Included after pixel().
 * Global app pointer: literal loaded by desktop main loop 0x0810d358.
 * Settings page: app+0x1e978, constructed at 0x08134a0c.
 * Kind: page+0xa3e0, stored by 0x0813ced0; settings kind 0x0c.
 * List layout: 0x0813d74a; row lookup id: 0x0813d9ca.
 */
#include "meter_shared.h"
#define APP_POINTER (*(volatile uint32_t *)0x3000d9fcu)
static int ram_span(uint32_t a,uint32_t n) {
    return (a>=0x24000000u && n<=0x80000u && a<=0x24080000u-n) ||
           (a>=0x30000000u && n<=0x48000u && a<=0x30048000u-n) ||
           (a>=0xc0000000u && n<=0x4000000u && a<=0xc4000000u-n);
}
static uint32_t threshold_row(void) {
    uint32_t app=APP_POINTER;
    if(!ram_span(app,0x2b8u)) return 0;
    uint32_t page=*(volatile uint32_t *)(app+0x2b4u);
    if(page!=app+0x1e978u || !ram_span(page,0xa3e4u)) return 0;
    if(*(volatile uint16_t *)(page+0xa3e0u)!=0xcu) return 0;
    uint32_t list=page+0x9c8u;
    unsigned count=*(volatile uint32_t *)(list+0x99e8u);
    if(count>48u) return 0;
    const volatile int32_t *lr=(const volatile int32_t *)(list+4u);
    if(lr[1]<0 || lr[3]<=0 || lr[3]>240 || lr[1]>240-lr[3]) return 0;
    for(unsigned k=0;k<count;k++) {
        uint32_t row=list+0xe8u+k*0x330u;
        if(*(volatile uint16_t *)(row+0x32cu)!=0x132u) continue;
        const volatile int32_t *rr=(const volatile int32_t *)(row+4u);
        int x=rr[0],y=rr[1],w=rr[2],h=rr[3];
        if(*(volatile uint8_t *)(row+0x30u) || x<0 || w<=0 || w>320 || x>320-w ||
           h!=28 || y<0 || y>212 || y<lr[1] || y>lr[1]+lr[3]-28) return 0;
        return row;
    }
    return 0;
}
static unsigned square_root(unsigned v) {
    unsigned r=0;
    for(unsigned bit=1u<<30;bit;bit>>=2)
        if(v>=r+bit) {v-=r+bit;r=(r>>1)+bit;} else r>>=1;
    return r;
}
static unsigned gr_width(unsigned gr,unsigned w) {return square_root(gr*w*w/400u);}
static void gr_draw(uint8_t *px,unsigned stride,unsigned fmt,unsigned bpp,unsigned valid,unsigned on,unsigned gr) {
    if(!valid || !on) return;
    uint32_t row=threshold_row();
    if(!row) return;
    const volatile int32_t *rr=(const volatile int32_t *)(row+4u);
    unsigned x0=(unsigned)rr[0],y=(unsigned)rr[1],w=(unsigned)rr[2];
    if(gr>400u) gr=400u;
    unsigned fill=gr_width(gr,w),t5=gr_width(50u,w),t10=gr_width(100u,w),t20=gr_width(200u,w);
    for(unsigned r=0;r<3u;r++) {
        uint8_t *p=px+((239u-y-r)*stride+x0)*bpp;
        for(unsigned x=0;x<w;x++,p+=bpp) {
            unsigned tick=x==t5 || x==t10 || x==t20;
            pixel(p,fmt,x<fill ? 0x79f7e0u : tick ? 0x506496u : 0x142040u);
        }
    }
}
/* A compact reference meter is independent of the settings page. Same reading
 * and square-root scale as the Thresh-row bar: 10 dB -> 7/14 rows, 40 dB -> full.
 * Columns 308..310 leave a gap before the CPU bar at 314..316. */
static void gr_small_draw(uint8_t *px,unsigned stride,unsigned fmt,unsigned bpp,unsigned valid,unsigned on,unsigned gr) {
    if(!valid) return;
    if(!on) gr=0u;
    if(gr>400u) gr=400u;
    unsigned fill=gr_width(gr,14u);
    for(unsigned k=0;k<14u;k++) {
        uint8_t *p=px+((3u+k)*stride+308u)*bpp;
        uint32_t color=k<fill ? 0x79f7e0u : 0x303030u;
        for(unsigned x=0;x<3u;x++,p+=bpp) pixel(p,fmt,color);
    }
}
struct gr_tick_state {uint32_t magic,seq;uint16_t gr;uint8_t on,active,height,row_visible;};
#define GT ((volatile struct gr_tick_state *)0x38800fc0u)
#define GT_MAGIC 0x44475434u
typedef void (*dirty_fn)(uint8_t *,uint8_t *);
#define STOCK_DIRTY ((dirty_fn)0x08139587u)
void gr_tick(uint8_t *pages,uint8_t *flags) {
    backup_enable();
    PWR_CR1 |= 1u<<8;
    if(GT->magic!=GT_MAGIC) {
        GT->seq=0;GT->gr=0;GT->on=0;GT->active=0;
        GT->height=0;GT->row_visible=0;GT->magic=GT_MAGIC;
    }
    if(GR_REPORT->magic==GR_MAGIC) {
        uint32_t seq=GR_REPORT->seq;
        unsigned on=GR_REPORT->on,gr=on ? GR_REPORT->gr : 0u;
        if(gr>400u) gr=400u;
        unsigned height=gr_width(gr,14u),row_visible=threshold_row()!=0;
        if(!GT->active || on!=GT->on || row_visible!=GT->row_visible || seq-GT->seq>=3u) {
            unsigned small_changed=!GT->active || on!=GT->on || height!=GT->height;
            unsigned row_changed=row_visible && (gr!=GT->gr || on!=GT->on || !GT->row_visible);
            if(small_changed || row_changed) {pages[0x38]=1;pages[0x39]=1;}
            GT->seq=seq;GT->gr=(uint16_t)gr;GT->on=(uint8_t)on;
            GT->height=(uint8_t)height;GT->row_visible=(uint8_t)row_visible;
        }
        GT->active=1;
    } else {
        GT->active=0;
    }
    STOCK_DIRTY(pages,flags);
}
