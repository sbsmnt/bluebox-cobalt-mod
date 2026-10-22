/* Overdub/recording throughput fix ported from the hardware-tested C16
 * experiment (tapeC16, 2026-10-21 overdub PASS), without its diagnostics.
 * Stock PCM24 conversion runs one SDRAM transaction per byte; these word/burst
 * loops (in place of the stock bodies in the ART-cached 0x081 page) and cold
 * wrappers produce identical PCM. See BASELINE_C16_* and INTEGRATION.md. */
/* C16: cold wrappers around the ART-resident burst loop. Edges (alignment
 * prefix, tail), NaN/infinity groups, other strides and any source/destination
 * overlap use the exact C15 scalar conversion; aligned bulk uses LDM/STM.
 * Stereo is packed in one pass from the native L call (see bridge below).
 * pcm24_hot_m4.h keeps the exact pinned C15 scalar source, now linked cold.
 */
#include "pcm24_hot_m4.h"
#include "pcm24_burst_m4.h"
static int pcm24_overlap(const void *a,uint32_t an,const void *b,uint32_t bn) {
    return (uintptr_t)a<(uintptr_t)b+bn && (uintptr_t)b<(uintptr_t)a+an;
}
void record_pcm24_pack(const float *src,int count,uint8_t *dst,int stride) {
    if(count<=0)return;
    if(stride!=1 || ((uintptr_t)src&3u) || pcm24_overlap(src,(uint32_t)count*4u,dst,(uint32_t)count*3u)) {
        record_pcm24_scalar(src,count,dst,stride);return;
    }
    /* 3k bytes reach a word boundary after k == dst mod 4 samples. */
    uint32_t n=(uint32_t)count,head=(uintptr_t)dst&3u;
    if(head>n)head=n;
    if(head){record_pcm24_scalar(src,(int)head,dst,1);src+=head;dst+=head*3u;n-=head;}
    struct pcm24_burst_state s={src,0,dst,n>>2};
    while(s.groups) {
        record_pcm24_burst(&s);
        if(s.groups){record_pcm24_scalar(s.l,4,s.dst,1);s.l+=4;s.dst+=12;s.groups--;}
    }
    if(n&3u)record_pcm24_scalar(s.l,(int)(n&3u),s.dst,1);
}
static void pcm24_stereo_scalar(const float *l,const float *r,int n,uint8_t *dst) {
    record_pcm24_scalar(l,n,dst,2);record_pcm24_scalar(r,n,dst+3,2);
}
void record_pcm24_stereo_pack(const float *l,const float *r,int n,uint8_t *dst) {
    if(n<=0)return;
    uint32_t frames=(uint32_t)n,bytes=frames*6u;
    if(!r || (((uintptr_t)l|(uintptr_t)r)&3u) || ((uintptr_t)dst&1u) ||
       pcm24_overlap(l,frames*4u,dst,bytes) || pcm24_overlap(r,frames*4u,dst,bytes)) {
        pcm24_stereo_scalar(l,r,n,dst);return;
    }
    if((uintptr_t)dst&2u){pcm24_stereo_scalar(l,r,1,dst);l++;r++;dst+=6;frames--;}
    struct pcm24_burst_state s={l,r,dst,frames>>1};
    while(s.groups) {
        record_pcm24_burst(&s);
        if(s.groups){pcm24_stereo_scalar(s.l,s.r,2,s.dst);s.l+=2;s.r+=2;s.dst+=12;s.groups--;}
    }
    if(frames&1u)pcm24_stereo_scalar(s.l,s.r,1,s.dst);
}
/* alpha.6: entry points without the C16 timing notes (diagnostics removed). */
void record_pcm24_fast(const float *src,int count,uint8_t *dst,int stride) {
    record_pcm24_pack(src,count,dst,stride);
}
void record_pcm24_stereo(const float *l,const float *r,int n,uint8_t *dst) {
    record_pcm24_stereo_pack(l,r,n,dst);
}
/* Replaces the native stereo-left BL at 0x0810aa50 (block 0x0810aa2e..aa71 is
 * hash-pinned). There r0=L source, r1=frames, r2=destination; caller sp+0x24
 * holds the partner index and sp+0x28 the source byte offset; r8 is the
 * storage object. Run the pure native descriptor helper 0x0810c312 early to get
 * R, pack both channels once, set native r7=source offset as the replaced R
 * pass would, and resume at 0x0810aa70 (b 0x0810a96c). */
__attribute__((naked)) void record_pcm24_stereo_bridge(void) {
    __asm volatile(
    "push {r4-r6,lr}\n"
    "mov r4,r0\nmov r5,r1\nmov r6,r2\n"
    "ldr r1,[sp,#0x34]\n"
    "ldr.w r0,[r8]\n"
    "bl 0x0810c312\n"
    "ldr r1,[r0]\n"
    "ldr r0,[sp,#0x38]\n"
    "add r1,r0\n"
    "mov r0,r4\nmov r2,r5\nmov r3,r6\n"
    "bl record_pcm24_stereo\n"
    "ldr r7,[sp,#0x38]\n"
    "pop {r4-r6,lr}\n"
    "add lr,lr,#0x1c\n"
    "bx lr\n");
}

/* C16: playback PCM24 decoding. The stock mono/stereo decoder bodies are
 * replaced in place by ART-resident LDM word loops (pcm24_burst_m4.h). These
 * cold wrappers decode the unaligned prefix/tail, odd or overlapping cases
 * with the same arithmetic: (b0<<8|b1<<16|b2<<24) as int32 times 2^-31, which
 * VCVT.F32.S32 #31 computes exactly, like stock VCVT then VMUL. */
static float pcm24_sample(const uint8_t *s) {
    uint32_t v=((uint32_t)s[0]<<8)|((uint32_t)s[1]<<16)|((uint32_t)s[2]<<24);float f;
    __asm volatile("vmov %0,%1\nvcvt.f32.s32 %0,%0,#31" : "=t"(f) : "r"(v));
    return f;
}
static void pcm24_unpack_scalar(const uint8_t *src,uint32_t n,float *l,float *r) {
    for(;n;n--) {
        *l++=pcm24_sample(src);src+=3;
        if(r){*r++=pcm24_sample(src);src+=3;}
    }
}
void record_pcm24_unpack_mono(const uint8_t *src,int count,float *dst) {
    if(count<=0)return;
    uint32_t n=(uint32_t)count;
    if(((uintptr_t)dst&3u) || pcm24_overlap(src,n*3u,dst,n*4u)){pcm24_unpack_scalar(src,n,dst,0);return;}
    uint32_t head=(uintptr_t)src&3u;
    if(head>n)head=n;
    pcm24_unpack_scalar(src,head,dst,0);src+=head*3u;dst+=head;n-=head;
    if(n>>2){record_pcm24_unburst_mono(src,n>>2,dst);src+=(n>>2)*12u;dst+=n&~3u;}
    pcm24_unpack_scalar(src,n&3u,dst,0);
}
void record_pcm24_unpack_stereo(const uint8_t *src,int count,float *l,float *r) {
    if(count<=0)return;
    uint32_t n=(uint32_t)count;
    if(((uintptr_t)src&1u) || (((uintptr_t)l|(uintptr_t)r)&3u) ||
       pcm24_overlap(src,n*6u,l,n*4u) || pcm24_overlap(src,n*6u,r,n*4u) || pcm24_overlap(l,n*4u,r,n*4u)) {
        pcm24_unpack_scalar(src,n,l,r);return;
    }
    if((uintptr_t)src&2u){pcm24_unpack_scalar(src,1,l,r);src+=6;l++;r++;n--;}
    if(n>>1){record_pcm24_unburst_stereo(src,n>>1,l,r);src+=(n>>1)*12u;l+=n&~1u;r+=n&~1u;}
    pcm24_unpack_scalar(src,n&1u,l,r);
}
/* alpha.6: entry points without the C16 timing notes (diagnostics removed). */
void record_pcm24_read_mono(const uint8_t *src,int n,float *dst) {
    record_pcm24_unpack_mono(src,n,dst);
}
void record_pcm24_read_stereo(const uint8_t *src,int n,float *l,float *r) {
    record_pcm24_unpack_stereo(src,n,l,r);
}
