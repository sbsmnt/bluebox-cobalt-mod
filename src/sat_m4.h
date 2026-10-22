#include "granular_params.h"
#include "shoegaze_params.h"
#include "tape_params.h"
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
