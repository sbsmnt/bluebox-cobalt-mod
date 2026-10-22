/* C16: word/burst PCM24 bulk loops inside the ART-cached 0x081 page.
 * The M4 heap (cache pages and file staging) is SDRAM. Stock and C15 touch it
 * once per float plus three single-byte stores/loads per sample. These loops
 * move four mono samples or two stereo frames per iteration with LDM/STM:
 * 12 PCM bytes as three aligned words. Callers guarantee aligned pointers,
 * groups>0 and no source/destination overlap; edges stay in the cold wrappers.
 *
 * Recording: VCVT.S32.F32 #31 is the stock float*2^31, truncate, saturate
 * sequence for finite values. Any group whose OR-ed exponent is 0xff may hold
 * NaN/infinity; the loop stops there and the wrapper converts it with the exact
 * C15 scalar path (stock positive clip for NaN). Normal audio never stops.
 * Playback: VCVT.F32.S32 #31 equals stock int->float then *2^-31; both exact.
 */
struct pcm24_burst_state {const float *l,*r;uint8_t *dst;uint32_t groups;};
_Static_assert(sizeof(struct pcm24_burst_state)==16,"burst state is shared with assembly");

/* State {l, r or 0 for mono, aligned dst, groups} is advanced in place. */
__attribute__((naked,section(".pcmhot"),noinline)) void record_pcm24_burst(struct pcm24_burst_state *) {
    __asm volatile(
    "push {r4-r10,lr}\n"
    "ldm r0,{r1,r2,r3,r12}\n"
    "1:\n"
    "cbz r2,2f\n"
    "ldmia r1!,{r4,r6}\n"          /* L0->a, L1->c */
    "ldmia r2!,{r5,r7}\n"          /* R0->b, R1->d */
    "b 3f\n"
    "2:\n"
    "ldmia r1!,{r4-r7}\n"          /* a b c d */
    "3:\n"
    "orr r8,r4,r5\n"
    "orr r9,r6,r7\n"
    "orr r8,r8,r9\n"
    "lsl r8,r8,#1\n"
    "cmn r8,#0x01000000\n"         /* carry: some exponent field is 0xff */
    "bcs 5f\n"
    "vmov s0,s1,r4,r5\n"
    "vmov s2,s3,r6,r7\n"
    "vcvt.s32.f32 s0,s0,#31\n"
    "vcvt.s32.f32 s1,s1,#31\n"
    "vcvt.s32.f32 s2,s2,#31\n"
    "vcvt.s32.f32 s3,s3,#31\n"
    "vmov r4,r5,s0,s1\n"
    "vmov r6,r7,s2,s3\n"
    "lsr r8,r4,#8\n"
    "lsr r4,r5,#8\n"
    "orr r8,r8,r4,lsl #24\n"       /* a1 a2 a3 b1 */
    "lsr r9,r5,#16\n"
    "lsr r4,r6,#8\n"
    "orr r9,r9,r4,lsl #16\n"       /* b2 b3 c1 c2 */
    "lsr r10,r6,#24\n"
    "bic r7,r7,#0xff\n"
    "orr r10,r10,r7\n"             /* c3 d1 d2 d3 */
    "stmia r3!,{r8-r10}\n"
    "subs r12,r12,#1\n"
    "bne 1b\n"
    "b 6f\n"
    "5:\n"
    "cbz r2,4f\n"
    "subs r1,r1,#8\n"
    "subs r2,r2,#8\n"
    "b 6f\n"
    "4:\n"
    "subs r1,r1,#16\n"
    "6:\n"
    "stm r0,{r1,r2,r3,r12}\n"
    "pop {r4-r10,pc}\n");
}

/* src 4-aligned: groups x {4 samples} -> 4 floats each. */
__attribute__((naked,section(".pcmdm"),noinline)) void record_pcm24_unburst_mono(const uint8_t *,uint32_t,float *) {
    __asm volatile(
    "push {r4-r7,lr}\n"
    "1:\n"
    "ldmia r0!,{r4,r5,r6}\n"
    "lsls r7,r4,#8\n"              /* a */
    "lsrs r4,r4,#16\n"
    "bic r4,r4,#0xff\n"
    "orr r4,r4,r5,lsl #16\n"       /* b */
    "lsrs r5,r5,#8\n"
    "bic r5,r5,#0xff\n"
    "orr r5,r5,r6,lsl #24\n"       /* c */
    "bic r6,r6,#0xff\n"            /* d */
    "vmov s0,s1,r7,r4\n"
    "vmov s2,s3,r5,r6\n"
    "vcvt.f32.s32 s0,s0,#31\n"
    "vcvt.f32.s32 s1,s1,#31\n"
    "vcvt.f32.s32 s2,s2,#31\n"
    "vcvt.f32.s32 s3,s3,#31\n"
    "vstmia r2!,{s0-s3}\n"
    "subs r1,#1\n"
    "bne 1b\n"
    "pop {r4-r7,pc}\n");
}

/* src 4-aligned: groups x {L0 R0 L1 R1} -> two floats per channel. */
__attribute__((naked,section(".pcmds"),noinline)) void record_pcm24_unburst_stereo(const uint8_t *,uint32_t,float *,float *) {
    __asm volatile(
    "push {r4-r7,lr}\n"
    "1:\n"
    "ldmia r0!,{r4,r5,r6}\n"
    "lsls r7,r4,#8\n"              /* L0 */
    "lsrs r4,r4,#16\n"
    "bic r4,r4,#0xff\n"
    "orr r4,r4,r5,lsl #16\n"       /* R0 */
    "lsrs r5,r5,#8\n"
    "bic r5,r5,#0xff\n"
    "orr r5,r5,r6,lsl #24\n"       /* L1 */
    "bic r6,r6,#0xff\n"            /* R1 */
    "vmov s0,s1,r7,r5\n"
    "vmov s2,s3,r4,r6\n"
    "vcvt.f32.s32 s0,s0,#31\n"
    "vcvt.f32.s32 s1,s1,#31\n"
    "vcvt.f32.s32 s2,s2,#31\n"
    "vcvt.f32.s32 s3,s3,#31\n"
    "vstmia r2!,{s0,s1}\n"
    "vstmia r3!,{s2,s3}\n"
    "subs r1,#1\n"
    "bne 1b\n"
    "pop {r4-r7,pc}\n");
}
