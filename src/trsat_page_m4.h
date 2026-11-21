/* TRACK third control section: per-track Sat and its Curve on the left
 * square. A (knob focus) cycles first square -> second square -> Sat -> first.
 * The Sat section is the native first section (encoder routing, page 5)
 * with VOL rebound to Sat, PAN (encoder 3) to Curve and every other pot
 * hidden. */
struct trk_sat_ui {uint8_t on,want;uint16_t ch;};
#define TRK_SAT_UI ((volatile struct trk_sat_ui *)0x38800ce0u) /* M4 only */
static const uint16_t ts_hidden[]={0xcb38,0xcf08,0xd2d8,0xd6a8,0xda78,0xde48};
/* Hide or show the other pots. The two reused pots' names, set once at
 * construction (+0xf8) and copied into their drawn labels (+0xd1), read
 * SAT/CURVE or the native VOL/PAN; six-byte copies in their 0xff buffers. */
static const uint16_t ts_pots[2]={0xc398,0xc768};
static const char ts_names[2][2][6]={{"VOL","SAT"},{"PAN","CURVE"}};
static void ts_page_hide(uint8_t *t,uint8_t hide) {
    for(unsigned i=0;i<sizeof ts_hidden/sizeof *ts_hidden;i++)t[ts_hidden[i]+0x30]=hide;
    for(unsigned p=0;p<2;p++)
        for(unsigned i=0;i<6;i++)
            t[ts_pots[p]+0xf8+i]=t[ts_pots[p]+0xd1+i]=(uint8_t)ts_names[p][hide][i];
}
static void ts_page_bind(uint8_t *t,unsigned ch) {
    static const uint16_t ids[2]={TS_ID4,TC_ID4};
    for(unsigned p=0;p<2;p++) {
        uint8_t *w=t+ts_pots[p];
        mix_knob_bind(w,ch,ids[p],fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,ids[p]));
        w[0x30]=0;w[0x2d4]=0;
    }
    ts_page_hide(t,1);
}
/* Every section change on TRACK (A, auto-flip, page entry) goes through
 * here: leave the Sat section first, then enter it if A asked for it. */
void ts_track_section(uint8_t *t,unsigned sec) {
    volatile struct trk_sat_ui *s=TRK_SAT_UI;
    unsigned want=s->want,ch=s->ch;
    s->want=0;
    if(s->on) {
        uint32_t info[2]={0x08148664u,ch};
        s->on=0;
        ts_page_hide(t,0);
        gate_track_bind(t,(uint8_t *)info);
    }
    ((void (*)(void *,unsigned))0x081327cdu)(t,sec);
    if(want && sec==0 && ch<12) {
        s->on=1;
        ts_page_bind(t,ch);
    }
}
/* A on TRACK: page 5 (first section) <-> page 6 (second section). */
void ts_track_a(void *model,unsigned page,unsigned a2,unsigned a3) {
    volatile struct trk_sat_ui *s=TRK_SAT_UI;
    if(page==5)s->want=1;
    else if(page==6 && s->on)page=5;
    ((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)(model,page,a2,a3);
}
/* Track selection rebinds natively; keep the Sat section on the new track.
 * Main has no Sat and the native binder leaves the knobs on the previous
 * track: rebind that track natively and show the first section again. */
static void ts_track_rebind(uint8_t *p,unsigned ch) {
    volatile struct trk_sat_ui *s=TRK_SAT_UI;
    if(s->on && ch<12)ts_page_bind(p,ch);
    else if(s->on) {
        uint32_t info[2]={0x08148664u,s->ch};
        s->on=0;
        ts_page_hide(p,0);
        gate_track_bind(p,(uint8_t *)info);
    }
    s->ch=(uint16_t)ch;
}
