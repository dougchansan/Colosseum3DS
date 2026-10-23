#ifndef C3_BATTLE_STAGE_H
#define C3_BATTLE_STAGE_H
#include "stage_renderer.h"
#include "colosseum3ds/battle_presentation.h"
#include <stdio.h>
#define C3_BATTLE_STAGE_MODELS 16u
typedef struct {
    C3Title models[C3_BATTLE_STAGE_MODELS];
    const C3Title *draw[C3_BATTLE_STAGE_MODELS];
    uint32_t count, slots[C3_BATTLE_STAGE_MODELS], roles[C3_BATTLE_STAGE_MODELS][9];
    uint32_t clips[C3_BATTLE_STAGE_MODELS];
    float transforms[C3_BATTLE_STAGE_MODELS][5], clocks[C3_BATTLE_STAGE_MODELS];
    C3TitleCamera camera;
    C3BattlePresentation presentation;
    uint8_t initialized, fresh[C3_BATTLE_STAGE_MODELS];
    char error[192];
    FILE *trace;
} C3BattleStage;
/* Descriptor and all paths are private prepared assets. No guessed fallback. */
int c3_battle_stage_load(C3BattleStage *,const char *root,const uint16_t hp[4],FILE *,char *,size_t);
int c3_battle_stage_submit(C3BattleStage *,const C3BattlePreviewEvent *,uint32_t);
int c3_battle_stage_step(C3BattleStage *,uint32_t,char *,size_t);
int c3_battle_stage_draw(C3BattleStage *,C3TitleRenderStats *,char *,size_t);
/* Caller polls until true before releasing storage or restoring field. */
int c3_battle_stage_close(C3BattleStage *);
#endif
