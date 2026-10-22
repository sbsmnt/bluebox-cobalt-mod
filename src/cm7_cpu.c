/* Audio-engine load, not whole-device CPU utilization.
 * Replaces desktop BL 0x0804c50e -> stock profiler exit 0x0804bbf8.
 * Stock exit tail-calls 0x08041750 and appends {period,busy} DWT cycles.
 * The ring pointer is engine+0x3e58; its count is engine+0x3e5c.
 */
#include "shared.h"
typedef void (*prof_fn)(uint8_t *);
#define STOCK_PROF_END ((prof_fn)0x0804bbf9u)
struct accumulator { uint32_t magic, busy, period, last_start; uint16_t peak; };
#define ACC ((volatile struct accumulator *)0x38800f20u)
#define ACC_MAGIC 0x44504143u
#define REPORT_CYCLES 120000000u
void cpu_prof_end(uint8_t *engine) {
    STOCK_PROF_END(engine);
    backup_enable();
    PWR_CR1 |= 1u << 8;
    if (ACC->magic != ACC_MAGIC) {
        ACC->busy = ACC->period = 0;
        ACC->peak = 0;
        ACC->last_start = *(const uint32_t *)(engine + 0x4264);
        ACC->magic = ACC_MAGIC;
        REPORT->magic = 0;
        REPORT->seq = 0;
        return;
    }
    const uint32_t *ring = *(uint32_t *const *)(engine + 0x3e58);
    uint32_t count = *(const uint32_t *)(engine + 0x3e5c);
    if (!ring || !count) return;
    const uint32_t *e = ring + ((count - 1u) & 0x7ffu) * 2u;
    /* Stock ring's period is exit-to-exit and shrinks/grows with load.
     * Use start-to-start for a meaningful recent peak when block load changes.
     * Unsigned subtraction also handles the 32-bit DWT counter wrapping. */
    uint32_t start = *(const uint32_t *)(engine + 0x4264);
    uint32_t period = start - ACC->last_start, busy = e[1];
    ACC->last_start = start;
    if (period < 1000u || period > 50000000u || busy > period) return;
    unsigned load = busy / (period / 1000u + 1u);
    if (load > ACC->peak) ACC->peak = (uint16_t)load;
    ACC->busy += busy;
    ACC->period += period;
    if (ACC->period >= REPORT_CYCLES) {
        unsigned avg = ACC->busy / (ACC->period / 1000u + 1u);
        REPORT->avg = (uint16_t)(avg > 1000u ? 1000u : avg);
        REPORT->peak = ACC->peak > 1000u ? 1000u : ACC->peak;
        REPORT->seq++;
        REPORT->magic = REPORT_MAGIC;
        __asm volatile("dsb" ::: "memory");
        SCB_DCCMVAC = (uint32_t)REPORT;
        __asm volatile("dsb\nisb" ::: "memory");
        ACC->busy = ACC->period = 0;
        ACC->peak = 0;
    }
}
