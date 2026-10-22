/* Read-only compressor telemetry, separate backup-SRAM cache lines. */
struct gr_report { uint32_t magic; uint16_t gr; uint8_t on, reserved; uint32_t seq; };
#define GR_REPORT ((volatile struct gr_report *)0x38800f40u)
#define GR_MAGIC 0x44475232u
