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
static int fx_mode(void *app) {int v=app?fw_get((uint8_t *)app+0xe48,G_MODE):0;return v>=0&&v<=5?v:0;}
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
    } else if(fx_mode(app)==4) {
        uint16_t ids[]={fw_get((uint8_t *)app+0xe48,0x35)?0x34:T_SEGMENT,0x39,T_TONE,0x4a,0x3d,0x35};
        fw_clear(list);
        for(unsigned i=0;i<sizeof(ids)/sizeof(ids[0]);i++)fw_append(list,ids[i],fw_get((uint8_t *)app+0xe48,ids[i]));
    } else {
        uint16_t ids[13],id;int values[13],value;unsigned count=0;
        for(unsigned i=0;((unsigned (*)(void *,unsigned,uint16_t *,int *,void *))0x08138505u)(list,i,&id,&value,0);i++) {
            if((id>=G_MODE && id<=T_WILD) || id==T_LOCUT)continue; /* Lo C: Tape inserts it below */
            int mode=fx_mode(app);
            if(mode==2 && (id==0x4a || id==0x4b))continue;
            if(mode==5 && (id==0x4a || id==0x4b || id==0x3a || id==0x3d))continue;
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
            } else if(mode==4 && i==5){x=x0;y=y0+45;}
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
static int value_index(unsigned id) {
    if(id>=SG_SAT && id<=SG_LOCUT)return (int)(id-SG_SAT);
    switch(id){case G_POS:case T_DRIVE:case T_DUCK:case T_TONE:return 0;case G_SIZE:case T_SEGMENT:return 1;case G_DENSITY:return 2;case 0x4a:return 3;case G_JITTER:case T_RELEASE:return 4;case 0x33:return 5;case T_HICUT:return 6;case T_LOCUT:return 2;default:return -1;}
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
/* Use the native selector list and selection emitter, including highlight and
 * scroll position. FX2 executes its original fill routine without modification. */
void dly_selector_fill(uint8_t *p) {
    int delay=*(uint16_t *)(p+0xd99a)==20;
    /* The popup instance is reused: restore both headers when opening FX2. */
    ((void (*)(void *,const char *))0x0813820du)(p+0xf50,delay?"FX1":"FX2");
    ((void (*)(void *,const char *))0x0813820du)(p+0xff0,delay?"Delay":"Reverb");
    if(*(uint16_t *)(p+0xd99a)!=20) {
        ((void (*)(void *))0x08139aedu)(p);return;
    }
    void *list=p+0x1090;
    ((void (*)(void *))0x0813c075u)(list);
    *(uint16_t *)(p+0xd998)=G_MODE;
    const char *const *names=((const char *const *(*)(unsigned))0x08136065u)(G_MODE);
    unsigned count=((unsigned (*)(unsigned))0x08136095u)(G_MODE);
    if(count>6)count=6;
    /* Rows use native ordinals; the popup event wrapper preserves saved mode ids. */
    for(unsigned i=0;i<count;i++) {
        unsigned id=i?i-1:5;
        ((void (*)(void *,const char *,unsigned))0x0813c0abu)(list,names[id],i);
    }
    int selected=fx_mode((void *)0x300173d0u);
    selected=selected==5?0:selected+1;
    ((void (*)(void *,unsigned))0x0813c203u)(list,(unsigned)selected);
    ((void (*)(void *,unsigned))0x0813c2d1u)(list,(unsigned)selected);
}
unsigned dly_selector_event(uint8_t *p,uint32_t *ev) {
    if(*(uint16_t *)ev==0x150 && *(uint16_t *)(p+0xd99a)==20) {
        uint32_t mapped[6];for(unsigned i=0;i<6;i++)mapped[i]=ev[i];
        unsigned row=ev[3];if(row>=6)return 0;
        mapped[3]=row?row-1:5;
        return ((unsigned (*)(void *,void *))0x08139aa9u)(p,mapped);
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
    ((void (*)(void *,const char *))0x0813fe4bu)(label,
        fx_mode((void *)0x300173d0u)==0?"Warp":fx_mode((void *)0x300173d0u)==1?"Granulr":fx_mode((void *)0x300173d0u)==2?"Tape":fx_mode((void *)0x300173d0u)==3?"Ducked":fx_mode((void *)0x300173d0u)==4?"RevDly":stock);
}
void sg_title(void *label,const char *stock) {
    ((void (*)(void *,const char *))0x0813fe4bu)(label,sg_selected((void *)0x300173d0u)?"Gazer":stock);
}
/* Native integer knob rendering divides by the number of discrete states,
 * so Density 4 yields (4-1)/4. Correct only its drawing fraction; the native
 * cursor range, input mapping, saved value and DSP remain 1..4 unchanged. */
float dly_knob_fraction(uint8_t *w) {
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
    ((void (*)(void *,const char *))0x0813820du)((uint8_t *)label+0xa0,"Cb 1.1a4");
}
