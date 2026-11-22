#include "insert_drive.h"
/* The EQ object's channel tag sits just before the sidecar; it selects the
 * track's shifter state in the SDRAM bank. */
void insert_drive_audio(struct insert_drive *s,float *l,float *r,unsigned n) {
    unsigned ch=*(const uint32_t *)((const uint8_t *)s-(INS_STATE_OFFSET-INS_CHANNEL_OFFSET));
    ins_process(s,fsh_bank_state(ch),l,r,n);
}
