/* DJ EQ on the M4: parameters, defaults and the DJ view.
 * - Parameter definitions and per-track / Main defaults are appended to
 *   Cobalt's chains, so values live in the project and reach the M7 like
 *   any native parameter (the M7 side is dj_m7.c).
 * - DJ view: the third EQ press (EQ -> EQ full screen -> DJ) opens the TRACK
 *   page in DJ mode. Its eight native knobs keep their two A-switched
 *   blocks, encoders, track selection and MIDI learn, but are bound to
 *   High / Hi Mid / Lo Mid / Low and Filter / Res; the buttons on the right,
 *   the CUE button and the level meter are hidden. The EQ LED stays lit,
 *   B opens MAIN's settings (DJ Curve, crossovers), turning a knob shows its
 *   value in its label. Leaving the TRACK view, or pressing EQ again, ends
 *   DJ mode and the native bindings come back.
 * TODO: a third MIXER page with the DJ controls of all tracks (needs a new
 *   view in the firmware's page switching). */
#include "shared.h"
#define SESSION ((uint8_t *)0x300173d0u)
#define APP_POINTER (*(volatile uint32_t *)0x3000d9fcu)
#define UI_NOW (*(volatile uint32_t *)0x3001eb3cu)   /* native millisecond clock */
#define TRACK_VT 0x0814bb3cu
#define VIEW_TRACK 5u
#define VIEW_TRACK_ALT 6u
#define VIEW_INPUTS 7u
#define VIEW_MAIN_SETTINGS 9u
#define VIEW_EQ 0x14u
#define LED_EQ 3u
#define LED_TRACK 1u
#define LEDS ((void *)0x30001f90u)
#define DJ_ID0 0x1bau
#define DJ_ID_CURVE 0x1c0u
/* Main (0x0c): 0x1c0 Curve, 0x1c1..3 Xover Lo/Mid/Hi, 0x1c4 Bands, 0x1c5 Preset
 * (4 bands), 0x1c6 Preset (3 bands), 0x1c7 Filter Slope, 0x1c8 Res Max. */
#define DJ_XLO (DJ_ID_CURVE+1u)
#define DJ_XMID (DJ_ID_CURVE+2u)
#define DJ_XHI (DJ_ID_CURVE+3u)
#define DJ_BANDS (DJ_ID_CURVE+4u)
#define DJ_PRESET4 (DJ_ID_CURVE+5u)
#define DJ_PRESET3 (DJ_ID_CURVE+6u)
#define DJ_SLOPE (DJ_ID_CURVE+7u)
#define DJ_RESMAX (DJ_ID_CURVE+8u)
#define DJ_MAIN_N 9u
#define DJ_LABEL_MS 1200u
#define MAIN_CELL 0x0cu       /* the DJ settings live on Main */
/* M4-only line 0x1e0..0x1ff: cue view 0x1e0..0x1eb, DJ view 0x1ec..0x1ff. */
/* eq_dj: the DJ view was the last EQ view, so EQ from another page reopens it. */
/* settings: 1 while the DJ settings page is wanted (the stock edit page on
 * Main's cell, showing only the DJ rows), 2 once it is shown. */
struct dj_ui {uint32_t magic,app,until;uint8_t ch,bound,view,label,active,eq_dj,settings,pad;};
#define DJ_UI ((volatile struct dj_ui *)0x388001ecu)
#define DJ_UI_MAGIC 0x444a5534u
/* Second record in the free gap 0x1d0..0x1df (after Gate's accel record):
 * the centre detent of the knob being turned, a value forced by it, and the
 * native button states saved while the DJ view uses those buttons. */
struct dj_aux {uint32_t magic;uint16_t id;uint8_t ch,n;int16_t forced;uint8_t turning,has_forced,saved,learn,pad[2];};
#define DJ_AUX ((volatile struct dj_aux *)0x388001d0u)
#define DJ_AUX_MAGIC 0x444a5835u
_Static_assert(sizeof(struct dj_aux)==16,"DJ aux record fills 0x1d0..0x1df");
/* Button-only ids, never stored or sent: kills 0x1f8..0x1fb (High, Hi Mid,
 * Lo Mid, Low), 0x1fc filter reset. */
#define DJ_KILL0 0x1f8u
#define DJ_FRESET 0x1fcu
#define DJ_DETENT 3   /* extra encoder steps that a band or the filter rests at 0 */
#define btn_state ((void (*)(void *,unsigned))0x08127061u)
#define btn_text2 ((void (*)(void *,const char *))0x08126ffdu)   /* a space starts line 2 */
/* The one-line setter leaves line 2 alone: clear it first. */
static void btn_text(uint8_t *w,const char *text) {((void (*)(void *,const char *))0x0813fe4bu)(w+0xa0,"");btn_text2(w,text);}
#define btn_lit_color ((void (*)(void *,unsigned))0x081382b1u)
#define LIT_COLOR 5u          /* the native lit fill of MUTE, SIDE CHAIN and CUE */
#define KILL_COLOR 0x15u      /* the native red of REC: kills and their knob labels */
#define LABEL_COLOR 3u        /* native knob label text */
/* The band values a kill replaced, per track: a second press returns to them.
 * Free backup SRAM between the ring clear state (ends 0x17c) and Gate's
 * accel record (0x1c0). */
struct dj_prev {uint32_t magic;int8_t v[12][4];};
#define DJ_PREV ((volatile struct dj_prev *)0x38800180u)
#define DJ_PREV_MAGIC 0x444a5036u
#define DJ_PREV_NONE 127
_Static_assert(0x180+sizeof(struct dj_prev)<=0x1c0,"DJ kill memory before Gate accel");
_Static_assert(0x1ec+sizeof(struct dj_ui)<=0x200,"DJ view state before delay types at 0x200");
typedef void (*def_fn)(void *,unsigned,unsigned,const char *,int,int,const char *);
#define STOCK_DEF ((def_fn)0x08134c8du)
#define STOCK_ENUM ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
#define STOCK_ADD ((void (*)(void *,unsigned,int))0x0812028fu)
#define fw_get ((int (*)(void *,unsigned))0x08120235u)
#define show_view ((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)
#define set_led ((void (*)(void *,unsigned))0x08102a4du)
#define knob_bind ((void (*)(void *,unsigned,int,unsigned))0x08127b85u)
#define set_label ((void (*)(void *,const char *))0x0813fe4bu)
#define model_set ((void (*)(void *,uint16_t *,unsigned,int,unsigned))0x08124b45u)
void pc_defs_hook(void *table,unsigned id,unsigned type,const char *label,int min,int max,const char *key);
void sat_default(void *set,unsigned id,int value);
void gate_default(void *set,unsigned id,int value);
unsigned gate_track_local(uint8_t *p,uint32_t *ev);
void gate_track_bind(uint8_t *p,uint8_t *info);
unsigned gate_turn(uint8_t *w,int delta);
void cue_ui_tick(uint8_t *pages,uint8_t *flags);
int pc_page_rows(void *session,uint16_t *cell,void *list);
void pc_knob(void *session,uint16_t *cell,unsigned id,int value,int extra);

/* ---- presets ---------------------------------------------------------- *
 * Crossovers modelled on known mixers. Their published figures are EQ band
 * frequencies (shelf corners, mid centres), so the crossovers sit between
 * neighbouring bands (geometric mean); a preset recreates the band split,
 * not the exact EQ shapes. Presets set the ISO curve. Index 0 is Custom. */
static const char *const preset4_name[5]={"Custom","Default","Xone:92","Xone:96","DJM-V10"};
static const uint16_t preset4_hz[5][3]={{0,0,0},{200,1000,5000},{250,840,2500},{180,620,3000},{200,690,2000}};
static const char *const preset3_name[6]={"Custom","Default","DJM","Xone:42","Rane","Evo4"};
static const uint16_t preset3_hz[6][2]={{0,0},{300,3000},{270,3600},{710,1800},{600,2200},{490,2800}};

/* ---- parameters ------------------------------------------------------- */
void dj_defs(void *table,unsigned id,unsigned type,const char *label,int min,int max,const char *key) {
    pc_defs_hook(table,id,type,label,min,max,key);
    /* Bands are bipolar so the knob centre is 0 dB: -100 = -26 dB / kill, 100 = +6 dB. */
    STOCK_DEF(table,DJ_ID0+0,1,"High:",-100,100,"djhigh");
    STOCK_DEF(table,DJ_ID0+1,1,"Hi Mid:",-100,100,"djhimid");
    STOCK_DEF(table,DJ_ID0+2,1,"Lo Mid:",-100,100,"djlomid");
    STOCK_DEF(table,DJ_ID0+3,1,"Low:",-100,100,"djlow");
    STOCK_DEF(table,DJ_ID0+4,1,"Filter:",-100,100,"djfilter");
    STOCK_DEF(table,DJ_ID0+5,8,"Res:",0,1000,"djres");
    static const char *const curves[]={"ISO","EQ"};
    STOCK_ENUM(table,DJ_ID_CURVE,"Curve:",curves,2,"djcurve");
    STOCK_DEF(table,DJ_XLO,1,"Xover Lo:",40,1000,"djxlo");
    STOCK_DEF(table,DJ_XMID,1,"Xover Mid:",300,3000,"djxmid");
    STOCK_DEF(table,DJ_XHI,1,"Xover Hi:",1500,12000,"djxhi");
    static const char *const bands[]={"4 Bands","3 Bands"};
    STOCK_ENUM(table,DJ_BANDS,"Bands:",bands,2,"djbands");
    STOCK_ENUM(table,DJ_PRESET4,"Preset:",preset4_name,5,"djpreset4");
    STOCK_ENUM(table,DJ_PRESET3,"Preset:",preset3_name,6,"djpreset3");
    static const char *const slopes[]={"12 dB","24 dB"};
    STOCK_ENUM(table,DJ_SLOPE,"Filter Slope:",slopes,2,"djslope");
    STOCK_DEF(table,DJ_RESMAX,8,"Res Max:",0,1000,"djresmax");
}
/* Main defaults: ISO, 200 Hz / 1 kHz / 5 kHz, 4 bands, Default presets,
 * 12 dB filter, full resonance range. */
static const int16_t main_default[DJ_MAIN_N]={0,200,1000,5000,0,1,1,0,1000};
void dj_channel_default(void *set,unsigned id,int value) {
    gate_default(set,id,value);
    for(unsigned i=0;i<6;i++)STOCK_ADD(set,DJ_ID0+i,0);
}
void dj_master_default(void *set,unsigned id,int value) {
    sat_default(set,id,value);
    for(unsigned i=0;i<DJ_MAIN_N;i++)STOCK_ADD(set,DJ_ID_CURVE+i,main_default[i]);
}

/* Projects saved before the DJ EQ have no DJ entries in their sets, and the
 * stock setter then only sends the value to the audio core: nothing is
 * stored or shown. Add a missing entry with its default first. A set is
 * {.., items +4, count +10, capacity +12}, items {u16 id, pad, s32 value}. */
static int dj_has(uint8_t *set,unsigned id) {
    const uint8_t *items=*(uint8_t *const *)(set+4);
    unsigned n=*(uint16_t *)(set+10);
    if(!items)return 0;
    for(unsigned i=0;i<n;i++)if(*(const uint16_t *)(items+8u*i)==id)return 1;
    return 0;
}
static void dj_ensure(uint8_t *set,unsigned id,int value) {
    if(!dj_has(set,id) && *(uint16_t *)(set+10)<*(uint16_t *)(set+12))STOCK_ADD(set,id,value);
}
static void dj_ensure_track(unsigned ch) {
    if(ch>=12u)return;
    uint8_t *set=SESSION+0xc08u+ch*0x30u;
    for(unsigned i=0;i<6;i++)dj_ensure(set,DJ_ID0+i,0);
}
static void dj_ensure_main(void) {
    for(unsigned i=0;i<DJ_MAIN_N;i++)dj_ensure(SESSION+0xe78u,DJ_ID_CURVE+i,main_default[i]);
}

/* ---- DJ view ---------------------------------------------------------- */
static volatile struct dj_ui *dj_ui(void) {
    backup_enable();PWR_CR1|=1u<<8;
    volatile struct dj_ui *s=DJ_UI;
    if(s->magic!=DJ_UI_MAGIC){s->active=0;s->eq_dj=0;s->settings=0;s->app=0;s->until=0;s->ch=0;s->bound=0;s->view=0;s->label=0xff;s->magic=DJ_UI_MAGIC;}
    return s;
}
static unsigned dj_view(uint32_t app) {return app ? *(volatile uint8_t *)(app+0x739au) : 0u;}
/* TRACK page knobs, in block order: block 1 TL TR BL BR, block 2 TL TR BL BR. */
static const uint16_t knob_off[8]={0xc398,0xc768,0xcb38,0xd6a8,0xcf08,0xd2d8,0xda78,0xde48};
static const uint16_t native_id[8]={0x4,0x63,0x3,0x15d,0xda,0xdb,0x15e,0x1ae};
static const char *const native_name[8]={"VOL","PAN","GAIN","CUE","FX1","FX2","OUT2","GATE"};
/* DJ knob slots -> parameter (0 High, 1 Hi Mid / Mid, 2 Lo Mid, 3 Low, 4
 * Filter, 5 Res; -1 hidden). Knobs keep their places: with 3 bands Mid sits
 * where Hi Mid is and the Lo Mid place stays empty. */
static const int8_t slot4[8]={0,1,2,3,4,5,-1,-1},slot3[8]={0,1,-1,3,4,5,-1,-1};
static const char *const name4[8]={"HIGH","HI MID","LO MID","LOW","FILTER","RES",0,0};
static const char *const name3[8]={"HIGH","MID",0,"LOW","FILTER","RES",0,0};
static int dj_three(void) {return fw_get(SESSION+0xe78u,DJ_BANDS)!=0;}
static const int8_t *dj_slots(int three) {return three ? slot3 : slot4;}
static const char *dj_slot_name(int three,unsigned k) {return (three ? name3 : name4)[k];}
static int dj_slot_of(int three,unsigned which) {
    const int8_t *m=dj_slots(three);
    for(unsigned k=0;k<8;k++)if(m[k]==(int8_t)which)return (int)k;
    return -1;
}
/* Hidden in DJ mode: the level meter and its frame. */
static const uint16_t hide_off[2]={0xbcbc,0xc358};
/* The right block's buttons become kills, in block 1's knob order: bottom
 * SIDE CHAIN (High), MUTE (Hi Mid), top REC (Lo Mid), SOLO (Low). */
static const uint16_t kill_off[4]={0xe9e8,0xe46c,0xe298,0xe640};
static const uint16_t kill_native_id[4]={0x2b,0x160,0x162,0x161};
static const uint8_t kill_native_inv[4]={1,0,0,0};
static const char *const kill_native_name[4]={"SIDE CHAIN","MUTE","REC","SOLO"};
static const uint8_t kill_native_color[4]={5,5,0x15,0x17};   /* REC red, SOLO green */
static const char *const kill4[4]={"Kill High","Kill Hi Mid","Kill Lo Mid","Kill Low"};
static const char *const kill3[4]={"Kill High","Kill Mid",0,"Kill Low"};
/* The CUE button (CUE Style Toggle) moves over the Filter knob as its reset. */
#define CUE_BTN 0xe814u
#define CUE_X 80
#define RESET_X 128

/* AXI SRAM, D2 SRAM or SDRAM (the app and its pages live in SDRAM). */
static int ram(uint32_t a,uint32_t n) {
    return (a>=0x24000000u && n<=0x80000u && a<=0x24080000u-n) ||
           (a>=0x30000000u && n<=0x48000u && a<=0x30048000u-n) ||
           (a>=0xc0000000u && n<=0x4000000u && a<=0xc4000000u-n);
}
static volatile struct dj_aux *dj_aux(void) {
    backup_enable();PWR_CR1|=1u<<8;
    volatile struct dj_aux *a=DJ_AUX;
    if(a->magic!=DJ_AUX_MAGIC){a->id=0;a->ch=0xff;a->n=0;a->forced=0;a->turning=0;a->has_forced=0;a->saved=0;a->magic=DJ_AUX_MAGIC;}
    return a;
}
/* Widget positions are absolute; children are linked from +0x18 by +0x20. */
static void move_x(uint8_t *w,int dx,unsigned depth) {
    *(int *)(w+4)+=dx;
    if(depth>3u)return;
    for(uint8_t *c=*(uint8_t **)(w+0x18);c && ram((uint32_t)c,0x40u);c=*(uint8_t **)(c+0x20))move_x(c,dx,depth+1u);
}
static int dj_band_value(unsigned ch,unsigned which) {return fw_get(SESSION+0xc08u+ch*0x30u,DJ_ID0+which);}
/* MIDI learn: the native bind asks the app for each knob's learn state
 * (learn mode, selected target = blue frame, mapping text) by parameter id.
 * The DJ knobs carry other ids, so ask for those. */
#define LEARN_MODE (*(volatile uint8_t *)(0x300173d0u+0x73b3u))
static void knob_learn(uint8_t *w,unsigned ch,unsigned id) {
    uint8_t st[32];for(unsigned i=0;i<sizeof st;i++)st[i]=0;
    uint16_t c=(uint16_t)ch;
    ((void (*)(void *,uint16_t *,unsigned,void *))0x08123d01u)(SESSION,&c,id,st);
    ((void (*)(void *,void *))0x08127f7du)(w,st);
}
static void button_learn(uint8_t *w,unsigned ch,unsigned id) {
    uint8_t st[32];for(unsigned i=0;i<sizeof st;i++)st[i]=0;
    uint16_t c=(uint16_t)ch;
    ((void (*)(void *,uint16_t *,unsigned,void *))0x08123d01u)(SESSION,&c,id,st);
    ((void (*)(void *,void *))0x08127077u)(w,st);
}
static void dj_learn(uint8_t *p,unsigned ch,int three) {
    const int8_t *m=dj_slots(three);
    for(unsigned k=0;k<8;k++)if(m[k]>=0)knob_learn(p+knob_off[k],ch,DJ_ID0+(unsigned)m[k]);
    /* The kill and reset buttons are not parameters and cannot be learned:
     * no learn frame (it would also cover their second text line). */
    uint8_t off[32];for(unsigned i=0;i<sizeof off;i++)off[i]=0;
    /* SIDE CHAIN (Kill High) has no native learn state: leave it alone. */
    for(unsigned k=1;k<4;k++)((void (*)(void *,void *))0x08127077u)(p+kill_off[k],off);
    ((void (*)(void *,void *))0x08127077u)(p+CUE_BTN,off);
}
static void label_color(uint8_t *label,unsigned color) {
    if(label[0x53]==color)return;
    label[0x53]=(uint8_t)color;
    label[label[0x52] ? 0x56 : 0x55]=1;   /* redraw, as the label's own text setter marks it */
}
/* Lit: a band at its kill end (its knob label in the same colour), the
 * filter away from Off. */
static void dj_button_states(uint8_t *p,unsigned ch,int three) {
    for(unsigned k=0;k<4;k++) {
        if(three && k==2)continue;
        int killed=dj_band_value(ch,k)<=-100;
        btn_state(p+kill_off[k],killed);
        int slot=dj_slot_of(three,k);
        if(slot>=0)label_color(p+knob_off[slot]+0xa0,killed ? KILL_COLOR : LABEL_COLOR);
    }
    btn_state(p+CUE_BTN,dj_band_value(ch,4)!=0);
}
static void dj_bind_button(uint8_t *w,unsigned ch,unsigned id,const char *name,unsigned color) {
    *(uint16_t *)(w+0xf8)=(uint16_t)ch;*(uint16_t *)(w+0xfa)=(uint16_t)id;
    w[0xfc]=0;btn_text(w,name);btn_lit_color(w,color);w[0x30]=0;
}
static uint8_t *track_page(void) {
    uint32_t app=APP_POINTER;
    if(!ram(app,0x2b8u))return 0;
    uint32_t page=*(volatile uint32_t *)(app+0x2b4u);
    if(!ram(page,0xe000u))return 0;
    return *(uint32_t *)page==TRACK_VT ? (uint8_t *)page : 0;
}
static void copy_name(uint8_t *w,const char *name) {
    char *copy=(char *)w+0xf8;unsigned i=0;
    for(;name[i] && i<7u;i++)copy[i]=name[i];
    copy[i]=0;
}
static void name_knob(uint8_t *w,const char *name) {copy_name(w,name);set_label(w+0xa0,name);}
static void dj_bind(uint8_t *p,unsigned ch,int three) {
    uint8_t *cell=SESSION+0xc08u+ch*0x30u;
    const int8_t *m=dj_slots(three);
    dj_ensure_track(ch);
    for(unsigned k=0;k<8;k++) {
        uint8_t *w=p+knob_off[k];
        if(m[k]>=0) {
            unsigned id=DJ_ID0+(unsigned)m[k];
            knob_bind(w,id,fw_get(cell,id),1);
            *(uint16_t *)(w+0x32)=(uint16_t)ch;
            name_knob(w,dj_slot_name(three,k));
            w[0x30]=0;w[0x2d4]=0;
        } else w[0x30]=1;
    }
    for(unsigned i=0;i<2;i++)p[hide_off[i]+0x30]=1;
    volatile struct dj_aux *a=dj_aux();
    if(*(uint16_t *)(p+kill_off[0]+0xfa)<DJ_KILL0) {
        /* Native states, restored when the DJ view ends. */
        unsigned saved=0x80u;
        for(unsigned k=0;k<4;k++)saved|=(unsigned)(p[kill_off[k]+0x96]!=0)<<k;
        saved|=(unsigned)(p[CUE_BTN+0x96]!=0)<<4;
        a->saved=(uint8_t)saved;
    }
    for(unsigned k=0;k<4;k++) {
        const char *name=(three ? kill3 : kill4)[k];
        if(name)dj_bind_button(p+kill_off[k],ch,DJ_KILL0+k,name,KILL_COLOR);
        else p[kill_off[k]+0x30]=1;
    }
    uint8_t *r=p+CUE_BTN;
    if(*(int *)(r+4)==CUE_X)move_x(r,RESET_X-CUE_X,0);
    dj_bind_button(r,ch,DJ_FRESET,"Filter Reset",LIT_COLOR);
    dj_button_states(p,ch,three);
    dj_learn(p,ch,three);dj_aux()->learn=LEARN_MODE;
}
static void dj_unbind(uint8_t *p,unsigned ch) {
    uint8_t *cell=SESSION+0xc08u+ch*0x30u;
    int toggle=fw_get(SESSION+0xe78u,0x172)!=0;   /* CUE Style Toggle shows a button */
    for(unsigned k=0;k<8;k++) {
        uint8_t *w=p+knob_off[k];
        knob_bind(w,native_id[k],fw_get(cell,native_id[k]),1);
        *(uint16_t *)(w+0x32)=(uint16_t)ch;
        name_knob(w,native_name[k]);
        w[0x30]=k==3 && toggle;w[0x2d4]=0;
    }
    for(unsigned i=0;i<2;i++)p[hide_off[i]+0x30]=0;
    volatile struct dj_aux *a=dj_aux();
    unsigned saved=a->saved;a->saved=0;
    for(unsigned k=0;k<4;k++) {
        uint8_t *w=p+kill_off[k];
        *(uint16_t *)(w+0xf8)=(uint16_t)ch;*(uint16_t *)(w+0xfa)=kill_native_id[k];
        w[0xfc]=kill_native_inv[k];btn_text(w,kill_native_name[k]);btn_lit_color(w,kill_native_color[k]);w[0x30]=0;
        if(saved&0x80u)((void (*)(void *,unsigned))0x08138221u)(w,(saved>>k)&1u);
    }
    uint8_t *r=p+CUE_BTN;
    if(*(int *)(r+4)==RESET_X)move_x(r,CUE_X-RESET_X,0);
    *(uint16_t *)(r+0xf8)=(uint16_t)ch;*(uint16_t *)(r+0xfa)=0x173;r[0xfc]=0;
    btn_text(r,"CUE");btn_lit_color(r,LIT_COLOR);r[0x30]=!toggle;
    /* Their native learn state (frame, mapping) comes back too. */
    for(unsigned k=1;k<4;k++)button_learn(p+kill_off[k],ch,kill_native_id[k]);
    button_learn(r,ch,0x173);
    for(unsigned k=0;k<8;k++)if(p[knob_off[k]+0xa0+0x53]==KILL_COLOR)label_color(p+knob_off[k]+0xa0,LABEL_COLOR);
    if(saved&0x80u)((void (*)(void *,unsigned))0x08138221u)(r,(saved>>4)&1u);
}
static void dj_value_text(char *out,unsigned which,int v);
/* Every slot must match: a native rebind (e.g. returning from a menu, where
 * Cobalt's Gate shows its knob again) can change any one of them. */
static int dj_is_bound(uint8_t *p,unsigned ch,int three) {
    const int8_t *m=dj_slots(three);
    for(unsigned k=0;k<8;k++) {
        uint8_t *w=p+knob_off[k];
        if(m[k]<0){if(!w[0x30])return 0;continue;}
        if(w[0x30] || *(uint16_t *)(w+0x94)!=DJ_ID0+(unsigned)m[k] || *(uint16_t *)(w+0x32)!=ch)return 0;
    }
    for(unsigned i=0;i<2;i++)if(!p[hide_off[i]+0x30])return 0;
    for(unsigned k=0;k<4;k++) {
        uint8_t *w=p+kill_off[k];
        if(three && k==2){if(!w[0x30])return 0;continue;}
        if(w[0x30] || *(uint16_t *)(w+0xfa)!=DJ_KILL0+k || *(uint16_t *)(w+0xf8)!=ch)return 0;
    }
    uint8_t *r=p+CUE_BTN;
    return !r[0x30] && *(uint16_t *)(r+0xfa)==DJ_FRESET && *(uint16_t *)(r+0xf8)==ch && *(int *)(r+4)==RESET_X;
}
static void dj_sync_page(uint8_t *p) {
    volatile struct dj_ui *s=dj_ui();
    unsigned view=dj_view(s->app);
    int settings_view=view==VIEW_MAIN_SETTINGS && s->settings;
    if(s->active && view!=VIEW_TRACK && view!=VIEW_TRACK_ALT && !settings_view)s->active=0;
    if(!s->active)s->settings=0;
    /* A switches the block by showing TRACK 5/6, which lights TRACK. */
    if(!p || *(uint32_t *)p!=TRACK_VT)return;
    unsigned ch=*(uint16_t *)(p+knob_off[1]+0x32);   /* PAN / Hi Mid carries the track */
    if(ch>=12)return;
    if(s->active) {
        int three=dj_three();
        if(!dj_is_bound(p,ch,three)) {
            dj_bind(p,ch,three);
            /* Every edit makes the page rebind (0x66); keep a value that is
             * still on show instead of flashing the name in between. */
            if(s->label<8u && dj_slots(three)[s->label]>=0 && (int32_t)(UI_NOW-s->until)<0) {
                uint8_t *w=p+knob_off[s->label];char text[12];
                dj_value_text(text,(unsigned)dj_slots(three)[s->label],*(int *)(w+0x98));name_knob(w,text);
            } else s->label=0xff;
        }
        s->bound=(uint8_t)(1+three);s->ch=(uint8_t)ch;
        dj_button_states(p,ch,three);
        /* Learn mode on, a target picked or a mapping made: refresh the
         * frames while it lasts, and once more when it ends. */
        volatile struct dj_aux *a=dj_aux();
        unsigned learn=LEARN_MODE;
        if(learn || a->learn){dj_learn(p,ch,three);a->learn=(uint8_t)learn;}
        /* Value shown while turning; the name comes back after a pause. */
        if(s->label<8u && (int32_t)(UI_NOW-s->until)>=0) {
            if(dj_slot_name(three,s->label))name_knob(p+knob_off[s->label],dj_slot_name(three,s->label));
            s->label=0xff;
        }
    } else {
        if(s->bound || *(uint16_t *)(p+knob_off[0]+0x94)==DJ_ID0 || *(uint16_t *)(p+CUE_BTN+0xfa)==DJ_FRESET) {
            dj_unbind(p,ch);s->bound=0;s->label=0xff;
        }
        /* Cobalt's Gate knob (OUT3 place) has the same gap in MIDI learn. */
        volatile struct dj_aux *a=dj_aux();
        unsigned learn=LEARN_MODE;
        uint8_t *g=p+0xde48;
        if((learn || a->learn) && *(uint16_t *)(g+0x94)==0x1aeu)knob_learn(g,*(uint16_t *)(g+0x32),0x1aeu);
        a->learn=(uint8_t)learn;
    }
}
static void dj_sync(void) {dj_sync_page(track_page());}

/* EQ in full screen: open the DJ view instead of going back to EQ. */
void dj_eq_from_full(uint8_t *app,unsigned view,unsigned a,unsigned b) {
    (void)view;
    volatile struct dj_ui *s=dj_ui();
    s->app=(uint32_t)app;s->active=1;s->eq_dj=1;s->view=0;
    show_view(app,VIEW_TRACK,a,b);
    dj_sync();
}
/* EQ from anywhere else: from the DJ view back to EQ, otherwise native. */
void dj_eq_other(uint8_t *app,unsigned view,unsigned a,unsigned b) {
    volatile struct dj_ui *s=dj_ui();
    unsigned cur=dj_view((uint32_t)app);
    if(s->active && (cur==VIEW_TRACK || cur==VIEW_TRACK_ALT || cur==VIEW_MAIN_SETTINGS)) {
        s->active=0;s->settings=0;s->eq_dj=0;view=VIEW_EQ;
    } else if(!s->active && s->eq_dj) {
        /* Back to EQ from another page: reopen the DJ view, as FX keeps its page. */
        dj_eq_from_full(app,view,a,b);return;
    }
    show_view(app,view,a,b);
    dj_sync();
}
/* TRACK on the TRACK view normally opens Side Chain. From the DJ view it
 * opens the plain TRACK page instead. */
void dj_track_press(uint8_t *app,unsigned view,unsigned a,unsigned b) {
    volatile struct dj_ui *s=dj_ui();
    if(s->active){s->active=0;s->settings=0;view=VIEW_TRACK;}
    show_view(app,view,a,b);
    set_led(LEDS,LED_TRACK);
    dj_sync();
}
/* B on the TRACK view opens the inputs; in the DJ view it opens MAIN's
 * settings, where DJ Curve and the crossovers are. */
void dj_b_press(uint8_t *app,unsigned view,unsigned a,unsigned b) {
    volatile struct dj_ui *s=dj_ui();
    if(s->active) {
        /* B opens the DJ settings page; B there returns to the DJ view. The
         * view stays TRACK (5/6) meanwhile, so EQ, TRACK and B keep working. */
        /* The native MAIN settings view (9): encoders and edits are routed
         * by the current view, so the page must be shown as view 9. It
         * does not touch the LEDs; EQ stays lit. */
        dj_ensure_main();s->settings=1;show_view(app,VIEW_MAIN_SETTINGS,a,b);
        return;
    }
    show_view(app,view,a,b);
    dj_sync();
}
/* B on MAIN settings (view 9) normally shows MAIN (8); from the DJ settings
 * it returns to the DJ view. */
void dj_b_main_settings(uint8_t *app,unsigned view,unsigned a,unsigned b) {
    volatile struct dj_ui *s=dj_ui();
    if(s->active && s->settings){s->settings=0;view=VIEW_TRACK;}
    show_view(app,view,a,b);
    dj_sync();
}
/* TRACK from any other view shows the remembered TRACK view; from the DJ
 * settings that must be the plain TRACK page, not DJ. */
void dj_track_other(uint8_t *app,unsigned view,unsigned a,unsigned b) {
    volatile struct dj_ui *s=dj_ui();
    if(s->active){s->active=0;s->settings=0;}
    show_view(app,view,a,b);
    dj_sync();
}
/* Edit page rows: on the DJ settings page (Main's cell, opened from the DJ
 * view) only the DJ rows. Main's real cell keeps reads, edits and refresh
 * native; a made-up cell cannot be read back by the stock getter. */
int dj_page_rows(void *session,uint16_t *cell,void *list) {
    volatile struct dj_ui *s=dj_ui();
    if(*cell!=MAIN_CELL || !s->settings)return pc_page_rows(session,cell,list);
    dj_ensure_main();
    /* Bands first; the preset list and crossovers follow the band count. */
    static const uint16_t rows4[8]={DJ_BANDS,DJ_PRESET4,DJ_ID_CURVE,DJ_XLO,DJ_XMID,DJ_XHI,DJ_SLOPE,DJ_RESMAX};
    static const uint16_t rows3[7]={DJ_BANDS,DJ_PRESET3,DJ_ID_CURVE,DJ_XLO,DJ_XHI,DJ_SLOPE,DJ_RESMAX};
    int three=dj_three();
    const uint16_t *rows=three ? rows3 : rows4;unsigned n=three ? 7u : 8u;
    *(uint32_t *)list=0;
    for(unsigned i=0;i<n;i++)((void (*)(void *,unsigned,int))0x08138547u)(list,rows[i],fw_get(SESSION+0xe78u,rows[i]));
    return 1;
}
static void dj_main_set(unsigned id,int value) {
    uint16_t main=MAIN_CELL;
    if(fw_get(SESSION+0xe78u,id)!=value)model_set(SESSION,&main,id,value,0);
}
/* A preset writes the crossovers and the ISO curve of its band count. */
static void dj_apply_preset(int three,int preset) {
    if(three) {
        if(preset<1 || preset>5)return;
        dj_main_set(DJ_ID_CURVE,0);
        dj_main_set(DJ_XLO,preset3_hz[preset][0]);dj_main_set(DJ_XHI,preset3_hz[preset][1]);
    } else {
        if(preset<1 || preset>4)return;
        dj_main_set(DJ_ID_CURVE,0);
        dj_main_set(DJ_XLO,preset4_hz[preset][0]);dj_main_set(DJ_XMID,preset4_hz[preset][1]);dj_main_set(DJ_XHI,preset4_hz[preset][2]);
    }
}
void dj_knob(void *session,uint16_t *cell,unsigned id,int value,int extra) {
    int dj=*cell==MAIN_CELL && id>=DJ_ID_CURVE && id<DJ_ID_CURVE+DJ_MAIN_N;
    if(dj)dj_ensure_main();
    pc_knob(session,cell,id,value,extra);
    if(!dj)return;
    int three=dj_three();
    if(id==DJ_BANDS) {
        dj_apply_preset(three,fw_get(SESSION+0xe78u,three ? DJ_PRESET3 : DJ_PRESET4));
        dj_ui()->bound=0;   /* the DJ view rebinds for the new band count */
    } else if(id==DJ_PRESET4 && !three)dj_apply_preset(0,value);
    else if(id==DJ_PRESET3 && three)dj_apply_preset(1,value);
    else if(id==DJ_ID_CURVE || id==DJ_XLO || id==DJ_XMID || id==DJ_XHI)
        dj_main_set(three ? DJ_PRESET3 : DJ_PRESET4,0);   /* edited by hand: Custom */
}
/* TRACK view LEDs (show view 5/6, also A's block switch): EQ while in DJ. */
void dj_led(void *leds,unsigned index) {
    volatile struct dj_ui *s=dj_ui();
    set_led(leds,s->active ? LED_EQ : index);
}
/* Track selection rebinds the native knobs; put the DJ ones back at once. */
void dj_track_bind(uint8_t *p,uint8_t *info) {
    gate_track_bind(p,info);
    volatile struct dj_ui *s=dj_ui();
    if(s->active){s->bound=0;dj_sync_page(p);}
}
static void dj_value_text(char *out,unsigned which,int v);
/* Show a value in a knob's label (the name copy too, which the knob redraws
 * from); the name comes back DJ_LABEL_MS after the last change. */
static void dj_show_value(uint8_t *w,unsigned which,int v) {
    char text[12];dj_value_text(text,which,v);
    name_knob(w,text);
    int slot=dj_slot_of(dj_three(),which);
    volatile struct dj_ui *s=dj_ui();
    s->label=slot<0 ? 0xffu : (uint8_t)slot;s->until=UI_NOW+DJ_LABEL_MS;
}
/* Encoders: block 1's fourth encoder drives the CUE-slot knob only with
 * CUE Style Fader; in the DJ view it always drives Low. idx is the
 * encoder (caller's r4, passed by the shim). */
unsigned dj_turn(uint8_t *w,int delta,unsigned idx) {
    volatile struct dj_ui *s=dj_ui();
    uint8_t *p=w-0xc768;
    if(s->active && idx==3u && ram((uint32_t)p,0xe000u) && *(uint32_t *)p==TRACK_VT &&
       *(uint16_t *)(w+0x94)==DJ_ID0+1u)
        w=p+0xd6a8;
    /* Hidden knobs keep a binding; the encoders must not reach them. */
    if(s->active && ram((uint32_t)(w-0xc398),0xe000u) && w[0x30])return 0;
    volatile struct dj_aux *a=dj_aux();
    a->turning=1;a->has_forced=0;
    unsigned r=gate_turn(w,delta);
    a->turning=0;
    unsigned id=*(uint16_t *)(w+0x94);
    if(a->has_forced) {
        /* The detent kept the value: put the knob back on it. */
        unsigned ch=*(uint16_t *)(w+0x32);
        a->has_forced=0;knob_bind(w,id,a->forced,1);*(uint16_t *)(w+0x32)=(uint16_t)ch;
    }
    if(s->active && id>=DJ_ID0 && id<DJ_ID0+6u)dj_show_value(w,id-DJ_ID0,*(int *)(w+0x98));
    return r;
}
__attribute__((naked)) void dj_turn_shim(void) {__asm volatile("mov r2,r4\nb dj_turn");}

static void put_num(char **p,unsigned v,int tenths) {
    char buf[8];unsigned n=0;
    if(tenths){buf[n++]=(char)('0'+v%10u);buf[n++]='.';v/=10u;}
    do{buf[n++]=(char)('0'+v%10u);v/=10u;}while(v && n<7u);
    while(n)*(*p)++=buf[--n];
}
/* Knob value text: bands in dB (KILL at the ISO end), Filter LP/OFF/HP, Res %. */
static void dj_value_text(char *out,unsigned which,int v) {
    char *p=out;
    if(which<4) {
        if(!v){*p++='0';*p++='d';*p++='B';}
        else if(v<=-100 && !fw_get(SESSION+0xe78u,DJ_ID_CURVE)){*p++='K';*p++='I';*p++='L';*p++='L';}
        else {
            int tenths=v<0 ? (-v*26+5)/10 : (v*6+5)/10;   /* 0.1 dB, rounded */
            *p++=v<0?'-':'+';put_num(&p,(unsigned)tenths,1);*p++='d';*p++='B';
        }
    } else if(which==4) {
        if(v>-2 && v<2){*p++='O';*p++='F';*p++='F';}
        else {*p++=v<0?'L':'H';*p++='P';*p++=' ';put_num(&p,(unsigned)(v<0?-v:v),0);}
    } else {put_num(&p,(unsigned)(v<0?0:v)/10u,0);*p++='%';}
    *p=0;
}
/* Centre detent for the bands and the filter while an encoder turns: a turn
 * across 0 stops at 0, and leaving 0 takes DJ_DETENT more steps. */
static int dj_detent(unsigned ch,unsigned id,int v) {
    volatile struct dj_aux *a=dj_aux();
    if(!a->turning || id>DJ_ID0+4u)return v;
    int old=dj_band_value(ch,id-DJ_ID0);
    if(a->id!=id || a->ch!=ch){a->id=(uint16_t)id;a->ch=(uint8_t)ch;a->n=0;}
    int keep=0;
    if((old>0 && v<0) || (old<0 && v>0)){keep=1;a->n=0;}
    else if(!old && v){if(a->n<DJ_DETENT){a->n++;keep=1;} else a->n=0;}
    else if(!v)a->n=0;
    if(!keep)return v;
    a->forced=0;a->has_forced=1;
    return 0;
}
static void dj_set_value(uint8_t *p,unsigned ch,unsigned which,int v) {
    uint16_t c=(uint16_t)ch;
    dj_ensure_track(ch);model_set(SESSION,&c,DJ_ID0+which,v,0);
    int slot=dj_slot_of(dj_three(),which);
    if(slot<0)return;
    uint8_t *w=p+knob_off[slot];
    knob_bind(w,DJ_ID0+which,v,1);*(uint16_t *)(w+0x32)=(uint16_t)ch;
    dj_show_value(w,which,v);
}
static volatile struct dj_prev *dj_prev(void) {
    backup_enable();PWR_CR1|=1u<<8;
    volatile struct dj_prev *m=DJ_PREV;
    if(m->magic!=DJ_PREV_MAGIC){for(unsigned i=0;i<12;i++)for(unsigned b=0;b<4;b++)m->v[i][b]=DJ_PREV_NONE;m->magic=DJ_PREV_MAGIC;}
    return m;
}
/* Kill toggles between the kill end and the value it replaced (0 dB when
 * the band was killed some other way). Filter reset: Off. */
static void dj_button(uint8_t *p,unsigned ch,unsigned which) {
    if(which<4) {
        volatile struct dj_prev *m=dj_prev();
        int v=dj_band_value(ch,which);
        if(v>-100){m->v[ch][which]=(int8_t)v;dj_set_value(p,ch,which,-100);}
        else {
            int back=m->v[ch][which];
            m->v[ch][which]=DJ_PREV_NONE;
            dj_set_value(p,ch,which,back>-100 && back<=100 ? back : 0);
        }
    } else dj_set_value(p,ch,4,0);
    dj_button_states(p,ch,dj_three());
}
/* Knob edits on the TRACK page: DJ parameters go to the native model
 * setter (audio command, project storage, notifications), like Gate. */
unsigned dj_track_local(uint8_t *p,uint32_t *ev) {
    unsigned type=(uint16_t)ev[0];
    if(type==0x6e && ev[3]>=DJ_KILL0 && ev[3]<=DJ_FRESET) {
        unsigned ch=(uint16_t)ev[2];
        if(ch<12 && *(uint32_t *)p==TRACK_VT)dj_button(p,ch,ev[3]-DJ_KILL0);
        return 0;
    }
    if(type==0x16a && ev[3]>=DJ_KILL0 && ev[3]<=DJ_FRESET)return 0;
    if(type==0x6e || type==0x3c || type==0x3d) {
        unsigned id=type==0x6e?ev[3]:ev[4];
        if(id>=DJ_ID0 && id<DJ_ID0+6u) {
            uint16_t ch=(uint16_t)ev[2];
            int v=(int)(type==0x6e?ev[4]:ev[3]);
            if(type==0x6e && ch<12)v=dj_detent(ch,id,v);
            if(ch<12){dj_ensure_track(ch);model_set(SESSION,&ch,id,v,0);}
            volatile struct dj_ui *s=dj_ui();
            int slot=dj_slot_of(dj_three(),id-DJ_ID0);
            if(s->active && *(uint32_t *)p==TRACK_VT && slot>=0)dj_show_value(p+knob_off[slot],id-DJ_ID0,v);
            return 0;
        }
    }
    /* A touch selects a knob's block by its parameter: block 1 is decided
     * by VOL/GAIN/PAN/CUE ids. Present the band knobs as VOL for that. */
    if(type==0x16a && ev[3]>=DJ_ID0 && ev[3]<DJ_ID0+6u) {
        int slot=dj_slot_of(dj_three(),ev[3]-DJ_ID0);
        if(slot>=0 && slot<4)ev[3]=0x4;
    }
    return gate_track_local(p,ev);
}
void dj_ui_tick(uint8_t *pages,uint8_t *flags) {
    volatile struct dj_ui *s=dj_ui();
    /* The DJ rows filter lasts while the page opened from the DJ view is
     * shown (the page appears a tick after the request). */

    unsigned was=s->active;
    dj_sync();
    if(was!=s->active){pages[0x38]=pages[0x39]=1;}
    cue_ui_tick(pages,flags);
}
