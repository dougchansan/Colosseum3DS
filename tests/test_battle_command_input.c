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
 * the last message expires opens the command page (FIGHT's move list,
 * "What will X do?") at the top of the loop, before that frame's press is
 * read and before the page is drawn. A press whose edge falls on that frame
 * was made while the message was on screen and must not choose a command;
 * the next press, and each one after it, advances exactly one level. */

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
    if (l->ui.page == C3_GUI_EVENTS && !l->message_frames) CHECK(c3_general_ui_commands(&l->ui, l->b));
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
static int nothing_chosen(const Loop *l)
{
    static const C3GeneralCommand zero[2];
    return l->ui.page == C3_GUI_MOVES && l->ui.position == 0 && l->ui.cursor == 0 && !memcmp(l->ui.commands, zero, sizeof zero);
}

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

    /* Before the gate: an A whose edge falls on the frame the command page
     * opens picks the first move and lands on target selection (two foes),
     * the hardware report. */
    b = start; loop_begin(&l, &b, &pack, 30);
    idle(&l, 30, 0);
    press(&l, C3_PAD_A, 0);
    CHECK(l.ui.page == C3_GUI_TARGET && l.ui.commands[0].slot == 0);

    /* With it, at every phase of the press against the message's end (the
     * press starting up to 12 frames before the page opens, on its opening
     * frame, or while the button is still down): the page is open at the
     * top with nothing chosen. */
    for (unsigned lead = 0; lead <= 12; ++lead) {
        b = start; loop_begin(&l, &b, &pack, 30);
        idle(&l, 30 - lead, 1);
        press(&l, C3_PAD_A, 1);
        idle(&l, 12, 1);
        CHECK(nothing_chosen(&l));
        /* Then one press per level: move list -> target list -> the
         * second Pokemon's move list -> its target list -> the turn runs. */
        idle(&l, 60, 1);
        press(&l, C3_PAD_A, 1);
        CHECK(l.ui.page == C3_GUI_TARGET && l.ui.position == 0 && l.ui.commands[0].slot == 0);
        idle(&l, 60, 1);
        press(&l, C3_PAD_A, 1);
        CHECK(l.ui.page == C3_GUI_MOVES && l.ui.position == 1 && l.ui.commands[0].target >= 2);
        idle(&l, 60, 1);
        press(&l, C3_PAD_A, 1);
        CHECK(l.ui.page == C3_GUI_TARGET && l.ui.position == 1);
        uint32_t turn = b.turn;
        idle(&l, 60, 1);
        press(&l, C3_PAD_A, 1);
        CHECK(l.ui.page == C3_GUI_EVENTS && b.turn == turn + 1);
        /* The next turn's page opens as the first did (its log started on
         * that press's first frame). */
        idle(&l, 111 - lead, 1);
        press(&l, C3_PAD_A, 1);
        idle(&l, 12, 1);
        CHECK(b.outcome != C3_GB_RUNNING || nothing_chosen(&l));
    }

    /* "Go, ESPEON!" with one foe left: the move list commits a move with
     * no target page, so a carried press would queue it outright. */
    {
        C3GeneralBattle one = start;
        one.party[1][one.active[1][1]].hp = 0;
        b = one; loop_begin(&l, &b, &pack, 20);
        idle(&l, 20, 0);
        press(&l, C3_PAD_A, 0);
        CHECK(l.ui.page == C3_GUI_MOVES && l.ui.position == 1); /* before the gate: the first Pokemon's move went in */
        b = one; loop_begin(&l, &b, &pack, 20);
        idle(&l, 20, 1);
        press(&l, C3_PAD_A, 1);
        CHECK(nothing_chosen(&l));
        idle(&l, 60, 1);
        press(&l, C3_PAD_A, 1);
        CHECK(l.ui.page == C3_GUI_MOVES && l.ui.position == 1 && l.ui.commands[0].target == 2);
    }

    /* B held from the log (the "hurry" button) does not back out of the
     * page later; it is free again once released. Direction edges after
     * the opening frame move the cursor one row each. */
    b = start; loop_begin(&l, &b, &pack, 10);
    idle(&l, 10, 1);
    for (unsigned k = 0; k < 20; ++k) loop_frame(&l, C3_PAD_B, 1);
    CHECK(nothing_chosen(&l));
    loop_frame(&l, C3_PAD_B | C3_PAD_DOWN, 1);
    CHECK(l.ui.page == C3_GUI_MOVES && l.ui.cursor == 1);
    loop_frame(&l, 0, 1);
    press(&l, C3_PAD_A, 1);
    CHECK(l.ui.page == C3_GUI_TARGET && l.ui.commands[0].slot == 1);
    press(&l, C3_PAD_B, 1);
    CHECK(l.ui.page == C3_GUI_MOVES && l.ui.position == 0);

    c3_battle_pack_free(&pack); free(bytes);
    printf("battle command input: one press per UI layer\n");
    return 0;
}
