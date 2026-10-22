#ifndef BB_GATE_DIAG_SHARED_H
#define BB_GATE_DIAG_SHARED_H
#define GD_AUDIO_MAGIC 0x47444131u
#define GD_UI_MAGIC 0x47445533u
/* First line is a published, cache-cleaned snapshot. The second line is
 * M7-only accumulation state. Neither line overlaps the production gate. */
struct gate_audio_diag {
    uint32_t magic,seq;
    int32_t threshold;
    float left,right,env,gain;
    uint32_t events;
    uint32_t ticks,total_events,owner;
    float max_left,max_right;
    uint32_t spare[3];
};
#define GD_AUDIO ((volatile struct gate_audio_diag *)0x38800680u)
struct gate_ui_diag {
    uint32_t magic,mix_events,mix_gate_events,mix_local,track_events;
    uint32_t mix_panel,mix_knob,track_knob;
    int32_t mix_ch,track_ch,last_ch;
    uint32_t mix_pid,mix_kid,source;
    uint32_t ux_tick,ux_page;
};
#define GD_UI ((volatile struct gate_ui_diag *)0x38800a00u)
_Static_assert(sizeof(struct gate_audio_diag)==64,"diagnostic cache-line ABI");
_Static_assert(0x680+12*sizeof(struct gate_audio_diag)<=0xa00,"diagnostic report extent");
_Static_assert(0xa00+sizeof(struct gate_ui_diag)<=0xa40,"diagnostic UI extent");
#endif
