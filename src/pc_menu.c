#include "colosseum3ds/pc_menu.h"

#include <stdio.h>
#include <string.h>

enum { PAD_LEFT = 1, PAD_RIGHT = 2, PAD_DOWN = 4, PAD_UP = 8, PAD_R = 0x20, PAD_L = 0x40, PAD_A = 0x100, PAD_B = 0x200 };
/* Common message group texts of the PC (fn_800FF730(0x391) menus). */
enum {
    M_PC = 0x1B59, M_ACCESS = 0x1B5A, M_STORAGE = 0x1B5B, M_ITEMS = 0x1B5C, M_SAVE = 0x1B5D, M_EXIT = 0x1B5E,
    M_DO_WHAT = 0x1B74, M_WITHDRAW = 0x1B77, M_CANCEL = 0x1B79, M_DEPOSIT = 0x1B7A, M_PARTY_FULL = 0x1B7D,
    M_WHICH_BOX = 0x1B80, M_BOX_FULL = 0x1B81, M_LV = 0x1B82, M_LAST = 0x1B90, M_PARTY = 0x1B8F
};
#define BOX_ROWS 7u
#define BOXES 3u
#define SLOTS 30u

static void text(const C3PcMenu *m, uint32_t id, const char *name, char *out, size_t capacity)
{
    if (!m->text || !m->text(m->context, id, name, out, capacity)) snprintf(out, capacity, "?");
}
static int party_record(const C3PcMenu *m, unsigned slot, uint8_t r[0x138])
{
    return c3_game_state_pokemon(m->state, slot, r) && (r[0] | r[1]);
}
static void record_name(const uint8_t *r, char *out, size_t capacity)
{
    c3_game_text_ascii(r + 0x2e, 11, out, capacity);
}

void c3_pc_menu_open(C3PcMenu *m, C3GameState *state, C3PcText text_source, void *context)
{
    if (!m) return;
    memset(m, 0, sizeof *m);
    m->state = state; m->text = text_source; m->context = context;
}

static C3PcEvent notice(C3PcMenu *m, uint32_t id, const char *name, C3PcPhase back)
{
    text(m, id, name, m->notice, sizeof m->notice);
    m->phase = C3_PC_NOTICE; m->notice_return = back;
    return C3_PC_CHANGED;
}

static C3PcEvent storage(C3PcMenu *m, uint16_t pressed)
{
    uint8_t r[0x138];
    if (pressed & PAD_B) { m->phase = C3_PC_TOP; return C3_PC_CHANGED; }
    if (pressed & (PAD_L | PAD_R)) {
        m->box = pressed & PAD_L ? (m->box + BOXES - 1) % BOXES : (m->box + 1) % BOXES;
        return C3_PC_CHANGED;
    }
    if (pressed & (PAD_LEFT | PAD_RIGHT)) { m->side ^= 1u; return C3_PC_CHANGED; }
    if (pressed & (PAD_UP | PAD_DOWN)) {
        int up = (pressed & PAD_UP) != 0;
        if (!m->side) m->party = up ? (m->party + 5) % 6 : (m->party + 1) % 6;
        else {
            m->box_cursor = up ? (m->box_cursor + SLOTS - 1) % SLOTS : (m->box_cursor + 1) % SLOTS;
            if (m->box_cursor < m->box_scroll) m->box_scroll = m->box_cursor;
            if (m->box_cursor >= m->box_scroll + BOX_ROWS) m->box_scroll = m->box_cursor - BOX_ROWS + 1;
        }
        return C3_PC_CHANGED;
    }
    if (pressed & PAD_A) {
        int filled = m->side ? c3_game_state_box_pokemon(m->state, m->box, m->box_cursor, r) : party_record(m, m->party, r);
        if (!filled) return C3_PC_NONE;
        m->phase = C3_PC_ACTION; m->action = 0;
        return C3_PC_CHANGED;
    }
    return C3_PC_NONE;
}

C3PcEvent c3_pc_menu_update(C3PcMenu *m, uint16_t pressed)
{
    uint8_t r[0x138];
    char name[32];
    if (!m || !m->state) return C3_PC_NONE;
    switch (m->phase) {
    case C3_PC_TOP:
        if (pressed & PAD_B) return C3_PC_EXIT;
        if (pressed & PAD_UP) { m->top = (m->top + 3) % 4; if (m->top == 1) m->top = 0; return C3_PC_CHANGED; }
        if (pressed & PAD_DOWN) { m->top = (m->top + 1) % 4; if (m->top == 1) m->top = 2; return C3_PC_CHANGED; }
        if (pressed & PAD_A) {
            if (m->top == 0) { m->phase = C3_PC_STORAGE; m->side = 0; return C3_PC_CHANGED; }
            if (m->top == 2) return C3_PC_SAVE;
            if (m->top == 3) return C3_PC_EXIT;
        }
        return C3_PC_NONE;
    case C3_PC_STORAGE:
        return storage(m, pressed);
    case C3_PC_ACTION:
        if (pressed & PAD_B) { m->phase = C3_PC_STORAGE; return C3_PC_CHANGED; }
        if (pressed & (PAD_UP | PAD_DOWN)) { m->action ^= 1u; return C3_PC_CHANGED; }
        if (pressed & PAD_A) {
            if (m->action) { m->phase = C3_PC_STORAGE; return C3_PC_CHANGED; }
            if (!m->side) {
                unsigned count = 0;
                for (unsigned i = 0; i < 6; ++i) count += party_record(m, i, r);
                if (count < 2) return notice(m, M_LAST, NULL, C3_PC_STORAGE);
                m->phase = C3_PC_BOX_CHOICE; m->choice = m->box;
                return C3_PC_CHANGED;
            }
            if (!c3_game_state_box_pokemon(m->state, m->box, m->box_cursor, r)) { m->phase = C3_PC_STORAGE; return C3_PC_CHANGED; }
            if (c3_game_state_box_withdraw(m->state, m->box, m->box_cursor) < 0) return notice(m, M_PARTY_FULL, NULL, C3_PC_STORAGE);
            ++m->withdrawals; m->phase = C3_PC_STORAGE;
            return C3_PC_CHANGED;
        }
        return C3_PC_NONE;
    case C3_PC_BOX_CHOICE:
        if (pressed & PAD_B) { m->phase = C3_PC_STORAGE; return C3_PC_CHANGED; }
        if (pressed & PAD_UP) { m->choice = (m->choice + BOXES - 1) % BOXES; return C3_PC_CHANGED; }
        if (pressed & PAD_DOWN) { m->choice = (m->choice + 1) % BOXES; return C3_PC_CHANGED; }
        if (pressed & PAD_A) {
            if (c3_game_state_box_used(m->state, m->choice) >= (int)SLOTS) return notice(m, M_BOX_FULL, NULL, C3_PC_BOX_CHOICE);
            if (!party_record(m, m->party, r)) { m->phase = C3_PC_STORAGE; return C3_PC_CHANGED; }
            record_name(r, name, sizeof name);
            if (c3_game_state_box_deposit(m->state, m->party, m->choice) < 0) return notice(m, M_LAST, NULL, C3_PC_STORAGE);
            ++m->deposits; m->box = m->choice; m->phase = C3_PC_STORAGE;
            while (m->party && !party_record(m, m->party, r)) --m->party;
            return C3_PC_CHANGED;
        }
        return C3_PC_NONE;
    case C3_PC_NOTICE:
        if (pressed & (PAD_A | PAD_B)) { m->phase = m->notice_return; return C3_PC_CHANGED; }
        return C3_PC_NONE;
    }
    return C3_PC_NONE;
}

static void pokemon_row(const C3PcMenu *m, C3BottomUi *ui, int x, int y, int width, const uint8_t *r, int filled, int selected)
{
    char name[32], label[48], lv[16];
    if (selected) { c3_ui_rect(ui, x, y, width, 18, 0x30494bu); c3_ui_rect(ui, x, y, 2, 18, C3_UI_GOLD); }
    if (!filled) { c3_ui_text(ui, x + 8, y, width - 10, 1, "-", C3_UI_MUTED); return; }
    record_name(r, name, sizeof name);
    text(m, M_LV, NULL, lv, sizeof lv);
    snprintf(label, sizeof label, "%s%u", lv, (unsigned)r[0x60]);
    c3_ui_text(ui, x + 8, y, width - 50, 1, name, C3_UI_WHITE);
    c3_ui_text_right(ui, x + width - 4, y, 46, label, C3_UI_MUTED);
}

void c3_pc_menu_draw(const C3PcMenu *m, C3BottomUi *ui)
{
    char title[64], line[96], name[32];
    uint8_t r[0x138];
    if (!m || !ui || !m->state) return;
    if (m->phase == C3_PC_TOP) {
        static const uint32_t rows[4] = {M_STORAGE, M_ITEMS, M_SAVE, M_EXIT};
        text(m, M_PC, NULL, title, sizeof title);
        text(m, M_ACCESS, NULL, line, sizeof line);
        c3_ui_header(ui, title, line);
        c3_ui_panel(ui, 9, 58, 302, 136);
        for (unsigned i = 0; i < 4; ++i) {
            text(m, rows[i], NULL, line, sizeof line);
            c3_ui_row(ui, 68 + (int)i * 32, line, m->top == i, i == 1);
        }
        c3_ui_footer(ui, "A: choose   B: exit");
        return;
    }
    /* Storage: the party (left) and the current BOX (right). */
    c3_game_state_box_name(m->state, m->box, name, sizeof name);
    snprintf(title, sizeof title, "%s  %d/30", name, c3_game_state_box_used(m->state, m->box));
    text(m, M_PARTY, NULL, line, sizeof line);
    c3_ui_header(ui, title, "L / R: BOX");
    c3_ui_panel(ui, 5, 54, 150, 160);
    c3_ui_panel(ui, 161, 54, 154, 160);
    c3_ui_text(ui, 12, 56, 140, 1, line, C3_UI_GOLD);
    for (unsigned i = 0; i < 6; ++i)
        pokemon_row(m, ui, 9, 76 + (int)i * 22, 142, r, party_record(m, i, r), !m->side && m->party == i);
    for (unsigned i = 0; i < BOX_ROWS; ++i) {
        unsigned slot = m->box_scroll + i;
        int filled = c3_game_state_box_pokemon(m->state, m->box, slot, r);
        pokemon_row(m, ui, 165, 58 + (int)i * 22, 146, r, filled, m->side && m->box_cursor == slot);
    }
    if (m->phase == C3_PC_ACTION) {
        int filled = m->side ? c3_game_state_box_pokemon(m->state, m->box, m->box_cursor, r) : party_record(m, m->party, r);
        if (filled) record_name(r, name, sizeof name); else name[0] = 0;
        text(m, M_DO_WHAT, name, line, sizeof line);
        c3_ui_panel(ui, 30, 120, 260, 92);
        c3_ui_text(ui, 40, 124, 240, 1, line, C3_UI_WHITE);
        char a[32], b[32];
        text(m, m->side ? M_WITHDRAW : M_DEPOSIT, NULL, a, sizeof a);
        text(m, M_CANCEL, NULL, b, sizeof b);
        c3_ui_row(ui, 150, a, m->action == 0, 0);
        c3_ui_row(ui, 176, b, m->action == 1, 0);
    } else if (m->phase == C3_PC_BOX_CHOICE) {
        text(m, M_WHICH_BOX, NULL, line, sizeof line);
        c3_ui_panel(ui, 30, 96, 260, 116);
        c3_ui_text(ui, 40, 100, 240, 1, line, C3_UI_WHITE);
        for (unsigned i = 0; i < BOXES; ++i) {
            char label[48];
            c3_game_state_box_name(m->state, i, name, sizeof name);
            snprintf(label, sizeof label, "%s  %d/30", name, c3_game_state_box_used(m->state, i));
            c3_ui_row(ui, 124 + (int)i * 26, label, m->choice == i, 0);
        }
    } else if (m->phase == C3_PC_NOTICE) {
        c3_ui_panel(ui, 30, 130, 260, 60);
        c3_ui_text(ui, 40, 140, 240, 2, m->notice, C3_UI_WHITE);
    }
    c3_ui_footer(ui, m->phase == C3_PC_STORAGE ? "D-pad: choose   A: select   B: back" : "A: confirm   B: back");
}
