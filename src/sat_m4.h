#include "granular_params.h"
#include "shoegaze_params.h"
#include "tape_params.h"
#include "insert_params.h"
#include "synth_params.h"
/* Desktop 1.5.2 native parameter registration; verified unused id 0x43.
 * Preserve the final stock definition/default before appending our own. */
typedef void (*def_fn)(void *,unsigned,unsigned,const char *,int,int,const char *);
#define STOCK_DEF ((def_fn)0x08134c8du)
void sat_defs(void *table,unsigned id,unsigned type,const char *label,int min,int max,const char *key) {
    STOCK_DEF(table,id,type,label,min,max,key);
    STOCK_DEF(table,0x43,8,"Saturate:",0,1000,"mstdrive");
    static const char *const sat_modes[]={"Soft","Tape"};
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,MT_MODE,"Sat Mode:",sat_modes,2,"mstsatmode");
    STOCK_DEF(table,MT_TONE,8,"Tape Tone:",0,1000,"msttapetone");
    STOCK_DEF(table,MT_OUTPUT,7,"Sat Out:",-12000,6000,"mstsatout");
    STOCK_DEF(table,0x3a,8,"Drift:",0,1000,"dlyflutter");
    STOCK_DEF(table,0x3d,8,"To Rev:",0,1000,"dlysend");
    STOCK_DEF(table,0x4a,1,"Pitch:",-12,12,"dlypitch");
    STOCK_DEF(table,0x4b,4,"Pitch:",0,1,"dlypitchon");
    static const char *const modes[]={"Warp delay","Granular","Tape","Ducked","Reverse","Delay"};
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,G_MODE,"Mode:",modes,6,"dlymode");
    STOCK_DEF(table,G_POS,8,"Position:",0,1000,"grnposition");
    STOCK_DEF(table,G_SIZE,1,"Size ms:",20,250,"grnsize");
    STOCK_DEF(table,G_DENSITY,1,"Density:",1,4,"grndensity");
    STOCK_DEF(table,G_JITTER,8,"Jitter:",0,1000,"grnjitter");
    STOCK_DEF(table,G_FREEZE,4,"Freeze:",0,1,"grnfreeze");
    STOCK_DEF(table,G_REVERSE,4,"Reverse:",0,1,"grnreverse");
    STOCK_DEF(table,T_DRIVE,8,"Drive:",0,1000,"tapedrive");
    STOCK_DEF(table,T_DUCK,8,"Duck:",0,1000,"dlyduck");
    STOCK_DEF(table,T_RELEASE,1,"Release ms:",20,2000,"duckrelease");
    STOCK_DEF(table,T_SEGMENT,1,"Segment ms:",40,600,"revsegment");
    STOCK_DEF(table,T_TONE,8,"Tone:",0,1000,"revtone");
    STOCK_DEF(table,T_HICUT,1,"Hi C:",500,16000,"tapehicut");
    STOCK_DEF(table,T_LOCUT,1,"Lo C:",20,2000,"tapelocut");
    STOCK_DEF(table,T_WILD,4,"Wild:",0,1,"tapewild");
    STOCK_DEF(table,0x1ae,7,"Gate:",-96000,-12000,"ngthreshold");
    /* Ordinal 0 is a fresh track's CLIP before INSRT is switched on (was Off);
     * ordinal 2 was SIN, loaded as CLIP. 5..8 are filters, 9..14 the SPEC
     * group: frequency shifter (F1) and resonator bank (F4). */
    static const char *const insert_modes[]={"CLIP","CLIP","CLIP","FOLD","WRAP","LP","BP","HP","DJ","SHFT","SPRD","RING","HARM","CHRD","BELL","SAT"};
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,INS_MODE,"Insert:",insert_modes,16,"insmode");
    STOCK_DEF(table,INS_DRIVE,8,"Drive:",0,1000,"insdrive");
    STOCK_DEF(table,INS_TONE,8,"Tone:",0,1000,"instone");
    STOCK_DEF(table,INS_MIX,8,"Mix:",0,1000,"insmix");
    STOCK_DEF(table,INS_OUTPUT,7,"Output:",-18000,6000,"insoutput");
    static const char *const synth_switch[]={"Off","On"};
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,INS_ON,"Insert On:",synth_switch,2,"inson");
    static const char *const insert_chords[]={"MIN9","MAJ9","MIN7","MAJ7","SUS","5THS","4THS","DIM"};
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,INS_CHORD,"Chord:",insert_chords,INS_CHORD_COUNT,"inschord");
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_ON,"Synth:",synth_switch,2,"synon");
    /* Spectral F2: VA and Cluster only (FM and Shape removed). Labels past
     * the count keep older projects' ordinals readable: 2 shows VA, 3 and 4
     * (Swarm before S14) show Cluster, as the audio core plays them. */
    static const char *const synth_engines[]={"VA","Cluster","VA","Cluster","Cluster"};
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_ENGINE,"Engine:",synth_engines,SYN_ENGINE_COUNT,"synengine");
    STOCK_DEF(table,SYN_CHANNEL,1,"MIDI Ch:",1,16,"synchannel");
    STOCK_DEF(table,SYN_HARMONICS,8,"Harm:",0,1000,"synharm");
    STOCK_DEF(table,SYN_TIMBRE,8,"Timbre:",0,1000,"syntimbre");
    STOCK_DEF(table,SYN_MORPH,8,"Morph:",0,1000,"synmorph");
    STOCK_DEF(table,SYN_ATTACK,1,"Attack:",1,2000,"synattack");
    STOCK_DEF(table,SYN_RELEASE,1,"Release:",5,5000,"synrelease");
    STOCK_DEF(table,SYN_LEVEL,8,"Level:",0,1000,"synlevel");
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_DRONE,"Drone:",synth_switch,2,"syndrone");
    STOCK_DEF(table,SYN_CUTOFF,1,"Cutoff Hz:",20,20000,"syncutoff");
    STOCK_DEF(table,SYN_LFO_RATE,1,"LFO mHz:",10,20000,"synlforate");
    static const char *const synth_dest[]={"Off","Timbre","Harm","Morph","Cutoff"};
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_LFO_DEST,"LFO Dest:",synth_dest,SYN_DEST_COUNT,"synlfodest");
    STOCK_DEF(table,SYN_LFO_AMOUNT,8,"LFO Amp:",0,1000,"synlfoamt");
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_ENV_DEST,"Env Dest:",synth_dest,SYN_DEST_COUNT,"synenvdest");
    STOCK_DEF(table,SYN_ENV_AMOUNT,1,"Env %:",-100,100,"synenvamt");
    STOCK_DEF(table,SYN_ENV_DECAY,1,"Env Dec:",5,5000,"synenvdecay");
    STOCK_DEF(table,SYN_ENV_ATTACK,1,"Env Atk:",0,5000,"synenvattack");
    static const char *const lfo_shapes[]={"Triangle","Sine","Square","Random"};
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_LFO_SHAPE,"LFO Shape:",lfo_shapes,SYN_LFO_SHAPE_COUNT,"synlfoshape");
    /* 13..24 are Free values that remember a division; see synth_params.h. */
    static const char *const lfo_sync[]={"Free","1/64","1/32","1/16","1/8T","1/16D","1/8","1/4T","1/8D","1/4","1/2T","1/2","1 bar",
        "Free","Free","Free","Free","Free","Free","Free","Free","Free","Free","Free","Free"};
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_LFO_SYNC,"LFO Div:",lfo_sync,SYN_LFO_SYNC_MAX+1u,"synlfosync");
    static const char *const lfo_modes[]={"Free","Sync"};
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_LFO_MODE,"LFO Sync:",lfo_modes,2,"synlfomode");
    static const char *const seeds[SYN_SEED_COUNT]={"1","2","3","4","5","6","7","8","9","10","11","12","13","14","15","16",
        "17","18","19","20","21","22","23","24","25","26","27","28","29","30","31","32"};
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_SEED,"Seed:",seeds,SYN_SEED_COUNT,"synseed");
    /* S10: second LFO. */
    STOCK_DEF(table,SYN_LFO2_RATE,1,"LFO2 mHz:",10,20000,"synlfo2rate");
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_LFO2_DEST,"LFO2 Dest:",synth_dest,SYN_DEST_COUNT,"synlfo2dest");
    STOCK_DEF(table,SYN_LFO2_AMOUNT,8,"LFO2 Amp:",0,1000,"synlfo2amt");
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_LFO2_SHAPE,"LFO2 Shape:",lfo_shapes,SYN_LFO_SHAPE_COUNT,"synlfo2shape");
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_LFO2_SYNC,"LFO2 Div:",lfo_sync,SYN_LFO_SYNC_MAX+1u,"synlfo2sync");
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,SYN_LFO2_MODE,"LFO2 Sync:",lfo_modes,2,"synlfo2mode");
    STOCK_DEF(table,SYN_RESO,8,"Reso:",0,1000,"synreso"); /* S18 */
    /* Keep the original style ordinals; only the final row is experimental. */
    static const char *const styles[]={"Tight Ambience","Bright Room","Large Room","Wide Slap","Notchy Ambiance","Reso Room","Marble Hall","Chamber","Dark Hall","Small Hall","Gated","Arena","Long Bright Bloom","Gravitas","Clouds","Gazer"};
    ((void (*)(void *,unsigned,const char *,const char *const *,unsigned,const char *))0x08134c39u)
        (table,0x155,"Style:",styles,16,"r2style");
    STOCK_DEF(table,SG_SAT,8,"Saturate:",0,1000,"sgsat");
    STOCK_DEF(table,SG_SIZE,8,"Size:",0,1000,"sgsize");
    STOCK_DEF(table,SG_DIFF,8,"Diffusion:",0,1000,"sgdiff");
    STOCK_DEF(table,SG_MOD,8,"Mod Depth:",0,1000,"sgmod");
    STOCK_DEF(table,SG_RATE,1,"Rate mHz:",10,2000,"sgrate");
    STOCK_DEF(table,SG_HICUT,1,"Hi C:",500,16000,"sghicut");
    STOCK_DEF(table,SG_LOCUT,1,"Lo C:",20,2000,"sglocut");
}
typedef void (*add_fn)(void *,unsigned,int);
#define STOCK_ADD ((add_fn)0x0812028fu)
void sat_default(void *set,unsigned id,int value) {
    STOCK_ADD(set,id,value);
    STOCK_ADD(set,0x43,0);
    STOCK_ADD(set,MT_MODE,0);STOCK_ADD(set,MT_TONE,500);STOCK_ADD(set,MT_OUTPUT,0);
}
void sg_defaults_ui(void *set,unsigned id,int value) {
    STOCK_ADD(set,id,value);
    static const struct {uint16_t id;int16_t value;} defaults[]={
        {SG_SAT,350},{SG_SIZE,600},{SG_DIFF,750},{SG_MOD,250},
        {SG_RATE,200},{SG_HICUT,5500},{SG_LOCUT,120}
    };
    for(unsigned i=0;i<sizeof(defaults)/sizeof(defaults[0]);i++)
        STOCK_ADD(set,defaults[i].id,defaults[i].value);
}

void dly_defaults(void *set) {
    static const struct {uint16_t id;int16_t value;} defaults[]={
        {0x33,400},{0x39,400},{0xe,120},{0xcb,1000},{0x4a,0},{0x3a,0},{0x3d,0},
        {0xca,1},{0x35,1},{0x36,1},{0x4b,0},{0x37,0},{0x34,6},
        {G_MODE,5},{G_POS,250},{G_SIZE,80},{G_DENSITY,4},{G_JITTER,0},{G_FREEZE,0},{G_REVERSE,0},
        {T_DRIVE,350},{T_DUCK,700},{T_RELEASE,300},{T_SEGMENT,400},{T_TONE,650},{T_HICUT,6000},{T_WILD,0},{T_LOCUT,20}
    };
    for(unsigned i=0;i<sizeof(defaults)/sizeof(defaults[0]);i++)
        STOCK_ADD(set,defaults[i].id,defaults[i].value);
}
