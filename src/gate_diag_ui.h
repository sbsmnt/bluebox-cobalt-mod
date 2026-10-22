#include "gate_diag_shared.h"
static void gate_ux_reset(void);
static volatile struct gate_ui_diag *gd_ui(void) {
    backup_enable();PWR_CR1|=1u<<8;
    if(GD_UI->magic!=GD_UI_MAGIC) {
        volatile uint32_t *p=(volatile uint32_t *)GD_UI;
        for(unsigned i=0;i<sizeof(*GD_UI)/4;i++)p[i]=0;
        GD_UI->mix_ch=GD_UI->track_ch=GD_UI->last_ch=-1;
        gate_ux_reset();
        GD_UI->magic=GD_UI_MAGIC;
    }
    return GD_UI;
}
static void gd_mix_capture(uint8_t *p,int ch) {
    volatile struct gate_ui_diag *d=gd_ui();
    d->mix_panel=(uint32_t)p;d->mix_pid=*(uint16_t *)(p+0xbca0);
    d->mix_ch=ch;
    if(ch>=0 && ch<12) {
        uint8_t *k=p+0xbe0+(unsigned)ch*0xd98+0x8f0;
        d->mix_knob=(uint32_t)k;d->mix_kid=*(uint16_t *)(k+0x94);
    } else d->mix_knob=0;
}
static void gd_mix_input(uint8_t *p,uint32_t *ev) {
    volatile struct gate_ui_diag *d=gd_ui();d->mix_events++;
    if(*(uint16_t *)(p+0xbca0)==0x1ae)d->mix_gate_events++;
    int index=(int16_t)ev[3];
    if(p[0xbcc0])index=index<0?-((-index)&1):(index&1);
    int ch=index+2 * *(int *)(p+0xbcbc);
    gd_mix_capture(p,ch);d->last_ch=ch;d->source=1;
}
static void gd_local(uint8_t *p,uint32_t *ev,unsigned source) {
    unsigned type=(uint16_t)ev[0];
    if(type!=0x6e && type!=0x3c && type!=0x3d)return;
    if((type==0x6e?ev[3]:ev[4])!=0x1ae)return;
    volatile struct gate_ui_diag *d=gd_ui();int ch=(uint16_t)ev[2];
    if(source==1){d->mix_local++;gd_mix_capture(p,ch);}
    else {d->track_events++;d->track_ch=ch;d->track_knob=(uint32_t)(p+0xde48);}
    d->last_ch=ch;d->source=source;
}
static void gd_track_bind(uint8_t *w,unsigned ch) {
    volatile struct gate_ui_diag *d=gd_ui();d->track_knob=(uint32_t)w;d->track_ch=(int)ch;
    if(d->last_ch<0)d->last_ch=(int)ch;
}
struct gd_line {char *s;unsigned n;};
static void gd_char(struct gd_line *p,char c){if(p->n<53)p->s[p->n++]=c;p->s[p->n]=0;}
static void gd_string(struct gd_line *p,const char *s){while(*s)gd_char(p,*s++);}
static void gd_uint(struct gd_line *p,unsigned n){char s[10];unsigned k=0;do{s[k++]=(char)('0'+n%10);n/=10;}while(n);while(k)gd_char(p,s[--k]);}
static void gd_count(struct gd_line *p,unsigned n){gd_uint(p,n%1000u);}
static void gd_ch(struct gd_line *p,int ch){if(ch<0 || ch>=12)gd_string(p,"--");else gd_uint(p,(unsigned)ch+1);}
static void gd_hex(struct gd_line *p,unsigned n){for(int i=8;i>=0;i-=4)gd_char(p,"0123456789ABCDEF"[(n>>i)&15]);}
static void gd_db(struct gd_line *p,int v,unsigned off) {
    if(off && v<=-96000){gd_string(p,"OFF");return;}
    if(v<-120000){gd_string(p,"<-120");return;}
    if(v>999000)v=999000;
    if(v<0){gd_char(p,'-');v=-v;}
    unsigned n=((unsigned)v+50u)/100u;gd_uint(p,n/10u);gd_char(p,'.');gd_uint(p,n%10u);
}
static int gd_model(int ch) {
    return fw_get((uint8_t *)0x300173d0u+0xc08+(unsigned)ch*0x30,0x1ae);
}
static void gd_values(struct gd_line *p,uint32_t k,int ch) {
    int valid=valid_buffer(k,0x3d0) || (k>=0x20000000u && k<=0x2001fc30u) ||
        (k>=0x38000000u && k<=0x3800fc30u);
    if(ch<0 || ch>=12 || !valid){gd_string(p,"U-- S--");return;}
    gd_char(p,'U');gd_db(p,*(int *)(k+0x98),1);gd_string(p," S");gd_db(p,gd_model(ch),1);
}
/* Log conversion is diagnostic formatting only; Gate's conversion is untouched. */
int gate_diag_db(float a) {
    if(!(a>0.f && a<1e12f))return -999000;
    union{float f;uint32_t u;} v={a};int e=(int)((v.u>>23)&255)-127;
    v.u=(v.u&0x7fffffu)|0x3f800000u;
    float z=(v.f-1.f)/(v.f+1.f),z2=z*z;
    float db=6.020599913f*(float)e+17.37177928f*z*(1.f+z2*(1.f/3.f+z2/5.f));
    return (int)(db*1000.f);
}
static int gd_snapshot(int ch,struct gate_audio_diag *out) {
    if(ch<0 || ch>=12)return 0;
    volatile const struct gate_audio_diag *d=&GD_AUDIO[ch];
    uint32_t seq=d->seq;if(!seq || (seq&1u) || d->magic!=GD_AUDIO_MAGIC)return 0;
    out->magic=d->magic;out->seq=seq;out->threshold=d->threshold;
    out->left=d->left;out->right=d->right;out->env=d->env;out->gain=d->gain;out->events=d->events;
    __asm volatile("dmb" ::: "memory");
    return d->seq==seq && d->magic==GD_AUDIO_MAGIC;
}
void gate_diag_lines(char rows[4][54]) {
    for(unsigned i=0;i<4;i++)rows[i][0]=0;
    if(GD_UI->magic!=GD_UI_MAGIC)return;
    volatile const struct gate_ui_diag *d=GD_UI;struct gd_line p={rows[0],0};
    gd_string(&p,"A3 M E");gd_count(&p,d->mix_events);gd_string(&p," G");gd_count(&p,d->mix_gate_events);
    gd_string(&p," L");gd_count(&p,d->mix_local);gd_string(&p," C");gd_ch(&p,d->mix_ch);
    gd_string(&p," P");gd_hex(&p,d->mix_pid);gd_string(&p," K");gd_hex(&p,d->mix_kid);
    p.s=rows[1];p.n=0;gd_string(&p,"M ");gd_values(&p,d->mix_knob,d->mix_ch);
    gd_string(&p," T");gd_ch(&p,d->track_ch);gd_string(&p," E");gd_count(&p,d->track_events);gd_char(&p,' ');
    gd_values(&p,d->track_knob,d->track_ch);
    struct gate_audio_diag a;int ch=d->last_ch;
    p.s=rows[2];p.n=0;gd_string(&p,"DSP C");gd_ch(&p,ch);
    if(!gd_snapshot(ch,&a)){gd_string(&p," WAIT");return;}
    gd_string(&p," T");gd_db(&p,a.threshold,1);gd_string(&p," RX");gd_count(&p,a.events);
    gd_string(&p," Q");gd_count(&p,a.seq/2);gd_string(&p," G");
    float g=a.gain;if(!(g>=0.f))g=0.f;if(g>1.f)g=1.f;gd_uint(&p,(unsigned)(g*100.f+.5f));gd_char(&p,'%');
    p.s=rows[3];p.n=0;
    if(a.left<0.f){gd_string(&p,"IN -- (GATE OFF)");return;}
    gd_string(&p,"IN L");gd_db(&p,gate_diag_db(a.left),0);gd_string(&p," R");gd_db(&p,gate_diag_db(a.right),0);
    gd_string(&p," ENV");gd_db(&p,gate_diag_db(a.env),0);
}
struct gd_glyph {char c;uint8_t r[7];};
static const struct gd_glyph gd_font[]={
 {'0',{14,17,19,21,25,17,14}},{'1',{4,12,4,4,4,4,14}},{'2',{14,17,1,2,4,8,31}},
 {'3',{30,1,1,14,1,1,30}},{'4',{2,6,10,18,31,2,2}},{'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}},{'7',{31,1,2,4,8,8,8}},{'8',{14,17,17,14,17,17,14}},
 {'9',{14,17,17,15,1,1,14}},{'A',{14,17,17,31,17,17,17}},{'B',{30,17,17,30,17,17,30}},
 {'C',{14,17,16,16,16,17,14}},{'D',{30,17,17,17,17,17,30}},{'E',{31,16,16,30,16,16,31}},
 {'H',{17,17,17,31,17,17,17}},
 {'F',{31,16,16,30,16,16,16}},{'G',{14,17,16,23,17,17,15}},{'I',{14,4,4,4,4,4,14}},
 {'K',{17,18,20,24,20,18,17}},{'L',{16,16,16,16,16,16,31}},{'M',{17,27,21,21,17,17,17}},
 {'N',{17,25,25,21,19,19,17}},{'O',{14,17,17,17,17,17,14}},{'P',{30,17,17,30,16,16,16}},
 {'Q',{14,17,17,17,21,18,13}},{'R',{30,17,17,30,20,18,17}},{'S',{15,16,16,14,1,1,30}},
 {'T',{31,4,4,4,4,4,4}},{'U',{17,17,17,17,17,17,14}},{'V',{17,17,17,17,17,10,4}},
 {'W',{17,17,17,21,21,21,10}},{'X',{17,17,10,4,10,17,17}},{'-',{0,0,0,31,0,0,0}},
 {'.',{0,0,0,0,0,6,6}},{'%',{25,25,2,4,8,19,19}},{'(',{2,4,8,8,8,4,2}},
 {')',{8,4,2,2,2,4,8}},{'<',{1,2,4,8,4,2,1}}
};
void gate_diag_draw(uint8_t *px,unsigned stride,unsigned fmt,unsigned bpp) {
    if(GD_UI->magic!=GD_UI_MAGIC)return;
    char rows[4][54];gate_diag_lines(rows);
    for(unsigned y=208;y<240;y++)for(unsigned x=0;x<320;x++)pixel(px+(y*stride+x)*bpp,fmt,0x101010u);
    for(unsigned row=0;row<4;row++)for(unsigned i=0;rows[row][i] && i<53;i++) {
        const uint8_t *glyph=0;
        for(unsigned j=0;j<sizeof(gd_font)/sizeof(gd_font[0]);j++)if(gd_font[j].c==rows[row][i]){glyph=gd_font[j].r;break;}
        if(!glyph)continue;
        for(unsigned y=0;y<7;y++)for(unsigned x=0;x<5;x++)if(glyph[y]&(16u>>x))
            pixel(px+(((208+8*row+y)*stride)+1+6*i+x)*bpp,fmt,0xffffffu);
    }
}
