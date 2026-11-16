#include "battle_general_scene.h"
#include "bottom_screen.h"
#include "n3ds_platform.h"
#include "music_player.h"
#include <stdlib.h>
#include <string.h>

#define EVENT_MS 900u

void c3_battle_music_begin(C3BattleMusic *m, const C3BattleEncounterInfo *info, FILE *trace)
{
    m->field[0] = c3_music_current(C3_MUSIC_SLOT_406);
    m->field[1] = c3_music_current(C3_MUSIC_SLOT_407);
    int bgm = info->bgm && c3_music_play(C3_MUSIC_SLOT_406, info->bgm, 0xFF, 0);
    if (info->environment_sound) c3_music_play(C3_MUSIC_SLOT_407, info->environment_sound, 0xFF, 1000);
    else c3_music_stop_slot(C3_MUSIC_SLOT_407, 1000);
    if (trace) { fprintf(trace, "battle_music bgm=%lx playing=%d environment=%lx field=%lx,%lx\n", (unsigned long)info->bgm, bgm,
        (unsigned long)info->environment_sound, (unsigned long)m->field[0], (unsigned long)m->field[1]); fflush(trace); }
}

void c3_battle_music_end(const C3BattleMusic *m)
{
    if (m->field[0]) c3_music_play(C3_MUSIC_SLOT_406, m->field[0], 0xFF, 1000); else c3_music_stop_slot(C3_MUSIC_SLOT_406, 1000);
    if (m->field[1]) c3_music_play(C3_MUSIC_SLOT_407, m->field[1], 0xFF, 1000); else c3_music_stop_slot(C3_MUSIC_SLOT_407, 1000);
}

static unsigned living_foes(const C3GeneralBattle *b, unsigned out[2])
{
    unsigned n = 0;
    for (unsigned i = 0; i < b->active_count; ++i) {
        uint8_t m = b->active[1][i];
        if (m != 255 && b->party[1][m].hp) out[n++] = 2 + i;
    }
    return n;
}
static const C3GeneralMon *hero(const C3GeneralBattle *b, unsigned pos)
{
    uint8_t m = pos < b->active_count ? b->active[0][pos] : 255;
    return m != 255 ? &b->party[0][m] : NULL;
}
/* First hero position at or after `from` that still needs a command. */
static int next_position(const C3GeneralBattle *b, unsigned from)
{
    for (unsigned p = from; p < b->active_count; ++p) { const C3GeneralMon *m = hero(b, p); if (m && m->hp) return (int)p; }
    return -1;
}

void c3_general_ui_draw(const C3BattlePack *pack, const C3GeneralBattle *b, const C3GeneralUi *ui, const C3GeneralEvent *e)
{
    C3BottomUi *s = c3_bottom_screen_begin();
    char text[96];
    if (ui->page != C3_GUI_EVENTS) e = NULL;
    for (unsigned pos = 0; pos < 4; ++pos) {
        unsigned side = pos / 2, slot = pos % 2;
        if (slot >= b->active_count) continue;
        uint8_t m = b->active[side][slot];
        unsigned hp = e ? e->hp[pos] : m != 255 ? b->party[side][m].hp : 0;
        unsigned max = e ? e->max_hp[pos] : m != 255 ? b->party[side][m].max_hp : 0;
        if (m == 255 && !max) continue;
        const C3GeneralMon *mon = m != 255 ? &b->party[side][m] : NULL;
        snprintf(text, sizeof text, "%s Lv%u", mon ? mon->name : "-", mon ? mon->level : 0);
        int width = b->active_count == 2 ? 151 : 310;
        c3_ui_hp(s, 5 + (int)slot * 159, side ? 2 : 47, width, text, hp, max ? max : 1);
    }
    c3_ui_panel(s, 5, 94, 310, 50);
    const char *message = e ? e->text : ui->error[0] ? ui->error : "";
    if (ui->page == C3_GUI_RESULT && b->event_count) message = b->events[b->event_count - 1].text;
    if (ui->page == C3_GUI_MOVES || ui->page == C3_GUI_TARGET || ui->page == C3_GUI_SWITCH) {
        const C3GeneralMon *m = hero(b, ui->position);
        snprintf(text, sizeof text, "What will %s do?", m ? m->name : "?");
        message = ui->error[0] ? ui->error : text;
    }
    c3_ui_text(s, 13, 100, 294, 2, message, C3_UI_WHITE);
    if (ui->page == C3_GUI_MOVES) {
        const C3GeneralMon *m = hero(b, ui->position);
        uint8_t legal = c3_general_battle_legal_moves(b, pack, ui->position);
        for (unsigned i = 0; i < 4 && m; ++i) {
            char name[C3_GB_NAME] = "-", pp[16] = "";
            if (m->moves[i]) { c3_battle_pack_move_name(pack, m->moves[i], name); snprintf(pp, sizeof pp, "%u/%u", m->pp[i], m->pp_max[i]); }
            c3_ui_row_value(s, 148 + (int)i * 19, name, pp, ui->cursor == i, !(legal >> i & 1u));
        }
        c3_ui_footer(s, b->party_count[0] > b->active_count ? "A: use  B: back  Y: switch" : "A: use  B: back");
    } else if (ui->page == C3_GUI_TARGET) {
        unsigned foes[2], n = living_foes(b, foes);
        for (unsigned i = 0; i < n; ++i) {
            const C3GeneralMon *m = &b->party[1][b->active[1][foes[i] - 2]];
            c3_ui_row(s, 148 + (int)i * 19, m->name, ui->target_cursor == i, 0);
        }
        c3_ui_footer(s, "A: target  B: back");
    } else if (ui->page == C3_GUI_SWITCH) {
        for (unsigned i = 0; i < b->party_count[0]; ++i) {
            const C3GeneralMon *m = &b->party[0][i];
            int active = 0;
            for (unsigned a = 0; a < b->active_count; ++a) active |= b->active[0][a] == i;
            snprintf(text, sizeof text, "%u/%u", m->hp, m->max_hp);
            c3_ui_row_value(s, 148 + (int)i * 13, m->name, text, ui->cursor == i, active || !m->hp);
        }
        c3_ui_footer(s, "A: switch in  B: back");
    } else if (ui->page == C3_GUI_RESULT) {
        c3_ui_footer(s, "A: continue");
    } else {
        c3_ui_footer(s, "A: next");
    }
    c3_bottom_screen_present();
}

void c3_general_ui_commands(C3GeneralUi *ui, const C3GeneralBattle *b)
{
    memset(ui->commands, 0, sizeof ui->commands);
    int p = next_position(b, 0);
    ui->position = p < 0 ? 0 : (unsigned)p;
    ui->cursor = 0; ui->page = C3_GUI_MOVES;
}

int c3_general_ui_press(C3GeneralUi *ui, const C3BattlePack *pack, C3GeneralBattle *b, uint16_t pressed, FILE *trace)
{
    if (!pressed || ui->page == C3_GUI_EVENTS || ui->page == C3_GUI_RESULT) return 0;
    ui->error[0] = 0;
    int submit = 0;
    if (ui->page == C3_GUI_MOVES) {
        uint8_t legal = c3_general_battle_legal_moves(b, pack, ui->position);
        if (pressed & C3_PAD_UP) ui->cursor = (ui->cursor + 3) % 4;
        if (pressed & C3_PAD_DOWN) ui->cursor = (ui->cursor + 1) % 4;
        if ((pressed & C3_PAD_Y) && b->party_count[0] > b->active_count) { ui->page = C3_GUI_SWITCH; ui->cursor = 0; }
        else if (pressed & C3_PAD_B) {
            int back = -1;
            for (int p = (int)ui->position - 1; p >= 0; --p) { const C3GeneralMon *m = hero(b, (unsigned)p); if (m && m->hp) { back = p; break; } }
            if (back >= 0) { ui->position = (unsigned)back; ui->cursor = 0; }
        } else if (pressed & C3_PAD_A) {
            C3GeneralCommand *c = &ui->commands[ui->position];
            unsigned foes[2], n = living_foes(b, foes);
            c->kind = 0; c->target = n ? (uint8_t)foes[0] : 2;
            if (!legal) { c->slot = 4; submit = 1; }
            else if (!(legal >> ui->cursor & 1u)) snprintf(ui->error, sizeof ui->error, "That move cannot be used.");
            else {
                C3GeneralMoveInfo move;
                c->slot = (uint8_t)ui->cursor;
                const C3GeneralMon *m = hero(b, ui->position);
                int single = c3_battle_pack_move(pack, m->moves[ui->cursor], &move) && move.range <= 3;
                if (single && n > 1) { ui->page = C3_GUI_TARGET; ui->target_cursor = 0; }
                else submit = 1;
            }
        }
    } else if (ui->page == C3_GUI_TARGET) {
        unsigned foes[2], n = living_foes(b, foes);
        if (pressed & (C3_PAD_UP | C3_PAD_DOWN | C3_PAD_LEFT | C3_PAD_RIGHT)) ui->target_cursor = n ? (ui->target_cursor + 1) % n : 0;
        if (pressed & C3_PAD_B) ui->page = C3_GUI_MOVES;
        else if ((pressed & C3_PAD_A) && n) { ui->commands[ui->position].target = (uint8_t)foes[ui->target_cursor % n]; submit = 1; }
    } else if (ui->page == C3_GUI_SWITCH) {
        if (pressed & C3_PAD_UP) ui->cursor = (ui->cursor + b->party_count[0] - 1) % b->party_count[0];
        if (pressed & C3_PAD_DOWN) ui->cursor = (ui->cursor + 1) % b->party_count[0];
        if (pressed & C3_PAD_B) { ui->page = C3_GUI_MOVES; ui->cursor = 0; }
        else if (pressed & C3_PAD_A) {
            int active = 0;
            for (unsigned a = 0; a < b->active_count; ++a) active |= b->active[0][a] == ui->cursor;
            for (unsigned p = 0; p < ui->position; ++p) active |= ui->commands[p].kind == 1 && ui->commands[p].slot == ui->cursor;
            if (active || !b->party[0][ui->cursor].hp) snprintf(ui->error, sizeof ui->error, "That Pokemon cannot battle now.");
            else { ui->commands[ui->position].kind = 1; ui->commands[ui->position].slot = (uint8_t)ui->cursor; submit = 1; }
        }
    }
    if (!submit) return 0;
    int p = next_position(b, ui->position + 1);
    if (p >= 0) { ui->position = (unsigned)p; ui->cursor = 0; ui->page = C3_GUI_MOVES; return 0; }
    char error[C3_GB_ERROR_SIZE];
    if (!c3_general_battle_turn(b, pack, ui->commands, error)) {
        if (trace) { fprintf(trace, "general_battle_error turn=%lu error=%s\n", (unsigned long)b->turn, error); fflush(trace); }
        c3_general_ui_commands(ui, b);
        snprintf(ui->error, sizeof ui->error, "%s", error);
        return 0;
    }
    if (trace) { fprintf(trace, "general_battle_turn turn=%lu events=%lu outcome=%d draws=%llu\n",
        (unsigned long)b->turn, (unsigned long)b->event_count, (int)b->outcome, (unsigned long long)b->rng.draws); fflush(trace); }
    ui->page = C3_GUI_EVENTS;
    return 1;
}

int c3_battle_general_scene_run(const C3BattlePack *pack, C3GeneralBattle *b, FILE *trace)
{
    C3GeneralUi ui;
    C3N3dsInput input;
    unsigned event = 0;
    memset(&ui, 0, sizeof ui);
    ui.page = C3_GUI_EVENTS;
    uint64_t event_start = c3_n3ds_time_us();
    C3BattleMusic music; c3_battle_music_begin(&music, &b->info, trace);
    if (trace) { fprintf(trace, "general_battle_begin encounter=%lu trainer=%s/%s active=%u hero_party=%u foe_party=%u scene=2d\n",
        (unsigned long)b->encounter, b->info.title, b->info.trainer, b->active_count, b->party_count[0], b->party_count[1]); fflush(trace); }
    int dirty = 1;
    while (c3_n3ds_poll(&input)) {
        uint16_t pressed = input.pressed;
        uint64_t now = c3_n3ds_time_us();
        if (ui.page == C3_GUI_EVENTS) {
            if ((pressed & C3_PAD_A) || now - event_start >= EVENT_MS * 1000u) {
                ++event; event_start = now; dirty = 1;
                if (event >= b->event_count) {
                    if (b->outcome != C3_GB_RUNNING) ui.page = C3_GUI_RESULT;
                    else c3_general_ui_commands(&ui, b);
                }
            }
        } else if (ui.page == C3_GUI_RESULT) {
            if (pressed & C3_PAD_A) {
                if (trace) { fprintf(trace, "general_battle_end encounter=%lu outcome=%d turns=%lu\n",
                    (unsigned long)b->encounter, (int)b->outcome, (unsigned long)b->turn); fflush(trace); }
                c3_battle_music_end(&music);
                return 0;
            }
        } else if (pressed) {
            dirty = 1;
            if (c3_general_ui_press(&ui, pack, b, pressed, trace)) { event = 0; event_start = now; }
        }
        if (dirty) { c3_general_ui_draw(pack, b, &ui, event < b->event_count ? &b->events[event] : NULL); dirty = 0; }
    }
    c3_battle_music_end(&music);
    return 1;
}
