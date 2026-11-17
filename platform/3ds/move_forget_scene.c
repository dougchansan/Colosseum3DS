#include "move_forget_scene.h"
#include "bottom_screen.h"
#include "n3ds_platform.h"
#include "sfx_player.h"
#include "colosseum3ds/game_state.h"
#include "colosseum3ds/move_forget.h"
#include "colosseum3ds/pda.h"
#include <3ds.h>
#include <stdlib.h>
#include <string.h>

#define PDA_PATH "sdmc:/3ds/Colosseum3DS/boot/pda.c3pd"

static FILE *trace;
static int script[16]; static unsigned script_count, script_next, script_hold_ms;

void c3_move_forget_scene_set_script(const int *answers, unsigned count, unsigned hold_ms)
{
    script_count = answers && count <= 16 ? count : 0; script_next = 0; script_hold_ms = hold_ms;
    if (script_count) memcpy(script, answers, count * sizeof *answers);
}
void c3_move_forget_scene_bind(FILE *t)
{
    trace = t;
    c3_general_set_forget_prompt(c3_move_forget_scene_prompt, NULL);
}

static uint8_t *load_pda(C3PdaAssets *a)
{
    FILE *f = fopen(PDA_PATH, "rb"); long n = 0; uint8_t *d = NULL;
    if (f && !fseek(f, 0, SEEK_END) && (n = ftell(f)) >= 64 && n <= 8 * 1024 * 1024 && !fseek(f, 0, SEEK_SET) &&
        (d = malloc((size_t)n)) && (fread(d, 1, (size_t)n, f) != (size_t)n || !c3_pda_assets_open(a, d, (size_t)n))) {
        free(d); d = NULL;
    }
    if (f) fclose(f);
    return d;
}
static const uint8_t *pda_lookup(void *context, uint32_t id, size_t *size) { return c3_pda_entry(context, 1, id, size); }

static const char *step_name(C3MoveForgetStep s)
{
    static const char *const n[] = {"ask_forget", "choose", "ask_give_up", "forgot", "learned", "did_not_learn", "done"};
    return s <= C3_MF_DONE ? n[s] : "?";
}

int c3_move_forget_scene_prompt(void *context, const C3BattlePack *pack,
                                const uint8_t record[C3_GB_POKEMON_SIZE], uint16_t move)
{
    (void)context;
    C3PdaAssets assets; memset(&assets, 0, sizeof assets);
    uint8_t *pda = load_pda(&assets);
    char nickname[24] = "", names[5][C3_GB_NAME], text[512];
    C3MoveForget f; c3_move_forget_begin(&f, pack, record, move);
    c3_game_text_ascii(record + 0x2e, 11, nickname, sizeof nickname);
    for (unsigned k = 0; k < 4; ++k) if (!f.known[k] || !c3_battle_pack_move_name(pack, f.known[k], names[k])) names[k][0] = 0;
    if (!c3_battle_pack_move_name(pack, move, names[4])) snprintf(names[4], sizeof names[4], "?");
    if (trace) { fprintf(trace, "move_forget_open pokemon=%s move=%s known=%s,%s,%s,%s pda=%d\n", nickname, names[4],
                         names[0], names[1], names[2], names[3], pda != NULL); fflush(trace); }
    int yes = 1, cursor = 0, dirty = 1, sounded = 0, captured = 0;
    static unsigned page_number;
    C3MoveForgetStep shown = (C3MoveForgetStep)-1;
    uint64_t page_start = c3_n3ds_time_us();
    C3N3dsInput input; memset(&input, 0, sizeof input);
    while (f.step != C3_MF_DONE && c3_n3ds_poll(&input)) {
        if (input.exit_requested) break;
        uint16_t m; uint32_t id = c3_move_forget_message(&f, &m);
        if (f.step != shown) {
            shown = f.step; dirty = 1; page_start = c3_n3ds_time_us(); yes = 1; cursor = 0; sounded = 0; captured = 0;
            text[0] = 0;
            if (id) {
                char mv[C3_GB_NAME] = "?"; int sound = 0;
                c3_battle_pack_move_name(pack, m, mv);
                if (!pda || !c3_move_forget_text(pda_lookup, &assets, id, nickname, mv, text, sizeof text, &sound)) {
                    /* the original message was not available: say what the step asks */
                    static const char *const fallback[] = {"%s wants to learn %s. Forget a move?", "", "Give up learning %s?",
                        "%s forgot %s.", "%s learned %s!", "%s did not learn %s."};
                    if (f.step == C3_MF_ASK_GIVE_UP) snprintf(text, sizeof text, fallback[2], mv);
                    else snprintf(text, sizeof text, fallback[f.step], nickname, mv);
                }
                if (sound && c3_sfx_has(C3_MF_SOUND_FORGOT)) { c3_sfx_play(C3_MF_SOUND_FORGOT, 127); sounded = 1; }
            }
            if (trace) { fprintf(trace, "move_forget_step step=%s message=%04lx sound=%d text=\"", step_name(f.step), (unsigned long)id, sounded);
                for (const char *c = text; *c; ++c) fputc(*c == '\n' || *c == '"' ? ' ' : *c, trace);
                fputs("\"\n", trace); fflush(trace); }
        }
        uint16_t pressed = input.pressed;
        uint64_t held_ms = (c3_n3ds_time_us() - page_start) / 1000u;
        if (script_count && !captured && held_ms >= script_hold_ms / 2u) {
            char shot[96]; captured = 1;
            snprintf(shot, sizeof shot, "sdmc:/3ds/Colosseum3DS/forget_%u_%s_bottom.bmp", ++page_number, step_name(f.step));
            int okb = c3_n3ds_capture_bottom(shot) == 0;
            if (trace) { fprintf(trace, "move_forget_shot page=%u step=%s bottom=%d\n", page_number, step_name(f.step), okb); fflush(trace); }
        }
        int answer = -1;
        if (script_count) {
            if (held_ms >= script_hold_ms) {
                if (f.step == C3_MF_ASK_FORGET || f.step == C3_MF_ASK_GIVE_UP || f.step == C3_MF_CHOOSE)
                    answer = script_next < script_count ? script[script_next++] : (f.step == C3_MF_CHOOSE ? 4 : 1);
                else answer = 0;
            }
        } else if (f.step == C3_MF_ASK_FORGET || f.step == C3_MF_ASK_GIVE_UP) {
            if (pressed & (C3_PAD_UP | C3_PAD_DOWN | C3_PAD_LEFT | C3_PAD_RIGHT)) { yes = !yes; dirty = 1; }
            if (pressed & C3_PAD_A) answer = yes;
            else if (pressed & C3_PAD_B) answer = 0;
        } else if (f.step == C3_MF_CHOOSE) {
            if (pressed & C3_PAD_UP) { cursor = (cursor + 4) % 5; dirty = 1; }
            if (pressed & C3_PAD_DOWN) { cursor = (cursor + 1) % 5; dirty = 1; }
            if (pressed & C3_PAD_A) answer = cursor;
            else if (pressed & C3_PAD_B) answer = 4;
        } else if (pressed & (C3_PAD_A | C3_PAD_B)) answer = 0;
        if (answer >= 0) {
            if (f.step == C3_MF_CHOOSE) cursor = answer;
            if (f.step == C3_MF_ASK_FORGET || f.step == C3_MF_ASK_GIVE_UP) yes = answer == 1;
            C3MoveForgetStep before = f.step;
            c3_move_forget_answer(&f, answer);
            if (trace) { fprintf(trace, "move_forget_answer step=%s answer=%d hm_refused=%d\n", step_name(before), answer, f.hm_refused); fflush(trace); }
            if (f.step == before) { dirty = 1; captured = 0; page_start = c3_n3ds_time_us(); }
        }
        if (dirty) {
            C3BottomUi *ui = c3_bottom_screen_begin();
            if (ui) {
                if (f.step == C3_MF_CHOOSE) {
                    char sub[64]; snprintf(sub, sizeof sub, "Forget a move to learn %s", names[4]);
                    c3_ui_header(ui, "MOVES", sub);
                    for (unsigned k = 0; k < 4; ++k) c3_ui_row(ui, 64 + (int)k * 30, names[k][0] ? names[k] : "-", cursor == (int)k, !f.known[k] || f.hm[k]);
                    c3_ui_row(ui, 64 + 4 * 30, "CANCEL", cursor == 4, 0);
                    c3_ui_footer(ui, f.hm_refused ? "An HM move can't be forgotten." : "A: forget  B: cancel");
                } else {
                    c3_ui_header(ui, "MOVES", nickname);
                    c3_ui_panel(ui, 12, 60, 296, 120);
                    c3_ui_text(ui, 22, 70, 276, 7, text, C3_UI_WHITE);
                    if (f.step == C3_MF_ASK_FORGET || f.step == C3_MF_ASK_GIVE_UP) c3_ui_choices(ui, yes);
                    else c3_ui_footer(ui, "A: next");
                }
                c3_bottom_screen_present();
            }
            dirty = 0;
        }
        gspWaitForVBlank();
    }
    if (trace) { fprintf(trace, "move_forget_done slot=%d forgot=%s learned=%s\n", f.slot,
                         f.slot >= 0 ? names[f.slot] : "-", f.slot >= 0 ? names[4] : "-"); fflush(trace); }
    free(pda);
    return f.step == C3_MF_DONE ? f.slot : -1;
}
