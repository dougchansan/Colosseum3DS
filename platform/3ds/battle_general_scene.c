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

static const uint32_t bag_messages[C3_GUI_BAG_LABELS] = { 0x2AF9, 0x2AFA, 0x2AFC, 0x131, 0x134 };
static char bag_labels[C3_GUI_BAG_LABELS][24] = { "ITEMS", "POKE BALLS", "BERRIES", "USE", "CANCEL" };
uint32_t c3_general_ui_bag_message(unsigned entry) { return entry < C3_GUI_BAG_LABELS ? bag_messages[entry] : 0; }
void c3_general_ui_set_bag_label(unsigned entry, const char *text)
{
    if (entry < C3_GUI_BAG_LABELS && text && text[0]) snprintf(bag_labels[entry], sizeof bag_labels[entry], "%s", text);
}
const char *c3_general_ui_bag_label(unsigned entry) { return entry < C3_GUI_BAG_LABELS ? bag_labels[entry] : ""; }
/* The open pocket's entries: bag entries (Items, Berries) or ball ids. */
static unsigned pocket_list(const C3GeneralBattle *b, unsigned pocket, unsigned out[C3_GB_BAG])
{
    unsigned n = 0;
    if (pocket == C3_GUI_BAG_BALLS) {
        for (unsigned ball = 1; ball <= 12; ++ball) if (b->balls[ball]) out[n++] = ball;
        return n;
    }
    for (unsigned k = 0; k < b->bag_count && k < C3_GB_BAG; ++k)
        if (b->bag[k].pocket == (pocket == C3_GUI_BAG_ITEMS ? 0u : 4u) && b->bag[k].count) out[n++] = k;
    return n;
}
/* Uses of a bag entry (entry < 0: any; or balls) already chosen by an
 * earlier Pokemon this turn. */
static unsigned chosen_before(const C3GeneralUi *ui, unsigned kind, int entry)
{
    unsigned n = 0;
    for (unsigned p = 0; p < ui->position && p < 2; ++p)
        n += ui->commands[p].kind == kind && (entry < 0 || ui->commands[p].slot == (unsigned)entry);
    return n;
}
/* The Items pocket offers USE only for an item with a battleUseFunc
 * (summary menu flag 2); balls and berries always do. */
static int bag_offers_use(const C3GeneralBattle *b, const C3GeneralUi *ui)
{
    return ui->bag_pocket != C3_GUI_BAG_ITEMS || (ui->bag_entry < b->bag_count && b->bag[ui->bag_entry].use != C3_GB_USE_NONE);
}
static void item_refusal(C3GeneralUi *ui, C3GeneralItemCheck r)
{
    static const char *const text[] = {
        "", "Items can't be used now.", "That can't be used now.", "A Pokemon in Hyper Mode won't take it.",
        "It won't have any effect.", "There are none left.", "Choose a move.", "That item can't be used." };
    snprintf(ui->error, sizeof ui->error, "%s", text[r <= C3_GB_ITEM_INVALID ? r : C3_GB_ITEM_INVALID]);
}
static void open_bag(C3GeneralUi *ui)
{
    ui->page = C3_GUI_BAG; ui->bag_cursor = 0; ui->bag_confirm = 0; ui->bag_choice = 0;
    if (ui->bag_pocket > C3_GUI_BAG_BERRIES) ui->bag_pocket = C3_GUI_BAG_ITEMS;
}
/* USE on the chosen item. Returns 1 when it is the Pokemon's command. */
static int bag_use(C3GeneralUi *ui, const C3BattlePack *pack, const C3GeneralBattle *b)
{
    C3GeneralCommand *c = &ui->commands[ui->position];
    ui->bag_confirm = 0;
    if (ui->bag_pocket == C3_GUI_BAG_BALLS) {
        /* fn_80022050: one ball a turn (message 0x426D); then the Snag
         * Machine's target, a Shadow Pokemon only (0x7716 / 0x7702). */
        unsigned ball = ui->bag_entry, targets[2], t = snag_targets(b, pack, ui->position, ball, targets);
        if (chosen_before(ui, 2, -1)) { snprintf(ui->error, sizeof ui->error, "Only one ball can be thrown a turn."); return 0; }
        if (!t) { snprintf(ui->error, sizeof ui->error, "There is no Shadow Pokemon to snag."); return 0; }
        c->kind = 2; c->slot = (uint8_t)ball; c->target = (uint8_t)targets[0];
        if (t > 1) { ui->page = C3_GUI_TARGET; ui->target_cursor = 0; return 0; }
        return 1;
    }
    unsigned entry = ui->bag_entry;
    if (entry >= b->bag_count) return 0;
    const C3GeneralBagItem *e = &b->bag[entry];
    if (e->use == C3_GB_USE_BATTLE) {
        /* fn_8002217C: on the Pokemon choosing, no party list. */
        C3GeneralItemCheck r = c3_general_battle_item_check(b, ui->position, entry, 0, 0, chosen_before(ui, 4, (int)entry));
        if (r != C3_GB_ITEM_OK) { item_refusal(ui, r); return 0; }
        c->kind = 4; c->slot = (uint8_t)entry; c->target = b->active[0][ui->position];
        return 1;
    }
    if (e->use == C3_GB_USE_MEDICINE || e->use == C3_GB_USE_PP) {
        /* fn_800141BC: the party list, from the Pokemon choosing. */
        ui->page = C3_GUI_BAG_PARTY;
        ui->cursor = b->active[0][ui->position] < b->party_count[0] ? b->active[0][ui->position] : 0;
        return 0;
    }
    /* No battle use (0x4261), or fn_80023DA8 / the Enigma Berry (0x44C6). */
    item_refusal(ui, e->use == C3_GB_USE_NONE ? C3_GB_ITEM_NO_EFFECT : C3_GB_ITEM_CANT_USE);
    return 0;
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
    if (ui->page == C3_GUI_ACTION || ui->page == C3_GUI_MOVES || ui->page == C3_GUI_TARGET || ui->page == C3_GUI_SWITCH) {
        const C3GeneralMon *m = hero(b, ui->position);
        snprintf(text, sizeof text, "What will %s do?", m ? m->name : "?");
        message = ui->error[0] ? ui->error : text;
    }
    const char *item_name = ui->bag_pocket == C3_GUI_BAG_BALLS ? c3_general_ball_name(ui->bag_entry) :
                            ui->bag_entry < b->bag_count ? b->bag[ui->bag_entry].name : "";
    if (ui->page == C3_GUI_BAG) {
        snprintf(text, sizeof text, "%s", ui->bag_confirm ? item_name : bag_labels[ui->bag_pocket % 3u]);
        message = ui->error[0] ? ui->error : text;
    } else if (ui->page == C3_GUI_BAG_PARTY) {
        snprintf(text, sizeof text, "Use the %s on which Pokemon?", item_name);
        message = ui->error[0] ? ui->error : text;
    } else if (ui->page == C3_GUI_BAG_MOVE) {
        snprintf(text, sizeof text, "Restore which move's PP?");
        message = ui->error[0] ? ui->error : text;
    } else if (ui->page == C3_GUI_REPLACE) {
        snprintf(text, sizeof text, "Choose a Pokemon to send out.");
        message = ui->error[0] ? ui->error : text;
    }
    c3_ui_text(s, 13, 100, 294, 2, message, C3_UI_WHITE);
    if (ui->page == C3_GUI_ACTION) {
        int unavailable[C3_GUI_ACTIONS] = { 0, !b->info.use_item || b->link || b->info.cpu_side0, 0, !c3_general_battle_can_call(b, ui->position) };
        c3_ui_panel(s, 5, 148, 310, 68);
        for (unsigned i = 0; i < C3_GUI_ACTIONS; ++i)
            action_cell(s, 12 + (int)(i & 1u) * 150, 155 + (int)(i >> 1) * 30, action_labels[i], ui->action_cursor == i, unavailable[i]);
        c3_ui_footer(s, previous_position(ui, b) >= 0 && !chosen_before(ui, 4, -1) ? "A: choose  B: back" : "A: choose");
    } else if (ui->page == C3_GUI_MOVES) {
        const C3GeneralMon *m = hero(b, ui->position);
        uint8_t legal = c3_general_battle_legal_moves(b, pack, ui->position);
        for (unsigned i = 0; i < 4 && m; ++i) {
            char name[C3_GB_NAME] = "-", pp[16] = "";
            /* A Shadow Pokemon's stand-ins (Shadow Rush, "????") show no PP:
             * theirs never runs down (include/colosseum3ds/shadow_moves.h). */
            if (m->moves[i]) { c3_battle_pack_move_name(pack, m->moves[i], name);
                if (m->hidden >> i & 1u) snprintf(pp, sizeof pp, "--");
                else snprintf(pp, sizeof pp, "%u/%u", m->pp[i], m->pp_max[i]); }
            c3_ui_row_value(s, 148 + (int)i * 19, name, pp, ui->cursor == i, !(legal >> i & 1u));
        }
        c3_ui_footer(s, "A: use  B: back");
    } else if (ui->page == C3_GUI_BAG && ui->bag_confirm) {
        int use = bag_offers_use(b, ui);
        if (use) c3_ui_row(s, 148, bag_labels[C3_GUI_BAG_USE], ui->bag_choice == 0, 0);
        c3_ui_row(s, use ? 167 : 148, bag_labels[C3_GUI_BAG_CANCEL], !use || ui->bag_choice == 1, 0);
        c3_ui_footer(s, "A: choose  B: back");
    } else if (ui->page == C3_GUI_BAG) {
        unsigned list[C3_GB_BAG], n = pocket_list(b, ui->bag_pocket, list);
        unsigned first = ui->bag_cursor >= 4 ? ui->bag_cursor - 3 : 0;
        for (unsigned i = 0; i < 4 && first + i < n; ++i) {
            unsigned k = first + i, left;
            const char *name;
            if (ui->bag_pocket == C3_GUI_BAG_BALLS) { name = c3_general_ball_name(list[k]); left = b->balls[list[k]]; }
            else { name = b->bag[list[k]].name; left = b->bag[list[k]].count - chosen_before(ui, 4, (int)list[k]); }
            snprintf(text, sizeof text, "x%u", left);
            c3_ui_row_value(s, 148 + (int)i * 19, name, text, ui->bag_cursor == k, 0);
        }
        if (!n) c3_ui_row(s, 148, "-", 0, 1);
        c3_ui_footer(s, "A: choose  B: back  L/R: pocket");
    } else if (ui->page == C3_GUI_BAG_PARTY) {
        for (unsigned i = 0; i < b->party_count[0]; ++i) {
            const C3GeneralMon *m = &b->party[0][i];
            snprintf(text, sizeof text, "%u/%u", m->hp, m->max_hp);
            c3_ui_row_value(s, 148 + (int)i * 13, m->name, text, ui->cursor == i, 0);
        }
        c3_ui_footer(s, "A: use  B: back");
    } else if (ui->page == C3_GUI_BAG_MOVE) {
        const C3GeneralMon *m = ui->target_cursor < b->party_count[0] ? &b->party[0][ui->target_cursor] : NULL;
        for (unsigned i = 0; i < 4 && m; ++i) {
            char name[C3_GB_NAME] = "-", pp[16] = "";
            if (m->moves[i]) { c3_battle_pack_move_name(pack, m->moves[i], name); snprintf(pp, sizeof pp, "%u/%u", m->pp[i], m->pp_max[i]); }
            c3_ui_row_value(s, 148 + (int)i * 19, name, pp, ui->cursor == i, !m->moves[i]);
        }
        c3_ui_footer(s, "A: use  B: back");
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
    } else if (ui->page == C3_GUI_REPLACE) {
        unsigned taken = ui->replace_choice[ui->position ? 0 : 1];
        for (unsigned i = 0; i < b->party_count[0]; ++i) {
            const C3GeneralMon *m = &b->party[0][i];
            snprintf(text, sizeof text, "%u/%u", m->hp, m->max_hp);
            c3_ui_row_value(s, 148 + (int)i * 13, m->name, text, ui->cursor == i, !c3_general_battle_can_replace(b, ui->position, i, taken));
        }
        c3_ui_footer(s, "A: send out");
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

/* The forced replacement's party menu for the first pending position from
 * `from` on; the cursor on the first Pokemon that can go there. */
static int replace_position(C3GeneralUi *ui, const C3GeneralBattle *b, unsigned from)
{
    for (unsigned p = from; p < b->active_count && p < 2; ++p) {
        if (!(b->replace_pending >> p & 1u)) continue;
        unsigned taken = ui->replace_choice[p ? 0 : 1];
        ui->page = C3_GUI_REPLACE; ui->position = p; ui->cursor = 0;
        for (unsigned i = 0; i < b->party_count[0]; ++i)
            if (c3_general_battle_can_replace(b, p, i, taken)) { ui->cursor = i; break; }
        return 1;
    }
    return 0;
}
int c3_general_ui_replace(C3GeneralUi *ui, const C3GeneralBattle *b)
{
    ui->replace_choice[0] = ui->replace_choice[1] = 255;
    ui->error[0] = 0;
    if (!replace_position(ui, b, 0)) return 0;
    c3_press_gate_close(&ui->gate); /* the press that ended the log is not a choice */
    return 1;
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
            /* fightTrainerIsUsedItem: once an earlier Pokemon has chosen a
             * bag item (not a ball), B stays on this Pokemon's menu. */
            int back = previous_position(ui, b);
            if (back >= 0 && !chosen_before(ui, 4, -1)) { ui->position = (unsigned)back; open_action(ui); }
        } else if (pressed & C3_PAD_A) {
            C3GeneralCommand *c = &ui->commands[ui->position];
            unsigned foes[2], n = living_foes(b, foes);
            if (ui->action_cursor == C3_GUI_FIGHT) {
                /* No move with PP left: FIGHT commits Struggle at once. */
                if (!c3_general_battle_legal_moves(b, pack, ui->position)) {
                    c->kind = 0; c->slot = 4; c->target = n ? (uint8_t)foes[0] : 2; submit = 1;
                } else { ui->page = C3_GUI_MOVES; ui->cursor = 0; }
            } else if (ui->action_cursor == C3_GUI_ITEMS) {
                /* SubMain result 1: floor status 0x20 (the fight kind's
                 * useItem) clear refuses the bag with message 0x75F5. */
                if (!b->info.use_item || b->link || b->info.cpu_side0) item_refusal(ui, C3_GB_ITEM_NOT_NOW);
                else open_bag(ui);
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
    } else if (ui->page == C3_GUI_BAG && ui->bag_confirm) {
        /* USE / CANCEL (CANCEL alone for an item with no battle use). */
        unsigned choices = bag_offers_use(b, ui) ? 2u : 1u;
        if (pressed & (C3_PAD_UP | C3_PAD_DOWN)) ui->bag_choice = (ui->bag_choice + 1u) % choices;
        if (pressed & C3_PAD_B) ui->bag_confirm = 0;
        else if (pressed & C3_PAD_A) {
            if (choices == 1 || ui->bag_choice == 1) ui->bag_confirm = 0;
            else submit = bag_use(ui, pack, b);
        }
    } else if (ui->page == C3_GUI_BAG) {
        unsigned list[C3_GB_BAG], n = pocket_list(b, ui->bag_pocket, list);
        if (pressed & C3_PAD_L) { ui->bag_pocket = (ui->bag_pocket + 2u) % 3u; ui->bag_cursor = 0; }
        else if (pressed & C3_PAD_R) { ui->bag_pocket = (ui->bag_pocket + 1u) % 3u; ui->bag_cursor = 0; }
        else if (pressed & C3_PAD_B) { ui->page = C3_GUI_ACTION; ui->cursor = 0; }
        else if (n && (pressed & C3_PAD_UP)) ui->bag_cursor = (ui->bag_cursor + n - 1u) % n;
        else if (n && (pressed & C3_PAD_DOWN)) ui->bag_cursor = (ui->bag_cursor + 1u) % n;
        else if (n && (pressed & C3_PAD_A)) { ui->bag_entry = list[ui->bag_cursor % n]; ui->bag_confirm = 1; ui->bag_choice = 0; }
    } else if (ui->page == C3_GUI_BAG_PARTY) {
        unsigned n = b->party_count[0];
        if (n && (pressed & C3_PAD_UP)) ui->cursor = (ui->cursor + n - 1u) % n;
        if (n && (pressed & C3_PAD_DOWN)) ui->cursor = (ui->cursor + 1u) % n;
        if (pressed & C3_PAD_B) { ui->page = C3_GUI_BAG; ui->bag_confirm = 0; }
        else if ((pressed & C3_PAD_A) && n) {
            unsigned entry = ui->bag_entry, party = ui->cursor % n;
            C3GeneralItemCheck r = c3_general_battle_item_check(b, ui->position, entry, party, 255, chosen_before(ui, 4, (int)entry));
            if (r == C3_GB_ITEM_NEED_MOVE) { ui->page = C3_GUI_BAG_MOVE; ui->target_cursor = party; ui->cursor = 0; }
            else if (r != C3_GB_ITEM_OK) item_refusal(ui, r);
            else { C3GeneralCommand *c = &ui->commands[ui->position];
                   c->kind = 4; c->slot = (uint8_t)entry; c->target = (uint8_t)party; submit = 1; }
        }
    } else if (ui->page == C3_GUI_BAG_MOVE) {
        /* fn_80019B48: the move an Ether restores; B goes back to the party. */
        if (pressed & C3_PAD_UP) ui->cursor = (ui->cursor + 3u) % 4u;
        if (pressed & C3_PAD_DOWN) ui->cursor = (ui->cursor + 1u) % 4u;
        if (pressed & C3_PAD_B) { ui->page = C3_GUI_BAG_PARTY; ui->cursor = ui->target_cursor; }
        else if (pressed & C3_PAD_A) {
            unsigned entry = ui->bag_entry;
            C3GeneralItemCheck r = c3_general_battle_item_check(b, ui->position, entry, ui->target_cursor, ui->cursor, chosen_before(ui, 4, (int)entry));
            if (r != C3_GB_ITEM_OK) item_refusal(ui, r);
            else { C3GeneralCommand *c = &ui->commands[ui->position];
                   c->kind = 4; c->slot = (uint8_t)entry; c->target = (uint8_t)(ui->target_cursor | ui->cursor << 4); submit = 1; }
        }
    } else if (ui->page == C3_GUI_TARGET) {
        unsigned foes[2], n = living_foes(b, foes);
        C3GeneralCommand *c = &ui->commands[ui->position];
        if (pressed & (C3_PAD_UP | C3_PAD_DOWN | C3_PAD_LEFT | C3_PAD_RIGHT)) ui->target_cursor = n ? (ui->target_cursor + 1) % n : 0;
        if (pressed & C3_PAD_B) { if (c->kind == 2) { ui->page = C3_GUI_BAG; ui->bag_confirm = 0; } else { ui->page = C3_GUI_MOVES; ui->cursor = 0; } }
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
    } else if (ui->page == C3_GUI_REPLACE) {
        /* No cancel: B leaves the menu up (retry 0 reopens it). */
        unsigned n = b->party_count[0] ? b->party_count[0] : 1u;
        if (pressed & C3_PAD_UP) ui->cursor = (ui->cursor + n - 1) % n;
        if (pressed & C3_PAD_DOWN) ui->cursor = (ui->cursor + 1) % n;
        if (pressed & C3_PAD_A) {
            unsigned pos = ui->position, taken = ui->replace_choice[pos ? 0 : 1];
            if (!c3_general_battle_can_replace(b, pos, ui->cursor, taken)) {
                snprintf(ui->error, sizeof ui->error, "That Pokemon cannot battle now.");
                return 0;
            }
            ui->replace_choice[pos] = (uint8_t)ui->cursor;
            if (replace_position(ui, b, pos + 1)) return 0;
            char error[C3_GB_ERROR_SIZE];
            if (!c3_general_battle_replace(b, pack, ui->replace_choice, error)) {
                if (trace) { fprintf(trace, "general_battle_error turn=%lu error=%s\n", (unsigned long)b->turn, error); fflush(trace); }
                c3_general_ui_replace(ui, b);
                snprintf(ui->error, sizeof ui->error, "%s", error);
                return 0;
            }
            if (trace) { fprintf(trace, "general_battle_replace turn=%lu choice=%u,%u events=%lu draws=%llu\n", (unsigned long)b->turn,
                ui->replace_choice[0], ui->replace_choice[1], (unsigned long)b->event_count, (unsigned long long)b->rng.draws); fflush(trace); }
            ui->page = C3_GUI_EVENTS;
            return 1;
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
    /* The hero picks each replacement; probes and a CPU side 0 do not. */
    b->choose_replace = !c3_battle_general_autoplaying() && !b->info.cpu_side0 && !b->link;
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
                    else if (c3_general_ui_replace(&ui, b)) {
                        if (trace) { fprintf(trace, "general_battle_replace_menu turn=%lu pending=%u scene=2d\n", (unsigned long)b->turn, b->replace_pending); fflush(trace); }
                    } else {
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
