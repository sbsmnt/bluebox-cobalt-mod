/* Soft saturation curve shared by the master (saturation.h) and per-track
 * (track_saturation.h) stages, adapted from Bluelex mst_m7.c (see NOTICE.md).
 * Also compiled into the host tests. */
#ifndef BLUEBOX_SOFT_SAT_H
#define BLUEBOX_SOFT_SAT_H
#include <stdint.h>
#define SAT_REF .2f
static float exp2f_(float x)        /* 0 <= x < 8 */
{
    int i = (int)x;
    float f = x - (float)i;
    union { float f; uint32_t u; } v = { 1.f + f * (0.6931472f + f * (0.2402265f + f * (0.0555041f + f * (0.0096181f + f * 0.0013334f)))) };
    v.u += (uint32_t)i << 23;
    return v.f;
}
static float sat(float x)
{
    if (x > 3.f) return 1.f;
    if (x < -3.f) return -1.f;
    float x2 = x * x;
    return x * (27.f + x2) / (27.f + 9.f * x2);
}
#endif
