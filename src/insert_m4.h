/* Experimental TRACK banks. Native B input routing is preserved. A cycles
 * native left/right controls, then insert left/right controls. State belongs
 * only to M4 and occupies an isolated unused backup-SRAM cache line. */
struct insert_ui {
    uint32_t magic,active,rendered,group;
    uint32_t select_ch;int32_t select_value,select_fraction;
    uint32_t page;
};
#define INS_UI ((struct insert_ui *)0x388001e0u)
#define INS_UI_MAGIC 0x53554935u
_Static_assert(0x1c0+sizeof(struct gate_accel)<=0x1e0,"insert UI after Gate acceleration");
_Static_assert(0x1e0+sizeof(struct insert_ui)<=0x200,"insert UI before delay state");
static struct insert_ui *insert_ui(void) {
    backup_enable();PWR_CR1|=1u<<8;
    if(INS_UI->magic!=INS_UI_MAGIC) {
        INS_UI->active=INS_UI->rendered=INS_UI->group=0;
        INS_UI->select_ch=0xffffffffu;INS_UI->select_value=INS_UI->select_fraction=0;
        INS_UI->page=0;INS_UI->magic=INS_UI_MAGIC;
    }
    return INS_UI;
}
static uint8_t *insert_knob(uint8_t *p,unsigned i) {
    static const uint16_t offsets[]={0xc398,0xcb38,0xc768,0xd6a8,0xcf08,0xd2d8,0xda78,0xde48};
    return p+offsets[i];
}
static uint8_t *synth_knob(uint8_t *p,unsigned i) {
    if(i==5)i=6;
    else if(i==6)i=5;
    return insert_knob(p,i);
}
#define SYN_BUTTON_OFFSET 0xe814u /* existing native GAIN toggle, reused as SYN ON and INSRT */
/* S9a: MIDI Learn. The native TRACK refresh gives each knob and toggle the
 * learn state of its native parameter: 0x08123d00(session, &cell, id, info)
 * fills learn on / mapped / armed and the "ch/cc" text, 0x08127f7c (knob) or
 * 0x08127076 (toggle) shows it on the widget's learn overlay. A widget
 * rebound to a synth or insert id is refreshed again for that id, so Learn
 * outlines it, a tap arms that id (the widget posts 0x13e with its own cell
 * and id) and its mapped CC shows. id 0 hides the overlay. */
static void learn_bind(uint8_t *w,unsigned ch,unsigned id,unsigned toggle) {
    uint8_t info[32];
    for(unsigned i=0;i<sizeof info;i++)info[i]=0;
    uint16_t cell=(uint16_t)ch;
    if(id && ch<12)((int (*)(void *,uint16_t *,unsigned,void *))0x08123d01u)((void *)0x300173d0u,&cell,id,info);
    ((void (*)(void *,void *))(toggle?0x08127077u:0x08127f7du))(w,info);
}
/* A native toggle w takes the place of the knob at `at`, bound to an id. */
static void toggle_bind(uint8_t *w,uint8_t *at,unsigned ch,unsigned id,unsigned enabled,unsigned show,const char *on,const char *off) {
    /* S8e: inset into the knob cell like the stock REC / MUTE / SOLO /
     * SIDECHAIN grid (1 px right gap, lower row 2 px and upper row 1 px off
     * the bottom), so the page background shows as the same thin border. */
    const int *cell=(const int *)(at+4);
    int lift=cell[1]<=1?2:1;
    int rect[4]={cell[0],cell[1]+lift,cell[2]-1,cell[3]-lift};
    (*(void (***)(void *,int *))w)[0x20/4](w,rect);
    *(uint16_t *)(w+0xf8)=(uint16_t)ch;
    *(uint16_t *)(w+0xfa)=(uint16_t)id;w[0xfc]=0;
    w[0x9c]=1;w[0x30]=(uint8_t)!show;
    ((void (*)(void *,unsigned))0x08127061u)(w,enabled);
    ((void (*)(void *,const char *))0x0813820du)(w,enabled?on:off);
    learn_bind(w,ch,show && id!=INS_GROUP?id:0,1); /* DRV / FLTR picks a type group, not a value */
}
static void toggle_button_bind(uint8_t *p,uint8_t *at,unsigned ch,unsigned id,unsigned enabled,unsigned show,const char *on,const char *off) {
    toggle_bind(p+SYN_BUTTON_OFFSET,at,ch,id,enabled,show,on,off);
}
/* S8d: the TRACK page has no spare native button for DRV / FLTR, so one more
 * native toggle is built with the page (once per boot, from the permanent
 * arena) and linked as its child. Its address lives in free backup SRAM
 * after the FX UI state; it is rewritten at every boot before any use. */
struct insert_group_ui {uint32_t magic;uint8_t *page,*button;
    /* F4/F6: knobs 1..3 showing a value until their caption returns (bit i,
     * due time per knob), so several CC-moved knobs each return. Bit 4 /
     * due[3]: the synth's Seed knob (slot 7, 1.3 dev). */
    uint32_t caption_mask;uint32_t caption_due[4];};
#define INS_GROUP_UI ((struct insert_group_ui *)0x38800c00u)
#define INS_GROUP_MAGIC 0x49475231u
_Static_assert(0xb00+sizeof(struct fx_ui)<=0xc00,"FX UI state below the insert group button");
_Static_assert(0xc00+sizeof(struct insert_group_ui)<=0xc20,"insert group button below the synth resync state");
void *insert_track_construct(uint8_t *p) {
    void *result=((void *(*)(void *))0x081323b9u)(p);
    backup_enable();PWR_CR1|=1u<<8;
    struct insert_group_ui *g=INS_GROUP_UI;g->magic=0;g->caption_mask=0;
    FX1_PAGE->magic=0;   /* G19: the FX1 list page record starts clear after every boot */
    uint8_t *w=((uint8_t *(*)(unsigned,unsigned))0x08103129u)(0x1d4u,1);
    if(!w)return result;
    for(unsigned i=0;i<0x1d4u;i++)w[i]=0;
    /* As the native constructor builds its GAIN toggle. */
    ((void (*)(void *))0x08126fc1u)(w);
    ((void (*)(void *,const char *))0x0813820du)(w,"DRV");
    w[0x88]=8;w[0xf4]=8;*(uint16_t *)(w+0xfa)=INS_GROUP;
    ((void (*)(void *,unsigned))0x081382b1u)(w,5);
    ((void (*)(void *,void *))0x0813a345u)(p,w);
    w[0x30]=1;
    g->page=p;g->button=w;g->magic=INS_GROUP_MAGIC;
    return result;
}
static uint8_t *ins_group_button(uint8_t *p) {
    backup_enable();PWR_CR1|=1u<<8;
    struct insert_group_ui *g=INS_GROUP_UI;
    return g->magic==INS_GROUP_MAGIC && g->page==p?g->button:0;
}
static void synth_button_bind(uint8_t *p,unsigned ch,unsigned enabled,unsigned show) {
    toggle_button_bind(p,synth_knob(p,0),ch,SYN_ON,enabled,show,"SYN ON","SYN OFF");
}
static void synth_button_restore(uint8_t *p) {
    uint8_t *w=p+SYN_BUTTON_OFFSET;
    unsigned id=*(uint16_t *)(w+0xfa);
    if(id!=SYN_ON && id!=INS_ON)return;
    *(uint16_t *)(w+0xfa)=0x173u;w[0xfc]=0;
    ((void (*)(void *,const char *))0x0813820du)(w,"GAIN");
    /* Restore all original layout/visibility choices, including native GAIN. */
    ((void (*)(void *))0x08132ed5u)(p);
}
/* active: 0 normal, 1 inserts, 2 synth controls, 3 synth B setup, 4 (S25)
 * the third TRACK page: only the eighth knob, with Gate or OUT3 (whichever
 * page 1 does not show; OUT2 Mode Mono only).
 * The hardware TRACK key normally toggles normal/Sidechain (action 22).
 * Add Synth after Sidechain; another TRACK press returns to normal. */
#define INS_EXTRA 4u
#define SYN_UI(a) ((a)==2u || (a)==3u)
static unsigned ins_extra_available(void) {return gate_out3_live();}
void synth_track_button_action(uint8_t *app,unsigned requested,unsigned a,unsigned b) {
    struct insert_ui *ui=insert_ui();unsigned current=app[0x739a];
    if(current==22 && !SYN_UI(ui->active)) {
        ui->active=2;ui->page=0;
        ui->group=0;requested=5; // always enter the first, left-hand group
    }
    else if(SYN_UI(ui->active)) {ui->active=0;requested=5;}
    else if(current!=5 && current!=6)ui->active=0;
    ((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)(app,requested,a,b);
}
/* Combo: the DJ view (dj_m4.c) leaves the insert/synth pages when it opens. */
void insert_ui_leave(void) {struct insert_ui *ui=insert_ui();ui->active=0;ui->page=0;ui->group=0;}
void synth_track_b_action(uint8_t *app,unsigned requested,unsigned a,unsigned b) {
    struct insert_ui *ui=insert_ui();
    if(SYN_UI(ui->active)) {
        ui->active=ui->active==2?3:2;
        requested=ui->active==3?23:ui->group?6:5;
    }
    ((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)(app,requested,a,b);
}
void synth_send(void *ring,unsigned target,unsigned id,int value);
void insert_knob_text(uint8_t *w,unsigned caption);
static void synth_track_bind(uint8_t *p,unsigned ch,int seed_moved) {
    struct insert_ui *ui=insert_ui();
    static const unsigned ids[2][8]={
        {SYN_ON,SYN_TIMBRE,SYN_HARMONICS,SYN_MORPH,SYN_ATTACK,SYN_CUTOFF,SYN_RELEASE,SYN_RESO}, /* S18: Drone in the B menu */
        /* S13: both LFOs on the left (LFO top, LFO2 bottom), the envelope on
         * the right (Env % and Env Dec top, Env Atk bottom). In a group the
         * even slots are the top row, the odd slots the bottom row. */
        {SYN_LFO_RATE,SYN_LFO2_RATE,SYN_LFO_AMOUNT,SYN_LFO2_AMOUNT,SYN_ENV_AMOUNT,SYN_ENV_ATTACK,SYN_ENV_DECAY,SYN_SEED}
    };
    unsigned enabled=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,SYN_ON)!=0;
    if(!enabled)ui->page=0;
    unsigned page=ui->page&1u;
    for(unsigned i=0;i<8;i++) {
        uint8_t *w=synth_knob(p,i);unsigned id=ids[page][i];
        if(id==SYN_LFO_RATE && SYN_LFO_SYNCED(fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,SYN_LFO_SYNC)))id=SYN_LFO_SYNC;
        if(id==SYN_LFO2_RATE && SYN_LFO_SYNCED(fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,SYN_LFO2_SYNC)))id=SYN_LFO2_SYNC;
        /* S15: Seed (from the B menu) beside the envelope, only for Cluster,
         * the one engine that uses it. */
        if(id==SYN_SEED && !SYN_ENGINE_CLUSTER(fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,SYN_ENGINE)))id=0;
        int show=enabled && id && id!=SYN_ON;
        w[0xf8]=0;
        if(show) {
            int value=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,id);
            ((void (*)(void *,unsigned,int,unsigned))0x08127b85u)(w,id,value,1);
            *(uint16_t *)(w+0x32)=(uint16_t)ch;
            /* The definition labels give "LFO M" and "LFO2" on both LFO2 knobs;
             * captions name the rates by speed (S) and the amounts (A). */
            const char *caption=id==SYN_RESO?"RESO":id==SYN_LFO_RATE?"LFO S":id==SYN_LFO2_RATE?"LFO2 S":id==SYN_LFO2_SYNC?"LFO2 D":id==SYN_LFO2_AMOUNT?"LFO2 A":0;
            if(caption) {
                char *to=(char *)w+0xf8;do{*to++=*caption;}while(*caption++);
                ((void (*)(void *,unsigned))0x081278b1u)(w,1); /* redraw the caption, as ins_captions */
            }
            if((id==SYN_HARMONICS || id==SYN_TIMBRE || id==SYN_MORPH || id==SYN_LFO_AMOUNT || id==SYN_LFO2_AMOUNT || id==SYN_RESO) && (value<=0 || value>=1000)) {
                int position=value<=0?10:1010;
                ((void (*)(void *,int))0x0813b295u)(w+0x78,position);
                *(int *)(w+0x90)=position;
            }
            if(id==SYN_ON || id==SYN_DRONE) {
                const char *label=id==SYN_ON?(enabled?"SYN ON":"SYN OFF"):(value?"DRN ON":"DRN OFF");
                ((void (*)(void *,const char *))0x0813fe4bu)(w+0xa0,label);
            }
        }
        w[0x30]=w[0x2d4]=(uint8_t)!show;
        learn_bind(w,ch,show?id:0,0);
        if(i==7 && show && id==SYN_SEED && seed_moved)insert_knob_text(w,0);
    }
    synth_button_bind(p,ch,enabled,!page);
    ui->rendered=1;
}
static void ins_captions(uint8_t *p,int mode);
static int ins_is_spec(int mode);
static void ins_chord_bind(uint8_t *p,unsigned ch);
/* INSRT is lit when switched On and a type has been chosen (stored type 0 is
 * the fresh-track default: Off by default, see ins_order). */
static unsigned ins_switched_on(unsigned ch) {
    uint8_t *setting=(uint8_t *)0x300173d0u+0xc08+ch*0x30;
    return fw_get(setting,INS_ON)!=0 && fw_get(setting,INS_MODE)!=0;
}
void insert_knob_text(uint8_t *w,unsigned caption);
#define INS_BUTTON_KNOB(p) insert_knob(p,6) /* under Output */
#define INS_GROUP_KNOB(p) insert_knob(p,7)  /* beside INSRT */
static void ins_group_bind(uint8_t *p,unsigned ch);
void insert_track_bind(uint8_t *p,uint8_t *info) {
    struct insert_ui *ui=insert_ui();
    if(ui->rendered)for(unsigned i=0;i<8;i++) {
        uint8_t *w=insert_knob(p,i);w[0x30]=w[0x2d4]=0;
    }
    /* Native VOL/GAIN/PAN override captions are set once by construction,
     * not recreated by native binding. Restore them before normal binding. */
    static const char *const native_labels[]={"VOL","GAIN","PAN"};
    for(unsigned i=0;i<3;i++) {
        char *to=(char *)insert_knob(p,i)+0xf8;const char *from=native_labels[i];
        do {*to++=*from;}while(*from++);
    }
    /* Synth captions ("LFO2 A" on CUE, "RESO" on the Gate knob) must not
     * outlive the synth pages. */
    for(unsigned i=3;i<8;i++) {
        char *c=(char *)insert_knob(p,i)+0xf8;
        if((c[0]=='L' && c[1]=='F' && c[2]=='O') || (c[0]=='R' && c[1]=='E' && c[2]=='S' && c[3]=='O'))c[0]=0;
    }
    if(!SYN_UI(ui->active) || *(uint16_t *)(info+4)>=12)synth_button_restore(p);
    uint8_t *group=ins_group_button(p);
    if(group)group[0x30]=1;
    if(ui->active==INS_EXTRA && !ins_extra_available())ui->active=0; /* Stereo or Clk Out R: no third page */
    unsigned extra=ui->active==INS_EXTRA;
    unsigned ch=*(uint16_t *)(info+4);
    /* F6: a mapped CC (or a scene) changes a value on the shown track and
     * the page rebinds every knob (event 0x66), always with its caption. A
     * SPEC Shift / Pitch / Decay whose value moved on the same track, id and
     * type shows the value, as when turning (insert_knob_text, caption 0). */
    int moved[3]={0,0,0};
    if(ui->rendered && !extra && ui->active && !SYN_UI(ui->active) && ch<12) {
        uint8_t *t=insert_knob(p,0);
        int mode=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,INS_MODE);
        if(ins_is_spec(mode) && *(uint16_t *)(t+0x32)==ch && *(uint16_t *)(t+0x94)==INS_MODE && *(int *)(t+0x98)==mode)
            for(unsigned i=1;i<4;i++) {
                uint8_t *w=insert_knob(p,i);unsigned id=INS_MODE+i;
                moved[i-1]=*(uint16_t *)(w+0x32)==ch && *(uint16_t *)(w+0x94)==id &&
                    *(int *)(w+0x98)!=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,id);
            }
    }
    /* A mapped CC (or a scene) moving the Cluster Seed: the same 0x66
     * rebind; checked here, before the native bind retargets the knob. The
     * synth bind then shows the number (insert_knob_text, caption 0). */
    int seed_moved=0;
    if(ui->rendered && SYN_UI(ui->active) && (ui->page&1u) && ch<12) {
        uint8_t *sk=synth_knob(p,7);
        seed_moved=*(uint16_t *)(sk+0x32)==ch && *(uint16_t *)(sk+0x94)==SYN_SEED && !sk[0x30] &&
            *(int *)(sk+0x98)!=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,SYN_SEED);
    }
    gate_track_bind_page(p,info,extra);
    /* S9c: Gate threshold (OUT3's knob, rebound by gate_track_bind) is
     * learnable on TRACK as on MIXER. */
    if(!ui->active || ch>=12){ui->rendered=0;if(ch<12)learn_bind(p+0xde48,ch,gate_knob8_gate(0)?NG_ID4:0x15fu,0);return;}
    if(extra) {
        /* Third page: the eighth knob alone (Gate or OUT3), the rest hidden. */
        learn_bind(p+0xde48,ch,gate_knob8_gate(1)?NG_ID4:0x15fu,0);
        for(unsigned i=0;i<7;i++){uint8_t *w=insert_knob(p,i);w[0x30]=w[0x2d4]=1;learn_bind(w,ch,0,0);}
        ui->rendered=1;return;
    }
    if(SYN_UI(ui->active)){synth_track_bind(p,ch,seed_moved);return;}
    for(unsigned i=0;i<8;i++) {
        uint8_t *w=insert_knob(p,i);
        if(i<5) {
            w[0xf8]=0; /* use the new parameter caption, not the original knob caption */
            unsigned id=INS_MODE+i;
            int value=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,id);
            ((void (*)(void *,unsigned,int,unsigned))0x08127b85u)(w,id,value,1);
            *(uint16_t *)(w+0x32)=(uint16_t)ch;w[0x30]=w[0x2d4]=0;
            /* The type knob is not offered: a CC would sweep raw stored modes
             * across both groups, past the type order and INSRT rules. */
            learn_bind(w,ch,i?id:0,0);
            if(i>=1 && i<=3 && (value<=0 || value>=1000)) {
                int position=value<=0?10:1010;
                ((void (*)(void *,int))0x0813b295u)(w+0x78,position);
                *(int *)(w+0x90)=position;
            }
        } else {w[0x30]=w[0x2d4]=1;learn_bind(w,ch,0,0);}
    }
    ins_captions(p,fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,INS_MODE));
    for(unsigned i=1;i<4;i++)if(moved[i-1])insert_knob_text(insert_knob(p,i),0);
    ins_chord_bind(p,ch);
    /* Insert On/Off in the empty slot under Output, touch only (an encoder
     * here toggled on jitter). The lit state shows On. */
    unsigned on=ins_switched_on(ch);
    toggle_button_bind(p,INS_BUTTON_KNOB(p),ch,INS_ON,on,1,"INSRT","INSRT");
    ins_group_bind(p,ch);
    ui->rendered=1;
}
/* Observe the actual root TRACK focus selection, including touch changes.
 * TRACK uses native actions 5/6, event 0x8c. MIXER uses actions 2/3 and
 * event 0x7f; never intercept its independent A/0x164 notification path. */
void insert_track_group(uint8_t *p,unsigned group) {
    struct insert_ui *ui=insert_ui();
    if(ui->group!=(group!=0))ui->select_ch=0xffffffffu;
    ui->group=group!=0;
    ((void (*)(void *,unsigned))0x081327cdu)(p,group);
}
/* The DJ view (dj_m4.c: struct dj_ui at 0x38800c80, active at +16) uses the
 * TRACK page with its own knobs; A there only switches the native block. */
static int ins_dj_view(void) {
    volatile uint8_t *d=(volatile uint8_t *)0x38800c80u;
    return *(volatile uint32_t *)d==0x444a5534u && d[16];
}
void insert_track_a_action(uint8_t *app,unsigned requested,unsigned a,unsigned b) {
    if(ins_dj_view()){insert_ui_leave();((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)(app,requested,a,b);return;}
    struct insert_ui *ui=insert_ui();unsigned group=ui->group;
    if(SYN_UI(ui->active)) {
        if(ui->active==3)return;
        unsigned ch=*(uint16_t *)(app+0x7398);
        if(ch>=12 || !fw_get(app+0xc08+ch*0x30,SYN_ON))return;
        /* Native-style focus cycle: page1 left -> page1 right ->
         * page2 left -> page2 right -> page1 left. */
        if(group)ui->page^=1u;
    } else if(ui->active==INS_EXTRA) {
        /* Third page -> page 1 left. */
        ui->active=0;ui->group=0;
        ((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)(app,5,a,b);return;
    } else if(group) {
        /* S25: inserts right -> the third page (right focus: the eighth
         * knob is on the bottom-right encoder) when OUT3 exists. */
        if(ui->active==1 && ins_extra_available()) {
            /* Already on the right focus (6): pass by the left one so the
             * page rebinds with the new state, then back to the right. */
            ui->active=INS_EXTRA;
            ((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)(app,5,a,b);
            ((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)(app,6,a,b);return;
        }
        ui->active=!ui->active;
    }
    ((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)(app,group?5:6,a,b);
}
/* Insert types in encoder order, per group: DRV (CLIP, FOLD, WRAP), FLTR
 * (LP, BP, HP, DJ) and SPEC (SHFT, SPRD, RING frequency shifter; HARM,
 * CHRD, BELL resonator bank). The type encoder steps inside the shown
 * group; the group button cycles DRV -> FLTR -> SPEC. No Off: INSRT switches the insert. Stored 0 (every fresh track,
 * and pre-S8c Off) shows as CLIP not yet switched on; ordinal 2 (legacy SIN)
 * is CLIP. */
static const uint8_t ins_order[3][6]={{1,15,3,4},{5,6,7,8},{9,10,11,12,13,14}}; /* DRV: CLIP, SAT (15, pull request #7), FOLD, WRAP */
static const uint8_t ins_order_n[3]={4,4,6};
static int ins_is_filter(int mode){return mode>=5 && mode<=8;}
static int ins_is_spec(int mode){return mode>=9 && mode<=14;}
static int ins_is_shift(int mode){return mode>=9 && mode<=11;}
static int ins_is_reso(int mode){return mode>=12 && mode<=14;}
static unsigned ins_group_of(int mode){return ins_is_filter(mode)?1u:ins_is_spec(mode)?2u:0u;}
static unsigned ins_order_index(int mode) {
    if(mode==0 || mode==2)mode=1;
    unsigned group=ins_group_of(mode);
    for(unsigned i=0;i<ins_order_n[group];i++)if(ins_order[group][i]==mode)return i;
    return 0;
}
static void ins_commit(unsigned ch,unsigned id,int value) {
    uint16_t track=(uint16_t)ch;
    ((void (*)(void *,uint16_t *,unsigned,int,unsigned))0x08124b45u)((void *)0x300173d0u,&track,id,value,0);
}
/* TRACK knobs draw one text line, written by the native formatter only when
 * it is asked to show the caption (binding; turns leave it): the +0xf8
 * override if set, else the definition name. Filter types caption Tone and
 * Mix as RES and ENV, and Drive always shows the cutoff (insert_knob_text).
 * SPEC types caption Drive SHIFT / PITCH and Tone FDBK / DECAY (Mix stays
 * MIX); Shift, Pitch and Decay show their value while turning, then the
 * caption again (insert_caption_tick). Drive types use the definition names
 * DRIVE / TONE / MIX. */
static void ins_captions(uint8_t *p,int mode) {
    static const char *const filter_caption[3]={"","RES","ENV"};
    static const char *const shift_caption[3]={"SHIFT","FDBK",""};
    static const char *const reso_caption[3]={"PITCH","DECAY",""};
    for(unsigned i=1;i<4;i++) {
        uint8_t *w=insert_knob(p,i);char *to=(char *)w+0xf8;
        const char *from=ins_is_filter(mode)?filter_caption[i-1]:ins_is_shift(mode)?shift_caption[i-1]:
            ins_is_reso(mode)?reso_caption[i-1]:mode==15 && i==2?"CURVE":"";
        do {*to++=*from;}while(*from++);
        insert_knob_text(w,1);
    }
}
/* Picking a filter type opens its cutoff (LP top, HP bottom, BP/DJ centre).
 * Changing family loads that family's defaults for Drive/Tone/Mix, so a drive
 * never inherits a cutoff and Env always starts at centre (off). Entering the
 * shifter starts at +14 Hz, no feedback, half mix; the resonator at C2, 1 s
 * decay, 60% mix. Types inside the shifter or the resonator keep their
 * values; moving between them (same SPEC group, different meanings) loads
 * the other's defaults. */
static void ins_type_defaults(uint8_t *p,unsigned ch,int old,int next) {
    static const int16_t open_cutoff[4]={1000,500,0,500};
    int values[3]={-1,-1,-1};
    if(ins_is_filter(next)) {
        values[0]=open_cutoff[next-5];
        if(!ins_is_filter(old)){values[1]=300;values[2]=500;}
    } else if(ins_is_shift(next)) {
        if(!ins_is_shift(old)){values[0]=750;values[1]=0;values[2]=500;}
    } else if(ins_is_reso(next)) {
        if(!ins_is_reso(old)){values[0]=167;values[1]=547;values[2]=600;} /* C2, ~1 s */
    } else if(next==15) {
        /* SAT: about 11 dB of drive on the Soft curve, fully wet. */
        if(old!=15){values[0]=300;values[1]=0;values[2]=1000;}
    } else if(ins_group_of(old) || old==15) {
        values[0]=350;values[1]=650;values[2]=1000;
    }
    for(unsigned i=0;i<3;i++) {
        if(values[i]<0)continue;
        ins_commit(ch,INS_DRIVE+i,values[i]);
        ((void (*)(void *,unsigned,int,unsigned))0x08127b85u)(insert_knob(p,i+1),INS_DRIVE+i,values[i],1);
    }
    ins_captions(p,next);
    for(unsigned i=1;i<4;i++)((void (*)(void *,unsigned,int,unsigned))0x08127b85u)
        (insert_knob(p,i),INS_DRIVE+i-1,fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,INS_DRIVE+i-1),1);
}
/* Commit a new type from the stored one, as the encoder and group button do. */
static void ins_set_type(uint8_t *p,unsigned ch,int stored,int mode) {
    /* Picking a type on a fresh track (stored 0, INSRT unlit) keeps it Off. */
    if(stored==0 && fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,INS_ON))ins_commit(ch,INS_ON,0);
    /* Commit directly (the same native setter the 0x6e edit event reaches),
     * so the knobs re-formatted below already see the new mode. */
    ins_commit(ch,INS_MODE,mode);
    ((void (*)(void *,unsigned,int,unsigned))0x08127b85u)(insert_knob(p,0),INS_MODE,mode,1);
    ins_type_defaults(p,ch,stored==0 || stored==2?1:stored,mode);
    ins_group_bind(p,ch);
    ins_chord_bind(p,ch);
}
/* F6: CHRD's chord on the free knob beside Output (right page, top right encoder),
 * shown only for CHRD. Its text is always the chord name. */
static void ins_chord_bind(uint8_t *p,unsigned ch) {
    uint8_t *w=insert_knob(p,5);
    if(ch<12 && insert_ui()->active==1 && fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,INS_MODE)==13) {
        w[0xf8]=0;
        int value=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,INS_CHORD);
        ((void (*)(void *,unsigned,int,unsigned))0x08127b85u)(w,INS_CHORD,value,1);
        *(uint16_t *)(w+0x32)=(uint16_t)ch;w[0x30]=w[0x2d4]=0;
        learn_bind(w,ch,INS_CHORD,0);
    } else {w[0x30]=w[0x2d4]=1;learn_bind(w,ch,0,0);}
}
/* One chord per 400 raw encoder units, no acceleration, stops at each end
 * (as the type knob). */
static void ins_chord_turn(uint8_t *w,int delta) {
    unsigned ch=*(uint16_t *)(w+0x32);
    if(ch>=12 || *(uint16_t *)(w+0x94)!=INS_CHORD || w[0x30] || w[0x2d4] || !delta)return;
    struct insert_ui *ui=insert_ui();int old=*(int *)(w+0x98),hi=(int)INS_CHORD_COUNT-1;
    if(old<0)old=0;
    if(old>hi)old=hi;
    unsigned key=ch|(INS_CHORD<<8);int f=ui->select_fraction;
    if(ui->select_ch!=key || ui->select_value!=old || (delta<0 && f>0) || (delta>0 && f<0))f=0;
    ui->select_ch=key;ui->select_value=old;
    if((old==0 && delta<0)||(old==hi && delta>0)){ui->select_fraction=0;return;}
    if(delta>400)delta=400;
    if(delta< -400)delta=-400;
    int total=f+delta,step=total/400,next=old+step;
    ui->select_fraction=total-step*400;
    if(next<0)next=0;
    if(next>hi)next=hi;
    if(next==old)return;
    ins_commit(ch,INS_CHORD,next);
    ((void (*)(void *,unsigned,int,unsigned))0x08127b85u)(w,INS_CHORD,next,1);
    ui->select_value=next;
}
/* Hardware encoder events contain travel, not a single +/-1 choice.
 * Accumulate 400 raw units per mode, with no speed acceleration. Cap each
 * message before accumulation so a burst cannot skip intermediate modes.
 * This follows the native edit/bind path but bypasses enum cursor scaling. */
unsigned insert_mode_turn(uint8_t *p,uint8_t *w,int delta) {
    unsigned ch=*(uint16_t *)(w+0x32);
    if(ch>=12 || *(uint16_t *)(w+0x94)!=INS_MODE || w[0x30] || w[0x2d4] || !delta)return 0;
    struct insert_ui *ui=insert_ui();int stored=*(int *)(w+0x98);
    unsigned group=ins_group_of(stored);
    int old=(int)ins_order_index(stored),last=(int)ins_order_n[group]-1;
    int f=ui->select_fraction;
    if(ui->select_ch!=ch || ui->select_value!=stored ||
       (delta<0 && f>0) || (delta>0 && f<0))f=0;
    ui->select_ch=ch;ui->select_value=stored;
    if((old==0 && delta<0)||(old==last && delta>0)) {ui->select_fraction=0;return 0;}
    if(delta>400)delta=400;
    if(delta< -400)delta=-400;
    int total=f+delta,step=total/400,next=old+step;
    ui->select_fraction=total-step*400;
    if(next<0)next=0;
    if(next>last)next=last;
    if((next==0 && total<0)||(next==last && total>0))ui->select_fraction=0;
    if(next==old)return 0;
    ins_set_type(p,ch,stored,ins_order[group][next]);
    ui->select_value=*(int *)(w+0x98);
    return 1;
}
/* The group button cycles DRV (to CLIP) -> FLTR (to LP) -> SPEC (to SHFT). */
static void ins_group_bind(uint8_t *p,unsigned ch) {
    static const char *const names[3]={"DRV","FLTR","SPEC"};
    uint8_t *w=ins_group_button(p);
    if(!w)return;
    const char *name=names[ins_group_of(fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,INS_MODE))];
    toggle_bind(w,INS_GROUP_KNOB(p),ch,INS_GROUP,0,1,name,name);
}
static void ins_group_tap(uint8_t *p,unsigned ch) {
    static const uint8_t next_first[3]={5,9,1};
    if(ch>=12)return;
    int stored=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,INS_MODE);
    insert_ui()->select_ch=0xffffffffu;
    ins_set_type(p,ch,stored,next_first[ins_group_of(stored)]);
}
/* Drive's text in filter modes, after the native knob formatter (all six of
 * its call sites go through here): Cutoff in Hz, DJ as OFF / LP / HP, also
 * while turning. Other knobs are untouched. */
static char *ins_put_uint(char *t,unsigned v) {
    char d[10];unsigned n=0;
    do {d[n++]=(char)('0'+v%10u);v/=10u;}while(v && n<10);
    while(n)*t++=d[--n];
    return t;
}
static char *ins_put_hz(char *t,float hz) {
    unsigned v=(unsigned)(hz+.5f);
    if(v<1000u)return ins_put_uint(t,v);
    unsigned c=(v+50u)/100u;
    if(c<100u){t=ins_put_uint(t,c/10u);*t++='.';t=ins_put_uint(t,c%10u);}
    else t=ins_put_uint(t,(v+500u)/1000u); /* 10K, not 10.0K */
    *t++='K';return t;
}
static float ins_ui_exp2(float x) {
    int e=(int)x;if((float)e>x)e--;
    float f=x-(float)e,p=1.f+f*(.69314718f+f*(.24022651f+f*(.05550411f+f*.00961813f)));
    union{float f;uint32_t u;} v={p};v.u+=(uint32_t)e<<23;return v.f;
}
/* F4: SPEC Shift / Pitch / Decay show their value while turning (the edit
 * paths format with caption 0), and the caption again ~1 s after the last
 * turn; a rebinding in that second keeps the value. */
#define INS_CAPTION_MS 1000u
static struct insert_group_ui *ins_caption_state(void) {
    backup_enable();PWR_CR1|=1u<<8;
    return INS_GROUP_UI->magic==INS_GROUP_MAGIC?INS_GROUP_UI:0;
}
/* Caption slot k (1..3 insert Drive/Tone/Mix, 4 synth Seed) -> its knob. */
static uint8_t *ins_caption_knob(uint8_t *p,unsigned k) {return insert_knob(p,k==4u?7u:k);}
static const char *const ins_note_names[12]={"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
void insert_knob_text(uint8_t *w,unsigned caption) {
    ((void (*)(void *,unsigned))0x081278b1u)(w,caption);
    unsigned id=*(uint16_t *)(w+0x94),ch=*(uint16_t *)(w+0x32);
    if(ch<12 && id==SYN_SEED) {
        /* Seed (Cluster) reads SEED at rest and its number while turning or
         * moved by a CC, back to SEED ~1 s later, like the SPEC knobs. */
        struct insert_group_ui *g=ins_caption_state();
        uint32_t now=*(volatile uint32_t *)0x3001eb3cu;
        int mine=g && ins_caption_knob(g->page,4)==w;
        if(caption) {
            if(mine && (g->caption_mask>>4&1u) && (int32_t)(now-g->caption_due[3])<0)return; /* just turned: keep the number */
            ((void (*)(void *,const char *))0x0813fe4bu)(w+0xa0,"SEED");
        } else if(mine){g->caption_mask|=1u<<4;g->caption_due[3]=now+INS_CAPTION_MS;}
        return;
    }
    if(ch<12 && id==INS_CHORD) {
        static const char *const chords[INS_CHORD_COUNT]={"MIN9","MAJ9","MIN7","MAJ7","SUS","5THS","4THS","DIM"};
        int v=*(int *)(w+0x98);
        ((void (*)(void *,const char *))0x0813fe4bu)(w+0xa0,chords[v>=0 && v<(int)INS_CHORD_COUNT?v:0]);
        return;
    }
    if(ch>=12 || id<INS_DRIVE || id>INS_MIX)return;
    int mode=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,INS_MODE);
    if(ins_is_filter(mode) ? id!=INS_DRIVE : !ins_is_spec(mode))return;
    if(ins_is_spec(mode)) {
        /* F6: all three SPEC knobs (also FDBK and MIX, which keep the
         * native value text) go back to their caption ~1 s after a change. */
        struct insert_group_ui *g=ins_caption_state();
        uint32_t now=*(volatile uint32_t *)0x3001eb3cu;
        unsigned k=0;
        if(g)for(unsigned i=1;i<4;i++)if(insert_knob(g->page,i)==w)k=i;
        if(caption) {
            /* Binding: the caption (already drawn), unless this knob was
             * just turned. */
            if(!k || !(g->caption_mask>>k&1u) || (int32_t)(now-g->caption_due[k-1])>=0)return;
        } else if(k){g->caption_mask|=1u<<k;g->caption_due[k-1]=now+INS_CAPTION_MS;}
    }
    int v=*(int *)(w+0x98);
    char text[16],*t=text;
    if(id==INS_MIX || (id==INS_TONE && ins_is_shift(mode))) {
        /* F6: SPEC FDBK and MIX in percent (0..1000 = 0..100%). */
        t=ins_put_uint(t,(unsigned)((v<0?0:v)+5)/10u);*t++='%';
    } else if(ins_is_reso(mode) && id==INS_TONE) {
        /* Decay T60: 50MS .. 950MS, 1.0S .. 12S (same curve as the DSP). */
        float s=.05f*ins_ui_exp2((float)v*(7.9068906f*.001f));
        if(s<.995f)t=ins_put_uint(t,(unsigned)(s*1000.f+.5f)),*t++='M';
        else if(s<9.95f){unsigned c=(unsigned)(s*10.f+.5f);t=ins_put_uint(t,c/10u);*t++='.';t=ins_put_uint(t,c%10u);}
        else t=ins_put_uint(t,(unsigned)(s+.5f));
        *t++='S';
    } else if(ins_is_reso(mode)) {
        /* Pitch: note name, C1..C7 in semitones (same steps as the DSP). */
        int note=24+(int)((float)v*(72.f/1000.f)+.5f);
        const char *name=ins_note_names[note%12];
        while(*name)*t++=*name++;
        t=ins_put_uint(t,(unsigned)(note/12-1));
    } else if(ins_is_shift(mode)) {
        /* Shift in Hz: 0 in the centre dead zone, else signed (RING shows
         * the magnitude), one decimal below 10 Hz. Same curve as the DSP. */
        int d=v-500;
        float u=(float)(d<0?-d:d)/500.f-.02f;
        if(u<=0.f)*t++='0';
        else {
            float hz=.1f*ins_ui_exp2(u*(14.6f/.98f));
            if(mode!=11)*t++=d<0?'-':'+';
            if(hz<9.95f){unsigned c=(unsigned)(hz*10.f+.5f);t=ins_put_uint(t,c/10u);*t++='.';t=ins_put_uint(t,c%10u);}
            else t=ins_put_hz(t,hz);
        }
    } else if(mode==8) {
        int d=v-500;
        if(d>=-10 && d<=10){*t++='O';*t++='F';*t++='F';}
        else {
            float u=((float)(d<0?-d:d)/500.f-.02f)/.98f;
            *t++=d<0?'L':'H';*t++='P';*t++=' ';
            t=ins_put_hz(t,d<0?20000.f*ins_ui_exp2(u*-8.643856f):20.f*ins_ui_exp2(u*8.965784f));
        }
    } else t=ins_put_hz(t,20.f*ins_ui_exp2((float)v*.009965784f));
    *t=0;
    ((void (*)(void *,const char *))0x0813fe4bu)(w+0xa0,text);
}
/* From the periodic UI tick: once the last turn is ~1 s old, the knob
 * shows its caption again (native caption formatting, same widget). */
void insert_caption_tick(uint8_t *pages) {
    struct insert_group_ui *g=ins_caption_state();
    if(!g || !g->caption_mask)return;
    uint32_t now=*(volatile uint32_t *)0x3001eb3cu;
    for(unsigned k=1;k<5;k++) {
        if(!(g->caption_mask>>k&1u) || (int32_t)(now-g->caption_due[k-1])<0)continue;
        g->caption_mask&=~(1u<<k);
        insert_knob_text(ins_caption_knob(g->page,k),1); /* native caption, then SEED for the Seed knob */
        pages[0x38]=pages[0x39]=1;
    }
}

static void synth_turn(uint8_t *w,int delta) {
    unsigned id=*(uint16_t *)(w+0x94),ch=*(uint16_t *)(w+0x32);
    /* S23: Attack and Release are log controls too (1..2000, 5..5000 ms). */
    if(id==SYN_CUTOFF || id==SYN_LFO_RATE || id==SYN_LFO2_RATE || id==SYN_ENV_DECAY || id==SYN_ENV_ATTACK ||
       id==SYN_ATTACK || id==SYN_RELEASE) {
        if(!delta || ch>=12)return;
        struct insert_ui *ui=insert_ui();int old=*(int *)(w+0x98);
        unsigned key=ch|(id<<8);int f=ui->select_fraction;
        if(ui->select_ch!=key || ui->select_value!=old || (delta<0 && f>0) || (delta>0 && f<0))f=0;
        ui->select_ch=key;ui->select_value=old;
        int lo=id==SYN_CUTOFF?20:id==SYN_LFO_RATE || id==SYN_LFO2_RATE?10:id==SYN_ENV_ATTACK?0:id==SYN_ATTACK?1:5;
        int hi=id==SYN_ATTACK?2000:(id==SYN_ENV_DECAY || id==SYN_ENV_ATTACK || id==SYN_RELEASE)?5000:20000;
        if(old<lo)old=lo;
        if(old>hi)old=hi;
        int most=1000;
        {
            /* S21 Env Dec, S22 all five log controls (S15's LFO rate
             * speed-up replaced): the native knob acceleration HARM gets
             * (0x08127d50): the knob's history of the last 8 turns and their
             * times (+0x34) gives a 1..6x multiplier, 1 for slow turns. */
            void *history=w+0x34;
            ((void (*)(void *,int,uint32_t))0x0813b7e9u)(history,delta,*(volatile uint32_t *)0x3001eb3cu);
            delta*=((int (*)(void *))0x0813b805u)(history);
            most=40000;                     /* whole travel, as native */
        }
        if(delta>most)delta=most;
        if(delta< -most)delta=-most;
        int total=f+delta,step=total/40,next=old;
        ui->select_fraction=total-step*40;
        if(step) {
            int offset=id==SYN_ENV_ATTACK?SYN_ENV_ATTACK_OFFSET:id==SYN_ATTACK?SYN_ATTACK_OFFSET:id==SYN_LFO_RATE || id==SYN_LFO2_RATE?SYN_LFO_RATE_OFFSET:0;
            float span=lg2((float)(hi+offset)/(float)(lo+offset));
            float pos=1000.f*lg2((float)(old+offset)/(float)(lo+offset))/span+(float)step;
            if(pos<0.f)pos=0.f;
            if(pos>1000.f)pos=1000.f;
            next=(int)((float)(lo+offset)*ex2(pos*span*.001f)+.5f)-offset;
            if(next==old)next+=step>0?1:-1;
        }
        if(next<lo)next=lo;
        if(next>hi)next=hi;
        if((next==hi && total>0)||(next==lo && total<0))ui->select_fraction=0;
        if(next==old)return;
        uint32_t ev[6]={0x6e,0x08148664u,ch,id,(uint32_t)next,0};
        ((void (*)(void *,void *))0x0813a415u)(w,ev);
        ((void (*)(void *,unsigned,int,unsigned))0x08127b85u)(w,id,next,1);
        ui->select_value=next;return;
    }
    unsigned kind=id==SYN_LFO2_SYNC?SYN_LFO_SYNC:id; /* LFO2's division steps like LFO1's */
    if(kind!=SYN_ON && kind!=SYN_ENGINE && kind!=SYN_CHANNEL && kind!=SYN_DRONE && kind!=SYN_LFO_DEST && kind!=SYN_ENV_DEST && kind!=SYN_LFO_SHAPE && kind!=SYN_LFO_SYNC && kind!=SYN_SEED) {
        insert_ui()->select_ch=0xffffffffu;
        /* S14: LFO amounts speed up on fast turns. Up to 40 raw units (a
         * slow turn) stays one step per 40 units; faster turns multiply up
         * to 6x (S15; 8x in S14) on top of the native acceleration. */
        /* S23: no extra scaling; the native acceleration alone, as HARM. */
        ((unsigned (*)(void *,int))0x08127d51u)(w,delta);return;
    }
    if(!delta || ch>=12)return;
    struct insert_ui *ui=insert_ui();int old=*(int *)(w+0x98);
    /* F2: an older project's engine ordinal (2..4) steps as VA / Cluster. */
    if(kind==SYN_ENGINE)old=SYN_ENGINE_CLUSTER(old)?1:0;
    /* The sync encoder walks divisions only; Free is chosen in the B menu. */
    int lo=kind==SYN_CHANNEL || kind==SYN_LFO_SYNC?1:0,hi=kind==SYN_CHANNEL?16:kind==SYN_ENGINE?(int)SYN_ENGINE_COUNT-1:kind==SYN_LFO_SHAPE?(int)SYN_LFO_SHAPE_COUNT-1:kind==SYN_LFO_SYNC?(int)SYN_LFO_DIVISIONS:(kind==SYN_LFO_DEST || kind==SYN_ENV_DEST)?(int)SYN_DEST_COUNT-1:kind==SYN_SEED?(int)SYN_SEED_COUNT-1:1;
    unsigned key=ch|(id<<8);int f=ui->select_fraction;
    if(ui->select_ch!=key || ui->select_value!=old || (delta<0 && f>0) || (delta>0 && f<0))f=0;
    ui->select_ch=key;ui->select_value=old;
    if((old<=lo && delta<0)||(old>=hi && delta>0)){ui->select_fraction=0;return;}
    if(delta>400)delta=400;
    if(delta< -400)delta=-400;
    int total=f+delta,step=total/400,next=old+step;
    ui->select_fraction=total-step*400;
    if(next<lo)next=lo;
    if(next>hi)next=hi;
    if(next==old)return;
    uint32_t ev[6]={0x6e,0x08148664u,ch,id,(uint32_t)next,0};
    ((void (*)(void *,void *))0x0813a415u)(w,ev);
    ((void (*)(void *,unsigned,int,unsigned))0x08127b85u)(w,id,next,1);
    if(id==SYN_SEED)insert_knob_text(w,0); /* the number while turning, SEED ~1 s later */
    ui->select_value=next;
}
unsigned insert_track_event(uint8_t *p,uint32_t *ev) {
    struct insert_ui *ui=insert_ui();
    if(SYN_UI(ui->active) && *(uint16_t *)(p+0xbc9c)<12 && (uint16_t)ev[0]==0x32) {
        int encoder=(int16_t)ev[3];
        unsigned i=(*(uint32_t *)(p+0xebbc)?4u:0u)+(unsigned)encoder;
        if(encoder>=0 && encoder<4 && i<8) {
            uint8_t *w=synth_knob(p,i);
            if(!w[0x30] && !w[0x2d4])synth_turn(w,(int16_t)ev[4]);
        }
        return 0;
    }
    if(ui->active==1 && *(uint16_t *)(p+0xbc9c)<12 && (uint16_t)ev[0]==0x32) {
        int encoder=(int16_t)ev[3];
        unsigned ch=*(uint16_t *)(p+0xbc9c);
        if(ch<12 && encoder>=0 && encoder<4) {
            unsigned group=*(uint32_t *)(p+0xebbc)!=0;
            unsigned i=group?4u+(unsigned)encoder:(unsigned)encoder;
            int delta=(int16_t)ev[4];
            if(i==0)insert_mode_turn(p,insert_knob(p,0),delta);
            /* F7: the chord knob (slot 5) is drawn top right, which is the
             * right page's third encoder (0/2 top row, 1/3 bottom row). */
            else if(i==6)ins_chord_turn(insert_knob(p,5),delta);
            else if(i<5) {
                ui->select_ch=0xffffffffu;
                ((unsigned (*)(void *,int))0x08127d51u)(insert_knob(p,i),delta);
            }
        }
        return 0;
    }
    return ((unsigned (*)(void *,void *))0x08133195u)(p,ev);
}
unsigned insert_track_local(uint8_t *p,uint32_t *ev) {
    unsigned type=(uint16_t)ev[0];
    if(type==0x16a && SYN_UI(insert_ui()->active) && SYN_EDITABLE(ev[3])) {
        uint32_t focus[6];for(unsigned i=0;i<6;i++)focus[i]=ev[i];
        unsigned id=ev[3];
        unsigned right=id==SYN_ATTACK || id==SYN_CUTOFF || id==SYN_RELEASE || id==SYN_DRONE || id==SYN_RESO ||
            id==SYN_ENV_ATTACK || id==SYN_ENV_AMOUNT || id==SYN_ENV_DECAY || id==SYN_SEED;
        focus[3]=right?0xda:4;
        return gate_track_local(p,focus);
    }
    if(type==0x16a && insert_ui()->active==1 && ((ev[3]>=INS_MODE && ev[3]<=INS_OUTPUT) || ev[3]==INS_ON || ev[3]==INS_GROUP || ev[3]==INS_CHORD)) {
        /* Let native focus routing see a member of the corresponding native
         * bank. The original parameter event remains untouched. */
        uint32_t focus[6];for(unsigned i=0;i<6;i++)focus[i]=ev[i];
        focus[3]=ev[3]==INS_OUTPUT || ev[3]==INS_ON || ev[3]==INS_GROUP || ev[3]==INS_CHORD?0xda:4;
        return gate_track_local(p,focus);
    }
    if(type==0x6e || type==0x3c || type==0x3d) {
        unsigned id=type==0x6e?ev[3]:ev[4];uint16_t ch=(uint16_t)ev[2];
        if(id==INS_GROUP){if(insert_ui()->active==1)ins_group_tap(p,ch);return 0;}
        if(((id>=INS_MODE && id<=INS_OUTPUT)||id==INS_ON||id==INS_CHORD||SYN_EDITABLE(id)) && ch<12) {
            int value=(int)(type==0x6e?ev[4]:ev[3]);
            /* Switching a fresh track On starts the CLIP it shows. */
            if(id==INS_ON && value && !fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,INS_MODE))ins_commit(ch,INS_MODE,1);
            ((void (*)(void *,uint16_t *,unsigned,int,unsigned))0x08124b45u)
                ((void *)0x300173d0u,&ch,id,value,0);
            return 0;
        }
    }
    return gate_track_local(p,ev);
}

/* Reuse the real Sidechain settings list, including its row selection,
 * touch handling, native enum text and encoder editing. Only the list's
 * contents and synth edits differ while the synth B menu is active. */
void synth_menu_populate(uint8_t *p,short *slot) {
    unsigned ch=*(uint16_t *)(p+0xbc9c);
    if(insert_ui()->active!=3 || ch>=12) {
        ((void (*)(void *,short *))0x0812894du)(p,slot);return;
    }
    uint32_t list[148];
    ((void (*)(void *))0x081384abu)(list);
    static const unsigned ids[]={SYN_ENGINE,SYN_CHANNEL,SYN_DRONE,SYN_ENV_DEST,SYN_LFO_DEST,SYN_LFO_SHAPE,SYN_LFO_MODE,
        SYN_LFO2_DEST,SYN_LFO2_SHAPE,SYN_LFO2_MODE}; /* S15: Seed moved to page 2 */
    for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++) {
        unsigned mode=ids[i]==SYN_LFO_MODE || ids[i]==SYN_LFO2_MODE;
        int value=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,ids[i]==SYN_LFO_MODE?SYN_LFO_SYNC:ids[i]==SYN_LFO2_MODE?SYN_LFO2_SYNC:ids[i]);
        fw_append(list,ids[i],mode?SYN_LFO_SYNCED(value):value);
    }
    ((void (*)(void *,void *))0x0813da51u)(p+0xbd38,list);
    ((void (*)(void *,void *))0x0813d6b1u)(p+0xbd38,p+0xbc98);
    /* Native redraw polls Sidechain source even for this shared list. Keep
     * its snapshot in sync so the poll cannot rebuild and reset selection. */
    *(int *)(p+0xbcc4)=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,0x23);
}
unsigned synth_menu_local(uint8_t *p,uint32_t *ev) {
    unsigned type=(uint16_t)ev[0];unsigned ch=*(uint16_t *)(p+0xbc9c);
    /* A native enum can deliver its final edit after B has changed the active
     * view. Commit our explicit keys even then, using the menu's bound track. */
    if(ch<12 && (type==0x3c || type==0x3d || type==0x6e)) {
        unsigned id=type==0x6e?ev[3]:ev[4];
        if(id==SYN_ENGINE || id==SYN_CHANNEL || id==SYN_DRONE || id==SYN_LFO_DEST || id==SYN_ENV_DEST || id==SYN_LFO_SHAPE || id==SYN_LFO_SYNC || id==SYN_LFO_MODE || id==SYN_SEED ||
           id==SYN_LFO2_DEST || id==SYN_LFO2_SHAPE || id==SYN_LFO2_SYNC || id==SYN_LFO2_MODE) {
            int value=(int)(type==0x6e?ev[4]:ev[3]);
            unsigned kind=id==SYN_LFO2_DEST?SYN_LFO_DEST:id==SYN_LFO2_SHAPE?SYN_LFO_SHAPE:id==SYN_LFO2_SYNC?SYN_LFO_SYNC:id==SYN_LFO2_MODE?SYN_LFO_MODE:id;
            int lo=kind==SYN_CHANNEL?1:0,hi=kind==SYN_CHANNEL?16:kind==SYN_ENGINE?(int)SYN_ENGINE_COUNT-1:kind==SYN_LFO_SHAPE?(int)SYN_LFO_SHAPE_COUNT-1:kind==SYN_LFO_SYNC?(int)SYN_LFO_SYNC_MAX:kind==SYN_LFO_MODE || kind==SYN_DRONE?1:kind==SYN_SEED?SYN_SEED_COUNT-1:(int)SYN_DEST_COUNT-1;
            if(value<lo)value=lo;
            if(value>hi)value=hi;
            uint16_t track=(uint16_t)ch;
            if(kind==SYN_LFO_MODE) {
                /* Free/Sync keeps the division: Free stores it as 13..24. */
                unsigned sync=id==SYN_LFO2_MODE?SYN_LFO2_SYNC:SYN_LFO_SYNC;
                int stored=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,sync);
                int division=SYN_LFO_DIVISION(stored);
                ((void (*)(void *,uint16_t *,unsigned,int,unsigned))0x08124b45u)
                    ((void *)0x300173d0u,&track,sync,value?division:division+(int)SYN_LFO_DIVISIONS,0);
            }
            ((void (*)(void *,uint16_t *,unsigned,int,unsigned))0x08124b45u)
                ((void *)0x300173d0u,&track,id,value,0);
            return 0;
        }
        if(insert_ui()->active==3)return 0; /* Ignore unrelated Sidechain edits only inside Synth B. */
    }
    return ((unsigned (*)(void *,void *))0x081290f9u)(p,ev);
}
void synth_menu_title(const char *title,int *rect,unsigned color,void *surface) {
    if(insert_ui()->active==3)title="Synth";
    ((void (*)(const char *,int *,unsigned,void *))0x08136aa5u)(title,rect,color,surface);
}

/* S7a: re-send each enabled synth track's saved values to the audio engine,
 * a few per display frame. Project load rebuilds the M7 graph and not every
 * synth value reaches the fresh voices; repeating a value the DSP already
 * holds is a no-op there. Same IPC call as the native setter, without
 * marking the project dirty. Waits a few seconds after boot.
 * S9a: a project load runs in the PresetMgr task, which writes the same
 * M4->M7 command ring (0x08108784: no lock, no full check) while this runs in
 * the UI task. So nothing is sent while a load is pending (PresetMgr's
 * pending-load name 0x3001734c, LoadBank's own busy test) or for
 * SYN_RESYNC_HOLD_MS after it, and never while the ring holds more than
 * SYN_RESYNC_RING_MAX unread commands (write +0x1804, M7 read +0x1800).
 * State moved from 0x1b8 (an M7-cached line beside the synth registry) to an
 * M4-only line after the insert group button. */
struct synth_resync {uint32_t magic,cursor,load_ms,loading;};
#define SYN_RESYNC ((volatile struct synth_resync *)0x38800c20u)
#define SYN_RESYNC_MAGIC 0x53525332u
#define SYN_RESYNC_PER_FRAME 4u
#define SYN_RESYNC_START_MS 4000u
#define SYN_RESYNC_HOLD_MS 1000u
#define SYN_RESYNC_RING_MAX 32u
_Static_assert(0xc20+sizeof(struct synth_resync)<=0xc40,"resync state in its own line after the insert group button");
static int synth_load_pending(void) {
    const char *name=*(const char *const *)0x3001734cu;
    return name && *name;
}
void synth_resync_tick(void) {
    static const uint16_t ids[]={SYN_ON,SYN_ENGINE,SYN_CHANNEL,SYN_HARMONICS,SYN_TIMBRE,SYN_MORPH,
        SYN_ATTACK,SYN_RELEASE,SYN_DRONE,SYN_CUTOFF,SYN_LFO_RATE,SYN_LFO_DEST,SYN_LFO_AMOUNT,
        SYN_ENV_DEST,SYN_ENV_AMOUNT,SYN_ENV_DECAY,SYN_ENV_ATTACK,SYN_LFO_SHAPE,SYN_LFO_SYNC,SYN_SEED,
        SYN_LFO2_RATE,SYN_LFO2_DEST,SYN_LFO2_AMOUNT,SYN_LFO2_SHAPE,SYN_LFO2_SYNC,SYN_RESO,
        3 /* S15: track Gain, which the synth applies itself */};
    const unsigned n=sizeof(ids)/sizeof(ids[0]);
    backup_enable();PWR_CR1|=1u<<8;
    if(SYN_RESYNC->magic!=SYN_RESYNC_MAGIC){
        SYN_RESYNC->cursor=0;SYN_RESYNC->load_ms=0;SYN_RESYNC->loading=0;SYN_RESYNC->magic=SYN_RESYNC_MAGIC;
    }
    uint32_t now=UI_NOW;
    if(synth_load_pending()){SYN_RESYNC->loading=1;SYN_RESYNC->load_ms=now;return;}
    if(SYN_RESYNC->loading){
        if(now-SYN_RESYNC->load_ms<SYN_RESYNC_HOLD_MS)return;
        SYN_RESYNC->loading=0;
    }
    if(now<SYN_RESYNC_START_MS)return;
    uint8_t *ipc=*(uint8_t **)0x3000a894u;
    if(!ipc)return;
    uint32_t written=*(volatile uint32_t *)(ipc+0x1804),read=*(volatile uint32_t *)(ipc+0x1800);
    if(written-read>SYN_RESYNC_RING_MAX)return;
    for(unsigned k=0;k<SYN_RESYNC_PER_FRAME;k++) {
        unsigned c=SYN_RESYNC->cursor;
        if(c>=12u*n)c=0;
        SYN_RESYNC->cursor=c+1u>=12u*n?0u:c+1u;
        unsigned ch=c/n,id=ids[c%n];
        uint8_t *setting=(uint8_t *)0x300173d0u+0xc08+ch*0x30;
        if(!fw_get(setting,SYN_ON))continue;
        synth_send(ipc,ch,id,fw_get(setting,id)); /* S14: through the Input None stand-in */
    }
}
