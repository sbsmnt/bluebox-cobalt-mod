/* Desktop port of BlueLex's CPU meter. See NOTICE.md for upstream attribution.
 * Desktop 1.5.2 only. Both cores access these isolated backup-SRAM cache lines.
 */
#include <stdint.h>
#define RCC_AHB4ENR (*(volatile uint32_t *)0x580244e0u)
#define PWR_CR1 (*(volatile uint32_t *)0x58024800u)
#define SCB_DCCMVAC (*(volatile uint32_t *)0xe000ef68u)
struct cpu_report { uint32_t magic; uint16_t avg, peak; uint32_t seq; };
#define REPORT ((volatile struct cpu_report *)0x38800f00u)
#define REPORT_MAGIC 0x44504355u
static inline void backup_enable(void) {
    RCC_AHB4ENR |= 1u << 28;
    (void)RCC_AHB4ENR;
}
