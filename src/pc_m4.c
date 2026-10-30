/* Scenes experiment (bluebox-scenes), built on the PC-map experiment.
 * - Scenes: up to 16 snapshots of the mixer + FX state per project, saved and
 *   recalled from the PROJ File menu or by MIDI Program Change (program 0 =
 *   scene 1). Recall replays changed values through the native knob path, so
 *   sound and screen follow without loading anything. Stored as <scenes> in
 *   the project XML. See ../SCENES.md.
 * - Banks: MIDI Bank Select (CC 0) loads the project with that number (value 0
 *   = Bank 1), numbers assigned with File > Bank # (PC-map machinery: FNV-1a
 *   hashes of the lower-cased project name in settings.tml tags 0x20b+n,
 *   attributes pcmap0..pcmap127; see ../../bluebox-pc-map/INVESTIGATION.md).
 * Built as its own payload, so Cobalt's M4 code layout is unchanged. */
#include "shared.h"
typedef int (*get_fn)(void *,unsigned);
#define fw_get ((get_fn)0x08120235u)
typedef void (*def_fn)(void *,unsigned,unsigned,const char *,int,int,const char *);
#define STOCK_DEF ((def_fn)0x08134c8du)
typedef void (*add_fn)(void *,unsigned,int);
#define STOCK_ADD ((add_fn)0x0812028fu)
void sat_defs(void *table,unsigned id,unsigned type,const char *label,int min,int max,const char *key);
static void sc_boot(void);
#define PC_TAG0 0x20bu
#define PC_SLOTS 128u
#define PC_EVT 0x1f0u /* A-menu items 0x1f0 Bank #, 0x1f1 Save Scene, 0x1f2 Scene */
#define PC_BANK_CC 0x28bu /* settings tag "bankcc": 0 = Bank Select MSB (CC 0), 1 = LSB (CC 32) */
#define PC_SCENE_ROW 0x28cu /* scene page rows; values live in the scene bank */
#define PC_SAVE_ROW 0x28du
#define PC_BANKNUM_ROW 0x28eu
#define PC_CH 0x28fu /* settings "PC Ch" (key scenech): 0 All, 1 None, 2..17 = MIDI channel 1..16;
                         filters both Bank Select and Program Change */
#define PC_BANK_CH 0x290u /* s3/s3.1 "Bank Ch": unused since s3.2, kept so settings.tml still parses */
#define PC_MIDI_ROW 0x291u /* Tools row "MIDI Settings": a tap, or B with the row selected, opens the MIDI page */
#define PC_DIN_OUT 0x292u /* m3 MIDI page "DIN Out" (key dinout): 0 Default, 1 USB Thru, 2 All Thru */
#define PC_CC_SYNC 0x293u /* m8 MIDI page "Map CC Sync" (key ccsync, was m3 dinsync): 0 Off, 1 USB, 2 USB+MIDI Out, 3 MIDI Out */
#define PC_CLK_SRC 0x294u /* m6 MIDI page "Clock Src" (key clksrc): 0 MIDI In first, 1 USB first */
#define PC_LAST_TAG 0x294u
#define PC_CELL_SCENES 0x17u /* edit page (B page) cells of our own */
#define PC_CELL_MIDI 0x1bu
#define PC_PAGE_MAIN 8u
#define PC_PAGE_PROJ 0x18u
#define PC_PAGE_PROJSET 0x19u /* B on PROJ: edit page for cell 0x16 */
#define PC_MSG 0x1000u /* IRQ messages: 0x1000 | channel<<8 | value */
#define PC_SETTLE_MS 300u /* Bank Selects closer together than this: only the last one loads */
#define PC_RESTART_MS 300u /* after a load completes, before restarting playback */
#define PC_CLOCK_MS 500u /* MIDI realtime seen this recently = external clock */
/* pending: Program Change from the IRQ (PC_MSG). want: Bank Select waiting to
 * load (PC_MSG), since: UI ms clock of its arrival. page: 0x100|page to return
 * to after a bank load. kb/kb_hash/kp_mode: unused since s3. */
struct pc_state {uint32_t magic,pending,kb,kb_hash,want,since,page,kp_mode;};
#define PC_STATE ((volatile struct pc_state *)0x38800fe0u)
#define PC_MAGIC 0x50434d31u
_Static_assert(0xfe0+sizeof(struct pc_state)<=0x1000,"PC state inside backup SRAM");
/* clock_ms: UI ms clock of the last MIDI realtime byte (IRQ). phase: bank load
 * sequence (PC_PHASE_*, plus PC_RESTART when Play follows); posted: phase
 * start; quiet: last time a load was still running. pos/moved: engine
 * position sample, last time it moved. bank / bank_lsb: Bank Select MSB (CC 0)
 * / LSB (CC 32) from the IRQ (0x100|value). The 32-byte line 0xfa0..0xfbf is M4-only (the M7 caches
 * backup SRAM; Cobalt's M7 saturation state owns the 0xf80 line). */
struct pc_transport {uint32_t clock_ms,phase,posted,quiet,pos,moved,bank,bank_lsb;};
#define PC_TRANSPORT ((volatile struct pc_transport *)0x38800fa0u)
_Static_assert(sizeof(struct pc_transport)==32,"PC transport fills exactly the 0xfa0 cache line");
_Static_assert(__builtin_offsetof(struct pc_transport,bank)==0x18 && __builtin_offsetof(struct pc_transport,bank_lsb)==0x1c,"CC stub writes 0x38800fb8 / 0x38800fbc");
#define PC_PHASE_POSTED 2u /* Load event posted, load not seen running yet */
#define PC_PHASE_LOADING 3u /* load seen running */
#define PC_RESTART 0x10u
#define PC_SESSION ((uint8_t *)0x300173d0u)
#define PC_SETTINGS (PC_SESSION+0x737c)
#define PC_LIST ((void *)0x300172fcu)
#define PC_LIST_NAME ((int (*)(void *,unsigned,char *))0x0810204fu)
#define PC_TAG_SET ((void (*)(void *,unsigned,int))0x08120265u)
#define PC_SAVE_FILE ((void (*)(void *,const char *,void *))0x0811e921u)
#define PC_POST ((void (*)(void *,void *))0x0813a415u)
#define PC_KB_SET ((void (*)(void *,const char *))0x081405adu)
#define PC_KB_GET ((int (*)(void *,char *))0x08140a5du)
#define PC_MENU_ADD ((void (*)(void *,const char *,unsigned,unsigned))0x0813dd91u)
#define PC_KP_SET ((void (*)(void *,const char *))0x0813ca95u)
#define PC_KP_GET ((int (*)(void *,char *))0x0813cb79u)
#define PC_UI_NOW (*(volatile uint32_t *)0x3001eb3cu) /* native UI millisecond clock */
#define PC_PAGE (PC_SESSION[0x739a]) /* current page (0x08122508 bookkeeping) */
/* 0x73d6 record requested, 0x73d7 recording (start 0x08123694; both cleared on stop and by LoadBank) */
#define PC_RECORDING (PC_SESSION[0x73d6]!=0 || PC_SESSION[0x73d7]!=0)
#define PC_ENGINE (*(volatile uint32_t *)0x3000a894u)
#define PC_APP (*(volatile uint32_t *)0x3000d9fcu)
struct pc_event {uint16_t id,pad0;uint32_t vtable;uint16_t sub,pad1;uint32_t a,b,c;};

static volatile struct pc_state *pc_state(void) {
    backup_enable();PWR_CR1|=1u<<8;
    volatile struct pc_state *s=PC_STATE;
    if(s->magic!=PC_MAGIC){
        s->pending=0;s->kb=0;s->kb_hash=0;s->want=0;s->since=0;s->page=0;s->kp_mode=0;
        volatile struct pc_transport *t=PC_TRANSPORT;
        t->clock_ms=0;t->phase=0;t->posted=0;t->quiet=0;t->pos=0;t->moved=0;t->bank=0;t->bank_lsb=0;
        s->magic=PC_MAGIC;
    }
    return s;
}
static uint32_t pc_hash(const char *s) {
    uint32_t h=2166136261u;
    for(;*s;s++){unsigned c=(uint8_t)*s;if(c>='A'&&c<='Z')c+=32u;h=(h^c)*16777619u;}
    return h?h:1u;
}
static int pc_slot_of(uint32_t h) {
    for(unsigned i=0;i<PC_SLOTS;i++)if((uint32_t)fw_get(PC_SETTINGS,PC_TAG0+i)==h)return (int)i;
    return -1;
}
static void pc_save(void) {PC_SAVE_FILE((void *)0x30016fd0u,(const char *)0x08146914u,PC_SETTINGS);}
static int pc_loading(void) {
    const char *name=*(const char *const *)0x3001734cu; /* LoadBank's pending-load name */
    return name && *name;
}
static int pc_ram(uint32_t a,uint32_t n) {
    return (a>=0x24000000u && n<=0x80000u && a<=0x24080000u-n) ||
           (a>=0x30000000u && n<=0x48000u && a<=0x30048000u-n) ||
           (a>=0xc0000000u && n<=0x4000000u && a<=0xc4000000u-n);
}
static int pc_forget(uint32_t h) {
    int changed=0;
    for(unsigned i=0;i<PC_SLOTS;i++)
        if((uint32_t)fw_get(PC_SETTINGS,PC_TAG0+i)==h){PC_TAG_SET(PC_SETTINGS,PC_TAG0+i,0);changed=1;}
    return changed;
}

/* Name table entries so settings.tml keeps the slots (unnamed tags are dropped). */
static const char *const pc_keys[PC_SLOTS]={
    "pcmap0", "pcmap1", "pcmap2", "pcmap3", "pcmap4", "pcmap5", "pcmap6", "pcmap7",
    "pcmap8", "pcmap9", "pcmap10", "pcmap11", "pcmap12", "pcmap13", "pcmap14", "pcmap15",
    "pcmap16", "pcmap17", "pcmap18", "pcmap19", "pcmap20", "pcmap21", "pcmap22", "pcmap23",
    "pcmap24", "pcmap25", "pcmap26", "pcmap27", "pcmap28", "pcmap29", "pcmap30", "pcmap31",
    "pcmap32", "pcmap33", "pcmap34", "pcmap35", "pcmap36", "pcmap37", "pcmap38", "pcmap39",
    "pcmap40", "pcmap41", "pcmap42", "pcmap43", "pcmap44", "pcmap45", "pcmap46", "pcmap47",
    "pcmap48", "pcmap49", "pcmap50", "pcmap51", "pcmap52", "pcmap53", "pcmap54", "pcmap55",
    "pcmap56", "pcmap57", "pcmap58", "pcmap59", "pcmap60", "pcmap61", "pcmap62", "pcmap63",
    "pcmap64", "pcmap65", "pcmap66", "pcmap67", "pcmap68", "pcmap69", "pcmap70", "pcmap71",
    "pcmap72", "pcmap73", "pcmap74", "pcmap75", "pcmap76", "pcmap77", "pcmap78", "pcmap79",
    "pcmap80", "pcmap81", "pcmap82", "pcmap83", "pcmap84", "pcmap85", "pcmap86", "pcmap87",
    "pcmap88", "pcmap89", "pcmap90", "pcmap91", "pcmap92", "pcmap93", "pcmap94", "pcmap95",
    "pcmap96", "pcmap97", "pcmap98", "pcmap99", "pcmap100", "pcmap101", "pcmap102", "pcmap103",
    "pcmap104", "pcmap105", "pcmap106", "pcmap107", "pcmap108", "pcmap109", "pcmap110", "pcmap111",
    "pcmap112", "pcmap113", "pcmap114", "pcmap115", "pcmap116", "pcmap117", "pcmap118", "pcmap119",
    "pcmap120", "pcmap121", "pcmap122", "pcmap123", "pcmap124", "pcmap125", "pcmap126", "pcmap127"
};
/* Chained after Cobalt's sat_defs on the same stock call (0x08135fec), so the
 * Cobalt M4 code keeps its hardware-tested layout. */
void pc_defs_hook(void *table,unsigned id,unsigned type,const char *label,int min,int max,const char *key) {
    sat_defs(table,id,type,label,min,max,key);
    for(unsigned i=0;i<PC_SLOTS;i++)STOCK_DEF(table,PC_TAG0+i,1,"PC slot:",-2147483647-1,2147483647,pc_keys[i]);
    typedef void (*enum_fn)(void *,unsigned,const char *,const char *const *,unsigned,const char *);
    enum_fn def_enum=(enum_fn)0x08134c39u;
    static const char *const bank_cc[]={"MSB CC0","LSB CC32"};
    static const char *const channels[18]={"All","None","1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16"};
    static const char *const midi_open[1]={""};
    static const char *const bank_num[PC_SLOTS+1]={
    "None", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15",
    "16", "17", "18", "19", "20", "21", "22", "23", "24", "25", "26", "27", "28", "29", "30", "31",
    "32", "33", "34", "35", "36", "37", "38", "39", "40", "41", "42", "43", "44", "45", "46", "47",
    "48", "49", "50", "51", "52", "53", "54", "55", "56", "57", "58", "59", "60", "61", "62", "63",
    "64", "65", "66", "67", "68", "69", "70", "71", "72", "73", "74", "75", "76", "77", "78", "79",
    "80", "81", "82", "83", "84", "85", "86", "87", "88", "89", "90", "91", "92", "93", "94", "95",
    "96", "97", "98", "99", "100", "101", "102", "103", "104", "105", "106", "107", "108", "109", "110", "111",
    "112", "113", "114", "115", "116", "117", "118", "119", "120", "121", "122", "123", "124", "125", "126", "127",
    "128"
    };
    def_enum(table,PC_BANK_CC,"Bank CC:",bank_cc,2,"bankcc");
    STOCK_DEF(table,PC_SCENE_ROW,1,"Load Scene:",1,16,"scenesel");
    STOCK_DEF(table,PC_SAVE_ROW,1,"Save Scene:",1,16,"scenesave");
    def_enum(table,PC_BANKNUM_ROW,"Bank #:",bank_num,PC_SLOTS+1,"bankassign"); /* 0 shows "None" */
    def_enum(table,PC_CH,"PC Ch:",channels,18,"scenech");
    def_enum(table,PC_BANK_CH,"Bank Ch:",channels,18,"bankch");
    def_enum(table,PC_MIDI_ROW,"MIDI Settings",midi_open,1,"midimenu"); /* one value: turning does nothing */
    static const char *const din_out[3]={"Default","USB Thru","All Thru"};
    static const char *const cc_sync[4]={"Off","USB","USB+MIDI Out","MIDI Out"};
    def_enum(table,PC_DIN_OUT,"MIDI Out:",din_out,3,"dinout");
    def_enum(table,PC_CC_SYNC,"Map CC Sync:",cc_sync,4,"ccsync");
    static const char *const clk_src[2]={"MIDI In","USB"};
    def_enum(table,PC_CLK_SRC,"Clock Src:",clk_src,2,"clksrc");
    sc_boot(); /* runs once per boot, before the main loop */
}
/* settings.tml defaults: the reader only fills tags present here. */
void pc_settings_defaults(void *set,unsigned id,int value) {
    STOCK_ADD(set,id,value);
    for(unsigned i=0;i<PC_SLOTS;i++)STOCK_ADD(set,PC_TAG0+i,0);
    STOCK_ADD(set,PC_BANK_CC,0);   /* MIDI page */
    STOCK_ADD(set,PC_CH,0);        /* All */
    STOCK_ADD(set,PC_BANK_CH,0);   /* unused */
    STOCK_ADD(set,PC_MIDI_ROW,0);  /* Tools row; a tap opens the MIDI page */
    STOCK_ADD(set,PC_DIN_OUT,0);   /* MIDI page: Default */
    STOCK_ADD(set,PC_CC_SYNC,1);   /* MIDI page: USB */
    STOCK_ADD(set,PC_CLK_SRC,0);   /* MIDI page: MIDI In first */
}

/* MIDI poll (TIM4 IRQ) at 0x0810341a: r0=status, r4=message (data1 at +1).
 * Program Change stores 0x1000|channel<<8|program for a scene recall (the
 * channel is filtered in the main loop); stock forwarding is unchanged. */
__attribute__((naked)) void pc_midi_isr(void) {
    __asm volatile(
    "cmp r0,#0xc0\n"
    "bne 1f\n"
    "ldrb r1,[r4,#1]\n"
    "and r1,r1,#0x7f\n"
    "ldrb r2,[r4]\n"
    "and r2,r2,#0x0f\n"
    "orr r1,r1,r2,lsl #8\n"
    "orr r1,r1,#0x1000\n"
    "movw r2,#0x0fe4\n"
    "movt r2,#0x3880\n"
    "str r1,[r2]\n"
    "b.w 0x08103386\n"
    "1: b.w 0x081033ba\n");
}

/* MIDI poll at 0x08103378 (cmp r0,#0xb0; beq 0x08103386): Bank Select MSB
 * (CC 0) and LSB (CC 32), any channel, store their values (the Tools "Bank CC"
 * setting picks one in the main loop); flags of the replayed cmp are kept for
 * the stock bhi at 0x0810337c. */
__attribute__((naked)) void pc_midi_cc(void) {
    __asm volatile(
    "cmp r0,#0xb0\n"
    "bne.w 0x0810337c\n"
    "ldrb r1,[r4,#1]\n"
    "movw r2,#0x0fb8\n"
    "cbz r1,2f\n"
    "cmp r1,#32\n"
    "bne 1f\n"
    "movw r2,#0x0fbc\n"
    "2: movt r2,#0x3880\n"
    "ldrb r1,[r4,#2]\n"
    "and r1,r1,#0x7f\n"
    "ldrb r3,[r4]\n"
    "and r3,r3,#0x0f\n"
    "orr r1,r1,r3,lsl #8\n"
    "orr r1,r1,#0x1000\n"
    "str r1,[r2]\n"
    "1: b.w 0x08103386\n");
}

/* MIDI poll realtime path (clock, Start/Continue/Stop) at 0x081033e0: replay
 * movs r3,#0; str.w r3,[sp,#0x11], stamp the UI ms clock, resume at 0x081033e6. */
__attribute__((naked)) void pc_midi_realtime(void) {
    __asm volatile(
    "movs r3,#0\n"
    "str.w r3,[sp,#0x11]\n"
    "movw r1,#0xeb3c\n"
    "movt r1,#0x3001\n"
    "ldr r1,[r1]\n"
    "movw r2,#0x0fa0\n"
    "movt r2,#0x3880\n"
    "str r1,[r2]\n"
    "b.w 0x081033e6\n");
}

/* Front-panel event ring (0x30000180): same producer pattern as the knob scan. */
static void pc_post_load(unsigned index) {
    uint8_t *base=(uint8_t *)0x30000180u;
    unsigned k=*(volatile uint32_t *)(base+0x1488)&0x3fu;
    uint8_t *e=base+k*24u;
    *(volatile uint16_t *)(e+0xe84)=0x18;
    *(volatile uint16_t *)(e+0xe8c)=0;
    *(volatile uint32_t *)(e+0xe90)=index;
    *(volatile uint32_t *)(e+0xe94)=0;
    *(volatile uint32_t *)(e+0xe98)=0;
    ((void (*)(void *))0x08100b19u)(base+0x1488);
}
/* Engine transport position (as read by 0x08121fc4): moving = playing. */
static int pc_playing(volatile struct pc_transport *t,uint32_t now) {
    uint32_t e=PC_ENGINE;
    if(!pc_ram(e,0x7900u))return 0;
    uint32_t pos=*(volatile uint32_t *)(e+0x78b0u)^*(volatile uint32_t *)(e+0x78b8u);
    if(pos!=t->pos){t->pos=pos;t->moved=now;}
    return now-t->moved<100u;
}
/* PLAY key (event 0xf5 at 0x08123cba): engine command 0x7a/0x18, record=0. */
static void pc_play(void) {
    uint32_t e=PC_ENGINE;
    if(pc_ram(e,0x2000u))((void (*)(void *,unsigned,unsigned,unsigned,unsigned))0x081087e9u)((void *)e,0x7a,0x18,0,0);
}
static void pc_sequence(volatile struct pc_transport *t,uint32_t now) {
    uint32_t phase=t->phase&0xfu;
    if(pc_loading()){t->phase=(t->phase&PC_RESTART)|PC_PHASE_LOADING;t->quiet=now;}
    else if(phase==PC_PHASE_LOADING && now-t->quiet>=PC_RESTART_MS){
        if(t->phase&PC_RESTART)pc_play();
        t->phase=0;
        return;
    }
    if(now-t->posted>15000u)t->phase=0;
}
static void sc_main(int busy);
static void sc_request(unsigned program);
static void pc_quicksave(uint32_t now);
static void pc_autosave(int busy);
static void pc_band_refresh(uint32_t now);
static void pc_list_scroll(uint32_t now);
static void mo_main(uint32_t now);
#define MO_TO_USB 1u /* dump targets */
#define MO_TO_DIN 2u
static void mo_request(uint32_t now,uint32_t delay,unsigned to);
static char *mo_message(char *d,unsigned n);
static int pc_channel_ok(unsigned tag,uint32_t msg) {
    unsigned v=(unsigned)fw_get(PC_SETTINGS,tag);
    return v==0u || (v>=2u && ((msg>>8)&0xfu)==v-2u);
}
/* Hash of the project with Bank # value+1, 0 if unassigned or already loaded. */
static uint32_t pc_bank_target(unsigned value) {
    uint32_t h=(uint32_t)fw_get(PC_SETTINGS,PC_TAG0+(value&0x7fu));
    if(!h)return 0;
    char cur[256];cur[0]=0;
    ((void (*)(void *,char *))0x08122459u)(PC_SESSION,cur); /* current project name */
    return cur[0] && pc_hash(cur)==h ? 0 : h;
}
/* Main loop 0x081269d8: knob scan, then
 * - Bank Select (channel per Tools > MIDI): after PC_SETTLE_MS without another
 *   one, post a Load (event 0x18 = LoadBank(index), what the browser's Load
 *   button posts) for the project with that Bank #. Ignored while recording,
 *   while a load runs, and for the project already loaded. If the internal
 *   transport was playing, Play follows once the load is done (external clock:
 *   wait for the master's Start).
 * - Program Change (its channel): recall a scene; deferred until a bank load
 *   is done.
 * - Hold A on MAIN: quick save; a scene save then saves the project.
 * - Keeps the message band on screen (pc_band_refresh). */
void pc_main(void *inputs) {
    ((void (*)(void *))0x08100731u)(inputs);
    volatile struct pc_state *s=pc_state();
    volatile struct pc_transport *t=PC_TRANSPORT;
    uint32_t primask;
    __asm volatile("mrs %0,primask\ncpsid i":"=r"(primask)::"memory");
    uint32_t p=s->pending,msb=t->bank,lsb=t->bank_lsb;s->pending=0;t->bank=0;t->bank_lsb=0;
    __asm volatile("msr primask,%0"::"r"(primask):"memory");
    uint32_t now=PC_UI_NOW;
    int playing=pc_playing(t,now);
    if(t->phase)pc_sequence(t,now);
    int busy=PC_RECORDING || pc_loading() || t->phase;
    uint32_t bank=fw_get(PC_SETTINGS,PC_BANK_CC)==1 ? lsb : msb;
    if((bank&PC_MSG) && pc_channel_ok(PC_CH,bank)){
        if(busy || !pc_bank_target(bank))s->want=0;
        else {s->want=bank;s->since=now;}
    }
    if((p&PC_MSG) && pc_channel_ok(PC_CH,p))sc_request(p&0x7fu);
    if(s->want&PC_MSG){
        if(busy)s->want=0;
        else if(now-s->since>=PC_SETTLE_MS){
            uint32_t h=pc_bank_target(s->want);
            s->want=0;
            char name[256];
            if(h)for(unsigned i=0;PC_LIST_NAME(PC_LIST,i,name);i++)
                if(pc_hash(name)==h){
                    int external=t->clock_ms && now-t->clock_ms<PC_CLOCK_MS;
                    s->page=0x100u|PC_PAGE;
                    t->posted=now;t->quiet=now;
                    t->phase=PC_PHASE_POSTED|(playing && !external ? PC_RESTART : 0u);
                    pc_post_load(i);
                    break;
                }
        }
    }
    sc_main(pc_loading() || t->phase || (s->want&PC_MSG));
    pc_quicksave(now);
    pc_autosave(PC_RECORDING || pc_loading() || t->phase || (s->want&PC_MSG));
    pc_band_refresh(now);
    pc_list_scroll(now);
    mo_main(now);
}
/* Load event 0x18 then shows MIXER (0x08125e2a). After a PC load, show the
 * page that was current instead (keyboard 0x0a / keypad 0x1e overlays excepted). */
void pc_after_load(void *session,unsigned page,unsigned a,unsigned b) {
    volatile struct pc_state *s=pc_state();
    uint32_t back=s->page;s->page=0;
    if(back&0x100u){unsigned p=back&0xffu;if(p && p!=0x0au && p!=0x1eu)page=p;}
    ((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)(session,page,a,b);
}
/* Tools (settings) panel lists every settings tag (0x08121e82): keep the PC slots out. */
void pc_tools_append(void *list,unsigned id,int value) {
    if(id>=PC_TAG0 && id<=PC_LAST_TAG && id!=PC_MIDI_ROW)return; /* Bank # slots and the MIDI page settings */
    if(id==0x117u)return; /* MIDIStopRcv: moved to the MIDI page */
    ((void (*)(void *,unsigned,int))0x08138547u)(list,id,value);
}

/* ------------------------------------------------- Scene page, MIDI page
 * Both are the stock edit page (the B page: view at app+0x1e978, event 0xab
 * with a cell) showing cells of our own: rows from pc_page_rows (builder call
 * 0x0813d20a), edits caught in pc_knob (event 0x6e -> knob path, 0x08125e04).
 * - Scene page: A on PROJ opens it as page 0x19 (the project-settings page)
 *   with cell 0x17 instead of 0x16 (pc_projset_post at 0x081227f8). Rows
 *   Load Scene / Save Scene / Bank #; encoders pick numbers, the Select button
 *   confirms the selected row, A leaves.
 * - MIDI page: a tap on the Tools "MIDI Settings" row, or B on Tools, opens
 *   it with cell 0x1b. Its edits are applied as settings (cell 0x18) and
 *   saved. */
struct sc_bank;
static struct sc_bank *sc_bank(void);
static unsigned sc_current(void);
static int sc_next_free(void);
static int sc_used(unsigned scene);
static void sc_save(unsigned scene);
static void sc_recall(unsigned scene);
static uint32_t *sc_field(unsigned which);
static char *sc_name(void);
static void sc_clear(void);
enum {F_SEL_SCENE,F_SEL_SAVE,F_SEL_BANK,F_BANK_HASH,F_PAGE,F_ABTN,F_HELD,F_DONE,F_TOAST,F_TOAST_UNTIL,
      F_AUTOSAVE,F_BAND,F_REDRAWN,F_SCROLL_KEY,F_SCROLL_AT,F_SCROLL_OFF};
#define PC_TOAST(kind,n) (((kind)<<8)|(n))
enum {T_RECALL=1,T_EMPTY,T_SAVED,T_NOSLOTS,T_BANK,T_BANK_CLEAR,T_BANK_USED,T_MOUT,T_CLOCK};
static void pc_toast(unsigned kind,unsigned n) {
    uint32_t *t=sc_field(F_TOAST),*u=sc_field(F_TOAST_UNTIL);
    if(t && u){*t=PC_TOAST(kind,n);*u=PC_UI_NOW+1500u;}
}
#define PC_EDIT_VT 0x0814ca10u
static uint8_t *pc_app_view(uint32_t offset,uint32_t vtable) {
    uint32_t app=PC_APP;
    if(!pc_ram(app,0x150cb0u))return 0;
    uint8_t *v=(uint8_t *)(app+offset);
    return *(uint32_t *)v==vtable ? v : 0;
}
static void pc_post_ui(unsigned id,unsigned sub) {
    struct pc_event e={(uint16_t)id,0,0x08148664u,(uint16_t)sub,0,0,0,0};
    ((void (*)(void *,void *))0x0813c385u)(PC_SESSION,&e);
}
/* Page 0x19's case posts {0xab, cell 0x16}: show our cell while the scene page is open. */
void pc_projset_post(void *session,struct pc_event *e) {
    uint32_t *page=sc_field(F_PAGE);
    if(page && *page && e->id==0xabu)e->sub=PC_CELL_SCENES;
    ((void (*)(void *,void *))0x0813c385u)(session,e);
}
static void pc_open_scenes(void) {
    uint32_t *page=sc_field(F_PAGE);
    uint8_t *browser=pc_app_view(0x28d60u,0x0814cb14u);
    if(!page)return;
    unsigned cur=sc_current();
    int next=sc_next_free();
    *sc_field(F_SEL_SCENE)=cur ? cur : 1u;
    *sc_field(F_SEL_SAVE)=next>=0 ? (unsigned)next+1u : (cur ? cur : 1u);
    *sc_field(F_SEL_BANK)=0;*sc_field(F_BANK_HASH)=0;
    char name[256],*keep=sc_name();
    keep[0]=0;
    if(browser && PC_LIST_NAME(PC_LIST,*(uint32_t *)(browser+0x614),name)){
        uint32_t h=pc_hash(name);
        *sc_field(F_BANK_HASH)=h;
        *sc_field(F_SEL_BANK)=(uint32_t)(pc_slot_of(h)+1);
        unsigned i=0;
        for(;name[i] && i<31u;i++)keep[i]=name[i];
        keep[i]=0;
    }
    *page=1;
    ((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)(PC_SESSION,PC_PAGE_PROJSET,0,0);
}
static void pc_open_midi(void) {pc_post_ui(0xabu,PC_CELL_MIDI);}
/* The edit page's bottom bar (view+0x34) holds one button (view+0x888, id at
 * +0x14) that stock leaves empty, full width. The scene page labels it
 * "Select" at the right, where the browser's Load button sits; a tap posts
 * event 2 with the id (caught in pc_edit_event). */
#define PC_SELECT_ID 0x2a0u
static void pc_select_button(int on) {
    uint8_t *edit=pc_app_view(0x1e978u,PC_EDIT_VT);
    if(!edit)return;
    uint8_t *b=edit+0x888u;
    ((void (*)(void *,const char *))0x0813820du)(b,on ? "Select" : "");
    *(uint32_t *)(b+0x14u)=on ? PC_SELECT_ID : 0u;
    int32_t r[4]={on ? 226 : 0,182,on ? 93 : 320,38};
    ((void (*)(void *,int32_t *))*(uint32_t *)(*(uint32_t *)b+0x20u))(b,r);
}
/* Edit page builder (0x0813d20a). */
int pc_page_rows(void *session,uint16_t *cell,void *list) {
    typedef void (*append_fn)(void *,unsigned,int);
    append_fn append=(append_fn)0x08138547u;
    pc_select_button(*cell==PC_CELL_SCENES && sc_bank());
    if(*cell==PC_CELL_SCENES && sc_bank()){
        *(uint32_t *)list=0;
        append(list,PC_SCENE_ROW,(int)*sc_field(F_SEL_SCENE));
        append(list,PC_SAVE_ROW,(int)*sc_field(F_SEL_SAVE));
        append(list,PC_BANKNUM_ROW,(int)*sc_field(F_SEL_BANK));
        return 1;
    }
    if(*cell==PC_CELL_MIDI){
        static const uint16_t ids[]={PC_BANK_CC,PC_CH,0x117,PC_DIN_OUT,PC_CC_SYNC,PC_CLK_SRC};
        *(uint32_t *)list=0;
        for(unsigned i=0;i<sizeof ids/sizeof ids[0];i++)append(list,ids[i],fw_get(PC_SETTINGS,ids[i]));
        return 1;
    }
    return ((int (*)(void *,uint16_t *,void *))0x08121cf5u)(session,cell,list);
}
/* Bank # n (1-128) already belongs to a project other than h. */
static int pc_bank_taken(unsigned n,uint32_t h) {
    if(!n || n>PC_SLOTS)return 0;
    uint32_t owner=(uint32_t)fw_get(PC_SETTINGS,PC_TAG0+n-1u);
    return owner && owner!=h;
}
static unsigned pc_clamp(int v,unsigned lo,unsigned hi) {return v<(int)lo ? lo : v>(int)hi ? hi : (unsigned)v;}
/* Event 0x6e -> knob path (0x08125e04): our cells never reach the engine. */
void pc_knob(void *session,uint16_t *cell,unsigned id,int value,int extra) {
    typedef void (*knob_fn)(void *,uint16_t *,unsigned,int,int);
    knob_fn knob=(knob_fn)0x08124b45u;
    if(*cell==PC_CELL_SCENES){
        if(!sc_bank())return;
        if(id==PC_SCENE_ROW)*sc_field(F_SEL_SCENE)=pc_clamp(value,1,16);
        else if(id==PC_SAVE_ROW)*sc_field(F_SEL_SAVE)=pc_clamp(value,1,16);
        else if(id==PC_BANKNUM_ROW){
            unsigned n=pc_clamp(value,0,PC_SLOTS);
            *sc_field(F_SEL_BANK)=n;
            if(pc_bank_taken(n,*sc_field(F_BANK_HASH)))pc_toast(T_BANK_USED,n); /* warn while turning */
            else if(*sc_field(F_TOAST)>>8==T_BANK_USED)*sc_field(F_TOAST_UNTIL)=PC_UI_NOW;
        }
        return;
    }
    if(*cell==PC_CELL_MIDI){
        uint16_t settings=0x18;
        knob(session,&settings,id,value,extra);
        pc_save();
        return;
    }
    knob(session,cell,id,value,extra);
}
/* Tools view event method (vtable 0x0814ba3c, stock 0x08131210): the "MIDI:"
 * row has no value to change; swallow its edits (0x3c/0x3d, id at +0x10). */
int pc_tools_event(void *view,struct pc_event *e) {
    if((e->id==0x3cu || e->id==0x3du) && e->b==PC_MIDI_ROW)return 0;
    return ((int (*)(void *,void *))0x08131211u)(view,e);
}
/* List widget event method (vtable slot 0x0814cac4, stock 0x0813d89c, shared by
 * every row list): a tap posts 0x145 with the row index. After the stock
 * selection, a tap on the MIDI row (only the Tools list has one) opens the
 * MIDI page. Rows: list+0xe8+i*0x330, id at +0x32c; count at list+0x99e8. */
int pc_list_event(uint8_t *list,struct pc_event *e) {
    int r=((int (*)(void *,void *))0x0813d89du)(list,e);
    if(e->id==0x145u && e->a<*(uint32_t *)(list+0x99e8u) &&
       *(uint16_t *)(list+0xe8u+e->a*0x330u+0x32cu)==PC_MIDI_ROW)pc_open_midi();
    return r;
}
/* Select on the scene page: act on the selected row. */
static void pc_scene_action(void) {
    uint8_t *edit=pc_app_view(0x1e978u,PC_EDIT_VT);
    if(!edit)return;
    unsigned row=*(uint32_t *)(edit+0x9c8u+0x99ecu);
    if(row==0){
        unsigned n=*sc_field(F_SEL_SCENE);
        if(sc_used(n)){sc_recall(n);pc_toast(T_RECALL,n);} else pc_toast(T_EMPTY,n);
    } else if(row==1){
        unsigned n=*sc_field(F_SEL_SAVE);
        sc_save(n);pc_toast(T_SAVED,n);*sc_field(F_AUTOSAVE)=1;
    } else if(row==2){
        uint32_t h=*sc_field(F_BANK_HASH);
        unsigned n=*sc_field(F_SEL_BANK);
        if(!h)return;
        if(pc_bank_taken(n,h)){pc_toast(T_BANK_USED,n);return;}
        pc_forget(h);
        if(n){PC_TAG_SET(PC_SETTINGS,PC_TAG0+n-1u,(int)h);pc_toast(T_BANK,n);} else pc_toast(T_BANK_CLEAR,0);
        pc_save();
    }
}
/* Edit page event method (vtable slot 0x0814ca40, stock 0x0813cfb8): the
 * scene page's Select button. */
int pc_edit_event(void *view,struct pc_event *e) {
    if(e->id==2u && e->a==PC_SELECT_ID){
        uint32_t *page=sc_field(F_PAGE);
        if(page && *page && PC_PAGE==PC_PAGE_PROJSET)pc_scene_action();
        return 0;
    }
    return ((int (*)(void *,void *))0x0813cfb9u)(view,e);
}
/* A-button default path 0x08123902 (bl 0x08123818, r1=0) for every page:
 * PROJ opens the scene page; on the scene page A leaves it (back to PROJ). */
void pc_button_a(void *session,unsigned arg) {
    ((void (*)(void *,unsigned))0x08123819u)(session,arg);
    uint32_t *page=sc_field(F_PAGE);
    if(!page)return;
    if(PC_PAGE==PC_PAGE_PROJ)pc_open_scenes();
    else if(PC_PAGE==PC_PAGE_PROJSET && *page){
        *page=0;
        ((void (*)(void *,unsigned,unsigned,unsigned))0x08122509u)(PC_SESSION,PC_PAGE_PROJ,0,0);
    }
}
/* B-button default path 0x081239e0 (bl 0x08123818, r1=0). The root view
 * (app+0x280) keeps the shown view at +0x34; Tools is app+0x119a88 (its row
 * list at +0x590), the edit page app+0x1e978 (cell at +0xa3e0).
 * - Tools with the "MIDI Settings" row selected: open the MIDI page.
 * - MIDI page: back to Tools (UI event 0xcb shows the Tools view). */
void pc_button_b(void *session,unsigned arg) {
    uint32_t app=PC_APP;
    unsigned go=0;
    if(pc_ram(app,0x150cb0u)){
        uint32_t shown=*(uint32_t *)(app+0x2b4u);
        uint8_t *list=(uint8_t *)(app+0x119a88u+0x590u);
        uint32_t sel=*(uint32_t *)(list+0x99ecu);
        if(shown==app+0x119a88u && sel<*(uint32_t *)(list+0x99e8u) &&
           *(uint16_t *)(list+0xe8u+sel*0x330u+0x32cu)==PC_MIDI_ROW)go=1;
        else if(shown==app+0x1e978u && *(uint16_t *)(app+0x1e978u+0xa3e0u)==PC_CELL_MIDI)go=2;
    }
    ((void (*)(void *,unsigned))0x08123819u)(session,arg);
    if(go==1)pc_open_midi();
    else if(go==2)pc_post_ui(0xcbu,0);
}
/* PROJ Save button (0x0813e4a8, bl SessionMgr::Save 0x081224d4): a Bank #
 * chosen on the scene page for the loaded project is assigned on save
 * (None: the project keeps its number; a number another project has: not
 * applied, "BANK n IN USE"). */
void pc_project_save(void *session) {
    uint32_t *sel=sc_field(F_SEL_BANK),*hash=sc_field(F_BANK_HASH);
    if(sel && *sel && *sel<=PC_SLOTS && *hash){
        char cur[256];cur[0]=0;
        ((void (*)(void *,char *))0x08122459u)(PC_SESSION,cur);
        uint32_t owner=(uint32_t)fw_get(PC_SETTINGS,PC_TAG0+*sel-1u);
        if(cur[0] && pc_hash(cur)==*hash && owner && owner!=*hash)pc_toast(T_BANK_USED,*sel);
        else if(cur[0] && pc_hash(cur)==*hash && owner!=*hash){
            pc_forget(*hash);
            PC_TAG_SET(PC_SETTINGS,PC_TAG0+*sel-1u,(int)*hash);
            pc_save();
            pc_toast(T_BANK,*sel);
        }
    }
    ((void (*)(void *))0x081224d5u)(session);
}
/* Quick save: hold A for 2 s on MAIN (page 8). A's button entry is the one
 * whose press id is 0x0c in the debounced button table 0x30009d80 (13
 * buttons of 24 B, set up at 0x081047a0: 0-7 track keys 0xf9, 8 REC 0xf4,
 * 9 PLAY 0xf6, 10 STOP 0xf7, 11 B 0x07, 12 A 0x0c; level at +0x0e); idle
 * level = majority of the eight track keys. */
#define PC_BTN(k) (*(volatile uint8_t *)(0x30009d80u+24u*(k)+0x0eu))
#define PC_BTN_PRESS(k) (*(volatile uint16_t *)(0x30009d80u+24u*(k)+0x08u))
#define PC_HOLD_MS 2000u
#define PC_HOLD_SHOW_MS 250u
static int pc_a_held(void) {
    uint32_t *abtn=sc_field(F_ABTN);
    if(!*abtn){
        for(unsigned k=0;k<13u;k++)if(PC_BTN_PRESS(k)==0x0cu){*abtn=k+1u;break;}
        if(!*abtn)return 0;
    }
    unsigned ones=0;
    for(unsigned k=0;k<8u;k++)ones+=PC_BTN(k)!=0;
    unsigned idle=ones>=4u;
    return (PC_BTN(*abtn-1u)!=0)!=idle;
}
static void pc_quicksave(uint32_t now) {
    if(!sc_bank())return;
    uint32_t *held=sc_field(F_HELD),*done=sc_field(F_DONE),*page=sc_field(F_PAGE);
    if(*page && PC_PAGE!=PC_PAGE_PROJSET)*page=0; /* left the scene page */
    if(PC_PAGE!=PC_PAGE_MAIN || !pc_a_held()){*held=0;*done=0;return;}
    if(!*held)*held=now ? now : 1u;
    if(*done || now-*held<PC_HOLD_MS)return;
    *done=1;
    int k=sc_next_free();
    if(k<0){pc_toast(T_NOSLOTS,0);return;}
    sc_save((unsigned)k+1u);
    pc_toast(T_SAVED,(unsigned)k+1u);
    *sc_field(F_AUTOSAVE)=1;
}
/* A scene save is followed by a project save (scenes live in the project
 * XML): SessionMgr::Save (0x081224d4, what PROJ Save calls) queues the SD
 * write. Deferred while recording or loading. */
static void pc_autosave(int busy) {
    uint32_t *a=sc_field(F_AUTOSAVE);
    if(!a || !*a || busy)return;
    *a=0;
    ((void (*)(void *))0x081224d5u)(PC_SESSION);
}
/* The band is drawn at the screen flip, which only runs when the UI is dirty:
 * while it shows (and once after, to clear it), mark the root view's layer 1
 * dirty (app+0x280, +0x39) every 40 ms. */
static int pc_band_on(uint32_t now) {
    uint32_t held=*sc_field(F_HELD),toast=*sc_field(F_TOAST);
    return (held && !*sc_field(F_DONE) && now-held>=PC_HOLD_SHOW_MS) ||
           (toast && (int32_t)(now-*sc_field(F_TOAST_UNTIL))<0);
}
static void pc_band_refresh(uint32_t now) {
    if(!sc_bank())return;
    uint32_t on=(uint32_t)pc_band_on(now),*last=sc_field(F_BAND),*at=sc_field(F_REDRAWN);
    if(!on && !*last)return;
    if(on && *last && now-*at<40u)return;
    uint32_t app=PC_APP;
    if(pc_ram(app,0x150cb0u))*(volatile uint8_t *)(app+0x2b9u)=1;
    *last=on;*at=now;
}

/* On-screen feedback: a 12-pixel band at the top of the UI layer, drawn
 * before Cobalt's flip hook (cpu_present). Font from the doom spike. */
void cpu_present(void *display,int layer,int a2,int a3);
struct pc_glyph {char c;uint8_t r[9];}; /* 5x7, rows 7-8 for descenders */
static const struct pc_glyph pc_font[]={
 {'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},
 {'3',{30,1,1,14,1,1,30}},{'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},{'8',{14,17,17,14,17,17,14}},
 {'9',{14,17,17,15,1,1,14}},{'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},
 {'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},{'E',{31,16,16,30,16,16,31}},
 {'H',{17,17,17,31,17,17,17}},{'I',{14,4,4,4,4,4,14}},{'K',{17,18,20,24,20,18,17}},
 {'L',{16,16,16,16,16,16,31}},{'M',{17,27,21,21,17,17,17}},{'N',{17,25,25,21,19,19,17}},
 {'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},{'R',{30,17,17,30,20,18,17}},
 {'S',{15,16,16,14,1,1,30}},{'T',{31,4,4,4,4,4,4}},{'U',{17,17,17,17,17,17,14}},
 {'V',{17,17,17,17,17,10,4}},{'Y',{17,17,10,4,4,4,4}},{':',{0,6,6,0,6,6,0}},
 {'F',{31,16,16,30,16,16,16}},{'G',{14,17,16,23,17,17,15}},{'J',{7,2,2,2,2,18,12}},
 {'Q',{14,17,17,17,21,18,13}},{'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},
 {'Z',{31,1,2,4,8,16,31}},{'-',{0,0,0,31,0,0,0}},{'_',{0,0,0,0,0,0,31}},
 {'.',{0,0,0,0,0,12,12}},{',',{0,0,0,0,12,4,8}},{'(',{2,4,8,8,8,4,2}},
 {')',{8,4,2,2,2,4,8}},{'!',{4,4,4,4,4,0,4}},{'&',{12,18,20,8,21,18,13}},
 {'\'',{4,4,8,0,0,0,0}},{'+',{0,4,4,31,4,4,0}},{'#',{10,10,31,10,31,10,10}},
 {'/',{1,1,2,4,8,16,16}},{'>',{8,4,2,1,2,4,8}},{'?',{14,17,1,2,4,0,4}},
 {'a',{0,0,14,1,15,17,15}},{'b',{16,16,22,25,17,17,30}},{'c',{0,0,14,16,16,17,14}},
 {'d',{1,1,13,19,17,17,15}},{'e',{0,0,14,17,31,16,14}},{'f',{6,9,8,28,8,8,8}},
 {'g',{0,0,15,17,17,17,15,1,14}},{'h',{16,16,22,25,17,17,17}},{'i',{4,0,12,4,4,4,14}},
 {'j',{2,0,6,2,2,2,2,18,12}},{'k',{16,16,18,20,24,20,18}},{'l',{12,4,4,4,4,4,14}},
 {'m',{0,0,26,21,21,17,17}},{'n',{0,0,22,25,17,17,17}},{'o',{0,0,14,17,17,17,14}},
 {'p',{0,0,30,17,17,17,30,16,16}},{'q',{0,0,15,17,17,17,15,1,1}},{'r',{0,0,22,25,16,16,16}},
 {'s',{0,0,14,16,14,1,30}},{'t',{8,8,28,8,8,9,6}},{'u',{0,0,17,17,17,19,13}},
 {'v',{0,0,17,17,17,10,4}},{'w',{0,0,17,17,21,21,10}},{'x',{0,0,17,10,4,10,17}},
 {'y',{0,0,17,17,17,17,15,1,14}},{'z',{0,0,31,2,4,8,31}}
};
struct pc_canvas {uint8_t *px;unsigned w,h,fmt,bpp;};
static void pc_pixel(struct pc_canvas *c,unsigned x,unsigned y,uint32_t rgb) {
    if(x>=c->w || y>=c->h)return;
    uint8_t *p=c->px+(y*c->w+x)*c->bpp;
    unsigned r=(rgb>>16)&255u,g=(rgb>>8)&255u,b=rgb&255u;
    switch(c->fmt){
    case 1: *(uint32_t *)p=0xff000000u|rgb; break;
    case 2: p[0]=(uint8_t)b;p[1]=(uint8_t)g;p[2]=(uint8_t)r; break;
    case 3: *(uint16_t *)p=(uint16_t)(0x8000u|((r>>3)<<10)|((g>>3)<<5)|(b>>3)); break;
    case 4: *(uint16_t *)p=(uint16_t)(((r>>3)<<11)|((g>>2)<<5)|(b>>3)); break;
    }
}
/* 5x7 glyphs (descenders below), scaled k times. */
static void pc_text_k(struct pc_canvas *c,unsigned x,unsigned y,const char *s,uint32_t rgb,unsigned k) {
    for(;*s;s++,x+=6u*k){
        for(unsigned j=0;j<sizeof pc_font/sizeof pc_font[0];j++)
            if(pc_font[j].c==*s){
                for(unsigned r=0;r<9u*k;r++)for(unsigned b=0;b<5u*k;b++)
                    if(pc_font[j].r[r/k]&(16u>>(b/k)))pc_pixel(c,x+b,y+r,rgb);
                break;
            }
    }
}
static void pc_text(struct pc_canvas *c,unsigned x,unsigned y,const char *s,uint32_t rgb) {pc_text_k(c,x,y,s,rgb,1);}
static char *pc_cat(char *d,const char *s) {while(*s)*d++=*s++;*d=0;return d;}
static char *pc_num(char *d,unsigned n) {
    char t[4];unsigned i=0;
    do{t[i++]=(char)('0'+n%10u);n/=10u;}while(n && i<3u);
    while(i)*d++=t[--i];
    *d=0;return d;
}
static int pc_message(char *out,unsigned *fill) {
    uint32_t now=PC_UI_NOW;
    uint32_t held=*sc_field(F_HELD),done=*sc_field(F_DONE);
    *fill=0;out[0]=0;
    if(held && !done && now-held>=PC_HOLD_SHOW_MS){
        int k=sc_next_free();
        if(k<0){pc_cat(out,"NO SCENE SLOTS");return 1;}
        pc_num(pc_cat(out,"HOLD A: SAVE SCENE "),(unsigned)k+1u);
        uint32_t e=now-held;
        *fill=e>=PC_HOLD_MS ? 320u : e*320u/PC_HOLD_MS;
        return 1;
    }
    uint32_t t=*sc_field(F_TOAST);
    if(!t || (int32_t)(now-*sc_field(F_TOAST_UNTIL))>=0)return 0;
    unsigned kind=t>>8,n=t&0xffu;
    char *d=out;
    switch(kind){
    case T_RECALL: d=pc_num(pc_cat(d,"SCENE "),n); break;
    case T_EMPTY: d=pc_cat(pc_num(pc_cat(d,"SCENE "),n)," IS EMPTY"); break;
    case T_SAVED: d=pc_cat(pc_num(pc_cat(d,"SCENE "),n)," SAVED"); break;
    case T_NOSLOTS: d=pc_cat(d,"NO SCENE SLOTS"); break;
    case T_BANK: d=pc_num(pc_cat(d,"BANK "),n); break;
    case T_BANK_CLEAR: d=pc_cat(d,"BANK CLEARED"); break;
    case T_BANK_USED: d=pc_cat(pc_num(pc_cat(d,"BANK "),n)," IN USE"); break;
    case T_MOUT: d=mo_message(d,n); break;
    case T_CLOCK: d=pc_cat(d,n ? "CLOCK SRC: USB-B > USB-A > MIDI IN" : "CLOCK SRC: MIDI IN > USB-B > USB-A"); break;
    default: return 0;
    }
    return 1;
}
/* Scene page shown: the edit page is the root's shown view and our flag is
 * set on page 0x19. */
static int pc_scene_page_shown(void) {
    uint32_t app=PC_APP,*page=sc_field(F_PAGE);
    return page && *page && PC_PAGE==PC_PAGE_PROJSET && pc_ram(app,0x150cb0u) &&
           *(uint32_t *)(app+0x2b4u)==app+0x1e978u;
}
/* The project name goes in the left part of the button strip at the top of
 * the edit page (screen y 24-62, Select at the right from x 226): 2x size
 * when it fits (17 characters), else 1x. */
#define PC_NAME_Y 24u
void pc_present(void *display,int layer,int a2,int a3) {
    char msg[48];unsigned fill; /* longest: "CLOCK SRC: MIDI IN > USB-B > USB-A" */
    int band=layer==1 && sc_bank() && pc_message(msg,&fill);
    const char *name=sc_bank() && pc_scene_page_shown() ? sc_name() : 0;
    if(band || (name && *name)){
        const uint8_t *d=((const uint8_t *(*)(void *,int))0x0810139bu)(display,layer);
        if(d){
            struct pc_canvas c={*(uint8_t *const *)d,*(const uint16_t *)(d+4),*(const uint16_t *)(d+6),d[8],*(const uint16_t *)(d+10)};
            if(c.w==320u && c.h==240u && pc_ram((uint32_t)c.px,c.w*c.h*c.bpp) &&
               ((c.fmt==1u && c.bpp==4u) || (c.fmt==2u && c.bpp==3u) || ((c.fmt==3u||c.fmt==4u) && c.bpp==2u))){
                if(name && *name){
                    unsigned n=0;while(name[n])n++;
                    unsigned k=n<=17u ? 2u : 1u;
                    pc_text_k(&c,8,PC_NAME_Y+(38u-7u*k)/2u,name,0xffffffu,k);
                }
                if(band){
                    for(unsigned y=0;y<12u;y++)for(unsigned x=0;x<320u;x++)pc_pixel(&c,x,y,y>=10u && x<fill ? 0x09d7f5u : 0x101010u);
                    pc_text(&c,3,2,msg,0xffffffu);
                }
            }
        }
    }
    cpu_present(display,layer,a2,a3);
}
/* PROJ list rows (0x0813dfba, project list name for each visible row): the
 * loaded project gets a "* " in front. */
int pc_list_row(void *list,unsigned index,char *out) {
    int r=((int (*)(void *,unsigned,char *))0x0810204fu)(list,index,out);
    if(!r || !out[0])return r;
    char cur[256];cur[0]=0;
    ((void (*)(void *,char *))0x08122459u)(PC_SESSION,cur);
    if(!cur[0] || pc_hash(cur)!=pc_hash(out))return r;
    unsigned n=0;while(out[n] && n<250u)n++;
    out[n+2]=0;
    for(;n;n--)out[n+1]=out[n-1];
    out[0]='*';out[1]=' ';
    return r;
}
/* PROJ list: a selected name longer than a row shows (about 23 characters)
 * scrolls in the middle row (browser+0x3ac+3*0x58, text set by 0x0813fe4a as
 * the stock fill does; browser+0x6e5 = its layer-1 dirty flag). It waits
 * PC_SCROLL_HOLD_MS at each end. Another selection, or leaving the list, puts
 * the start of the name back. The name is read only when a step is due. */
#define PC_SCROLL_VIS 23u
#define PC_SCROLL_HOLD_MS 1500u
#define PC_SCROLL_STEP_MS 250u
static void pc_list_scroll(uint32_t now) {
    uint32_t *key=sc_field(F_SCROLL_KEY),*at=sc_field(F_SCROLL_AT),*off=sc_field(F_SCROLL_OFF);
    uint8_t *browser=pc_app_view(0x28d60u,0x0814cb14u);
    if(!key || !browser)return;
    int shown=PC_PAGE==PC_PAGE_PROJ && *(uint32_t *)(PC_APP+0x2b4u)==(uint32_t)browser;
    uint32_t k=shown ? *(uint32_t *)(browser+0x614)+1u : 0u;
    unsigned o=*off;
    if(k==*key && (!k || now-*at<(o ? PC_SCROLL_STEP_MS : PC_SCROLL_HOLD_MS)))return;
    char name[256];
    unsigned n=0;
    if(pc_list_row(PC_LIST,*(uint32_t *)(browser+0x614),name))while(name[n])n++;
    if(k!=*key)o=0; /* another selection or view: back to the start */
    else if(n<=PC_SCROLL_VIS)o=0;
    else if(o<n-PC_SCROLL_VIS)o++;
    else if(now-*at>=PC_SCROLL_HOLD_MS)o=0;
    else return;
    if(o!=*off && o<=n){
        ((int (*)(void *,const char *))0x0813fe4bu)(browser+0x3acu+3u*0x58u,name+o);
        browser[0x6e5]=1;
    }
    *key=k;*at=now;*off=o;
}
/* PROJ File > New (0x0813e8a2) and LoadBank with no projects (0x08125cfc)
 * create the project through 0x08122e3c without the project reader, so the
 * scene bank is cleared here: a new project starts at scene 1. */
void pc_new_project(void *session,const char *name) {
    sc_clear();
    ((void (*)(void *,const char *))0x08122e3du)(session,name);
}
/* Rename 0x0813e822 / delete 0x0813e5c0: keep assignments following the name. */
void pc_rename(void *session,const char *old,const char *name) {
    ((void (*)(void *,const char *,const char *))0x08122469u)(session,old,name);
    if(!old||!name)return;
    int slot=pc_slot_of(pc_hash(old));
    if(slot<0)return;
    PC_TAG_SET(PC_SETTINGS,PC_TAG0+(unsigned)slot,(int)pc_hash(name));
    pc_save();
}
void pc_delete(void *session,const char *name) {
    if(name && pc_forget(pc_hash(name)))pc_save();
    ((void (*)(void *,const char *))0x08122f51u)(session,name);
}

/* ---------------------------------------------------------------- Scenes
 * Template: the (cell,id) pairs a scene holds, built from the live tables
 * (tracks 0-11, master 0x0c, aux buses 0x0d-0x12, FX 0x14-0x16; cell map from
 * the panel list builder 0x08121cf4). Values: one row per scene. The bank is
 * allocated once per boot from the stock heap; its pointer lives in the
 * M4-only backup-SRAM line 0xfc0 (after Cobalt's 14-byte GR tick). */
#define SC_SCENES 16u
#define SC_MAX 640u
#define SC_BATCH 24u /* knob-path replays per main-loop pass (engine queue: 256) */
#define SC_CELL_MAX 64u
struct sc_item {uint8_t cell,pad;uint16_t id;};
struct sc_entry {uint16_t id,pad;int32_t value;}; /* native tag-table entry */
struct sc_bank {
    uint32_t magic,n,used,current,recall,cursor,want,spare;
    struct sc_item item[SC_MAX];
    int32_t value[SC_SCENES][SC_MAX];
    struct sc_entry scratch[SC_CELL_MAX]; /* project writer/reader only */
    uint16_t index[SC_CELL_MAX];
    /* s3: scene page selections, quick save, on-screen messages */
    uint32_t sel_scene,sel_save,sel_bank,bank_hash,page;
    uint32_t abtn,held_since,hold_done,toast,toast_until;
    /* s3.3: project save after a scene save; band redraw state */
    uint32_t autosave,band,redrawn;
    /* s3.4: name of the project the scene page was opened for */
    char name[32];
    /* s3.6: PROJ list scrolling of the selected row */
    uint32_t scroll_key,scroll_at,scroll_off;
    /* m1/m2: USB MIDI feedback (mo_main) */
    uint32_t mo_attached,mo_loading,mo_pending,mo_at,mo_flip,mo_mark,mo_count;
    uint32_t mo_dumping,mo_cursor,mo_busy_since;
    /* m3: DIN Out mode (copy of the setting for the TIM4 IRQ), dump targets */
    uint32_t mo_din,mo_to;
    /* m5: clock owner (mo_next, TIM4 IRQ): input rank (0 none), its last tick */
    uint32_t mo_clk_src,mo_clk_at;
    /* m6: copy of the Clock Src setting (1 = USB inputs outrank MIDI in) */
    uint32_t mo_clk_usb;
    /* m7: Clock Src value last seen + 1 (0 before the first look), for the band */
    uint32_t mo_clk_seen;
};
struct sc_ptr {uint32_t magic;struct sc_bank *bank;};
#define SC_PTR ((volatile struct sc_ptr *)0x38800fd0u)
_Static_assert(0xfd0+sizeof(struct sc_ptr)<=0xfe0,"scene pointer between Cobalt's GR tick (0xfc0..0xfcd) and the PC state (0xfe0)");
#define SC_MAGIC 0x53434e31u
/* Header of a native tag table: vtable[0] returns the type byte at +8. */
struct sc_table {uint32_t vtable;struct sc_entry *data;uint8_t type,pad;uint16_t count,cap,pad2;};
#define SC_NO_TYPE 0xffu /* no "type" attribute, so the table reader never re-initialises it */
#define SC_ITER ((int (*)(void *,unsigned,uint16_t *,int32_t *))0x08120a25u)
#define SC_KNOB ((void (*)(void *,uint16_t *,unsigned,int,int))0x08124b45u)
#define SC_ALLOC ((void *(*)(unsigned,unsigned))0x08103129u)
#define X_NEW ((void *(*)(void *,const char *))0x08107b17u)
#define X_INSERT ((void *(*)(void *,void *))0x08106c93u)
#define X_FIRST ((void *(*)(void *,const char *))0x08106ae9u)
#define X_NEXT ((void *(*)(void *,const char *))0x08106b2bu)
#define X_ATTR ((void *(*)(void *,const char *))0x081074ddu)
#define X_SETINT ((void (*)(void *,int))0x08106dc7u)
#define X_FIND ((void *(*)(void *,const char *))0x0810747du)
#define X_QUERY ((int (*)(void *,int *))0x08106d9fu)
#define X_TABLE_WRITE ((void (*)(void *,void *,void *))0x08120b85u)
#define X_TABLE_READ ((void (*)(void *,void *))0x08120c3du)
#define X_DOC_FREE ((void (*)(void *))0x081079c1u)

static uint8_t *sc_table(unsigned cell) {
    if(cell<12u)return PC_SESSION+0xc08u+cell*0x30u;
    if(cell>=0x0cu && cell<=0x12u)return PC_SESSION+0xe78u+(cell-0x0cu)*0x10u;
    if(cell>=0x14u && cell<=0x16u)return PC_SESSION+0xe48u+(cell-0x14u)*0x10u;
    return 0;
}
/* Mixer + FX + BPM: track level/pregain/pan/sends/mute/solo/comp/EQ/gate;
 * master compressor/EQ/level/mute/saturation; aux level/mute; every FX1/FX2
 * parameter (cells 0x14/0x15); BPM from the project settings (cell 0x16). */
static int sc_allowed(unsigned cell,unsigned id) {
    int eq=id>=0x119u && id<=0x12fu;
    if(cell<12u)return eq || id==0x4u || id==0x3u || id==0x63u || id==0xdau || id==0xdbu ||
        (id>=0x15du && id<=0x161u) || id==0x163u || id==0x1aeu;
    if(cell==0x0cu)return eq || id==0x4u || id==0x160u || id==0xb5u || (id>=0x130u && id<=0x13bu) ||
        id==0x43u || (id>=0x1b6u && id<=0x1b8u);
    if(cell<=0x12u)return id==0x4u || id==0x160u;
    if(cell==0x16u)return id==0x85u; /* project settings: BPM only */
    return 1;
}
static int sc_mode(unsigned id) {return id==0x1a0u || id==0x155u;} /* FX type first */
static struct sc_bank *sc_bank(void) {
    backup_enable();PWR_CR1|=1u<<8;
    volatile struct sc_ptr *p=SC_PTR;
    struct sc_bank *b=p->magic==SC_MAGIC ? p->bank : 0;
    return b && pc_ram((uint32_t)b,sizeof *b) && b->magic==SC_MAGIC ? b : 0;
}
static void sc_boot(void) {
    backup_enable();PWR_CR1|=1u<<8;
    volatile struct sc_ptr *p=SC_PTR;
    p->magic=0;
    struct sc_bank *b=SC_ALLOC(sizeof(struct sc_bank),1);
    if(!pc_ram((uint32_t)b,sizeof *b))return;
    b->n=0;b->used=0;b->current=0;b->recall=0;b->cursor=0;b->want=0;b->spare=0;
    b->sel_scene=1;b->sel_save=1;b->sel_bank=0;b->bank_hash=0;b->page=0;
    b->abtn=0;b->held_since=0;b->hold_done=0;b->toast=0;b->toast_until=0;
    b->autosave=0;b->band=0;b->redrawn=0;b->name[0]=0;
    b->scroll_key=0;b->scroll_at=0;b->scroll_off=0;
    b->mo_attached=0;b->mo_loading=0;b->mo_pending=0;b->mo_at=0;b->mo_flip=0;b->mo_mark=0;b->mo_count=0;
    b->mo_dumping=0;b->mo_cursor=0;b->mo_busy_since=0;b->mo_din=0;b->mo_to=0;b->mo_clk_src=0;b->mo_clk_at=0;b->mo_clk_usb=0;b->mo_clk_seen=0;
    b->magic=SC_MAGIC;
    p->bank=b;p->magic=SC_MAGIC;
}
static int sc_get(unsigned cell,unsigned id) {return fw_get(sc_table(cell),id);}
static void sc_template(struct sc_bank *b) {
    /* 0x16 is the project settings table (B on PROJ): only its BPM is captured. */
    static const uint8_t cells[]={0,1,2,3,4,5,6,7,8,9,10,11,0x0c,0x0d,0x0e,0x0f,0x10,0x11,0x12,0x14,0x15,0x16};
    unsigned n=0;
    for(unsigned c=0;c<sizeof cells;c++){
        unsigned cell=cells[c];
        uint8_t *t=sc_table(cell);
        if(!*(void *const *)(t+4))continue;
        for(unsigned pass=0;pass<2u;pass++){
            uint16_t id;int32_t v;
            for(unsigned i=0;SC_ITER(t,i,&id,&v);i++){
                if(!sc_allowed(cell,id) || (cell>=0x14u && (pass==0)!=sc_mode(id)) || (cell<0x14u && pass))continue;
                if(n<SC_MAX){b->item[n].cell=(uint8_t)cell;b->item[n].pad=0;b->item[n].id=id;n++;}
            }
        }
    }
    b->n=n;
}
static void sc_clear(void) {
    struct sc_bank *b=sc_bank();
    if(!b)return;
    b->used=0;b->current=0;b->recall=0;b->cursor=0;b->want=0;b->n=0;
    b->sel_scene=1;b->sel_save=1;
}
static unsigned sc_current(void) {struct sc_bank *b=sc_bank();return b ? b->current : 0;}
static int sc_used(unsigned scene) {struct sc_bank *b=sc_bank();return b && scene>=1u && scene<=SC_SCENES && (b->used>>(scene-1u)&1u);}
static int sc_next_free(void) {
    struct sc_bank *b=sc_bank();
    if(!b)return -1;
    for(unsigned k=0;k<SC_SCENES;k++)if(!(b->used>>k&1u))return (int)k;
    return -1;
}
static char *sc_name(void) {struct sc_bank *b=sc_bank();return b ? b->name : 0;}
static uint32_t *sc_field(unsigned which) {
    struct sc_bank *b=sc_bank();
    if(!b)return 0;
    uint32_t *f[]={&b->sel_scene,&b->sel_save,&b->sel_bank,&b->bank_hash,&b->page,&b->abtn,&b->held_since,&b->hold_done,&b->toast,&b->toast_until,
                   &b->autosave,&b->band,&b->redrawn,&b->scroll_key,&b->scroll_at,&b->scroll_off};
    return f[which];
}
/* Save Scene n: snapshot the template values (a new template when no scene
 * exists yet). Scenes live in the project XML: callers request a project save
 * (F_AUTOSAVE) so they reach the SD card. */
static void sc_save(unsigned scene) {
    struct sc_bank *b=sc_bank();
    if(!b || scene<1u || scene>SC_SCENES)return;
    if(!b->used || !b->n)sc_template(b);
    unsigned k=scene-1u;
    for(unsigned j=0;j<b->n;j++)b->value[k][j]=sc_get(b->item[j].cell,b->item[j].id);
    b->used|=1u<<k;
    b->current=scene;
}
static void sc_recall(unsigned scene) {
    struct sc_bank *b=sc_bank();
    if(!b || scene<1u || scene>SC_SCENES || !(b->used>>(scene-1u)&1u))return;
    b->recall=scene;b->cursor=0;
}
/* Program Change: recall in the main loop, after any bank load in progress. */
static void sc_request(unsigned program) {
    struct sc_bank *b=sc_bank();
    if(b && program<SC_SCENES)b->want=0x100u|program;
}
static void sc_main(int busy) {
    struct sc_bank *b=sc_bank();
    if(!b || busy)return;
    if(b->want&0x100u){
        unsigned scene=(b->want&0x7fu)+1u;
        b->want=0;
        sc_recall(scene);
    }
    if(!b->recall)return;
    unsigned k=b->recall-1u,done=0;
    while(b->cursor<b->n && done<SC_BATCH){
        unsigned j=b->cursor++;
        uint16_t cell=b->item[j].cell;
        unsigned id=b->item[j].id;
        int v=b->value[k][j];
        if(sc_get(cell,id)!=v){SC_KNOB(PC_SESSION,&cell,id,v,0);done++;}
    }
    if(b->cursor>=b->n){b->current=b->recall;b->recall=0;mo_request(PC_UI_NOW,50u,MO_TO_USB|MO_TO_DIN);}
}

static void *x_el(void *node) {return node ? (*(void *(**)(void *))*(void **)node)(node) : 0;}
static void x_setint(void *el,const char *name,int v) {void *a=X_ATTR(el,name);if(a)X_SETINT(a,v);}
static int x_getint(void *el,const char *name,int *v) {void *a=X_FIND(el,name);return a && X_QUERY(a,v)==0;}
static void sc_header(struct sc_table *h,unsigned cell,struct sc_entry *data,unsigned m) {
    h->vtable=*(const uint32_t *)sc_table(cell);h->data=data;h->type=SC_NO_TYPE;h->pad=0;
    h->count=(uint16_t)m;h->cap=(uint16_t)m;h->pad2=0;
}
/* Project writer 0x0812109e: InsertEndChild(session, <cvmap>) with r6 = the
 * document. Append <scenes current=..><scene n=..><cell row=..><params ../>. */
void *sc_write(void *session,void *child,void *doc) {
    void *r=X_INSERT(session,child);
    struct sc_bank *b=sc_bank();
    if(!b || !b->used || !b->n)return r;
    void *scenes=x_el(X_INSERT(session,X_NEW(doc,"scenes")));
    if(!scenes)return r;
    x_setint(scenes,"current",(int)b->current);
    for(unsigned k=0;k<SC_SCENES;k++){
        if(!(b->used>>k&1u))continue;
        void *scene=x_el(X_INSERT(scenes,X_NEW(doc,"scene")));
        if(!scene)continue;
        x_setint(scene,"n",(int)k+1);
        for(unsigned j=0;j<b->n;){
            unsigned cell=b->item[j].cell,m=0;
            while(j<b->n && b->item[j].cell==cell && m<SC_CELL_MAX){
                b->scratch[m].id=b->item[j].id;b->scratch[m].pad=0;b->scratch[m].value=b->value[k][j];
                m++;j++;
            }
            void *el=x_el(X_INSERT(scene,X_NEW(doc,"cell")));
            if(!el)continue;
            x_setint(el,"row",(int)cell);
            struct sc_table h;
            sc_header(&h,cell,b->scratch,m);
            X_TABLE_WRITE(doc,el,&h);
        }
    }
    return r;
}
__attribute__((naked)) void sc_write_hook(void) {
    __asm volatile("mov r2,r6\nb.w sc_write\n");
}
/* Project reader 0x08121416: every exit frees the document here. Rebuild the
 * template from the freshly loaded tables, then read <scenes> (values missing
 * from the file keep the project's own values). */
void sc_read(void *doc) {
    struct sc_bank *b=sc_bank();
    if(b){
        b->recall=0;b->cursor=0;b->used=0;b->current=0;
        sc_template(b);
        void *e=X_FIRST(doc,"document");
        if(e)e=X_FIRST(e,"session");
        void *scenes=e ? X_FIRST(e,"scenes") : 0;
        for(void *scene=scenes ? X_FIRST(scenes,"scene") : 0;scene;scene=X_NEXT(scene,"scene")){
            int n=0;
            if(!x_getint(scene,"n",&n) || n<1 || n>(int)SC_SCENES)continue;
            unsigned k=(unsigned)n-1u;
            for(unsigned j=0;j<b->n;j++)b->value[k][j]=sc_get(b->item[j].cell,b->item[j].id);
            for(void *el=X_FIRST(scene,"cell");el;el=X_NEXT(el,"cell")){
                int row=-1;
                if(!x_getint(el,"row",&row) || !sc_table((unsigned)row))continue;
                unsigned m=0;
                for(unsigned j=0;j<b->n && m<SC_CELL_MAX;j++)
                    if(b->item[j].cell==(unsigned)row){
                        b->scratch[m].id=b->item[j].id;b->scratch[m].pad=0;b->scratch[m].value=b->value[k][j];
                        b->index[m++]=(uint16_t)j;
                    }
                if(!m)continue;
                struct sc_table h;
                sc_header(&h,(unsigned)row,b->scratch,m);
                X_TABLE_READ(el,&h);
                for(unsigned i=0;i<m;i++)b->value[k][b->index[i]]=b->scratch[i].value;
            }
            b->used|=1u<<k;
        }
        int cur=0;
        if(scenes && x_getint(scenes,"current",&cur) && cur>=1 && cur<=(int)SC_SCENES && (b->used>>(cur-1)&1u))
            b->current=(unsigned)cur;
    }
    X_DOC_FREE(doc);
}

/* m2: USB MIDI feedback (../EXPLORATION.md). After a project load completes,
 * when a controller is plugged in and after a scene recall, every CC item of
 * the project's MIDI map is sent to the USB controller with the current value
 * of its parameter, so the controller shows what the bluebox has.
 * Transport: the stock USB-host MIDI class opens an OUT pipe and its SOF
 * handler (0x0810596c) sends MidiPort's TX slots (+0x6180), filled from the
 * USB-out ring (+0x5070) by the flush at the end of the TIM4 MIDI pump; stock
 * never writes that ring. Main loop = only producer, TIM4 IRQ = consumer.
 * m9: the m2 Launchpad Pro MK3 profile (DAW Fader layout, values on cable 2)
 * is gone; every device gets the same dump on cable 0.
 * m3: the same dump on the DIN outputs (m8: MIDI page "Map CC Sync"), and the
 * "DIN Out" thru modes (mo_next). */
#define MO_PORT ((uint8_t *)0x30002c50u)
#define MO_RING (MO_PORT+0x5070u) /* 256 MidiMsg {status,d1,d2,cable,length} */
#define MO_RING_R (*(volatile uint32_t *)(MO_PORT+0x5570u))
#define MO_RING_W (*(volatile uint32_t *)(MO_PORT+0x5574u))
#define MO_SLOT_R (*(volatile uint32_t *)(MO_PORT+0x6d88u))
#define MO_SLOT_W (*(volatile uint32_t *)(MO_PORT+0x6d8cu))
/* USBH MIDI class (flash 0x0814d2c8, .data copy at 0x30000098): pData = the
 * handle 0x3000a854 from Init until DeInit. Handle +3 OUT endpoint, +0x18 tx
 * state (0 idle). phost 0x30009fe8: device descriptor at +0x326. */
#define MO_CLASS_DATA (*(volatile uint32_t *)0x300000b4u)
#define MO_HANDLE ((volatile uint8_t *)0x3000a854u)
/* Project MIDI map (SessionMgr+0x5ef8): {items, count}, 20-byte items. */
struct mo_item {uint16_t chan,msg,ctrl,param,cell,pad;int32_t range,offset;};
struct mo_map {struct mo_item *items;uint32_t count;};
#define MO_MAP ((volatile struct mo_map *)0x3001d2c8u)
#define MO_DEFS (*(uint8_t *volatile *)0x3001eb30u) /* parameter definitions, 28 bytes per id */
#define MO_LAST_ID 0x291u
#define MO_SETTLE_MS 500u
#define MO_TOAST_MS 3000u
#define MO_STUCK_MS 1000u
#define MO_BATCH 48u /* messages per TX slot (0x300 bytes = 192 packets max) */
#define MO_RAW 0x80u /* MidiMsg byte 3: raw USB packet, CIN 4 + bits 4-5 (unused since m9) */
enum {MO_STUCK=3,MO_DIN_STUCK=5}; /* toast codes (1, 2, 4 retired in m8) */
static int mo_attached(void) {return MO_CLASS_DATA==(uint32_t)MO_HANDLE;}
static unsigned mo_out_ep(void) {return mo_attached() ? MO_HANDLE[3]&0x0fu : 0u;}
/* Stock converter (0x08103b9a) builds USB-MIDI packets for Note On/Off and
 * F8/FA/FC only; at its type check (0x08103c3e) every channel message now gets
 * CIN = status >> 4, system realtime keeps the stock test, and MO_RAW messages
 * (ours: SysEx) give their own CIN. The cable number is MidiMsg byte 3 bits
 * 0-3 (unused by stock, zero in stock messages), from the message copy at r4. */
__attribute__((naked)) void mo_convert(void) {
    __asm volatile(
    "ldrb r2,[r4,#3]\n"
    "tst r2,#0x80\n"
    "beq 0f\n"
    "ubfx r3,r2,#4,#2\n"
    "adds r3,#4\n"
    "b 3f\n"
    "0: cmp r0,#0x80\n"
    "blo 1f\n"
    "cmp r0,#0xf0\n"
    "bhs 2f\n"
    "lsrs r3,r0,#4\n"
    "b 3f\n"
    "2: adds r0,#8\n"
    "uxtb r0,r0\n"
    "cmp r0,#4\n"
    "bhi 1f\n"
    "movs r3,#0x15\n"
    "lsrs r3,r3,r0\n"
    "tst r3,#1\n"
    "beq 1f\n"
    "movs r3,#15\n"
    "3: ldrb r2,[r4,#3]\n"
    "and r2,r2,#15\n"
    "orr r3,r3,r2,lsl #4\n"
    "b.w 0x08103bb8\n"
    "1: b.w 0x08103bee\n");
}
/* Nothing is written while a batch is still in the ring, a TX slot or the USB
 * transfer: the SOF handler can replace a slot that is still waiting, so one
 * batch at a time. */
static int mo_idle(void) {
    return MO_RING_R==MO_RING_W && MO_SLOT_R==MO_SLOT_W && MO_HANDLE[0x18]==0u;
}
struct mo_batch {uint8_t m[MO_BATCH][4];unsigned n;};
static void mo_add(struct mo_batch *q,unsigned a,unsigned b,unsigned c,unsigned cable) {
    if(q->n>=MO_BATCH)return;
    uint8_t *m=q->m[q->n++];
    m[0]=(uint8_t)a;m[1]=(uint8_t)b;m[2]=(uint8_t)c;m[3]=(uint8_t)cable;
}
/* One batch with IRQs masked, so the next TIM4 flush puts it in one TX slot. */
static void mo_push(const struct mo_batch *q) {
    uint32_t primask;
    __asm volatile("mrs %0,primask\ncpsid i":"=r"(primask)::"memory");
    uint32_t w=MO_RING_W,room=256u-(w-MO_RING_R);
    unsigned n=q->n<room ? q->n : room;
    for(unsigned i=0;i<n;i++){
        uint8_t *e=MO_RING+((w+i)&0xffu)*5u;
        e[0]=q->m[i][0];e[1]=q->m[i][1];e[2]=q->m[i][2];e[3]=q->m[i][3];e[4]=3;
    }
    __asm volatile("dmb":::"memory");
    MO_RING_W=w+n;
    __asm volatile("msr primask,%0"::"r"(primask):"memory");
}
/* The M7 maps CC -> value as round(min + (max-min)*cc/127) with the
 * parameter's definition range (0x0804f2d2; enums 0..count-1, 0x080555e8);
 * this is the inverse. Toggles (0..1) give 0 / 127. */
static int mo_cc(const struct mo_item *it,unsigned *cc) {
    if(it->msg!=0xb0u || it->chan>15u || it->ctrl>127u || it->param>MO_LAST_ID)return 0;
    uint8_t *t=sc_table(it->cell),*defs=MO_DEFS;
    if(!t || !pc_ram((uint32_t)defs,(MO_LAST_ID+1u)*28u))return 0;
    const uint8_t *d=defs+it->param*28u;
    if(!*(const uint16_t *)d)return 0;
    int lo=*(const int32_t *)(d+0x10),hi=d[2]==5u || d[2]==6u ? *(const int32_t *)(d+0x0c)-1 : *(const int32_t *)(d+0x14);
    if(hi<=lo)return 0;
    int v=fw_get(t,it->param);
    if(v<lo)v=lo;
    if(v>hi)v=hi;
    *cc=(unsigned)((float)(v-lo)*127.0f/(float)(hi-lo)+0.5f);
    return 1;
}
static void mo_request(uint32_t now,uint32_t delay,unsigned to) {
    struct sc_bank *b=sc_bank();
    if(b){b->mo_pending|=to;b->mo_at=now+delay;}
}
static void mo_toast(unsigned n) {
    uint32_t *u=sc_field(F_TOAST_UNTIL);
    pc_toast(T_MOUT,n);
    if(u)*u=PC_UI_NOW+MO_TOAST_MS;
}
/* m3: DIN out. MidiPort has two serial outputs (USART6 / USART2, 31250 baud),
 * each a queue of 4 slots x 0x804 bytes (256 MidiMsg + count at +0x500); the
 * writer appends to slot w (+0x2010), the TIM4 flush closes that slot and starts
 * sending slot r (+0x2014). Stock (M7 notes, clock out) sends to both. Neither
 * index has a full check, so we only write while at most 2 slots wait. */
#define MO_SERIAL(i) (MO_PORT+0x630u+(i)*0x2018u)
#define MO_SERIAL_ADD ((void (*)(void *,const void *))0x08104481u)
#define MO_DIN_BATCH 16u /* 48 bytes, about 15 ms on the wire */
static uint32_t mo_q_w(const uint8_t *q) {return *(volatile const uint32_t *)(q+0x2010u);}
static uint32_t mo_q_r(const uint8_t *q) {return *(volatile const uint32_t *)(q+0x2014u);}
static uint32_t mo_q_open(const uint8_t *q) {return *(volatile const uint32_t *)(q+(mo_q_w(q)&3u)*0x804u+0x500u);}
static int mo_q_room(const uint8_t *q,unsigned n) {return mo_q_w(q)-mo_q_r(q)<=2u && mo_q_open(q)+n<=200u;}
static int mo_din_idle(void) {
    for(unsigned i=0;i<2u;i++){const uint8_t *q=MO_SERIAL(i);if(mo_q_w(q)!=mo_q_r(q) || mo_q_open(q))return 0;}
    return 1;
}
static void mo_din_push(const struct mo_batch *d) {
    uint32_t primask;
    __asm volatile("mrs %0,primask\ncpsid i":"=r"(primask)::"memory");
    for(unsigned i=0;i<2u;i++){
        uint8_t *q=MO_SERIAL(i);
        if(!mo_q_room(q,d->n))continue;
        for(unsigned k=0;k<d->n;k++){
            uint8_t m[5]={d->m[k][0],d->m[k][1],d->m[k][2],0,3};
            MO_SERIAL_ADD(q,m);
        }
    }
    __asm volatile("msr primask,%0"::"r"(primask):"memory");
}
/* DIN Out thru. Replaces the stock input merger 0x08103c62 (called by the TIM4
 * MIDI poll at 0x081033c0 / 0x0810343a) with the same sources in the same
 * order: serial parsers +0x000 / +0x210 (src 0), parser +0x420 (src 1, not fed
 * by stock), USB host ring +0x4660 and M7 ring +0x4b68 (USB from the computer)
 * (src 1). Each message it returns is also queued on both serial outputs:
 * USB Thru for the two USB rings, All Thru for every source, except clock,
 * start/continue/stop and song position. Runs in the IRQ:
 * the scene pointer is read without touching RCC/PWR (sc_bank does). */
#define MO_PARSE ((int (*)(void *,uint8_t *))0x0810329fu)
#define MO_BUMP ((void (*)(volatile uint32_t *))0x08100b19u)
static int mo_take(uint8_t *ring,volatile uint32_t *idx,uint8_t *msg) {
    if(idx[0]==idx[1])return 0;
    const uint8_t *e=ring+(idx[0]&0xffu)*5u;
    for(unsigned i=0;i<5u;i++)msg[i]=e[i];
    MO_BUMP(idx);
    return 1;
}
static int mo_clock(unsigned status) {return status==0xf2u || status==0xf8u || (status>=0xfau && status<=0xfcu);}
static struct sc_bank *mo_irq_bank(void) {
    volatile struct sc_ptr *p=SC_PTR;
    struct sc_bank *b=p->magic==SC_MAGIC ? p->bank : 0;
    return b && pc_ram((uint32_t)b,sizeof *b) && b->magic==SC_MAGIC ? b : 0;
}
static void mo_thru(struct sc_bank *b,const uint8_t *msg,int usb) {
    unsigned mode=b->mo_din,len=msg[4];
    if(!(mode==2u || (mode==1u && usb)) || !len || len>3u)return;
    /* m4: clock and transport stay the bluebox's own (Clock Out), never thru'd. */
    if(mo_clock(msg[0]))return;
    for(unsigned i=0;i<2u;i++){uint8_t *q=MO_SERIAL(i);if(mo_q_room(q,1))MO_SERIAL_ADD(q,msg);}
}
/* m5: stock follows clock from every input at once, so two clocked inputs (the
 * OXI on MIDI in and a Launchpad on USB, both at 120) double the tempo. One
 * input owns clock and transport: MIDI in before the computer (USB device)
 * before the USB host port, or with Clock Src USB (m6) the computer, the USB
 * host port, then MIDI in. A higher one takes over at once, a lower one only
 * after the owner has been silent for MO_CLK_MS; the others' clock and
 * transport are dropped. */
#define MO_CLK_MS 300u
static unsigned mo_clk_rank(const struct sc_bank *b,unsigned from) {
    if(b->mo_clk_usb)return from==4u ? 1u : from==3u ? 2u : 3u;
    return from<=2u ? 1u : from==4u ? 2u : 3u;
}
static int mo_clock_ok(struct sc_bank *b,unsigned from) {
    uint32_t now=PC_UI_NOW;
    unsigned rank=mo_clk_rank(b,from);
    if(!b->mo_clk_src || now-b->mo_clk_at>MO_CLK_MS || rank<b->mo_clk_src)b->mo_clk_src=rank;
    if(b->mo_clk_src!=rank)return 0;
    b->mo_clk_at=now;
    return 1;
}
int mo_next(uint8_t *port,uint8_t *msg,uint8_t *src) {
    struct sc_bank *b=mo_irq_bank();
    for(;;){
        unsigned from;
        *src=0;
        if(MO_PARSE(port,msg))from=0;
        else if(MO_PARSE(port+0x210u,msg))from=1;
        else{
            *src=1;
            if(MO_PARSE(port+0x420u,msg))from=2;
            else if(mo_take(port+0x4660u,(volatile uint32_t *)(port+0x4b60u),msg))from=3;
            else if(mo_take(port+0x4b68u,(volatile uint32_t *)(port+0x5068u),msg))from=4;
            else return 0;
        }
        if(!b)return 1;
        if(msg[4] && mo_clock(msg[0]) && !mo_clock_ok(b,from))continue;
        mo_thru(b,msg,from>=3u);
        return 1;
    }
}
/* Next batch of the dump: map items from the cursor until a batch is full (USB: MO_BATCH messages,
 * DIN: MO_DIN_BATCH). */
static void mo_step(struct sc_bank *b,uint32_t now) {
    int usb=(b->mo_to&MO_TO_USB)!=0u,din=(b->mo_to&MO_TO_DIN)!=0u;
    int usb_busy=usb && !mo_idle(),din_busy=din && !mo_din_idle();
    if(usb_busy || din_busy){
        if(now-b->mo_busy_since>=MO_STUCK_MS){b->mo_dumping=0;mo_toast(usb_busy ? MO_STUCK : MO_DIN_STUCK);}
        return;
    }
    b->mo_busy_since=now;
    struct mo_batch q,d;q.n=0;d.n=0;
    volatile struct mo_map *map=MO_MAP;
    struct mo_item *items=map->items;
    unsigned count=map->count<=128u && pc_ram((uint32_t)items,128u*sizeof *items) ? map->count : 0u;
    while(b->mo_cursor<count && q.n<MO_BATCH && (!din || d.n<MO_DIN_BATCH)){
        unsigned cc;
        const struct mo_item *it=&items[b->mo_cursor++];
        if(!mo_cc(it,&cc))continue;
        if(usb)mo_add(&q,0xb0u|it->chan,it->ctrl,cc,0);
        if(din)mo_add(&d,0xb0u|it->chan,it->ctrl,cc,0);
    }
    if(q.n)mo_push(&q);
    if(d.n)mo_din_push(&d);
    if(b->mo_cursor>=count)b->mo_dumping=0; /* m8: no band when it completes */
}
static void mo_main(uint32_t now) {
    struct sc_bank *b=sc_bank();
    if(!b)return;
    b->mo_din=(uint32_t)fw_get(PC_SETTINGS,PC_DIN_OUT); /* read by mo_thru in the IRQ */
    b->mo_clk_usb=fw_get(PC_SETTINGS,PC_CLK_SRC)==1; /* read by mo_clock_ok in the IRQ */
    /* m7: a change of Clock Src shows the input order on the band. */
    if(b->mo_clk_seen && b->mo_clk_seen!=b->mo_clk_usb+1u){
        uint32_t *u=sc_field(F_TOAST_UNTIL);
        pc_toast(T_CLOCK,b->mo_clk_usb);
        if(u)*u=PC_UI_NOW+MO_TOAST_MS;
    }
    b->mo_clk_seen=b->mo_clk_usb+1u;
    uint32_t att=(uint32_t)mo_attached(),ld=(uint32_t)pc_loading();
    if(att && !b->mo_attached)mo_request(now,MO_SETTLE_MS,MO_TO_USB);
    if(!att)b->mo_to&=~MO_TO_USB;
    if(b->mo_loading && !ld)mo_request(now,MO_SETTLE_MS,MO_TO_USB|MO_TO_DIN);
    b->mo_attached=att;b->mo_loading=ld;
    if(b->mo_pending && !ld && (int32_t)(now-b->mo_at)>=0){
        unsigned want=b->mo_pending,to=0;
        b->mo_pending=0;
        /* USB only while a device with an OUT endpoint is attached: the TX slot
         * ring has no full check and is drained only by the attached class. */
        unsigned sync=(unsigned)fw_get(PC_SETTINGS,PC_CC_SYNC);
        if((want&MO_TO_USB) && att && (sync==1u || sync==2u) && mo_out_ep())to|=MO_TO_USB;
        if((want&MO_TO_DIN) && (sync==2u || sync==3u))to|=MO_TO_DIN;
        if(to){b->mo_to=to;b->mo_dumping=1;b->mo_cursor=0;b->mo_busy_since=now;}
    }
    if(!b->mo_to)b->mo_dumping=0;
    if(b->mo_dumping && !ld)mo_step(b,now);
}
/* m8: the band only reports a dump that could not be sent. */
static char *mo_message(char *d,unsigned n) {
    return pc_cat(d,n==MO_STUCK ? "MAP CC SYNC: USB OUT STUCK" : "MAP CC SYNC: MIDI OUT STUCK");
}
