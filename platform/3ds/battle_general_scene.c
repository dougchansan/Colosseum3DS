#include "battle_general_scene.h"
#ifdef C3_AGENT_HARNESS
#include "agent_link.h"
#endif
#include "battle_general_3d.h"
#include "bottom_screen.h"
#include "move_forget_scene.h"
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
/* Balls the hero holds (item ids), for the Snag Machine page. */
static unsigned ball_list(const C3GeneralBattle *b, unsigned out[12])
{
    unsigned n = 0;
    if (!b->snag || b->link) return 0;
    for (unsigned ball = 1; ball <= 12; ++ball) if (b->balls[ball]) out[n++] = ball;
    return n;
}
static unsigned snag_targets(const C3GeneralBattle *b, const C3BattlePack *pack, unsigned pos, unsigned ball, unsigned out[2])
{
    unsigned n = 0;
    for (unsigned t = 2; t < 2u + b->active_count; ++t) if (c3_general_battle_can_snag(b, pack, pos, ball, t)) out[n++] = t;
    return n;
}
/* First hero position at or after `from` that still needs a command. */
static int next_position(const C3GeneralUi *ui, const C3GeneralBattle *b, unsigned from)
{
    for (unsigned p = from; p < b->active_count; ++p) {
        if (ui->control_mask && !(ui->control_mask >> p & 1u)) continue;
        const C3GeneralMon *m = hero(b, p); if (m && m->hp) return (int)p;
    }
    return -1;
}
/* The controlled position before this one that is still able to act: B on
 * an action menu goes back to its command (SubMain's result 1, i != 0). */
static int previous_position(const C3GeneralUi *ui, const C3GeneralBattle *b)
{
    for (int p = (int)ui->position - 1; p >= 0; --p) {
        if (ui->control_mask && !(ui->control_mask >> p & 1u)) continue;
        const C3GeneralMon *m = hero(b, (unsigned)p); if (m && m->hp) return p;
    }
    return -1;
}
/* A Pokemon's command starts on the action menu with the cursor on FIGHT. */
static void open_action(C3GeneralUi *ui)
{
    ui->page = C3_GUI_ACTION; ui->action_cursor = C3_GUI_FIGHT; ui->cursor = 0;
}

static const uint32_t action_messages[C3_GUI_ACTIONS] = { 0x13C, 0x13D, 0x13E, 0x140 };
static char action_labels[C3_GUI_ACTIONS][24] = { "FIGHT", "ITEM", "POKEMON", "CALL" };
uint32_t c3_general_ui_action_message(unsigned entry) { return entry < C3_GUI_ACTIONS ? action_messages[entry] : 0; }
void c3_general_ui_set_action_label(unsigned entry, const char *text)
{
    if (entry < C3_GUI_ACTIONS && text && text[0]) snprintf(action_labels[entry], sizeof action_labels[entry], "%s", text);
}
const char *c3_general_ui_action_label(unsigned entry) { return entry < C3_GUI_ACTIONS ? action_labels[entry] : ""; }

/* One button of the action menu's 2x2 grid (items at GC x 87/227, y 373/401). */
static void action_cell(C3BottomUi *s, int x, int y, const char *label, int selected, int disabled)
{
    if (selected) {
        c3_ui_rect(s, x, y, 146, 24, 0x30494bu);
        c3_ui_rect(s, x, y, 2, 24, C3_UI_GOLD);
        c3_ui_sprite(s, C3_UI_HAND, x + 5, y + 4, 22, 16);
    }
    c3_ui_text(s, x + 32, y + 3, 108, 1, label, disabled ? C3_UI_MUTED : selected ? C3_UI_GOLD : C3_UI_WHITE);
}

void c3_general_ui_draw(const C3BattlePack *pack, const C3GeneralBattle *b, const C3GeneralUi *ui, const C3GeneralEvent *e)
{
    C3BottomUi *s = c3_bottom_screen_begin();
    char text[96];
    if (ui->page != C3_GUI_EVENTS) e = NULL;
    for (unsigned pos = 0; pos < 4; ++pos) {
        unsigned side = pos / 2, slot = pos % 2;
        if (slot >= b->active_count) continue;
        /* While the log plays, the Pokemon that was there at that event (a
         * fainted one keeps its name and level until the turn ends). */
        uint8_t m = e ? e->mon[pos] : b->active[side][slot];
        if (m != 255 && m >= b->party_count[side]) m = 255;
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
    if (ui->page == C3_GUI_WAIT) message = "Waiting for the other players...";
    if (ui->page == C3_GUI_ACTION || ui->page == C3_GUI_MOVES || ui->page == C3_GUI_TARGET || ui->page == C3_GUI_SWITCH || ui->page == C3_GUI_BALL) {
        const C3GeneralMon *m = hero(b, ui->position);
        snprintf(text, sizeof text, "What will %s do?", m ? m->name : "?");
        message = ui->error[0] ? ui->error : text;
    }
    c3_ui_text(s, 13, 100, 294, 2, message, C3_UI_WHITE);
    if (ui->page == C3_GUI_ACTION) {
        unsigned balls[12];
        int unavailable[C3_GUI_ACTIONS] = { 0, !ball_list(b, balls), 0, !c3_general_battle_can_call(b, ui->position) };
        c3_ui_panel(s, 5, 148, 310, 68);
        for (unsigned i = 0; i < C3_GUI_ACTIONS; ++i)
            action_cell(s, 12 + (int)(i & 1u) * 150, 155 + (int)(i >> 1) * 30, action_labels[i], ui->action_cursor == i, unavailable[i]);
        c3_ui_footer(s, previous_position(ui, b) >= 0 ? "A: choose  B: back" : "A: choose");
    } else if (ui->page == C3_GUI_MOVES) {
        const C3GeneralMon *m = hero(b, ui->position);
        uint8_t legal = c3_general_battle_legal_moves(b, pack, ui->position);
        for (unsigned i = 0; i < 4 && m; ++i) {
            char name[C3_GB_NAME] = "-", pp[16] = "";
            if (m->moves[i]) { c3_battle_pack_move_name(pack, m->moves[i], name); snprintf(pp, sizeof pp, "%u/%u", m->pp[i], m->pp_max[i]); }
            c3_ui_row_value(s, 148 + (int)i * 19, name, pp, ui->cursor == i, !(legal >> i & 1u));
        }
        c3_ui_footer(s, "A: use  B: back");
    } else if (ui->page == C3_GUI_BALL) {
        unsigned balls[12], n = ball_list(b, balls);
        for (unsigned i = 0; i < n && i < 4; ++i) {
            unsigned first = ui->cursor >= 4 ? ui->cursor - 3 : 0, k = first + i;
            if (k >= n) break;
            snprintf(text, sizeof text, "x%u", b->balls[balls[k]]);
            c3_ui_row_value(s, 148 + (int)i * 19, c3_general_ball_name(balls[k]), text, ui->cursor == k, 0);
        }
        c3_ui_footer(s, "A: throw  B: back");
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
    } else if (!ui->timed_log || (e && e->hold)) {
        c3_ui_footer(s, "A: next"); /* the 3D log's timed messages take no press */
    }
    c3_bottom_screen_present();
}

int c3_general_ui_commands(C3GeneralUi *ui, const C3GeneralBattle *b)
{
    memset(ui->commands, 0, sizeof ui->commands);
    int p = next_position(ui, b, 0);
    ui->position = p < 0 ? 0 : (unsigned)p;
    open_action(ui);
    c3_press_gate_close(&ui->gate); /* the press that ended the log is not a command */
    return p >= 0;
}

uint16_t c3_general_ui_input(C3GeneralUi *ui, uint16_t held, uint16_t pressed)
{
    return c3_press_gate_take(&ui->gate, held, pressed);
}

int c3_general_ui_press(C3GeneralUi *ui, const C3BattlePack *pack, C3GeneralBattle *b, uint16_t pressed, FILE *trace)
{
    if (!pressed || ui->page == C3_GUI_EVENTS || ui->page == C3_GUI_RESULT) return 0;
    c3_press_gate_consume(&ui->gate, pressed); /* this press acts on this page only */
    ui->error[0] = 0;
    int submit = 0;
    if (ui->page == C3_GUI_ACTION) {
        /* Menu 0x4B has no button shortcuts: the d-pad moves in the grid,
         * A takes the entry, B goes back to the previous Pokemon. */
        if (pressed & (C3_PAD_LEFT | C3_PAD_RIGHT)) ui->action_cursor ^= 1u;
        if (pressed & (C3_PAD_UP | C3_PAD_DOWN)) ui->action_cursor ^= 2u;
        ui->action_cursor %= C3_GUI_ACTIONS;
        if (pressed & C3_PAD_B) {
            int back = previous_position(ui, b);
            if (back >= 0) { ui->position = (unsigned)back; open_action(ui); }
        } else if (pressed & C3_PAD_A) {
            C3GeneralCommand *c = &ui->commands[ui->position];
            unsigned balls[12], foes[2], n = living_foes(b, foes);
            if (ui->action_cursor == C3_GUI_FIGHT) {
                /* No move with PP left: FIGHT commits Struggle at once. */
                if (!c3_general_battle_legal_moves(b, pack, ui->position)) {
                    c->kind = 0; c->slot = 4; c->target = n ? (uint8_t)foes[0] : 2; submit = 1;
                } else { ui->page = C3_GUI_MOVES; ui->cursor = 0; }
            } else if (ui->action_cursor == C3_GUI_ITEMS) {
                /* The bag's usable pocket here is the Snag Machine's balls. */
                if (ball_list(b, balls)) { ui->page = C3_GUI_BALL; ui->cursor = 0; }
                else snprintf(ui->error, sizeof ui->error, "There is no item to use now.");
            } else if (ui->action_cursor == C3_GUI_PKMN) {
                ui->page = C3_GUI_SWITCH; ui->cursor = 0;
            } else if (c3_general_battle_can_call(b, ui->position)) {
                c->kind = 3; c->slot = 0; c->target = (uint8_t)ui->position; submit = 1;
            } /* Call unavailable: the menu stays (SubMain's result 2). */
        }
    } else if (ui->page == C3_GUI_MOVES) {
        uint8_t legal = c3_general_battle_legal_moves(b, pack, ui->position);
        if (pressed & C3_PAD_UP) ui->cursor = (ui->cursor + 3) % 4;
        if (pressed & C3_PAD_DOWN) ui->cursor = (ui->cursor + 1) % 4;
        if (pressed & C3_PAD_B) { ui->page = C3_GUI_ACTION; ui->cursor = 0; }
        else if (pressed & C3_PAD_A) {
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
    } else if (ui->page == C3_GUI_BALL) {
        unsigned balls[12], n = ball_list(b, balls);
        if (!n) { ui->page = C3_GUI_ACTION; ui->cursor = 0; }
        else if (pressed & C3_PAD_UP) ui->cursor = (ui->cursor + n - 1) % n;
        else if (pressed & C3_PAD_DOWN) ui->cursor = (ui->cursor + 1) % n;
        else if (pressed & C3_PAD_B) { ui->page = C3_GUI_ACTION; ui->cursor = 0; }
        else if (pressed & C3_PAD_A) {
            /* The Snag Machine: a ball only at a Shadow Pokemon. */
            unsigned ball = balls[ui->cursor % n], targets[2], t = snag_targets(b, pack, ui->position, ball, targets);
            C3GeneralCommand *c = &ui->commands[ui->position];
            if (!t) snprintf(ui->error, sizeof ui->error, "There is no Shadow Pokemon to snag.");
            else { c->kind = 2; c->slot = (uint8_t)ball; c->target = (uint8_t)targets[0];
                   if (t > 1) { ui->page = C3_GUI_TARGET; ui->target_cursor = 0; } else submit = 1; }
        }
    } else if (ui->page == C3_GUI_TARGET) {
        unsigned foes[2], n = living_foes(b, foes);
        C3GeneralCommand *c = &ui->commands[ui->position];
        if (pressed & (C3_PAD_UP | C3_PAD_DOWN | C3_PAD_LEFT | C3_PAD_RIGHT)) ui->target_cursor = n ? (ui->target_cursor + 1) % n : 0;
        if (pressed & C3_PAD_B) { ui->page = c->kind == 2 ? C3_GUI_BALL : C3_GUI_MOVES; ui->cursor = 0; }
        else if ((pressed & C3_PAD_A) && n) {
            unsigned target = foes[ui->target_cursor % n];
            if (c->kind == 2 && !c3_general_battle_can_snag(b, pack, ui->position, c->slot, target))
                snprintf(ui->error, sizeof ui->error, "Only a Shadow Pokemon can be snagged.");
            else { c->target = (uint8_t)target; submit = 1; }
        }
    } else if (ui->page == C3_GUI_SWITCH) {
        if (pressed & C3_PAD_UP) ui->cursor = (ui->cursor + b->party_count[0] - 1) % b->party_count[0];
        if (pressed & C3_PAD_DOWN) ui->cursor = (ui->cursor + 1) % b->party_count[0];
        if (pressed & C3_PAD_B) { ui->page = C3_GUI_ACTION; ui->cursor = 0; }
        else if (pressed & C3_PAD_A) {
            int active = 0;
            for (unsigned a = 0; a < b->active_count; ++a) active |= b->active[0][a] == ui->cursor;
            for (unsigned p = 0; p < ui->position; ++p) active |= ui->commands[p].kind == 1 && ui->commands[p].slot == ui->cursor;
            if (b->owner_split && b->owner[0][ui->cursor] != ui->position) snprintf(ui->error, sizeof ui->error, "That Pokemon belongs to your partner.");
            else if (active || !b->party[0][ui->cursor].hp) snprintf(ui->error, sizeof ui->error, "That Pokemon cannot battle now.");
            else { ui->commands[ui->position].kind = 1; ui->commands[ui->position].slot = (uint8_t)ui->cursor; submit = 1; }
        }
    }
    if (!submit) return 0;
    int p = next_position(ui, b, ui->position + 1);
    if (p >= 0) { ui->position = (unsigned)p; open_action(ui); return 0; }
    if (ui->defer) { ui->page = C3_GUI_WAIT; return 2; }
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
    c3_move_forget_scene_bind(trace);
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
#ifdef C3_AGENT_HARNESS
        c3_agent_link_battle(b, (uint8_t)ui.page);
#endif
        uint16_t pressed = input.pressed, page_pressed = c3_general_ui_input(&ui, input.held, input.pressed);
        uint64_t now = c3_n3ds_time_us();
        if (ui.page == C3_GUI_EVENTS) {
            /* The partner's page (the Shadow notice) waits for A. */
            int hold = event < b->event_count && b->events[event].hold && !c3_battle_general_autoplaying();
            if ((pressed & C3_PAD_A) || (!hold && now - event_start >= EVENT_MS * 1000u)) {
                ++event; event_start = now; dirty = 1;
                if (event >= b->event_count) {
                    if (b->outcome != C3_GB_RUNNING) { ui.page = C3_GUI_RESULT; c3_press_gate_close(&ui.gate); }
                    else {
                        c3_general_ui_commands(&ui, b);
                        if (trace) { fprintf(trace, "general_battle_commands turn=%lu after_events=%lu last_kind=%u last_special=0x%x scene=2d\n", (unsigned long)b->turn,
                            (unsigned long)b->event_count, b->event_count ? (unsigned)b->events[b->event_count - 1].kind : 0u,
                    b->event_count ? (unsigned)b->events[b->event_count - 1].special : 0u); fflush(trace); }
                    }
                }
            }
        } else if (ui.page == C3_GUI_RESULT) {
            if ((page_pressed & C3_PAD_A) || c3_battle_general_autoplaying()) {
                if (trace) { fprintf(trace, "general_battle_end encounter=%lu outcome=%d turns=%lu\n",
                    (unsigned long)b->encounter, (int)b->outcome, (unsigned long)b->turn); fflush(trace); }
                c3_battle_music_end(&music);
                return 0;
            }
        } else if (c3_battle_general_autoplaying() || b->info.cpu_side0) {
            /* Probe builds: the probe hero's commands, no input. */
            C3GeneralCommand c[2]; char error[C3_GB_ERROR_SIZE];
            c3_battle_general_probe_commands(b, pack, c);
            if (!c3_general_battle_turn(b, pack, c, error)) {
                if (trace) { fprintf(trace, "general_battle_error turn=%lu error=%s\n", (unsigned long)b->turn, error); fflush(trace); }
                c3_battle_music_end(&music); return -1;
            }
            if (trace) { fprintf(trace, "general_battle_turn turn=%lu events=%lu outcome=%d autoplay=1 scene=2d\n",
                (unsigned long)b->turn, (unsigned long)b->event_count, (int)b->outcome); fflush(trace); }
            ui.page = C3_GUI_EVENTS; event = 0; event_start = now; dirty = 1;
        } else if (page_pressed) {
            dirty = 1;
            if (c3_general_ui_press(&ui, pack, b, page_pressed, trace)) { event = 0; event_start = now; }
        }
        if (dirty) { c3_general_ui_draw(pack, b, &ui, event < b->event_count ? &b->events[event] : NULL); dirty = 0; }
    }
    c3_battle_music_end(&music);
    return 1;
}
