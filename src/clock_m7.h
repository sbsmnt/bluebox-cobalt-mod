/* Out 2 clock (c1 spike): the PROJ settings rows that stock calls Clock In
 * (id 0x110) and Clock Out (id 0x111) become Clk Out L / Clk Out R, each
 * Off or 1, 2, 4, 8, 12, 24 PPQ. A channel that is on carries 5 ms
 * full-scale pulses on OUT2 left or right in place of its audio; a channel
 * that is Off keeps OUT2's audio. Included by cue_m7.c, so it runs in the
 * output node wrapper before the native node: the pulses go through the
 * native OUT2 level and on to the converter in the same block as the audio.
 *
 * Timing comes from the transport, as the native metronome does it
 * (0x0804eb2c): ctx[0] is the transport, +0x30 its tick position at the
 * block start (960 ticks per quarter note), +0x38 nonzero while playing, and
 * 0x08058910 turns a tick into a sample offset in this block. Every PPQ
 * divides 960, so each pulse starts on the sample of its tick. Pulses run
 * only while the transport plays; a pulse in progress always finishes. */
#define CLK_MAGIC 0x434c4b31u
#define CLK_ID 0x110u            /* 0x110 left, 0x111 right */
#define CLK_RATES 7u             /* 0 Off, 1..6 = 1, 2, 4, 8, 12, 24 PPQ */
#define CLK_PULSE 240u           /* 5 ms at 48 kHz */
#define CLK_BUS 16u              /* OUT2: the native Met Output OUT2 choice */
#define CLK_MAX_FRAMES 1024u
/* The stock graph builds the clock node (vtable 0x0806b610, handler
 * 0x080577fc) with index 24 (0x08050938); the 0x110/0x111 events go to its
 * queue with that target, not to the global queue 12. */
#define CLK_NODE 24u
/* rate: setting per channel. armed: last holds the tick of the last pulse
 * (low 32 bits), so a tick that ends one block and starts the next fires
 * once. left: samples still high of the pulse in progress. */
struct clk_state {uint32_t magic;uint8_t rate[2],armed[2];uint16_t left[2];uint32_t last[2];};
/* After the Tape Lo C state (0xb0..0xe3), below the Shoegaze link at 0x100;
 * M7-only, like everything else in this cache line. */
#define CLK ((struct clk_state *)0x388000e8u)
_Static_assert(0xe8+sizeof(struct clk_state)<=0x100,"clock state below the Shoegaze link");
#define clk_offset ((int (*)(void *,const int64_t *))0x08058911u)
#define clk_stereo ((int (*)(void *,float **,float **))0x0804ce19u)

/* Any M7 data RAM. Audio bus buffers come from the stock audio allocator
 * 0x080412a4, which hands out DTCM (0x20000000..0x2001f800) first. */
static int clk_ram(uint32_t a,uint32_t n) {
    return (a>=0x20000000u && n<=0x20000u && a<=0x20020000u-n) || cue_ram(a,n) ||
           (a>=0x38000000u && n<=0x10000u && a<=0x38010000u-n);
}

static const uint16_t clk_ticks[CLK_RATES]={0,960,480,240,120,80,40};

/* M4 settings rows (the patcher points the stock registrations here). New
 * keys: old projects' clkinppq/clkoutppq="5" (24 PPQ) are ignored on load
 * and the rows start Off, so no existing project loses its OUT2 audio. */
const char *const clk_rate_names[CLK_RATES]={"Off","1 PPQ","2 PPQ","4 PPQ","8 PPQ","12 PPQ","24 PPQ"};
const char clk_label_l[]="Clk Out L:";
const char clk_label_r[]="Clk Out R:";
const char clk_key_l[]="o2clkl";
const char clk_key_r[]="o2clkr";

static struct clk_state *clk_state(void) {
    backup_enable();PWR_CR1|=1u<<8;
    struct clk_state *k=CLK;
    if(k->magic!=CLK_MAGIC) {
        k->rate[0]=k->rate[1]=0;k->armed[0]=k->armed[1]=0;
        k->left[0]=k->left[1]=0;k->last[0]=k->last[1]=0;k->magic=CLK_MAGIC;
    }
    return k;
}

static void clk_event(struct clk_state *k,unsigned id,int value) {
    if(id-CLK_ID>1u)return;
    k->rate[id-CLK_ID]=(value>=0 && value<(int)CLK_RATES) ? (uint8_t)value : 0;
}

/* pos mod step (0..step-1, floor) without 64-bit division (no libgcc). */
static unsigned clk_mod(int64_t pos,unsigned step) {
    uint64_t a=pos<0 ? (uint64_t)-pos : (uint64_t)pos;
    unsigned hi=(unsigned)(a>>32)%step,lo=(unsigned)a%step;
    unsigned r=(hi*((0xffffffffu%step+1u)%step)+lo)%step;   /* 2^32 mod step */
    return pos<0 && r ? step-r : r;
}

static void clk_render(struct clk_state *k,void *ctx) {
    if(!(k->rate[0] | k->rate[1] | k->left[0] | k->left[1]))return;
    uint8_t *tr=*(uint8_t **)ctx;
    if(!clk_ram((uint32_t)tr,0x40u))return;
    void *bus=cue_bus_at(ctx,CLK_BUS);
    float *ch[2]={0,0};
    if(!clk_stereo(bus,&ch[0],&ch[1]))return;
    unsigned n=cue_frames(bus);
    if(n>CLK_MAX_FRAMES)n=CLK_MAX_FRAMES;
    int playing=tr[0x38]!=0;
    int64_t pos=*(const int64_t *)(tr+0x30);
    for(unsigned c=0;c<2;c++) {
        unsigned rate=k->rate[c],left=k->left[c];
        if(!rate && !left)continue;          /* Off: OUT2 audio stays */
        if(!clk_ram((uint32_t)ch[c],n*4u))continue;
        unsigned start[4],m=0;
        if(rate && playing) {
            unsigned step=clk_ticks[rate],r=clk_mod(pos,step);
            int64_t t=pos+(r ? (int64_t)(step-r) : 0);
            for(unsigned i=0;i<4u;i++,t+=step) {
                if(k->armed[c] && (uint32_t)t==k->last[c])continue;
                int off=clk_offset(tr,&t);
                if(off>=(int)n)break;
                start[m++]=off<0 ? 0u : (unsigned)off;
                k->last[c]=(uint32_t)t;k->armed[c]=1;
            }
        } else k->armed[c]=0;                /* replay from the same tick fires */
        float *o=ch[c];
        for(unsigned f=0,j=0;f<n;f++) {
            while(j<m && start[j]<=f){left=CLK_PULSE;j++;}
            if(left){o[f]=1.f;left--;}
            else o[f]=0.f;
        }
        k->left[c]=(uint16_t)left;
    }
}
