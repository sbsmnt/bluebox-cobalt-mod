/* Native desktop FX parameter lists and compact layout. */
#include <stdint.h>
typedef void (*vfn_rect)(void *,int *);
typedef void (*populate_fn)(uint8_t *,short *);
typedef void (*layout_fn)(uint8_t *);
#define fw_populate ((populate_fn)0x0812abe1u)
#define fw_layout ((layout_fn)0x0812b091u)
#define P_COMPACT_OFF 0xc398
#define P_SLOT 0xc3a4
#define KNOB_W(p,i) ((uint8_t *)(p)+0xc3b0+(i)*0x3d0)
#define TOG_W(p,i) ((uint8_t *)(p)+0x100b0+(i)*0x1d4)
typedef int (*get_fn)(void *,unsigned);
#define fw_get ((get_fn)0x08120235u)
#define fw_clear ((void (*)(void *))0x081384d1u)
#define fw_append ((void (*)(void *,unsigned,int))0x08138547u)
static int fx_mode(void *app) {int v=app?fw_get((uint8_t *)app+0xe48,G_MODE):0;return v>=0&&v<FX1_MODES?v:0;}
static int gran_mode(void *app) {return fx_mode(app)==1;}
static int sg_selected(void *app) {return app && fw_get((uint8_t *)app+0xe58,0x155)==SG_STYLE;}
/* Stock routine applies Beat and QUAD conditions first. Only the delay list
 * is filtered; all other FX panels retain the complete native list. */
unsigned dly_parameters(void *app,short *slot,void *list) {
    unsigned result=((unsigned (*)(void *,short *,void *))0x08121cf5u)(app,slot,list);
    if(*slot==0x15 && sg_selected(app)) {
        static const uint16_t ids[]={SG_SAT,SG_SIZE,SG_DIFF,SG_MOD,SG_RATE,SG_HICUT,SG_LOCUT,0x15a};
        fw_clear(list);
        for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++)fw_append(list,ids[i],fw_get((uint8_t *)app+0xe58,ids[i]));
        return result;
    }
    if(*slot==0x15) {
        /* The native kind-5 list enumerates every saved setting. The seven
         * custom keys belong only to Shoegaze, not to the stock styles. */
        uint16_t ids[16],id;int values[16],value;unsigned count=0;
        for(unsigned i=0;((unsigned (*)(void *,unsigned,uint16_t *,int *,void *))0x08138505u)(list,i,&id,&value,0);i++) {
            if(id>=SG_SAT && id<=SG_LOCUT)continue;
            if(count<16){ids[count]=id;values[count++]=value;}
        }
        fw_clear(list);
        for(unsigned i=0;i<count;i++)fw_append(list,ids[i],values[i]);
        return result;
    }
    if(*slot!=0x14)return result;
    if(gran_mode(app)) {
        static const uint16_t ids[]={G_POS,G_SIZE,G_DENSITY,0x4a,G_JITTER,0x39,0x3d,G_FREEZE,G_REVERSE};
        fw_clear(list);
        for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++)fw_append(list,ids[i],fw_get((uint8_t *)app+0xe48,ids[i]));
    } else if(fx_mode(app)==FX1_FREEZE_MODE) {
        /* Freeze, by encoder (list index i: column i/2, even top, odd
         * bottom): Fade / Time, Drift / Tone, Level, Mode / To Rev; the
         * Freeze toggle on the left (index 5, as Reverse's BEAT). */
        static const uint16_t ids[]={F_FADE,F_TIME,F_DRIFT,F_TONE,F_LEVEL,F_FREEZE,F_TMODE,0x3d};
        fw_clear(list);
        for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++)fw_append(list,ids[i],fw_get((uint8_t *)app+0xe48,ids[i]));
    } else if(fx_mode(app)==FX1_BLUR_MODE) {
        /* Blur: Blur / Drift, Tone / Level, To Rev (top) */
        static const uint16_t ids[]={B_BLUR,B_DRIFT,B_TONE,B_LEVEL,0x3d};
        fw_clear(list);
        for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++)fw_append(list,ids[i],fw_get((uint8_t *)app+0xe48,ids[i]));
    } else if(fx_mode(app)==4) {
        uint16_t ids[]={fw_get((uint8_t *)app+0xe48,0x35)?0x34:T_SEGMENT,0x39,T_TONE,0x4a,0x3d,0x35};
        fw_clear(list);
        for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++)fw_append(list,ids[i],fw_get((uint8_t *)app+0xe48,ids[i]));
    } else {
        uint16_t ids[13],id;int values[13],value;unsigned count=0;
        for(unsigned i=0;((unsigned (*)(void *,unsigned,uint16_t *,int *,void *))0x08138505u)(list,i,&id,&value,0);i++) {
            if((id>=G_MODE && id<=T_WILD) || id==T_LOCUT)continue; /* Lo C: Tape inserts it below */
            if(F_IS_FREEZE(id))continue; /* Freeze's own controls */
            if(id==S_SHIFT || id==S_SPREAD)continue; /* G30: Spiral maps them from Pitch's slots */
            int mode=fx_mode(app);
            if(mode==2 && (id==0x4a || id==0x4b))continue;
            if(mode==5 && (id==0x4a || id==0x4b || id==0x3a || id==0x3d))continue;
            /* G30 Spiral: Shift and SPRD take Pitch's knob and toggle */
            if(mode==FX1_SPIRAL_MODE && (id==0x36 || id==0x37))continue;   /* G31: no PING / QUAD */
            if(mode==FX1_SPIRAL_MODE && id==0x4a){id=S_SHIFT;value=fw_get((uint8_t *)app+0xe48,id);}
            if(mode==FX1_SPIRAL_MODE && id==0x4b){id=S_SPREAD;value=fw_get((uint8_t *)app+0xe48,id);}
            if(id==0xcb && mode==2){id=T_DRIVE;value=fw_get((uint8_t *)app+0xe48,id);}
            if(id==0xcb && mode==3){id=T_DUCK;value=fw_get((uint8_t *)app+0xe48,id);}
            if(id==0x3a && mode==3){id=T_RELEASE;value=fw_get((uint8_t *)app+0xe48,id);}
            /* exp.8 Tape: Drift moves to Cutoff's position, the new Lo C to
             * Drift's, and FILT is not shown (the native filter is bypassed). */
            if(mode==2 && id==0xca)continue;
            if(mode==2 && id==0xe){id=0x3a;value=fw_get((uint8_t *)app+0xe48,id);}
            else if(mode==2 && id==0x3a){id=T_LOCUT;value=fw_get((uint8_t *)app+0xe48,id);}
            if(count<13){ids[count]=id;values[count++]=value;}
            if(mode==2 && id==0x3d && count<13){ids[count]=T_HICUT;values[count++]=fw_get((uint8_t *)app+0xe48,T_HICUT);}
        }
        fw_clear(list);
        for(unsigned i=0;i<count;i++)fw_append(list,ids[i],values[i]);
        if(fx_mode(app)==2)fw_append(list,T_WILD,fw_get((uint8_t *)app+0xe48,T_WILD));
    }
    return result;
}
static void move(uint8_t *w,int x,int y,int h) {
    if(*(int *)(w+4)==x && *(int *)(w+8)==y && *(int *)(w+12)==48 && *(int *)(w+16)==h)return;
    int r[4]={x,y,48,h};(*(vfn_rect **)w)[0x20/4](w,r);
}
static void place(uint8_t *p,int slot) {
    if(p[P_COMPACT_OFF] || (uint16_t)(slot-0x14)>=2)return;
    int mode=slot==0x14?fx_mode((void *)0x300173d0u):-1;
    int gran=mode==1;
    int x0=*(int *)(p+4),y0=*(int *)(p+8);
    for(int w=0;w<13;w++) {
        /* Tape has no FILT since exp.8: its later widgets use FILT's old
         * index+1 positions, so BEAT/PING/QUAD/WILD stay where they were. */
        int i=slot==0x14 && mode==2 && w>=7 && w<=10?w+1:w; /* Tape has 11 controls */
        int x=x0+48*(i/2+1),y=y0+45-44*(i&1);
        if(slot==0x14) {
            if(mode==5 && i>=4 && i<8) {
                x=x0+(i<6?0:240);y=y0+45-44*(i&1);
            } else if((mode==4 || mode==FX1_FREEZE_MODE) && i==5){x=x0;y=y0+45;}
            else if(gran && (i==7||i==8)){x=x0;y=y0+45-44*(i-7);}
            else if(!gran && i>=8 && i<12) {
                x=x0+(i<10?0:240);
                y=y0+45-44*(i&1);
                if(mode==2 && i>=10) {
                    int quad=fw_get((uint8_t *)0x300173d0u+0xe78,0x18c)!=0;
                    y=y0+((quad && i==10)?1:45);
                }
            }
        }
        move(KNOB_W(p,w),x,y,44);move(TOG_W(p,w),x,y,44);
    }
}
static void ui_register(uint8_t *p,int slot);
void dly_populate(uint8_t *p,short *slot) {
    ui_register(p,*slot);
    place(p,*slot);fw_populate(p,slot);
}
void dly_layout(uint8_t *p) {
    fw_layout(p);place(p,*(short *)(p+P_SLOT));
}

/* UI-only state, disjoint from M7 grain state [000..0a8], extended delay
 * [d00..e5c] and meter/master regions [f00..]. No DSP parameters are rescaled. */
struct ui_value {uint8_t *widget;uint32_t last,active;};
struct fx_ui {
    uint32_t magic;
    uint8_t *panel;
    int32_t slot,mode,pitch_fraction,pitch_value;
    uint8_t *pitch_widget;
    int32_t density_fraction,density_value;uint8_t *density_widget;
    struct ui_value value[7];
    int32_t log_fraction,log_value;uint8_t *log_widget; /* exp.8 Hz/rate travel */
    void *title;uint32_t diag_last; /* FX1 title label; spare word (was the G2 Freeze diagnostic) */
};
#define UI ((struct fx_ui *)0x38800b00u)
#define UI_MAGIC 0x4658553bu
#define UI_NOW (*(volatile uint32_t *)0x3001eb3cu) /* native encoder/button millisecond clock */
_Static_assert(0xb00+sizeof(struct fx_ui)<=0xd00,"UI state below extended delay");
static struct fx_ui *ui_state(void) {
    backup_enable();PWR_CR1|=1u<<8;
    struct fx_ui *u=UI;
    if(u->magic!=UI_MAGIC) {
        uint32_t *p=(uint32_t *)u;
        for(unsigned i=0;i<sizeof(*u)/4;i++)p[i]=0;
        u->slot=-1;u->magic=UI_MAGIC;
    }
    return u;
}
/* A value from the audio core (a mapped CC) for FX1 / FX2 reaches the M4 in
 * 0x081252e0: it stores the value and only flags a page refresh, which also
 * restarts this UI state, so no knob event shows its value text. The last
 * such value is kept here, outside the UI state, and dly_tick shows it for
 * three seconds like a turn. */
struct fx_cc {uint32_t magic;uint16_t slot,id;uint32_t time;};
#define FX_CC ((volatile struct fx_cc *)0x38800cc0u)
#define FX_CC_MAGIC 0x46584343u
_Static_assert(0xb00+sizeof(struct fx_ui)<=0xcc0,"FX UI state below the CC value note");
static int value_index(unsigned id);
void dly_param_from_m7(uint8_t *app,uint8_t *msg) {
    ((void (*)(void *,void *))0x081252e1u)(app,msg);
    unsigned slot=*(uint16_t *)(msg+8),id=*(uint16_t *)(msg+0xc);
    if(msg[0]!=0x39 || !(slot==20 || (slot==21 && id>=SG_SAT && id<=SG_LOCUT)) || value_index(id)<0)return;
    (void)ui_state();
    FX_CC->slot=(uint16_t)slot;FX_CC->id=(uint16_t)id;FX_CC->time=UI_NOW;FX_CC->magic=FX_CC_MAGIC;
}
static int value_index(unsigned id) {
    if(id>=SG_SAT && id<=SG_LOCUT)return (int)(id-SG_SAT);
    switch(id){case G_POS:case T_DRIVE:case T_DUCK:case T_TONE:return 0;case G_SIZE:case T_SEGMENT:return 1;case G_DENSITY:return 2;case 0x4a:return 3;case G_JITTER:case T_RELEASE:return 4;case 0x33:return 5;case T_HICUT:return 6;case T_LOCUT:return 2;case S_SHIFT:return 0;default:return -1;}
}
static void ui_register(uint8_t *p,int slot) {
    struct fx_ui *u=ui_state();int mode=slot==0x14?fx_mode((void *)0x300173d0u):sg_selected((void *)0x300173d0u)?SG_STYLE:0;
    if(u->panel!=p || u->slot!=slot || u->mode!=mode) {
        u->panel=p;u->slot=slot;u->mode=mode;
        u->pitch_widget=0;u->pitch_fraction=0;
        u->density_widget=0;u->density_fraction=0;
        u->log_widget=0;u->log_fraction=0;
        for(unsigned i=0;i<7;i++)u->value[i].active=0;
    }
}
static float ex2(float x);
static void label_value(uint8_t *w) {
    unsigned id=*(uint16_t *)(w+0x94);
    int value=*(int *)(w+0x98);
    char text[16];char *p=text;
    if(id>=SG_SAT && id<=SG_LOCUT) {
        if(value<0)value=0;
        if(value>16000)value=16000;
        unsigned v=(unsigned)value;
        if(id==SG_RATE) {
            *p++=(char)('0'+v/1000u);*p++='.';
            *p++=(char)('0'+v/100u%10u);*p++=(char)('0'+v/10u%10u);
            *p++=(char)('0'+v%10u);
            *p++='H';*p++='z';
        } else {
            unsigned num=(id<=SG_MOD)?v/10u:v;
            if(num>=10000u)*p++=(char)('0'+num/10000u);
            if(num>=1000u)*p++=(char)('0'+num/1000u%10u);
            if(num>=100u)*p++=(char)('0'+num/100u%10u);
            if(num>=10u)*p++=(char)('0'+num/10u%10u);
            *p++=(char)('0'+num%10u);
            if(id<=SG_MOD){*p++='.';*p++=(char)('0'+v%10u);*p++='%';}
            else {*p++='H';*p++='z';}
        }
        *p=0;((void (*)(void *,const char *))0x0813fe4bu)(w+0xa0,text);return;
    }
    if(id==S_SHIFT) {
        /* G30: signed Hz as the DSP maps it (fsh_knob_hz): 0 inside the
         * centre dead zone, 0.1 Hz .. ~2.5 kHz exponentially outside */
        float d=((float)value-500.f)*(1.f/500.f),u=(d<0.f?-d:d)-.02f;
        if(u<=0.f){*p++='0';}
        else {
            float hz=.1f*ex2(u*(14.6f/.98f));
            *p++=d<0.f?'-':'+';
            unsigned t=(unsigned)(hz*10.f+.5f);
            if(t<100u){if(t>=10u)*p++=(char)('0'+t/10u);else *p++='0';*p++='.';*p++=(char)('0'+t%10u);}
            else {unsigned h=(t+5u)/10u;if(h>=1000u)*p++=(char)('0'+h/1000u);if(h>=100u)*p++=(char)('0'+h/100u%10u);*p++=(char)('0'+h/10u%10u);*p++=(char)('0'+h%10u);}
        }
        *p++='H';*p++='z';*p=0;
        ((void (*)(void *,const char *))0x0813fe4bu)(w+0xa0,text);return;
    }
    if(id==0x4a){if(value< -12)value=-12;if(value>12)value=12;
        if(value<0){*p++='-';value=-value;}}
    if(value<0)value=0;
    int limit=id==T_HICUT?16000:id==T_RELEASE || id==T_LOCUT?2000:1000;
    if(value>limit)value=limit;
    /* Stock free time is normalized 0..1000 across six seconds. Native
     * clamp at zero is 256 samples = 5.333ms; do not display a fictitious 0ms. */
    if(id==0x33 && !value) {
        const char *v="5.3ms";while(*v)*p++=*v++;
    } else if(id==T_LOCUT && value<=20) {
        *p++='O';*p++='f';*p++='f';
    } else if(id==G_POS || id==G_JITTER || id==T_DRIVE || id==T_DUCK || id==T_TONE) {
        int percent=value/10;
        if(percent>=100)*p++='1';
        if(percent>=10)*p++=(char)('0'+(percent/10)%10);
        *p++=(char)('0'+percent%10);*p++='.';*p++=(char)('0'+value%10);*p++='%';
    } else {
        if(id==0x33)value*=6;
        if(value>=10000)*p++=(char)('0'+value/10000);
        if(value>=1000)*p++=(char)('0'+(value/1000)%10);
        if(value>=100)*p++=(char)('0'+(value/100)%10);
        if(value>=10)*p++=(char)('0'+(value/10)%10);
        *p++=(char)('0'+value%10);
        if(id==G_SIZE || id==T_SEGMENT || id==T_RELEASE || id==0x33){*p++='m';*p++='s';}
        if(id==T_HICUT || id==T_LOCUT){*p++='H';*p++='z';}
        if(id==0x4a){*p++=' ';*p++='s';*p++='t';}
    }
    *p=0;
    ((void (*)(void *,const char *))0x0813fe4bu)(w+0xa0,text);
}
static void value_touch(uint8_t *w) {
    int i=value_index(*(uint16_t *)(w+0x94));
    unsigned slot=*(uint16_t *)(w+0x32),id=*(uint16_t *)(w+0x94);
    if(i<0 || !(slot==0x14 || (slot==0x15 && id>=SG_SAT && id<=SG_LOCUT)))return;
    struct fx_ui *u=ui_state();
    u->value[i].widget=w;u->value[i].last=UI_NOW;u->value[i].active=1;
}
/* Logarithmic travel for Hz/rate controls (exp.8). Position 0..1000 maps to
 * lo*(hi/lo)^(p/1000), so every step is the same musical interval, and one
 * step takes 40 raw units like a native 0..1000 control (Diffusion). */
static float lg2(float x) {
    union{float f;uint32_t u;} v={x};
    int e=(int)((v.u>>23)&255u)-127;v.u=(v.u&0x007fffffu)|0x3f800000u;
    float m=v.f-1.f; /* log2(1+m), m in [0,1): max error ~1e-4 */
    return (float)e+m*(1.4425449f+m*(-.7181452f+m*(.4575485f+m*(-.2779042f+m*(.121797f+m*-.0258411f)))));
}
static float ex2(float x) {
    int i=(int)x;if((float)i>x)i--;
    float f=x-(float)i;
    union{float f;uint32_t u;} v={1.f+f*(.6931472f+f*(.2402265f+f*(.0555041f+f*(.0096181f+f*.0013334f))))};
    v.u+=(uint32_t)i<<23;return v.f;
}
#define LOG_UNITS 40
/* Called at the native FX encoder-dispatch site. Preserve the accelerated
 * native numeric-knob path for every control except FX1 Pitch/Density/Hi Cut. For Pitch,
 * map 1000 accelerated encoder units across its 24-semitone range, retaining
 * fractional input so small moves accumulate instead of being discarded.
 * Density uses 256 slow units per integer, with its own fractional state.
 * Since exp.8 Pitch takes 160 slow units per semitone (was ~42), and the
 * Hz/rate controls use the logarithmic travel above. */
unsigned dly_turn(uint8_t *w,int delta) {
    unsigned id=*(uint16_t *)(w+0x94),slot=*(uint16_t *)(w+0x32);
    unsigned result=0;
    int sg_linear=slot==0x15 && (id==SG_RATE || id==SG_HICUT || id==SG_LOCUT);
    int logf=sg_linear || (slot==0x14 && (id==T_HICUT || id==T_LOCUT));
    if((((id==0x4a || id==G_DENSITY) && slot==0x14) || logf) && !w[0x30] && !w[0x2d4]) {
        struct fx_ui *u=ui_state();int old=*(int *)(w+0x98);
        int density=id==G_DENSITY;
        int lo=logf?(id==SG_RATE?10:id==SG_HICUT || id==T_HICUT?500:20):density?1:-12;
        int hi=logf?(id==SG_HICUT || id==T_HICUT?16000:2000):density?4:12;
        int scale=1;
        int denominator=logf?LOG_UNITS:density?256:160;
        int32_t *fraction=logf?&u->log_fraction:density?&u->density_fraction:&u->pitch_fraction;
        int32_t *saved=logf?&u->log_value:density?&u->density_value:&u->pitch_value;
        uint8_t **widget=logf?&u->log_widget:density?&u->density_widget:&u->pitch_widget;
        if(old<lo)old=lo;
        if(old>hi)old=hi;
        if(*widget!=w || *saved!=old ||
           (delta<0 && *fraction>0) || (delta>0 && *fraction<0))*fraction=0;
        *widget=w;
        ((void (*)(void *,int,unsigned))0x0813b7e9u)(w+0x34,delta,UI_NOW);
        unsigned acceleration=((unsigned (*)(void *))0x0813b805u)(w+0x34);
        if(acceleration<1)acceleration=1;
        if(acceleration>6)acceleration=6;
        if(density && acceleration>2)acceleration=2;
        if(delta>1000)delta=1000;
        if(delta< -1000)delta=-1000;
        int total=*fraction+delta*(int)acceleration*scale;
        int step=total/denominator,next=old+step;
        if(logf && step) {
            float span=lg2((float)hi/(float)lo);
            float pos=1000.f*lg2((float)old/(float)lo)/span+(float)step;
            if(pos<0.f)pos=0.f;
            if(pos>1000.f)pos=1000.f;
            next=(int)((float)lo*ex2(pos*span*.001f)+.5f);
            /* Integer Hz/mHz can be coarser than one step at the bottom. */
            if(next==old)next=old+(step>0?1:-1);
        }
        if(next>hi)next=hi;
        if(next<lo)next=lo;
        *fraction=total-step*denominator;
        if((next==hi && total>0)||(next==lo && total<0))*fraction=0;
        if(next!=old) {
            uint32_t ev[6]={0x6e,0x08148664u,slot,id,(uint32_t)next,0};
            ((void (*)(void *,void *))0x0813a415u)(w,ev);
            ((void (*)(void *,unsigned,int,unsigned))0x08127b85u)(w,id,next,1);
            result=1;
        }
        *saved=*(int *)(w+0x98);
    } else result=((unsigned (*)(void *,int))0x08127d51u)(w,delta);
    if((slot==0x14 || (slot==0x15 && id>=SG_SAT && id<=SG_LOCUT)) && value_index(id)>=0 && !w[0x30]){value_touch(w);label_value(w);}
    return result;
}
/* Runs at the native FX update vtable entry. Native widget setup and redraw
 * still execute; only the requested value labels are temporarily overridden. */
void dly_tick(uint8_t *p,void *flags) {
    ((void (*)(void *,void *))0x0812afd9u)(p,flags);
    struct fx_ui *u=ui_state();
    unsigned slot=*(uint16_t *)(p+P_SLOT);
    if(u->panel!=p || !(slot==0x14 || (slot==0x15 && sg_selected((void *)0x300173d0u))))return;
    uint32_t now=UI_NOW;
    if(FX_CC->magic==FX_CC_MAGIC) {
        if(now-FX_CC->time>=3000u)FX_CC->magic=0;
        else if(FX_CC->slot==slot)for(unsigned i=0;i<8;i++) {
            uint8_t *w=KNOB_W(p,i);int k=value_index(*(uint16_t *)(w+0x94));
            if(*(uint16_t *)(w+0x94)==FX_CC->id && k>=0 && !w[0x30]) {
                /* The later of the last turn and the last CC starts the 3 s. */
                if(!u->value[k].active || u->value[k].widget!=w || (int32_t)(FX_CC->time-u->value[k].last)>0)u->value[k].last=FX_CC->time;
                u->value[k].widget=w;u->value[k].active=1;
                break;
            }
        }
    }
    for(unsigned i=0;i<7;i++)if(u->value[i].active) {
        struct ui_value *v=&u->value[i];uint8_t *w=v->widget;
        int valid=w && *(uint16_t *)(w+0x32)==slot && value_index(*(uint16_t *)(w+0x94))==(int)i;
        if(!valid){v->active=0;continue;}
        if(now-v->last<3000u)label_value(w);
        else {v->active=0;((void (*)(void *,unsigned))0x081278b1u)(w,1);}
    }
}
/* Native action 14/15 selects FX1 (slot20), 16/17 selects FX2 (slot21).
 * Both mixer/control variants share B action18; retain native FX2 routing. */
void dly_choose_action(uint8_t *model,unsigned action,unsigned arg,unsigned extra) {
    if(action==18 && (model[0x739a]==14 || model[0x739a]==15))arg=0;
    ((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)(model,action,arg,extra);
}
/* G17-G20: the FX1 list in two pages, Delay (Delay, Warp, Granular, Tape,
 * Ducked, Reverse) and Spectral (Stasis, Blur). The header reads FX1 / Delay
 * or FX1 / Spectral in the stock labels (as FX2's FX2 / Reverb). G20: A
 * switches page while the list is open (G21: the page rows are gone). With a list open the session is in
 * popup state 18 (+0x739a, the page under it at +0x739b) and stock A emits
 * nothing there, only the default path (0x08123902), which the Scenes patch
 * already owns (pc_button_a); that calls a_flip through this record. The
 * record lives in free M4 backup SRAM (0xca0). */
struct fx1_page {uint32_t magic;uint8_t *popup;uint32_t open,spectral,flipped;void (*a_flip)(void);uint8_t row[2];};   /* row: each page's last picked row (G26) */   /* open: FX1's list was filled last; flipped: switched by hand since the last pick */
#define FX1_PAGE ((struct fx1_page *)0x38800ca0u)
#define FX1_PAGE_MAGIC 0x46585048u
_Static_assert(sizeof(struct fx1_page)<=0x20,"FX1 page record in the free gap 0xc94..0xcc0, below FX_CC");
static struct fx1_page *fx1_page(void) {
    backup_enable();PWR_CR1|=1u<<8;
    struct fx1_page *g=FX1_PAGE;
    if(g->magic!=FX1_PAGE_MAGIC){g->magic=FX1_PAGE_MAGIC;g->popup=0;g->open=0;g->spectral=0;g->flipped=0;g->a_flip=0;g->row[0]=0;g->row[1]=0;}
    return g;
}
/* Saved mode ids per page row. */
static const uint8_t fx1_delay_rows[6]={5,0,1,2,3,4},fx1_spectral_rows[3]={FX1_FREEZE_MODE,FX1_BLUR_MODE,FX1_SPIRAL_MODE};
static unsigned fx1_fill_rows(uint8_t *p) {   /* returns the highlighted row, or ~0u */
    struct fx1_page *g=fx1_page();
    unsigned spectral=g->spectral!=0;
    ((void (*)(void *,const char *))0x0813820du)(p+0xf50,"FX1");
    ((void (*)(void *,const char *))0x0813820du)(p+0xff0,spectral?"Spectral":"Delay");
    void *list=p+0x1090;
    ((void (*)(void *))0x0813c075u)(list);
    *(uint16_t *)(p+0xd998)=G_MODE;
    const char *const *names=((const char *const *(*)(unsigned))0x08136065u)(G_MODE);
    unsigned count=((unsigned (*)(unsigned))0x08136095u)(G_MODE);
    if(count<8)return ~0u;
    const uint8_t *rows=spectral?fx1_spectral_rows:fx1_delay_rows;
    unsigned n=spectral?3u:6u;
    for(unsigned i=0;i<n;i++)((void (*)(void *,const char *,unsigned))0x0813c0abu)(list,names[rows[i]],i);
    /* the current mode's row, else the row last picked on this page */
    int mode=fx_mode((void *)0x300173d0u);unsigned selected=g->row[spectral]<n?g->row[spectral]:0u;
    for(unsigned i=0;i<n;i++)if((int)rows[i]==mode)selected=i;
    ((void (*)(void *,unsigned))0x0813c203u)(list,selected);
    ((void (*)(void *,unsigned))0x0813c2d1u)(list,selected);
    return selected;
}
/* G26: A also picks the highlighted row (as a turn of the encoder does),
 * so closing the list keeps it: Delay -> A -> Stasis, Tape -> A -> A -> Tape. */
static void fx1_flip(uint8_t *p) {
    struct fx1_page *g=fx1_page();
    g->spectral=!g->spectral;g->flipped=1;
    unsigned spectral=g->spectral!=0,row=fx1_fill_rows(p);
    if(row>=(spectral?3u:6u))return;
    uint32_t ev[6]={0x150u,0,0,spectral?fx1_spectral_rows[row]:fx1_delay_rows[row],0,0};
    g->row[spectral]=(uint8_t)row;g->flipped=0;
    ((unsigned (*)(void *,void *))0x08139aa9u)(p,ev);
}
/* A in popup state 18 over FX1's controls (pc_button_a): the FX1 list was
 * filled last and not picked from or replaced by FX2's since. */
static void fx1_a_press(void) {
    struct fx1_page *g=fx1_page();
    uint8_t *p=g->popup;
    if(g->open && p && *(uint16_t *)(p+0xd99a)==20)fx1_flip(p);
}
/* Use the native selector list and selection emitter, including highlight and
 * scroll position. FX2 executes its original fill routine without modification. */
void dly_selector_fill(uint8_t *p) {
    struct fx1_page *g=fx1_page();
    g->popup=p;g->a_flip=fx1_a_press;
    if(*(uint16_t *)(p+0xd99a)!=20) {
        /* The popup instance is reused: restore both headers when opening FX2. */
        g->open=0;
        ((void (*)(void *,const char *))0x0813820du)(p+0xf50,"FX2");
        ((void (*)(void *,const char *))0x0813820du)(p+0xff0,"Reverb");
        ((void (*)(void *))0x08139aedu)(p);return;
    }
    /* Opening: the page of the current mode, unless the page was switched
     * with A since the last pick. */
    g->open=1;
    if(!g->flipped)g->spectral=FX1_SPEC_PAGE(fx_mode((void *)0x300173d0u));
    fx1_fill_rows(p);
}
unsigned dly_selector_event(uint8_t *p,uint32_t *ev) {
    unsigned type=*(uint16_t *)ev;
    if(*(uint16_t *)(p+0xd99a)==20) {
        if(type==0x150) {
            uint32_t mapped[6];for(unsigned i=0;i<6;i++)mapped[i]=ev[i];
            unsigned spectral=fx1_page()->spectral!=0,n=spectral?3u:6u;
            unsigned row=ev[3];
            if(row>=n)return 0;
            mapped[3]=spectral?fx1_spectral_rows[row]:fx1_delay_rows[row];
            fx1_page()->row[spectral]=(uint8_t)row;
            /* G24: turning the encoder picks on every step with the list
             * still open, so the list stays open for A (it is closed by
             * leaving popup state 18, or replaced by FX2's fill). */
            fx1_page()->flipped=0;
            return ((unsigned (*)(void *,void *))0x08139aa9u)(p,mapped);
        }
    }
    return ((unsigned (*)(void *,void *))0x08139aa9u)(p,ev);
}
unsigned dly_event(uint8_t *p,uint8_t *ev) {
    unsigned type=*(uint16_t *)ev;
    if(type==0x63) {(void)ui_state();UI->magic=0;ui_register(p,*(uint16_t *)(ev+8));}
    if(type==0x6e && *(uint16_t *)(p+P_SLOT)==21) {
        unsigned id=*(uint32_t *)(ev+12);
        if(id==0x155) {
            ((void (*)(void *,void *))0x0813a415u)(p,ev);
            short slot=21;dly_populate(p,&slot);return 0;
        }
        if(id>=SG_SAT && id<=SG_LOCUT)for(unsigned i=0;i<8;i++) {
            uint8_t *w=KNOB_W(p,i);
            if(*(uint16_t *)(w+0x94)==id){value_touch(w);break;}
        }
    }
    if(type==0x6e && *(uint16_t *)(p+P_SLOT)==20) {
        unsigned id=*(uint32_t *)(ev+12);
        if(id==G_MODE) {
            int before=fx_mode((void *)0x300173d0u);
            ((void (*)(void *,void *))0x0813a415u)(p,ev);
            if(before!=fx_mode((void *)0x300173d0u)){short slot=20;dly_populate(p,&slot);}
            return 0;
        }
        if(value_index(id)>=0)for(unsigned i=0;i<7;i++) {
            uint8_t *w=KNOB_W(p,i);
            if(*(uint16_t *)(w+0x94)==id){value_touch(w);break;}
        }
    }
    return ((unsigned (*)(void *,void *))0x0812b461u)(p,ev);
}

/* Patch only the native Delay title setter; FX2's title path remains native.
 * Mode 5 (plain Delay) keeps the stock "Delay" title. */
void dly_title(void *label,const char *stock) {
    {struct fx_ui *u=ui_state();u->title=label;u->diag_last=0;}
    ((void (*)(void *,const char *))0x0813fe4bu)(label,
        fx_mode((void *)0x300173d0u)==0?"Warp":fx_mode((void *)0x300173d0u)==1?"Granulr":fx_mode((void *)0x300173d0u)==2?"Tape":fx_mode((void *)0x300173d0u)==3?"Ducked":fx_mode((void *)0x300173d0u)==4?"RevDly":fx_mode((void *)0x300173d0u)==FX1_FREEZE_MODE?"Stasis":fx_mode((void *)0x300173d0u)==FX1_BLUR_MODE?"Flux":fx_mode((void *)0x300173d0u)==FX1_SPIRAL_MODE?"Spiral":stock);
}
void sg_title(void *label,const char *stock) {
    ((void (*)(void *,const char *))0x0813fe4bu)(label,sg_selected((void *)0x300173d0u)?"Gazer":stock);
}
/* Native integer knob rendering divides by the number of discrete states,
 * so Density 4 yields (4-1)/4. Correct only its drawing fraction; the native
 * cursor range, input mapping, saved value and DSP remain 1..4 unchanged. */
float dly_knob_fraction(uint8_t *w) {
    if(*(uint16_t *)(w+0x32)<12 && (*(uint16_t *)(w+0x94)==SYN_LFO_SYNC || *(uint16_t *)(w+0x94)==SYN_LFO2_SYNC))
        return SYN_LFO_SYNCED(*(int *)(w+0x98))?(float)(*(int *)(w+0x98)-1)/(float)(SYN_LFO_DIVISIONS-1u):0.f;
    if(*(uint16_t *)(w+0x32)<12 && *(uint16_t *)(w+0x94)==SYN_ENV_ATTACK) {
        int value=*(int *)(w+0x98);
        if(value<=0)return 0.f;
        if(value>=5000)return 1.f;
        return lg2((float)(value+SYN_ENV_ATTACK_OFFSET)/(float)SYN_ENV_ATTACK_OFFSET)/lg2((float)(5000+SYN_ENV_ATTACK_OFFSET)/(float)SYN_ENV_ATTACK_OFFSET);
    }
    if(*(uint16_t *)(w+0x32)<12 && (*(uint16_t *)(w+0x94)==SYN_LFO_RATE || *(uint16_t *)(w+0x94)==SYN_LFO2_RATE)) {
        /* S15: the ring follows the same log travel as the encoder. */
        int value=*(int *)(w+0x98);
        if(value<=10)return 0.f;
        if(value>=20000)return 1.f;
        return lg2((float)(value+SYN_LFO_RATE_OFFSET)/(float)(10+SYN_LFO_RATE_OFFSET))/lg2((float)(20000+SYN_LFO_RATE_OFFSET)/(float)(10+SYN_LFO_RATE_OFFSET));
    }
    if(*(uint16_t *)(w+0x32)<12 && *(uint16_t *)(w+0x94)==SYN_CUTOFF) {
        int value=*(int *)(w+0x98);
        if(value<=20)return 0.f;
        if(value>=20000)return 1.f;
        return lg2((float)value/20.f)/lg2(1000.f);
    }
    if(*(uint16_t *)(w+0x32)<12 && (*(uint16_t *)(w+0x94)==SYN_ATTACK || *(uint16_t *)(w+0x94)==SYN_RELEASE)) {
        /* S23: Attack 1..2000 ms and Release 5..5000 ms on their log travel. */
        int value=*(int *)(w+0x98),attack=*(uint16_t *)(w+0x94)==SYN_ATTACK;
        float lo=attack?1.f:5.f,hi=attack?2000.f:5000.f,offset=attack?(float)SYN_ATTACK_OFFSET:0.f;
        if(value<=(int)lo)return 0.f;
        if(value>=(int)hi)return 1.f;
        return lg2(((float)value+offset)/(lo+offset))/lg2((hi+offset)/(lo+offset));
    }
    if(*(uint16_t *)(w+0x32)<12 && *(uint16_t *)(w+0x94)==SYN_ENV_DECAY) {
        /* S20: the ring follows the encoder's and the CC's log travel
         * (5..5000 ms, as Cutoff); it was drawn linear. */
        int value=*(int *)(w+0x98);
        if(value<=5)return 0.f;
        if(value>=5000)return 1.f;
        return lg2((float)value/5.f)/lg2(1000.f);
    }
    if(*(uint16_t *)(w+0x32)<12 && *(uint16_t *)(w+0x94)==INS_MODE) {
        /* Per group: CLIP, SAT, FOLD, WRAP or LP, BP, HP, DJ or SHFT, SPRD, RING,
         * HARM, CHRD, BELL (0 and 2 show CLIP). */
        int m=*(int *)(w+0x98);
        if(m==15)return 1.f/3.f; /* SAT, second in the DRV group */
        if(m>=9)return (float)(m>14?5:m-9)*.2f;
        if(m>=5)return (float)(m-5)*(1.f/3.f);
        return m==3?2.f/3.f:m==4?1.f:0.f; /* CLIP, (SAT above), FOLD, WRAP */
    }
    if(*(uint16_t *)(w+0x32)==20 && *(uint16_t *)(w+0x94)==G_DENSITY) {
        int n=*(int *)(w+0x98);if(n<1)n=1;if(n>4)n=4;
        return (float)(n-1)/3.0f;
    }
    return ((float (*)(void *))0x08127309u)(w);
}
/* Tools header has three pre-existing fields: storage at +174, empty
 * middle at +d4, official version at +34. Reuse the empty field. */
void dly_tools_version(void *label,const char *stock) {
    ((void (*)(void *,const char *))0x0813820du)(label,stock);
    ((void (*)(void *,const char *))0x0813820du)((uint8_t *)label+0xa0,"Cblt 1.3d");
}

