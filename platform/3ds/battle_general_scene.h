#ifndef C3_BATTLE_GENERAL_SCENE_H
#define C3_BATTLE_GENERAL_SCENE_H
#include "colosseum3ds/battle_general.h"
#include "colosseum3ds/press_gate.h"
#include <stdio.h>

#define C3_BATTLE_PACK_PATH "sdmc:/3ds/Colosseum3DS/boot/battles.c3x"

/* Modal story battle on the bottom screen: plays back each turn's events,
 * takes the hero's commands (action menu, move, target, switch, bag, call)
 * and runs until the battle ends. Draws no top-screen frame and allocates
 * no GPU memory.
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
/* C3_GUI_BALL is retired (the balls are the bag's second pocket); the
 * values stay as they were for the agent link's page numbers. */
typedef enum { C3_GUI_EVENTS, C3_GUI_MOVES, C3_GUI_TARGET, C3_GUI_SWITCH, C3_GUI_RESULT, C3_GUI_WAIT, C3_GUI_BALL,
               C3_GUI_ACTION, C3_GUI_BAG, C3_GUI_BAG_PARTY, C3_GUI_BAG_MOVE } C3GeneralUiPage;
/* The action menu each Pokemon's command starts on: fightMenuFightTrainer-
 * GcHeroOpenMenu's menu 0x4B (menuFightMainCtrl), items 0xB5-0xB8 in a 2x2
 * grid, FIGHT ITEMS / PKMN CALL. The entry is the menu result its SubMain
 * takes: 0 the move list, 1 the bag, 2 the party, 3 Call. */
enum { C3_GUI_FIGHT, C3_GUI_ITEMS, C3_GUI_PKMN, C3_GUI_CALL, C3_GUI_ACTIONS };
/* The entry's text: the item's original message (common_rel 0x13C, 0x13D,
 * 0x13E, 0x140). The scene starts with plain stand-ins; set_action_label
 * replaces one with the game's text (copied, at most 23 bytes). */
uint32_t c3_general_ui_action_message(unsigned entry);
void c3_general_ui_set_action_label(unsigned entry, const char *text);
const char *c3_general_ui_action_label(unsigned entry);
/* ITEMS opens the bag (fight_menu.c SubItem, the item menu fn_80018F88 in
 * battle mode): the pockets with a battle use, ITEMS, POKe BALLS and
 * BERRIES (summary pages 5, 4 and 2: titles 0x2AF9, 0x2AFA, 0x2AFC),
 * switched with L/R; an item opens USE / CANCEL (common 0x131 / 0x134;
 * an Items-pocket item with no battleUseFunc offers only CANCEL); USE
 * opens the party (medicine), then a move (Ether), or goes straight on
 * (balls to the Shadow target, X items on the Pokemon choosing). B steps
 * back one page each time. Labels as for the action menu. */
enum { C3_GUI_BAG_ITEMS, C3_GUI_BAG_BALLS, C3_GUI_BAG_BERRIES, C3_GUI_BAG_USE, C3_GUI_BAG_CANCEL, C3_GUI_BAG_LABELS };
uint32_t c3_general_ui_bag_message(unsigned entry);
void c3_general_ui_set_bag_label(unsigned entry, const char *text);
const char *c3_general_ui_bag_label(unsigned entry);
typedef struct {
    C3GeneralUiPage page;
    unsigned cursor, position, target_cursor;
    /* The action menu's cursor: FIGHT for each Pokemon, kept while its
     * submenus open and close (fn_80011A1C's cursor). */
    unsigned action_cursor;
    /* The 3D scene times its messages; only a held one waits for A. */
    int timed_log;
    /* The bag: the pocket (kept for the battle), the list cursor, the
     * USE / CANCEL menu (bag_confirm, its cursor bag_choice) and the item
     * chosen (a bag entry, or a ball id in the ball pocket). */
    unsigned bag_pocket, bag_cursor, bag_confirm, bag_choice, bag_entry;
    C3GeneralCommand commands[2];
    char error[C3_GB_ERROR_SIZE];
    /* Link battles (docs/link-play.md): control_mask is the side-0
     * positions (bits 0/1) this console commands, 0 meaning all; defer
     * makes a complete set of commands wait on C3_GUI_WAIT (press returns
     * 2) instead of running the turn here. */
    unsigned control_mask;
    int defer;
    /* colosseum3ds/press_gate.h: a command page opened (or advanced) by a
     * press acts only on the next press. */
    C3PressGate gate;
} C3GeneralUi;
/* Start choosing the hero's commands for the next turn. Returns 0 when
 * none of the controlled positions has a Pokemon able to act. */
int c3_general_ui_commands(C3GeneralUi *ui, const C3GeneralBattle *b);
/* This frame's edges that the current page may act on (call every frame,
 * before c3_general_ui_press). A page opened by c3_general_ui_commands, a
 * result page and every page a press advances ignore the buttons down when
 * they appeared until those are released and pressed again. */
uint16_t c3_general_ui_input(C3GeneralUi *ui, uint16_t held, uint16_t pressed);
/* A press on a command page. Returns 1 when the commands were complete and
 * the turn ran (its events are in b->events), 2 when they are complete and
 * deferred (ui->defer), 0 otherwise; an engine error is shown on the page
 * and command entry starts again. */
int c3_general_ui_press(C3GeneralUi *ui, const C3BattlePack *pack, C3GeneralBattle *b, uint16_t pressed, FILE *trace);
/* shown: the logged event being presented (its text and HP), or NULL. */
void c3_general_ui_draw(const C3BattlePack *pack, const C3GeneralBattle *b, const C3GeneralUi *ui, const C3GeneralEvent *shown);
#endif
