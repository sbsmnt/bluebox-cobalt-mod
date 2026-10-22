/* Gate on MIXER sends page and beside OUT2 in the native Track controls. */
#define NG_ID4 0x1aeu
#include "gate_diag_ui.h"
void gate_default(void *set,unsigned id,int value) {
    STOCK_ADD(set,id,value);STOCK_ADD(set,NG_ID4,-96000);
}
/* Match the native sends-group visibility in 0x0812e706: setting 0
 * displays the six-output group; nonzero displays the five-output group.
 * Reversing this selects OUT3 when the visible sixth Gate button is pressed. */
static unsigned gate_quad(void) {return fw_get((uint8_t *)0x300173d0u+0xe78,0x18c)==0;}
static void gate_knob_bind(uint8_t *w,unsigned ch,int value) {
    ((void (*)(void *,unsigned,int,unsigned))0x08127b85u)(w,NG_ID4,value,1);
    *(uint16_t *)(w+0x32)=(uint16_t)ch;
    /* Native type-7 knobs reserve ten positions below the minimum. Gate has
     * a single Off position; start at its last equivalent native position so
     * the next 0.1 dB step enables the gate, rather than traversing that pad. */
    if(value<=-96000) {
        ((void (*)(void *,int))0x0813b295u)(w+0x78,10);
        *(int *)(w+0x90)=10;
    }
}
static void gate_button_rect(uint8_t *p,uint8_t *w,unsigned i,unsigned count) {
    int r[4]={*(int *)(p+4)+1+(int)(i*320u/count),*(int *)(p+8)+1,
        (int)((i+1)*320u/count-i*320u/count)-1,39};
    (*(void (***)(void *,int *))w)[0x20/4](w,r);
}
void gate_mix_setup(uint8_t *p,unsigned mode) {
    ((void (*)(void *,unsigned))0x0812e54du)(p,mode);
    uint8_t *normal=p+0xc3a4,*extra=p+0xbfe4;
    ((void (*)(void *,const char *))0x0813820du)(normal,"Gate");
    (*(void (***)(void *,unsigned))normal)[0x2c/4](normal,0);
    for(unsigned i=0;i<6;i++)gate_button_rect(p,p+0xc084+i*0xa0,i,6);
    for(unsigned i=0;i<6;i++)gate_button_rect(p,p+0xc444+i*0xa0,i,7);
    gate_button_rect(p,extra,6,7);
}
void gate_mix_select(uint8_t *p,unsigned index) {
    unsigned mode=*(unsigned *)(p+0xd26c),quad=gate_quad();
    /* Normal sends page uses its spare sixth button. Quad keeps all six
     * output controls and adds Gate in a seventh existing native child. */
    if(mode==1 && !quad && index==5)index=6;
    ((void (*)(void *,unsigned))0x0812e2e1u)(p,index);
    gd_mix_capture(p,*(uint16_t *)(p+0xbc9c));
    if(mode==1) {
        uint8_t *gate=p+(quad?0xbfe4:0xc3a4);
        ((void (*)(void *,unsigned))0x08138221u)(gate,index==6);
    }
}
void gate_column(uint8_t *p,short *slot) {
    /* Native binding preserves its disabled flag. Undo only our Main override
     * before letting the original binder choose the next parameter's state. */
    if(*slot==12) {
        uint8_t *k=p+0xbe0+12*0xd98+0x8f0;
        if(*(uint16_t *)(k+0x94)==NG_ID4)k[0x2d4]=0;
    }
    ((void (*)(void *,short *))0x0812dde9u)(p,slot);
    if(*(uint16_t *)(p+0xbca0)!=NG_ID4)return;
    int ch=*slot;if(ch<0 || ch>12)return;
    uint8_t *w=p+0xbe0+ch*0xd98;
    if(ch==12) {
        /* Main has no per-track threshold; keep its control disabled. */
        ((void (*)(void *,unsigned))0x0812d72fu)(w,1);
        ((void (*)(void *,unsigned))0x0812d507u)(w,0);
        uint8_t *knob=w+0x8f0;knob[0x2d4]=1;
        ((void (*)(void *,const char *))0x0813fe4bu)(knob+0xa0,"N/A");
        return;
    }
    int value=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,NG_ID4);
    /* Rebind from the authoritative channel setting, just like TRACK. */
    uint8_t *knob=w+0x8f0;
    gate_knob_bind(knob,(unsigned)ch,value);
    if(*(uint16_t *)(p+0xbc9c)==(unsigned)ch)gd_mix_capture(p,ch);
    *(float *)(w+0x400)=-96.f;*(float *)(w+0x404)=-12.f;
    ((void (*)(void *,float,unsigned))0x0812d43fu)(w,(float)value*.001f,1);
    ((void (*)(void *,unsigned))0x0812d6fdu)(w,1);
}
/* Called only at the native numeric-knob dB formatting call. The shim passes
 * its existing r4 widget as the fifth argument without changing native args. */
void gate_db_format(int value,unsigned decimal,unsigned sign,char *text,uint8_t *w) {
    if(*(uint16_t *)(w+0x94)==NG_ID4 && value<=-96000) {
        text[0]='O';text[1]='f';text[2]='f';text[3]=0;return;
    }
    ((void (*)(int,unsigned,unsigned,char *))0x081403c5u)(value,decimal,sign,text);
}
__attribute__((naked)) void gate_db_shim(void) {
    __asm volatile("push {r4,lr}\nbl gate_db_format\npop {r4,pc}");
}

static void gate_commit(uint16_t ch,int value);
#include "gate_ux.h"

static void gate_commit(uint16_t ch,int value) {
    if(ch>=12)return;
    if(value<-96000)value=-96000;
    if(value>-12000)value=-12000;
    ((void (*)(void *,uint16_t *,unsigned,int,unsigned))0x08124b45u)
        ((void *)0x300173d0u,&ch,NG_ID4,value,0);
}
unsigned gate_mix_event(uint8_t *p,uint32_t *ev) {
    unsigned type=(uint16_t)ev[0];
    if(type==0x32)gd_mix_input(p,ev);
    if(type==0x32 && *(uint16_t *)(p+0xbca0)==NG_ID4) {
        /* Native dispatch first turns the four base encoder bindings, then
         * the column knobs. An unknown selection can leave a base binding
         * on Vol. Gate turns bypass that path. Mirror native bank mapping. */
        int index=(int16_t)ev[3];
        if(p[0xbcc0])index=index<0?-((-index)&1):(index&1);
        int ch=index+2 * *(int *)(p+0xbcbc);
        if(ch>=0 && ch<12) {
            uint8_t *k=p+0xbe0+(unsigned)ch*0xd98+0x8f0;
            int value=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,NG_ID4);
            if(*(uint16_t *)(k+0x94)!=NG_ID4 || *(int *)(k+0x98)!=value)
                gate_knob_bind(k,(unsigned)ch,value);
            k[0x30]=0;k[0x2d4]=0;
            gate_turn(k,(int16_t)ev[4]);
            value=*(int *)(k+0x98);
            /* Child bubbling normally commits first. Retain an explicit
             * commit if an intermediate handler consumes the child edit. */
            if(value!=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,NG_ID4))
                gate_commit((uint16_t)ch,value);
        }
        return 0;
    }
    if(type==0x6e && (uint16_t)ev[2]==12 && ev[3]==NG_ID4)return 0;
    return ((unsigned (*)(void *,void *))0x0812e755u)(p,ev);
}
/* Child edits arrive at vtable +0x30, not +0x34 (global notifications).
 * Commit Gate through the same native model setter on both pages. It sends
 * the audio command, updates project storage and broadcasts native 0x66.
 * Consume the child event so parent routing cannot drop or apply it twice.
 * Other parameters keep their original event path. */
static unsigned gate_edit(uint32_t *ev) {
    unsigned type=(uint16_t)ev[0];
    if(type!=0x6e && type!=0x3c && type!=0x3d)return 0;
    unsigned id=type==0x6e?ev[3]:ev[4];
    if(id!=NG_ID4)return 0;
    uint16_t ch=(uint16_t)ev[2];
    gate_commit(ch,(int)(type==0x6e?ev[4]:ev[3]));
    return 1;
}
unsigned gate_mix_local(uint8_t *p,uint32_t *ev) {
    gd_local(p,ev,1);
    if(gate_edit(ev))return 0;
    return ((unsigned (*)(void *,void *))0x0812e469u)(p,ev);
}
unsigned gate_track_local(uint8_t *p,uint32_t *ev) {
    gd_local(p,ev,2);
    if(gate_edit(ev))return 0;
    return ((unsigned (*)(void *,void *))0x08132e39u)(p,ev);
}
void *gate_mix_construct(uint8_t *p) {
    void *result=((void *(*)(void *))0x0812daddu)(p);
    uint8_t *normal=p+0xc3a4,*extra=p+0xbfe4;
    (*(void (***)(void *,unsigned))normal)[0x24/4](normal,0x162);
    *(uint16_t *)(p+0xcb7c+0x36)=6;
    ((void (*)(void *,void *))0x08140c9du)(p+0xcb7c,normal);
    (*(void (***)(void *,unsigned))extra)[0x24/4](extra,0x163);
    *(uint16_t *)(p+0xcef4+0x36)=7;
    ((void (*)(void *,void *))0x08140c9du)(p+0xcef4,extra);
    ((void (*)(void *,const char *))0x0813820du)(extra,"Gate");
    return result;
}
/* Reuse the Track page's eighth native knob (OUT3), beside OUT2. OUT3
 * remains available on the MIXER sends page in Quad output mode. */
void gate_track_bind(uint8_t *p,uint8_t *info) {
    ((void (*)(void *,void *))0x081328f9u)(p,info);
    unsigned ch=*(uint16_t *)(info+4);if(ch>=12)return;
    uint8_t *w=p+0xde48;
    int value=fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,NG_ID4);
    gate_knob_bind(w,ch,value);
    w[0x2d4]=0;w[0x30]=0;
    gd_track_bind(w,ch);
}
