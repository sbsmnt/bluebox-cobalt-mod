/* Desktop1.5.2 recording float->PCM24 packing. Native uses float scale2^31,
 * software int64 truncation, int32 saturation, shift8 and three byte stores.
 * VCVT.S32.F32 supplies the same truncation/saturation for finite values and
 * infinities. Preserve stock NaN behavior explicitly: positive clip for every
 * NaN sign/payload. No out-of-range C float-to-int cast is used. PCM bytes are
 * the contract; floating exception sticky flags may differ. */
__attribute__((noinline)) void record_pcm24_scalar(const float *src,int count,uint8_t *dst,int stride) {
    if(count<=0)return;
    uint32_t step=(uint32_t)stride*3u;
    do {
        union {float f;uint32_t u;} sample;sample.f=*src++;
        uint32_t v;
        if((sample.u&0x7fffffffu)>0x7f800000u)v=0x7fffffffu;
        else {
            float scaled=sample.f*2147483648.f;int32_t value;
            __asm volatile("vcvt.s32.f32 %1,%1\nvmov %0,%1" : "=r"(value),"+t"(scaled));
            v=(uint32_t)value;
        }
        dst[0]=(uint8_t)(v>>8);dst[1]=(uint8_t)(v>>16);dst[2]=(uint8_t)(v>>24);
        dst=(uint8_t *)((uintptr_t)dst+step);
    } while(--count>0);
}
