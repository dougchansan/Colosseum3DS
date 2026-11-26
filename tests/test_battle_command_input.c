#include "battle_general_scene.h"
#include "bottom_screen.h"
#include "move_forget_scene.h"
#include "music_player.h"
#include "n3ds_platform.h"
#include "battle_general_3d.h"
#include "colosseum3ds/press_gate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* One press acts in one battle UI layer (platform/3ds/battle_general_3d.c
 * frame order, synthetic pack). The 3D scene's log is timed: the frame after
 * the last message expires opens the command page (the action menu, "What
 * will X do?") at the top of the loop, before that frame's press is read and
 * before the page is drawn. A press whose edge falls on that frame was made
 * while the message was on screen and must not choose anything; the next
 * press, and each one after it, advances exactly one level. The action menu
 * is the original's menu 0x4B (FIGHT ITEMS / PKMN CALL): a stray A there can
 * only open FIGHT's move list, never commit a move. */

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #x, error); exit(1); } } while (0)
static char error[256];
#include "battle_pack_fixture.h"

/* battle_general_scene.c's modal loop and draw are not run here. */
C3BottomUi *c3_bottom_screen_begin(void) { return NULL; }
void c3_bottom_screen_present(void) {}
int c3_music_play(int slot, uint32_t sound, uint32_t volume, uint32_t fade_ms) { (void)slot; (void)sound; (void)volume; (void)fade_ms; return 0; }
void c3_music_stop_slot(int slot, uint32_t fade_ms) { (void)slot; (void)fade_ms; }
uint32_t c3_music_current(int slot) { (void)slot; return 0; }
void c3_move_forget_scene_bind(FILE *trace) { (void)trace; }
bool c3_n3ds_poll(C3N3dsInput *input) { (void)input; return false; }
uint64_t c3_n3ds_time_us(void) { return 0; }
int c3_battle_general_autoplaying(void) { return 0; }
void c3_battle_general_probe_commands(C3GeneralBattle *b, const C3BattlePack *pack, C3GeneralCommand c[2]) { (void)b; (void)pack; (void)c; }

/* The pad as c3_n3ds_poll reports it: held, and edges held & ~previous. */
typedef struct { uint16_t previous; } Pad;
static C3N3dsInput pad_frame(Pad *pad, uint16_t held)
{
    C3N3dsInput in; memset(&in, 0, sizeof in);
    in.held = held; in.pressed = (uint16_t)(held & ~pad->previous); pad->previous = held;
    return in;
}

/* The 3D loop's input order for one frame; `gated` 0 replays the loop
 * before the press gate (the page acts on the raw edge). Returns the
 * c3_general_ui_press result, or -1 when no page took input. */
typedef struct {
    C3GeneralUi ui; Pad pad; C3GeneralBattle *b; const C3BattlePack *pack;
    unsigned message_frames; /* frames the last message still shows */
    unsigned frame;
} Loop;
static int loop_frame(Loop *l, uint16_t held, int gated)
{
    C3N3dsInput in = pad_frame(&l->pad, held);
    ++l->frame;
    if (l->ui.page == C3_GUI_EVENTS && !l->message_frames && !c3_general_ui_replace(&l->ui, l->b)) CHECK(c3_general_ui_commands(&l->ui, l->b));
    uint16_t page_pressed = c3_general_ui_input(&l->ui, in.held, in.pressed);
    if (!gated) page_pressed = in.pressed;
    int r = -1;
    if (l->ui.page != C3_GUI_EVENTS && l->ui.page != C3_GUI_WAIT && l->ui.page != C3_GUI_RESULT && page_pressed)
        r = c3_general_ui_press(&l->ui, l->pack, l->b, page_pressed, NULL);
    if (r == 1) l->message_frames = 120; /* the turn's log plays */
    if (l->ui.page == C3_GUI_EVENTS && l->message_frames) --l->message_frames; /* stage step */
    return r;
}
static void loop_begin(Loop *l, C3GeneralBattle *b, const C3BattlePack *pack, unsigned message_frames)
{
    memset(l, 0, sizeof *l);
    l->b = b; l->pack = pack; l->ui.page = C3_GUI_EVENTS; l->message_frames = message_frames;
}
/* A Luma input-redirection press: A down 9 frames (0.15 s), then up. */
static void press(Loop *l, uint16_t button, int gated)
{
    for (unsigned k = 0; k < 9; ++k) loop_frame(l, button, gated);
    loop_frame(l, 0, gated);
}
static void idle(Loop *l, unsigned frames, int gated) { while (frames--) loop_frame(l, 0, gated); }
/* A press whose c3_general_ui_press result is returned (the last one any
 * of its frames gave, -1 for none). */
static int press_result(Loop *l, uint16_t button)
{
    int r = -1, k;
    for (unsigned f = 0; f < 9; ++f) if ((k = loop_frame(l, button, 1)) >= 0) r = k;
    if ((k = loop_frame(l, 0, 1)) >= 0) r = k;
    return r;
}
static int nothing_chosen(const Loop *l)
{
    static const C3GeneralCommand zero[2];
    return l->ui.page == C3_GUI_ACTION && l->ui.position == 0 && l->ui.action_cursor == C3_GUI_FIGHT &&
           !memcmp(l->ui.commands, zero, sizeof zero);
}
/* Waits out a press, then makes the next one. */
static void next(Loop *l, uint16_t button) { idle(l, 60, 1); press(l, button, 1); }

int main(void)
{
    size_t n; uint8_t *bytes = make_pack(2, 5, &n);
    C3BattlePack pack; memset(&pack, 0, sizeof pack);
    CHECK(bytes && c3_battle_pack_decode(&pack, bytes, n, error));
    uint8_t party[6][0x138]; make_party(party, 1, 50, 2, 0);
    static C3GeneralBattle start, b;
    CHECK(c3_general_battle_begin(&start, &pack, 0, party, 7, error));
    CHECK(start.active_count == 2 && start.outcome == C3_GB_RUNNING);
    Loop l;

    {   /* The gate alone: a page closed with A down ignores A until it is
         * released, a replayed edge of that press included, then takes the
         * next press once. Other buttons pressed later pass. */
        C3PressGate g = {0};
        CHECK(c3_press_gate_take(&g, C3_PAD_A, C3_PAD_A) == C3_PAD_A);
        c3_press_gate_close(&g);
        CHECK(c3_press_gate_take(&g, C3_PAD_A, C3_PAD_A) == 0);
        CHECK(c3_press_gate_take(&g, C3_PAD_A | C3_PAD_B, C3_PAD_B) == C3_PAD_B);
        CHECK(c3_press_gate_take(&g, 0, C3_PAD_A) == 0); /* render-wait replay of the same press */
        CHECK(c3_press_gate_take(&g, C3_PAD_A, C3_PAD_A) == C3_PAD_A);
        c3_press_gate_consume(&g, C3_PAD_A);
        CHECK(c3_press_gate_take(&g, C3_PAD_A, 0) == 0 && g.blocked == C3_PAD_A);
        CHECK(c3_press_gate_take(&g, 0, 0) == 0 && g.blocked == 0);
    }

    {   /* The entries' texts: stand-ins until the game's messages are set. */
        CHECK(c3_general_ui_action_message(C3_GUI_FIGHT) == 0x13C && c3_general_ui_action_message(C3_GUI_ITEMS) == 0x13D);
        CHECK(c3_general_ui_action_message(C3_GUI_PKMN) == 0x13E && c3_general_ui_action_message(C3_GUI_CALL) == 0x140);
        CHECK(c3_general_ui_action_message(C3_GUI_ACTIONS) == 0);
        CHECK(!strcmp(c3_general_ui_action_label(C3_GUI_FIGHT), "FIGHT"));
        c3_general_ui_set_action_label(C3_GUI_CALL, "");
        CHECK(!strcmp(c3_general_ui_action_label(C3_GUI_CALL), "CALL"));
        c3_general_ui_set_action_label(C3_GUI_CALL, "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
        CHECK(!strcmp(c3_general_ui_action_label(C3_GUI_CALL), "ABCDEFGHIJKLMNOPQRSTUVW")); /* 23 bytes kept */
        c3_general_ui_set_action_label(C3_GUI_CALL, "CALL");
    }

    /* Before the gate: an A whose edge falls on the frame the command page
     * opens acts on the action menu, which only opens FIGHT's move list. */
    b = start; loop_begin(&l, &b, &pack, 30);
    idle(&l, 30, 0);
    press(&l, C3_PAD_A, 0);
    CHECK(l.ui.page == C3_GUI_MOVES && l.ui.position == 0 && l.ui.commands[0].kind == 0 && l.ui.commands[0].target == 0);

    /* With it, at every phase of the press against the message's end (the
     * press starting up to 12 frames before the page opens, on its opening
     * frame, or while the button is still down): the action menu is open
     * on FIGHT with nothing chosen. */
    for (unsigned lead = 0; lead <= 12; ++lead) {
        b = start; loop_begin(&l, &b, &pack, 30);
        idle(&l, 30 - lead, 1);
        press(&l, C3_PAD_A, 1);
        idle(&l, 12, 1);
        CHECK(nothing_chosen(&l));
        /* Then one press per level: FIGHT -> move list -> target list ->
         * the second Pokemon's action menu -> FIGHT -> its target list ->
         * the turn runs. */
        next(&l, C3_PAD_A);
        CHECK(l.ui.page == C3_GUI_MOVES && l.ui.position == 0 && l.ui.cursor == 0);
        next(&l, C3_PAD_A);
        CHECK(l.ui.page == C3_GUI_TARGET && l.ui.position == 0 && l.ui.commands[0].slot == 0);
        next(&l, C3_PAD_A);
        CHECK(l.ui.page == C3_GUI_ACTION && l.ui.position == 1 && l.ui.action_cursor == C3_GUI_FIGHT && l.ui.commands[0].target >= 2);
        next(&l, C3_PAD_A);
        CHECK(l.ui.page == C3_GUI_MOVES && l.ui.position == 1);
        next(&l, C3_PAD_A);
        CHECK(l.ui.page == C3_GUI_TARGET && l.ui.position == 1);
        uint32_t turn = b.turn;
        next(&l, C3_PAD_A);
        CHECK(l.ui.page == C3_GUI_EVENTS && b.turn == turn + 1);
        /* The next turn's page opens as the first did (its log started on
         * that press's first frame). */
        idle(&l, 111 - lead, 1);
        press(&l, C3_PAD_A, 1);
        idle(&l, 12, 1);
        CHECK(b.outcome != C3_GB_RUNNING || nothing_chosen(&l));
    }

    /* "Go, ESPEON!" with one foe left: the move list commits a move with
     * no target page. Even a carried press only opens FIGHT now. */
    {
        C3GeneralBattle one = start;
        one.party[1][one.active[1][1]].hp = 0;
        b = one; loop_begin(&l, &b, &pack, 20);
        idle(&l, 20, 0);
        press(&l, C3_PAD_A, 0);
        CHECK(l.ui.page == C3_GUI_MOVES && l.ui.position == 0 && l.ui.commands[0].target == 0); /* before the gate */
        b = one; loop_begin(&l, &b, &pack, 20);
        idle(&l, 20, 1);
        press(&l, C3_PAD_A, 1);
        CHECK(nothing_chosen(&l));
        next(&l, C3_PAD_A);
        CHECK(l.ui.page == C3_GUI_MOVES && l.ui.position == 0);
        next(&l, C3_PAD_A);
        CHECK(l.ui.page == C3_GUI_ACTION && l.ui.position == 1 && l.ui.commands[0].target == 2);
    }

    /* B held from the log (the "hurry" button) does not back out of the
     * page later; it is free again once released. Direction edges after
     * the opening frame move in the 2x2 grid: FIGHT ITEMS / PKMN CALL. */
    b = start; loop_begin(&l, &b, &pack, 10);
    idle(&l, 10, 1);
    for (unsigned k = 0; k < 20; ++k) loop_frame(&l, C3_PAD_B, 1);
    CHECK(nothing_chosen(&l));
    loop_frame(&l, C3_PAD_B | C3_PAD_DOWN, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.action_cursor == C3_GUI_PKMN);
    loop_frame(&l, 0, 1); loop_frame(&l, C3_PAD_RIGHT, 1);
    CHECK(l.ui.action_cursor == C3_GUI_CALL);
    loop_frame(&l, 0, 1); loop_frame(&l, C3_PAD_UP, 1);
    CHECK(l.ui.action_cursor == C3_GUI_ITEMS);
    loop_frame(&l, 0, 1); loop_frame(&l, C3_PAD_LEFT, 1);
    CHECK(l.ui.action_cursor == C3_GUI_FIGHT && l.ui.page == C3_GUI_ACTION);
    loop_frame(&l, 0, 1);
    /* B from the move list (and its target list) goes back up a level at a
     * time to the action menu, still on FIGHT. */
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_MOVES);
    loop_frame(&l, C3_PAD_DOWN, 1); loop_frame(&l, 0, 1);
    CHECK(l.ui.page == C3_GUI_MOVES && l.ui.cursor == 1);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_TARGET && l.ui.commands[0].slot == 1);
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_MOVES && l.ui.position == 0);
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.position == 0 && l.ui.action_cursor == C3_GUI_FIGHT);
    /* B on the first Pokemon's action menu stays (no earlier choice). */
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.position == 0);
    /* B on the second Pokemon's action menu goes back to the first one's,
     * on FIGHT, to choose again. */
    press(&l, C3_PAD_A, 1); press(&l, C3_PAD_A, 1); press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.position == 1);
    loop_frame(&l, C3_PAD_RIGHT, 1); loop_frame(&l, 0, 1);
    CHECK(l.ui.action_cursor == C3_GUI_ITEMS);
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.position == 0 && l.ui.action_cursor == C3_GUI_FIGHT);

    /* PKMN opens the party (both are out here, so neither can switch in);
     * B returns to the action menu with its cursor where it was. */
    b = start; loop_begin(&l, &b, &pack, 1);
    idle(&l, 30, 1);
    loop_frame(&l, C3_PAD_DOWN, 1); loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_SWITCH && l.ui.position == 0);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_SWITCH && l.ui.error[0] && l.ui.commands[0].kind == 0);
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.action_cursor == C3_GUI_PKMN);

    /* ITEMS with floor status 0x20 (the fight kind's useItem) clear: the
     * refusal (fight message 0x75F5) and the menu stays. */
    b = start; b.info.use_item = 0; loop_begin(&l, &b, &pack, 1);
    idle(&l, 30, 1);
    loop_frame(&l, C3_PAD_RIGHT, 1); loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.action_cursor == C3_GUI_ITEMS && l.ui.error[0]);

    /* ITEMS opens the bag on its ITEMS pocket even with nothing in it; B
     * goes back to the action menu on ITEMS. */
    b = start; b.snag = 0; loop_begin(&l, &b, &pack, 1);
    idle(&l, 30, 1);
    loop_frame(&l, C3_PAD_RIGHT, 1); loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG && l.ui.bag_pocket == C3_GUI_BAG_ITEMS && !l.ui.bag_confirm);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG && !l.ui.bag_confirm); /* an empty pocket: nothing to choose */
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.action_cursor == C3_GUI_ITEMS);

    /* The Snag Machine's balls are the bag's second pocket (R): a ball,
     * USE, and the throw at the one Shadow foe (no target page). */
    b = start; b.snag = 1; b.balls[1] = 1; loop_begin(&l, &b, &pack, 1);
    CHECK(c3_general_battle_can_snag(&b, &pack, 0, 1, 2) && !c3_general_battle_can_snag(&b, &pack, 0, 1, 3));
    idle(&l, 30, 1);
    loop_frame(&l, C3_PAD_RIGHT, 1); loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG && l.ui.bag_pocket == C3_GUI_BAG_ITEMS);
    press(&l, C3_PAD_R, 1);
    CHECK(l.ui.page == C3_GUI_BAG && l.ui.bag_pocket == C3_GUI_BAG_BALLS && l.ui.bag_cursor == 0);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG && l.ui.bag_confirm && l.ui.bag_entry == 1 && l.ui.bag_choice == 0);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.position == 1 && l.ui.action_cursor == C3_GUI_FIGHT);
    CHECK(l.ui.commands[0].kind == 2 && l.ui.commands[0].slot == 1 && l.ui.commands[0].target == 2);
    /* One ball a turn: the second Pokemon's USE on a ball is refused
     * (message 0x426D) and the pocket stays open. */
    b.balls[1] = 2;
    loop_frame(&l, C3_PAD_RIGHT, 1); loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG && l.ui.bag_pocket == C3_GUI_BAG_BALLS); /* the pocket is kept */
    press(&l, C3_PAD_A, 1); press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG && !l.ui.bag_confirm && l.ui.error[0] && l.ui.commands[1].kind == 0);
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.position == 1);
    b.balls[1] = 1;
    /* CALL commits the second Pokemon's command, and the turn runs. */
    loop_frame(&l, C3_PAD_DOWN, 1); loop_frame(&l, 0, 1);
    CHECK(l.ui.action_cursor == C3_GUI_CALL);
    {
        uint32_t turn = b.turn;
        CHECK(press_result(&l, C3_PAD_A) == 1);
        CHECK(l.ui.page == C3_GUI_EVENTS && b.turn == turn + 1 && l.ui.commands[1].kind == 3 && l.ui.commands[1].target == 1);
        CHECK(b.caught[0] == 1 && b.balls_used[1] == 1);
    }

    /* Bag items. A Hyper Potion, an Ether and a Poke Doll in the ITEMS
     * pocket, an HP Up (no battle use) and a berry. */
    C3GeneralBattle bag = start;
    {
        static const struct { uint16_t item, count; uint8_t use, pocket, at, value; const char *name; } rows[] = {
            { 21, 2, C3_GB_USE_MEDICINE, 0, 10, 200, "HYPER POTION" }, { 34, 1, C3_GB_USE_PP, 0, 11, 10, "ETHER" },
            { 80, 1, C3_GB_USE_REFUSED, 0, 16, 0, "POKE DOLL" }, { 63, 1, C3_GB_USE_NONE, 0, 8, 10, "HP UP" },
            { 139, 1, C3_GB_USE_MEDICINE, 4, 10, 10, "ORAN BERRY" } };
        bag.bag_count = 5;
        for (unsigned k = 0; k < 5; ++k) {
            C3GeneralBagItem *e = &bag.bag[k]; memset(e, 0, sizeof *e);
            e->item = rows[k].item; e->count = rows[k].count; e->use = rows[k].use; e->pocket = rows[k].pocket;
            e->param[rows[k].at] = rows[k].value; snprintf(e->name, sizeof e->name, "%s", rows[k].name);
        }
        bag.bag[1].param[4] = 0x20; /* the Ether is PP-selective */
        strcpy(bag.side_name[0], "WES");
    }
    /* B steps back one bag page at a time: move list -> party -> item list
     * (USE / CANCEL closed) -> action menu. */
    b = bag; b.party[0][0].pp[0] = 1; loop_begin(&l, &b, &pack, 1);
    idle(&l, 30, 1);
    loop_frame(&l, C3_PAD_RIGHT, 1); loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG && l.ui.bag_pocket == C3_GUI_BAG_ITEMS);
    loop_frame(&l, C3_PAD_DOWN, 1); loop_frame(&l, 0, 1);
    CHECK(l.ui.bag_cursor == 1);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG && l.ui.bag_confirm && l.ui.bag_entry == 1);
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_BAG && !l.ui.bag_confirm && l.ui.bag_cursor == 1);
    press(&l, C3_PAD_A, 1); press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG_PARTY && l.ui.cursor == 0); /* from the Pokemon choosing */
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG_MOVE && l.ui.target_cursor == 0 && l.ui.cursor == 0);
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_BAG_PARTY && l.ui.cursor == 0);
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_BAG && !l.ui.bag_confirm);
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.action_cursor == C3_GUI_ITEMS && l.ui.position == 0);
    /* The Ether on TACKLE: the move page commits it. */
    press(&l, C3_PAD_A, 1);
    loop_frame(&l, C3_PAD_DOWN, 1); loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1); press(&l, C3_PAD_A, 1); press(&l, C3_PAD_A, 1); press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.position == 1);
    CHECK(l.ui.commands[0].kind == 4 && l.ui.commands[0].slot == 1 && l.ui.commands[0].target == 0);
    /* fightTrainerIsUsedItem: B can't take the first Pokemon's item back. */
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.position == 1 && l.ui.commands[0].kind == 4);

    /* Refusals stay on their page with the reason: an item with no battle
     * use offers only CANCEL; the Poke Doll can't be used here; a Hyper
     * Potion at full HP has no effect; one on a Pokemon in Hyper Mode is
     * refused. */
    b = bag; loop_begin(&l, &b, &pack, 1);
    idle(&l, 30, 1);
    loop_frame(&l, C3_PAD_RIGHT, 1); loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1);
    loop_frame(&l, C3_PAD_UP, 1); loop_frame(&l, 0, 1);
    CHECK(l.ui.bag_cursor == 3);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.bag_confirm && l.ui.bag_entry == 3);
    press(&l, C3_PAD_A, 1); /* CANCEL, the only entry */
    CHECK(l.ui.page == C3_GUI_BAG && !l.ui.bag_confirm && !l.ui.error[0]);
    loop_frame(&l, C3_PAD_UP, 1); loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1); press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG && !l.ui.bag_confirm && l.ui.error[0] && l.ui.bag_entry == 2);
    loop_frame(&l, C3_PAD_UP, 1); loop_frame(&l, 0, 1); loop_frame(&l, C3_PAD_UP, 1); loop_frame(&l, 0, 1);
    CHECK(l.ui.bag_cursor == 0);
    press(&l, C3_PAD_A, 1); press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG_PARTY && !l.ui.error[0]);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG_PARTY && l.ui.error[0] && l.ui.commands[0].kind == 0);
    b.party[0][1].hyper = 1; b.party[0][1].hp = 1;
    loop_frame(&l, C3_PAD_DOWN, 1); loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG_PARTY && l.ui.error[0] && l.ui.commands[0].kind == 0);
    /* The Berries pocket (L from ITEMS wraps to BERRIES) lists the berry. */
    press(&l, C3_PAD_B, 1);
    press(&l, C3_PAD_L, 1);
    CHECK(l.ui.page == C3_GUI_BAG && l.ui.bag_pocket == C3_GUI_BAG_BERRIES && l.ui.bag_cursor == 0);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.bag_confirm && l.ui.bag_entry == 4);

    /* A Hyper Potion on the hurt second Pokemon from the first one's
     * command; the second fights; the item acts first and one is left. */
    b = bag; b.party[0][1].hp = 5; loop_begin(&l, &b, &pack, 1);
    idle(&l, 30, 1);
    loop_frame(&l, C3_PAD_RIGHT, 1); loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1); press(&l, C3_PAD_A, 1); press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_BAG_PARTY);
    loop_frame(&l, C3_PAD_DOWN, 1); loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_ACTION && l.ui.position == 1 && l.ui.commands[0].kind == 4 && l.ui.commands[0].target == 1);
    press(&l, C3_PAD_A, 1); press(&l, C3_PAD_A, 1);
    {
        uint32_t turn = b.turn;
        CHECK(press_result(&l, C3_PAD_A) == 1);
        CHECK(b.turn == turn + 1 && b.bag[0].count == 1 && b.bag[0].used == 1);
        CHECK(!strcmp(b.events[0].text, "WES used a HYPER POTION!") && b.events[1].kind == C3_GB_EVENT_HP && b.events[1].hp[1] > 5);
    }

    /* The old shortcuts are gone: X, Y and L do nothing on either page. */
    b = start; b.snag = 1; b.balls[1] = 1; loop_begin(&l, &b, &pack, 1);
    idle(&l, 30, 1);
    press(&l, C3_PAD_X, 1); press(&l, C3_PAD_Y, 1); press(&l, C3_PAD_L, 1);
    CHECK(nothing_chosen(&l));
    press(&l, C3_PAD_A, 1);
    press(&l, C3_PAD_X, 1); press(&l, C3_PAD_Y, 1); press(&l, C3_PAD_L, 1);
    CHECK(l.ui.page == C3_GUI_MOVES && l.ui.position == 0 && l.ui.commands[0].kind == 0 && l.ui.commands[0].target == 0);

    {   /* A faint with two in reserve: once the log ends the forced party
         * menu opens on the first Pokemon that can go (2); B does nothing,
         * a Pokemon already out is refused, and A sends the choice out. */
        uint8_t four[6][0x138]; make_party(four, 1, 50, 4, 0);
        CHECK(c3_general_battle_begin(&b, &pack, 0, four, 7, error));
        b.choose_replace = 1; b.info.critical = 0;
        for (unsigned i = 0; i < b.party_count[1]; ++i) b.party[1][i].hp = b.party[1][i].max_hp = 999;
        C3GeneralMon *m = &b.party[0][b.active[0][0]]; m->hp = 1; m->status = C3_GB_POISON;
        C3GeneralCommand c[2] = {{0, 0, 2}, {0, 0, 2}};
        CHECK(c3_general_battle_turn(&b, &pack, c, error) && b.replace_pending == 1u && b.outcome == C3_GB_RUNNING);
        loop_begin(&l, &b, &pack, 30);
        press(&l, C3_PAD_A, 1); /* a press while the log plays chooses nothing */
        idle(&l, 30, 1);
        CHECK(l.ui.page == C3_GUI_REPLACE && l.ui.position == 0 && l.ui.cursor == 2 && b.replace_pending == 1u);
        press(&l, C3_PAD_B, 1);
        CHECK(l.ui.page == C3_GUI_REPLACE && l.ui.cursor == 2);
        press(&l, C3_PAD_UP, 1);
        CHECK(l.ui.cursor == 1);
        press(&l, C3_PAD_A, 1);
        CHECK(l.ui.page == C3_GUI_REPLACE && l.ui.error[0] && b.replace_pending == 1u);
        press(&l, C3_PAD_DOWN, 1); press(&l, C3_PAD_DOWN, 1);
        CHECK(l.ui.cursor == 3);
        CHECK(press_result(&l, C3_PAD_A) == 1);
        CHECK(b.replace_pending == 0 && b.active[0][0] == 3 && l.ui.page == C3_GUI_EVENTS && b.events[0].kind == C3_GB_EVENT_SWITCH);
        idle(&l, 200, 1);
        CHECK(l.ui.page == C3_GUI_ACTION && l.ui.position == 0);
    }

    c3_battle_pack_free(&pack); free(bytes);
    printf("battle command input: action menu, bag, replacement, one press per UI layer\n");
    return 0;
}
