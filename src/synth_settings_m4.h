/* Application settings only. Cold startup preallocates track arrays before
 * defaults. Native reset keeps any existing allocation and ignores a larger
 * requested capacity. Cover both that preallocation and lazy/project resets. */
#define SYN_SETTING_CAPACITY 112u /* spectral, as combo: parameter sets 112 */
void synth_settings_reset(uint8_t *set,unsigned capacity,const uint8_t *kind) {
    if(*kind==1) {
        capacity=SYN_SETTING_CAPACITY;
        if(*(uint32_t *)(set+4) && *(uint16_t *)(set+12)<capacity) {
            /* The native permanent arena cannot free individual allocations.
             * This compatibility upgrade occurs once per undersized array.
             * Reset deliberately discards values before adding defaults. */
            *(uint32_t *)(set+4)=(uint32_t)((void *(*)(unsigned,unsigned))0x08103129u)(capacity*8u,1);
            *(uint16_t *)(set+12)=(uint16_t)capacity;
        }
    }
    ((void (*)(void *,unsigned))0x08120203u)(set,capacity);
}
__attribute__((naked)) void synth_settings_reset_shim(void) {
    __asm volatile("mov r2,r5\nb synth_settings_reset");
}

/* S14 experiment: a synth track with Input None. The audio core only runs
 * a track whose input (chansource, 0x112) is set; 0 is None. Every value
 * the interface sends to the audio core goes through 0x08108784(ring,
 * target, id, value), so synth_send passes everything through except:
 * - chansource 0 on a track with Synth On is sent as SYN_STAND_IN_INPUT
 *   (the synth replaces that input's audio on this track only);
 * - each Synth On/Off is followed by the track's chansource again, so the
 *   stand-in starts and stops with the synth (also at boot and load).
 * The project and the screen keep None. */
#define CHANSOURCE_ID 0x112u
#define SYN_STAND_IN_INPUT 1
static int synth_input_for(unsigned ch,unsigned id,int value) {
    if(id!=CHANSOURCE_ID || ch>=12u || value)return value;
    return fw_get((uint8_t *)0x300173d0u+0xc08+ch*0x30,SYN_ON)?SYN_STAND_IN_INPUT:0;
}
void synth_send(void *ring,unsigned target,unsigned id,int value) {
    ((void (*)(void *,unsigned,unsigned,int))0x08108785u)(ring,target,id,synth_input_for(target,id,value));
    if(id==SYN_ON && target<12u) {
        int input=fw_get((uint8_t *)0x300173d0u+0xc08+target*0x30,CHANSOURCE_ID);
        int sent=input?input:(value?SYN_STAND_IN_INPUT:0);
        ((void (*)(void *,unsigned,unsigned,int))0x08108785u)(ring,target,CHANSOURCE_ID,sent);
    }
}
