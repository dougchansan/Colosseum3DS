#ifndef C3_GAME_UI_VIEWS_H
#define C3_GAME_UI_VIEWS_H
#include "colosseum3ds/bottom_ui.h"
#include "colosseum3ds/field_story.h"
#include "colosseum3ds/battle_preview_menu.h"
#include "colosseum3ds/battle_presentation.h"

typedef enum { FIELD_SETUP, FIELD_ARRIVAL, FIELD_ARRIVAL_OUT, FIELD_FADE_IN, FIELD_CONTROL } FieldPhase;
typedef enum { PAUSE_CLOSED, PAUSE_MAIN, PAUSE_PARTY, PAUSE_SAVE, PAUSE_LEAVE, PAUSE_MESSAGE } PausePage;

uint32_t c3_field_ui_identity(PausePage page,const C3FieldStoryView *story);
void c3_field_ui_draw_to(C3BottomUi *ui,const C3GameSummary *summary,FieldPhase phase,PausePage page,
    unsigned selection,int yes,const char *message,const C3FieldStoryView *story,C3UiPager *pager);
void c3_battle_ui_draw_to(C3BottomUi *ui,const C3BattlePreview *battle,const C3BattlePreviewTables *tables,
    const C3BattlePreviewMenu *menu,const C3BattlePresentation *presentation,const char *error,C3UiPager *pager);
#endif
