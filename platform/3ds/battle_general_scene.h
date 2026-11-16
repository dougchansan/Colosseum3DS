#ifndef C3_BATTLE_GENERAL_SCENE_H
#define C3_BATTLE_GENERAL_SCENE_H
#include "colosseum3ds/battle_general.h"
#include <stdio.h>

#define C3_BATTLE_PACK_PATH "sdmc:/3ds/Colosseum3DS/boot/battles.c3x"

/* Modal story battle on the bottom screen: plays back each turn's events,
 * takes the hero's commands (move, target, switch) and runs until the
 * battle ends. Draws no top-screen frame and allocates no GPU memory.
 * Returns 0 when the battle finished, 1 when the application must exit,
 * -1 on an engine error (the battle is left unfinished). */
int c3_battle_general_scene_run(const C3BattlePack *pack, C3GeneralBattle *battle, FILE *trace);

/* Battle music as battle_main.c starts it: fn_801EF758 plays the encounter's
 * BGM (fightEncountGetBgmSndDataId) in slot 0x406, fn_801EF6FC the battle
 * floor's environment sound in slot 0x407 with a 1000 ms fade. end puts the
 * field's previous sounds back. */
typedef struct { uint32_t field[2]; } C3BattleMusic;
void c3_battle_music_begin(C3BattleMusic *music, const C3BattleEncounterInfo *info, FILE *trace);
void c3_battle_music_end(const C3BattleMusic *music);

/* The bottom-screen command UI, shared with the 3D battle scene
 * (battle_general_3d.c), which shows the events on the top screen. */
typedef enum { C3_GUI_EVENTS, C3_GUI_MOVES, C3_GUI_TARGET, C3_GUI_SWITCH, C3_GUI_RESULT, C3_GUI_WAIT } C3GeneralUiPage;
typedef struct {
    C3GeneralUiPage page;
    unsigned cursor, position, target_cursor;
    C3GeneralCommand commands[2];
    char error[C3_GB_ERROR_SIZE];
    /* Link battles (docs/link-play.md): control_mask is the side-0
     * positions (bits 0/1) this console commands, 0 meaning all; defer
     * makes a complete set of commands wait on C3_GUI_WAIT (press returns
     * 2) instead of running the turn here. */
    unsigned control_mask;
    int defer;
} C3GeneralUi;
/* Start choosing the hero's commands for the next turn. Returns 0 when
 * none of the controlled positions has a Pokemon able to act. */
int c3_general_ui_commands(C3GeneralUi *ui, const C3GeneralBattle *b);
/* A press on a command page. Returns 1 when the commands were complete and
 * the turn ran (its events are in b->events), 2 when they are complete and
 * deferred (ui->defer), 0 otherwise; an engine error is shown on the page
 * and command entry starts again. */
int c3_general_ui_press(C3GeneralUi *ui, const C3BattlePack *pack, C3GeneralBattle *b, uint16_t pressed, FILE *trace);
/* shown: the logged event being presented (its text and HP), or NULL. */
void c3_general_ui_draw(const C3BattlePack *pack, const C3GeneralBattle *b, const C3GeneralUi *ui, const C3GeneralEvent *shown);
#endif
