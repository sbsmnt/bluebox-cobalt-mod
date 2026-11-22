#ifndef COBALT_FREESTANDING_MATH
#define COBALT_FREESTANDING_MATH
static inline float fabsf(float x) {return __builtin_fabsf(x);}
static inline float sqrtf(float x) {float result;__asm("vsqrt.f32 %0, %1":"=w"(result):"w"(x));return result;}
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
