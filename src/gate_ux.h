/* UI only: native thresholds/IPC/DSP are unchanged. Reuse alpha.1's actual
 * audio-core snapshots; never substitute the VOL meter reference. */
struct gate_ux_track {uint32_t seq,changed;int32_t fraction,saved;};
#define GX ((volatile struct gate_ux_track *)0x38800a40u)
_Static_assert(0xa40+12*sizeof(struct gate_ux_track)==0xb00,"Gate UX ends before FX state");
/* GD_UI initialization also resets the paired UI-only state. */
static void gate_ux_reset(void) {
    for(unsigned ch=0;ch<12;ch++) {
        GX[ch].seq=GX[ch].changed=0;GX[ch].fraction=0;GX[ch].saved=0;
    }
}
/* Turn-speed acceleration for the threshold (TRACK and MIXER share this
 * wrapper). Speed is raw encoder units per millisecond, the same measure the
 * native history getter 0x0813B804 uses (it returns 1 below 2 units/ms, now
 * capped at 2x). Below 2 units/ms the existing fine travel is kept exactly
 * (0.1 dB, half that below -48 dB). Faster turns multiply the step before the
 * native knob applies its own 1..2x. A pause over 250 ms or a direction
 * change starts slow again. One shared record in the free backup-SRAM gap at
 * 0x1c0, M4 only (M7 never reads it). */
struct gate_accel {uint32_t magic,last;int32_t dir;float rate;};
#define GA ((volatile struct gate_accel *)0x388001c0u)
#define GA_MAGIC 0x47414332u
_Static_assert(0x1c0+sizeof(struct gate_accel)<=0x200,"Gate accel below delay types at 0x200");
static int gate_accel(int delta) {
    uint32_t now=UI_NOW;
    int dir=delta>0?1:-1,n=delta>0?delta:-delta;
    if(GA->magic!=GA_MAGIC){GA->magic=GA_MAGIC;GA->last=now-1000u;GA->dir=0;GA->rate=0.f;}
    uint32_t dt=now-GA->last;
    float rate=GA->rate;
    if(GA->dir!=dir || dt>250u || !(rate>=0.f && rate<1e6f))rate=0.f;
    else rate=.5f*rate+.5f*(float)n/(float)(dt?dt:1u);
    GA->last=now;GA->dir=dir;GA->rate=rate;
    /* exp.8: top end softened (was 3/8/16/32) after hardware feedback. */
    return rate<2.f?1:rate<4.f?3:rate<8.f?6:rate<16.f?12:20;
}
unsigned gate_turn(uint8_t *w,int delta) {
    if(*(uint16_t *)(w+0x94)!=NG_ID4)
        return ((unsigned (*)(void *,int))0x08127d51u)(w,delta);
    unsigned ch=*(uint16_t *)(w+0x32);
    if(ch>=12 || w[0x30] || w[0x2d4])return 0;
    (void)gd_ui();
    int value=*(int *)(w+0x98);
    volatile struct gate_ux_track *s=&GX[ch];
    /* Native knobs otherwise accumulate travel into ten equivalent Off
     * positions when turned down at minimum. Keep a single Off endpoint. */
    if((value<=-96000 && delta<=0) || (value>=-12000 && delta>=0)) {
        s->fraction=0;s->saved=value;return 0;
    }
    if(value<=-96000 && *(int *)(w+0x84)<10) {
        ((void (*)(void *,int))0x0813b295u)(w+0x78,10);
        *(int *)(w+0x90)=10;
    }
    /* Discard an old partial turn after an external edit or direction change.
     * Fraction is per track and shared by both views, never a threshold store. */
    int f=s->fraction;
    if(s->saved!=value || (delta>0 && f<0) || (delta<0 && f>0))f=0;
    if(delta>32767)delta=32767;
    if(delta< -32768)delta=-32768;
    int speed=gate_accel(delta),step;
    if(speed>1) {
        /* Fast travel: coarse steps, no fine fraction carried over. */
        step=delta*speed;f=0;s->fraction=0;
    } else {
        int divisor=value<-48000?2:1;
        int total=f+delta;step=total/divisor;
        s->fraction=total-step*divisor;
    }
    s->saved=value;
    unsigned result=0;
    if(step)result=((unsigned (*)(void *,int))0x08127d51u)(w,step);
    s->saved=*(int *)(w+0x98);
    if((s->saved<=-96000 && delta<0)||(s->saved>=-12000 && delta>0))s->fraction=0;
    return result;
}
/* Full scale is the detector's unity amplitude. Both input and threshold use
 * the same -96..0 dB ruler, independently of VOL gain/reference. */
unsigned gate_ux_pos(int db,unsigned width) {
    if(!width)return 0;
    if(db<-96000)db=-96000;
    if(db>0)db=0;
    return ((unsigned)(db+96000)*(width-1u)+48000u)/96000u;
}
/* Unavailable/old/mismatched snapshots must not masquerade as a closed gate.
 * Return: 0 pending, 1 Off, 2 open, 3 fade, 4 closed. */
unsigned gate_ux_state(unsigned ch,int requested,struct gate_audio_diag *out) {
    if(ch>=12)return 0;
    (void)gd_ui();
    if(!gd_snapshot((int)ch,out))return 0;
    volatile struct gate_ux_track *s=&GX[ch];uint32_t now=UI_NOW;
    if(s->seq!=out->seq){s->seq=out->seq;s->changed=now;}
    if((uint32_t)(now-s->changed)>500u || out->threshold!=requested)return 0;
    if(requested<=-96000)return 1;
    if(!(out->gain>=0.f && out->gain<=1.f))return 0;
    if(out->gain==0.f)return 4;
    return out->gain>=.9999f?2:3;
}
static void gx_rect(uint8_t *px,unsigned stride,unsigned fmt,unsigned bpp,
                    int x,int y,int w,int h,uint32_t color) {
    for(int row=0;row<h;row++)for(int col=0;col<w;col++)
        pixel(px+(((unsigned)(y+row)*stride)+(unsigned)(x+col))*bpp,fmt,color);
}
static void gx_text(uint8_t *px,unsigned stride,unsigned fmt,unsigned bpp,
                    int x,int y,const char *text,uint32_t color) {
    for(unsigned k=0;text[k] && k<8;k++) {
        const uint8_t *glyph=0;
        for(unsigned j=0;j<sizeof(gd_font)/sizeof(gd_font[0]);j++)
            if(gd_font[j].c==text[k]){glyph=gd_font[j].r;break;}
        if(glyph)for(unsigned row=0;row<7;row++)for(unsigned col=0;col<5;col++)
            if(glyph[row]&(16u>>col))pixel(px+(((unsigned)(y+(int)row)*stride)+(unsigned)(x+6*(int)k+(int)col))*bpp,fmt,color);
    }
}
static void gx_center(uint8_t *px,unsigned stride,unsigned fmt,unsigned bpp,
                      int x,int y,int w,const char *text,uint32_t color) {
    unsigned len=0;while(text[len] && len<8)len++;
    gx_text(px,stride,fmt,bpp,x+(w-(int)(len*6u-1u))/2,y,text,color);
}
/* Replace only the existing Gate control's own drawing rectangle. Input and
 * touch remain native. Native coordinates have a bottom-left origin. */
void gate_ux_control(uint8_t *px,unsigned stride,unsigned fmt,unsigned bpp,uint8_t *w) {
    if(!ram_span((uint32_t)w,0x3d0) || w[0x30] || w[0x2d4] || *(uint16_t *)(w+0x94)!=NG_ID4)return;
    int x=*(int *)(w+4),bottom=*(int *)(w+8),width=*(int *)(w+12),height=*(int *)(w+16);
    if(x<0 || width<44 || width>100 || x>320-width || bottom<0 || height<44 || height>100 || bottom>240-height)return;
    unsigned ch=*(uint16_t *)(w+0x32);if(ch>=12)return;
    int value=*(int *)(w+0x98),top=240-bottom-height;
    struct gate_audio_diag a;unsigned state=gate_ux_state(ch,value,&a);
    uint32_t bg=theme_knob_palette[2]&0xffffffu,fg=theme_knob_palette[3]&0xffffffu,accent=theme_knob_palette[34]&0xffffffu;
    /* Native selection and panel borders overlap the widget's outer rows.
     * Leave a 2px perimeter untouched instead of painting over those pixels. */
    gx_rect(px,stride,fmt,bpp,x+2,top+2,width-4,height-4,bg);
    int y=top+(height-44)/2;
    gx_center(px,stride,fmt,bpp,x,y+4,width,"GATE",accent);
    char value_text[54];value_text[0]=0;struct gd_line line={value_text,0};gd_db(&line,value,1);
    gx_center(px,stride,fmt,bpp,x,y+14,width,value_text,fg);
    /* A releasing gate still passes audio. Show OPN until applied gain is
     * zero; no intermediate FADE label or amber state is exposed. */
    const char *status=state==1?"OFF":state==4?"CLS":state>=2?"OPN":"--";
    uint32_t sc=state>=2 && state!=4?0x22bb22u:0xaaaaaau;
    gx_center(px,stride,fmt,bpp,x,y+24,width,status,sc);
    int bw=width-8,by=y+35;
    gx_rect(px,stride,fmt,bpp,x+4,by,bw,4,0x303030u);
    if(state>=2 && a.left>=0.f && a.right>=0.f) {
        float peak=a.left>a.right?a.left:a.right;
        unsigned fill=peak>0.f?gate_ux_pos(gate_diag_db(peak),(unsigned)bw)+1u:0;
        if(fill)gx_rect(px,stride,fmt,bpp,x+4,by,(int)fill,4,accent);
    }
    if(value>-96000) {
        unsigned marker=gate_ux_pos(value,(unsigned)bw);
        gx_rect(px,stride,fmt,bpp,x+4+(int)marker,by-1,1,6,fg);
    }
}
void gate_ux_draw(uint8_t *px,unsigned stride,unsigned fmt,unsigned bpp) {
    if(GD_UI->magic!=GD_UI_MAGIC)return;
    uint32_t app=APP_POINTER;if(!ram_span(app,0x2b8))return;
    uint32_t page=*(volatile uint32_t *)(app+0x2b4);
    uint32_t track=GD_UI->track_knob;
    /* MIXER uses its unmodified native renderer for every row layout. */
    if(track>=0xde48u && page==track-0xde48u)
        gate_ux_control(px,stride,fmt,bpp,(uint8_t *)track);
}

/* Bound UI refresh to 20Hz while Gate is on screen, including when an input
 * goes silent and native VOL meters no longer dirty their widgets. */
void gate_ux_tick(uint8_t *pages) {
    if(GD_UI->magic!=GD_UI_MAGIC)return;
    uint32_t app=APP_POINTER;if(!ram_span(app,0x2b8))return;
    uint32_t page=*(volatile uint32_t *)(app+0x2b4);
    uint32_t track=GD_UI->track_knob;
    unsigned active=track>=0xde48u && page==track-0xde48u &&
        ram_span(track,0x3d0) && !*(uint8_t *)(track+0x30);
    if(!active){GD_UI->ux_page=0;return;}
    uint32_t now=UI_NOW;
    if(GD_UI->ux_page!=page || (uint32_t)(now-GD_UI->ux_tick)>=50u) {
        GD_UI->ux_page=page;GD_UI->ux_tick=now;pages[0x38]=pages[0x39]=1;
    }
}

/* Retain the compressor redraw implementation verbatim. This wrapper adds
 * the independent Gate view refresh before its existing dirty traversal. */
void gate_ui_tick(uint8_t *pages,uint8_t *flags) {
    gate_ux_tick(pages);gr_tick(pages,flags);
}
