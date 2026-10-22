#include "game_menu.h"

#ifndef C3_MENU_HOST_TEST
#include <3ds.h>
#endif
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Control decisions follow Count-Sheep/pkmn-colosseum @51bfe4314a21:
 * gs_title.c fn_800210F0 / fn_80020C9C and menuNameEntry.c menuNameEntry.
 * The source mode-0 record at DOL 0x80266DD8 has length 7, three presets.
 * This is a native bottom-screen UI, not a port of the original window engine. */
#define TEXT_MAX_BYTES 16384u
#define TEXT_MAX_COUNT 64u
#define TEXT_HEADER_BYTES 16u
#define TEXT_ENTRY_BYTES 12u

static const char *const menu_labels[] = {
    "NEW GAME", "CONTINUE", "COLOSSEUM BATTLE", "BATTLE NOW", "OPTIONS", "QUIT"
};
static const char *const preset_names[] = {"WES", "SETH", "THOMAS"};
static const char keyboard_letters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -.'";

static uint32_t read32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static const char *message(const C3GameMenu *menu, uint32_t id, const char *fallback)
{
    for (uint32_t i = 0; i < menu->text_count; ++i) {
        const unsigned char *entry = menu->text_data + TEXT_HEADER_BYTES + i * TEXT_ENTRY_BYTES;
        if (read32(entry) == id) return (const char *)menu->text_data + read32(entry + 4);
    }
    return fallback;
}

static void page(C3GameMenu *menu, C3GameMenuPage next)
{
    menu->page = next;
    menu->dirty = 1;
    menu->direction_seconds = menu->repeat_seconds = 0.0f;
}

void c3_game_menu_init(C3GameMenu *menu, const C3GameMenuSave *save,
                       const C3GameMenuOptions *options)
{
    if (!menu) return;
    memset(menu, 0, sizeof(*menu));
    if (save) menu->save = *save;
    if (options) menu->options = *options;
    menu->options.sound_mono = !!menu->options.sound_mono;
    menu->options.rumble_disabled = !!menu->options.rumble_disabled;
    menu->initial_options = menu->options;
    menu->selection = menu->save.can_continue ? 1u : 0u;
    memcpy(menu->name, "WES", 4);
    page(menu, C3_MENU_MAIN);
}

int c3_game_menu_load_text(C3GameMenu *menu, const char *path, char *error, size_t capacity)
{
    unsigned char *data = NULL;
    FILE *stream = NULL;
    const char *why = "Invalid native menu text pack.";
    size_t bytes;
    uint32_t count, last_id = 0;
    if (!menu || !path) goto fail;
    stream = fopen(path, "rb");
    if (!stream) { why = "Private menu text pack is not available."; goto fail; }
    data = malloc(TEXT_MAX_BYTES + 1u);
    if (!data) { why = "Menu text allocation failed."; goto fail; }
    bytes = fread(data, 1, TEXT_MAX_BYTES + 1u, stream);
    if (ferror(stream) || bytes > TEXT_MAX_BYTES || bytes < TEXT_HEADER_BYTES) goto fail;
    fclose(stream); stream = NULL;
    if (memcmp(data, "C3U1", 4) || read32(data + 4) != 1 || read32(data + 8) != bytes) goto fail;
    count = read32(data + 12);
    if (!count || count > TEXT_MAX_COUNT || TEXT_HEADER_BYTES + count * TEXT_ENTRY_BYTES > bytes) goto fail;
    for (uint32_t i = 0; i < count; ++i) {
        const unsigned char *entry = data + TEXT_HEADER_BYTES + i * TEXT_ENTRY_BYTES;
        uint32_t id = read32(entry), offset = read32(entry + 4), length = read32(entry + 8);
        if (id <= last_id || offset < TEXT_HEADER_BYTES + count * TEXT_ENTRY_BYTES ||
            length < 1 || length > 512 || offset > bytes || length > bytes - offset ||
            data[offset + length - 1] != 0) goto fail;
        for (uint32_t k = 0; k + 1 < length; ++k)
            if ((data[offset + k] < 32 && data[offset + k] != '\n') || data[offset + k] > 126) goto fail;
        last_id = id;
    }
    free(menu->text_data);
    menu->text_data = data;
    menu->text_size = bytes;
    menu->text_count = count;
    menu->dirty = 1;
    return 1;
fail:
    if (stream) fclose(stream);
    free(data);
    if (error && capacity) snprintf(error, capacity, "%s", why);
    return 0;
}

void c3_game_menu_close(C3GameMenu *menu)
{
    if (!menu) return;
    free(menu->text_data);
    menu->text_data = NULL;
    menu->text_count = 0;
    menu->text_size = 0;
}

void c3_game_menu_set_save(C3GameMenu *menu, const C3GameMenuSave *save)
{
    if (menu && save) { menu->save = *save; menu->dirty = 1; }
}

void c3_game_menu_set_pending(C3GameMenu *menu, const char *text)
{
    if (!menu) return;
    snprintf(menu->notice, sizeof(menu->notice), "%s", text ? text : "Please wait...");
    page(menu, C3_MENU_PENDING);
}

void c3_game_menu_set_error(C3GameMenu *menu, const char *text)
{
    if (!menu) return;
    snprintf(menu->notice, sizeof(menu->notice), "%s", text ? text : "The requested action failed.");
    page(menu, C3_MENU_ERROR);
}

const char *c3_game_menu_page_name(C3GameMenuPage p)
{
    static const char *const names[] = {"main", "new_confirm", "name_choices", "name_keyboard",
        "name_confirm", "continue_confirm", "options", "notice", "pending", "error"};
    return (unsigned)p < sizeof(names) / sizeof(*names) ? names[p] : "invalid";
}

static uint16_t directions(C3GameMenu *menu, const C3N3dsInput *input, float delta)
{
    uint16_t held = input->held & (C3_PAD_UP | C3_PAD_DOWN | C3_PAD_LEFT | C3_PAD_RIGHT);
    if (input->stick_x > 36) held |= C3_PAD_RIGHT;
    if (input->stick_x < -36) held |= C3_PAD_LEFT;
    if (input->stick_y > 36) held |= C3_PAD_UP;
    if (input->stick_y < -36) held |= C3_PAD_DOWN;
    if ((held & (C3_PAD_UP | C3_PAD_DOWN)) == (C3_PAD_UP | C3_PAD_DOWN)) held &= ~(C3_PAD_UP | C3_PAD_DOWN);
    if ((held & (C3_PAD_LEFT | C3_PAD_RIGHT)) == (C3_PAD_LEFT | C3_PAD_RIGHT)) held &= ~(C3_PAD_LEFT | C3_PAD_RIGHT);
    if (held != menu->previous_directions) {
        menu->previous_directions = held;
        menu->direction_seconds = menu->repeat_seconds = 0.0f;
        return held;
    }
    if (!held) return 0;
    if (!isfinite(delta) || delta < 0.0f) delta = 0.0f;
    if (delta > 0.25f) delta = 0.25f;
    menu->direction_seconds += delta;
    if (menu->direction_seconds < 0.35f) return 0;
    menu->repeat_seconds += delta;
    if (menu->repeat_seconds < 0.12f) return 0;
    menu->repeat_seconds = 0.0f;
    return held;
}

static void choose(C3GameMenu *menu, uint16_t move, unsigned *selection, unsigned count)
{
    unsigned before = *selection;
    if ((move & C3_PAD_UP) && *selection) --*selection;
    else if ((move & C3_PAD_DOWN) && *selection + 1u < count) ++*selection;
    if (before != *selection) menu->dirty = 1;
}

static void confirm_move(C3GameMenu *menu, uint16_t move)
{
    if (move & (C3_PAD_LEFT | C3_PAD_UP)) menu->confirm_yes = 1;
    else if (move & (C3_PAD_RIGHT | C3_PAD_DOWN)) menu->confirm_yes = 0;
    if (move) menu->dirty = 1;
}

static void edit_letter(C3GameMenu *menu, char letter)
{
    size_t length = strlen(menu->name);
    if (length == C3_GAME_MENU_NAME_MAX) --length;
    menu->name[length++] = letter;
    menu->name[length] = 0;
    if (length == C3_GAME_MENU_NAME_MAX) menu->keyboard_selection = 43;
    menu->dirty = 1;
}

static void confirm_name(C3GameMenu *menu)
{
    size_t nonblank = strspn(menu->name, " ");
    if (!menu->name[nonblank]) snprintf(menu->name, sizeof(menu->name), "%s", preset_names[0]);
    menu->confirm_yes = 1;
    page(menu, C3_MENU_NAME_CONFIRM);
}

C3GameMenuEvent c3_game_menu_update(C3GameMenu *menu, const C3N3dsInput *input, float delta)
{
    C3GameMenuEvent event = {0};
    if (!menu || !input) return event;
    uint16_t move = directions(menu, input, delta);
    int accept = !!(input->pressed & C3_PAD_A), back = !!(input->pressed & C3_PAD_B);
    switch (menu->page) {
    case C3_MENU_MAIN:
        choose(menu, move, &menu->selection, 6);
        if (back || (accept && menu->selection == 5)) {
            event.kind = C3_MENU_EVENT_RETURN_TITLE;
            c3_game_menu_set_pending(menu, "Returning to the title...");
        } else if (accept && menu->selection == 0) {
            menu->replace_authorized = 0;
            menu->confirm_yes = !menu->save.present;
            page(menu, C3_MENU_NEW_CONFIRM);
        } else if (accept && menu->selection == 1) {
            if (menu->save.valid && menu->save.can_continue) {
                menu->confirm_yes = 1;
                page(menu, C3_MENU_CONTINUE_CONFIRM);
            } else {
                snprintf(menu->notice, sizeof(menu->notice), "%s", menu->save.reason[0] ? menu->save.reason : "No supported Story Mode save is available.");
                page(menu, C3_MENU_NOTICE);
            }
        } else if (accept && (menu->selection == 2 || menu->selection == 3)) {
            snprintf(menu->notice, sizeof(menu->notice), "%s is not available in this native build.", menu_labels[menu->selection]);
            page(menu, C3_MENU_NOTICE);
        } else if (accept && menu->selection == 4) {
            menu->initial_options = menu->options;
            menu->selection = 0;
            page(menu, C3_MENU_OPTIONS);
        }
        break;
    case C3_MENU_NEW_CONFIRM:
        confirm_move(menu, move);
        if (back || (accept && !menu->confirm_yes)) page(menu, C3_MENU_MAIN);
        else if (accept) {
            menu->replace_authorized = !!menu->save.present;
            menu->name_selection = 0;
            page(menu, C3_MENU_NAME_CHOICES);
        }
        break;
    case C3_MENU_NAME_CHOICES:
        choose(menu, move, &menu->name_selection, 4);
        if (back) { menu->replace_authorized = 0; page(menu, C3_MENU_MAIN); }
        else if (accept && menu->name_selection == 0) {
            menu->name[0] = 0;
            menu->keyboard_selection = 0;
            menu->lower_case = 0;
            page(menu, C3_MENU_NAME_KEYBOARD);
        } else if (accept) {
            snprintf(menu->name, sizeof(menu->name), "%s", message(menu, 0x2EFEu + menu->name_selection - 1u, preset_names[menu->name_selection - 1u]));
            confirm_name(menu);
        }
        break;
    case C3_MENU_NAME_KEYBOARD: {
        unsigned row = menu->keyboard_selection / 8u, col = menu->keyboard_selection % 8u;
        if (move & C3_PAD_UP) row = row ? row - 1u : 5u;
        else if (move & C3_PAD_DOWN) row = row == 5u ? 0u : row + 1u;
        unsigned columns = row == 5u ? 4u : 8u;
        if (col >= columns) col = columns - 1u;
        if (move & C3_PAD_LEFT) col = col ? col - 1u : columns - 1u;
        else if (move & C3_PAD_RIGHT) col = col + 1u == columns ? 0u : col + 1u;
        menu->keyboard_selection = row * 8u + col;
        if (move) menu->dirty = 1;
        if (input->pressed & (C3_PAD_R | C3_PAD_X)) { menu->lower_case ^= 1u; menu->dirty = 1; }
        if (back || (accept && menu->keyboard_selection == 42u)) {
            size_t length = strlen(menu->name);
            if (length) menu->name[length - 1u] = 0;
            else if (back) page(menu, C3_MENU_NAME_CHOICES);
            menu->dirty = 1;
        } else if ((input->pressed & C3_PAD_START) || (accept && menu->keyboard_selection == 43u)) {
            confirm_name(menu);
        } else if (accept && menu->keyboard_selection == 40u) {
            menu->lower_case ^= 1u; menu->dirty = 1;
        } else if (accept && menu->keyboard_selection == 41u) edit_letter(menu, ' ');
        else if (accept) {
            char letter = keyboard_letters[menu->keyboard_selection];
            if (menu->lower_case && letter >= 'A' && letter <= 'Z') letter += 'a' - 'A';
            edit_letter(menu, letter);
        }
        break;
    }
    case C3_MENU_NAME_CONFIRM:
        confirm_move(menu, move);
        if (back || (accept && !menu->confirm_yes)) page(menu, C3_MENU_NAME_CHOICES);
        else if (accept) {
            event.kind = C3_MENU_EVENT_NEW_GAME;
            memcpy(event.name, menu->name, sizeof(event.name));
            event.options = menu->options;
            event.replace_authorized = menu->replace_authorized;
            c3_game_menu_set_pending(menu, "Preparing the new Story...");
        }
        break;
    case C3_MENU_CONTINUE_CONFIRM:
        confirm_move(menu, move);
        if (back || (accept && !menu->confirm_yes)) page(menu, C3_MENU_MAIN);
        else if (accept && menu->save.valid && menu->save.can_continue) {
            event.kind = C3_MENU_EVENT_CONTINUE;
            c3_game_menu_set_pending(menu, "Loading the saved Story...");
        }
        break;
    case C3_MENU_OPTIONS:
        choose(menu, move, &menu->selection, 3);
        if (menu->selection == 0 && (move & (C3_PAD_LEFT | C3_PAD_RIGHT))) {
            menu->options.sound_mono = !!(move & C3_PAD_LEFT); menu->dirty = 1;
        } else if (menu->selection == 1 && (move & (C3_PAD_LEFT | C3_PAD_RIGHT))) {
            menu->options.rumble_disabled = !!(move & C3_PAD_RIGHT); menu->dirty = 1;
        }
        if (back) {
            menu->options = menu->initial_options;
            menu->selection = 4;
            page(menu, C3_MENU_MAIN);
        } else if (accept && menu->selection == 2) {
            event.kind = C3_MENU_EVENT_OPTIONS_CHANGED;
            event.options = menu->options;
            menu->initial_options = menu->options;
            menu->selection = 4;
            page(menu, C3_MENU_MAIN);
        }
        break;
    case C3_MENU_NOTICE:
    case C3_MENU_ERROR:
        if (accept || back) page(menu, C3_MENU_MAIN);
        break;
    case C3_MENU_PENDING:
        break;
    default:
        c3_game_menu_set_error(menu, "Invalid menu state.");
        break;
    }
    return event;
}

/* Wrap source strings to the 40-column bottom console without treating them
 * as format strings or allowing extracted control codes into the terminal. */
static void paragraph(const char *text)
{
    unsigned column = 0;
    while (*text) {
        if (*text == '\n') { putchar('\n'); column = 0; ++text; continue; }
        if (*text == ' ') {
            if (column && column < 38) { putchar(' '); ++column; }
            ++text;
            continue;
        }
        const char *end = text;
        while (*end && *end != ' ' && *end != '\n') ++end;
        size_t word = (size_t)(end - text);
        if (column && column + word > 38) { putchar('\n'); column = 0; }
        while (text < end) {
            if (column == 38) { putchar('\n'); column = 0; }
            putchar(*text++); ++column;
        }
    }
    putchar('\n');
}

static void confirm_draw(const C3GameMenu *menu)
{
    printf("\n  %s YES       %s NO\n\n", menu->confirm_yes ? ">" : " ", menu->confirm_yes ? " " : ">");
    printf("D-Pad: choose   A: confirm   B: back\n");
}

void c3_game_menu_draw(C3GameMenu *menu)
{
    if (!menu || !menu->dirty) return;
    menu->dirty = 0;
#ifndef C3_MENU_HOST_TEST
    consoleClear();
#endif
    printf("POKEMON COLOSSEUM\n\n");
    switch (menu->page) {
    case C3_MENU_MAIN: {
        const uint32_t descriptions[] = {0x3C15, 0x3C14, 0x3C19, 0x3C1B, 0x3C17, 0x3C1D};
        const char *fallbacks[] = {"Start the Story from the beginning.", "Continue your saved Story.",
            "Colosseum Battle is not available.", "Battle Now is not available.",
            "Adjust sound and Controller Rumble.", "Return to the title screen."};
        for (unsigned i = 0; i < 6; ++i) {
            int disabled = i == 2 || i == 3 || (i == 1 && !menu->save.can_continue);
            printf("%s %-17s%s\n\n", menu->selection == i ? ">" : " ", menu_labels[i], disabled ? " [unavailable]" : "");
        }
        paragraph(message(menu, descriptions[menu->selection], fallbacks[menu->selection]));
        printf("\nD-Pad / Circle Pad: move\nA: choose   B: title\n");
        break;
    }
    case C3_MENU_NEW_CONFIRM:
        printf("NEW GAME\n\n");
        if (menu->save.present)
            paragraph("A card file already exists. Starting a new Story permits your next Save to replace that file. Continue?");
        else paragraph(message(menu, 0x3C3A, "Is it okay to start a new Story?"));
        confirm_draw(menu);
        break;
    case C3_MENU_NAME_CHOICES:
        paragraph(message(menu, 0x2EED, "Your name?"));
        printf("\n%s %s\n\n", menu->name_selection == 0 ? ">" : " ", message(menu, 0x2EFD, "NEW NAME"));
        for (unsigned i = 0; i < 3; ++i)
            printf("%s %s\n\n", menu->name_selection == i + 1 ? ">" : " ", message(menu, 0x2EFE + i, preset_names[i]));
        printf("D-Pad: choose   A: confirm   B: back\n");
        break;
    case C3_MENU_NAME_KEYBOARD:
        paragraph(message(menu, 0x2EED, "Your name?"));
        printf("\n  [%-7s]   %lu / 7\n\n", menu->name, (unsigned long)strlen(menu->name));
        for (unsigned row = 0; row < 5; ++row) {
            for (unsigned col = 0; col < 8; ++col) {
                unsigned index = row * 8 + col;
                char letter = keyboard_letters[index];
                if (menu->lower_case && letter >= 'A' && letter <= 'Z') letter += 'a' - 'A';
                printf(menu->keyboard_selection == index ? "[%c] " : " %c  ", letter);
            }
            printf("\n\n");
        }
        const char *commands[] = {"CASE", "SPACE", "BACK", "DONE"};
        for (unsigned i = 0; i < 4; ++i)
            printf("%c%-6s ", menu->keyboard_selection == i + 40 ? '>' : ' ', commands[i]);
        printf("\n\nA: letter   B: erase/back\nR/X: case   START: done\n");
        break;
    case C3_MENU_NAME_CONFIRM:
        printf("Is %s OK?\n", menu->name);
        confirm_draw(menu);
        break;
    case C3_MENU_CONTINUE_CONFIRM:
        printf("CONTINUE\n\n%s\n%s\n\nMoney: %lu\nTime: %lu:%02lu\n\n", menu->save.name,
               menu->save.location, (unsigned long)menu->save.money,
               (unsigned long)(menu->save.play_seconds / 3600u),
               (unsigned long)(menu->save.play_seconds / 60u % 60u));
        paragraph(message(menu, 0x3C22, "Is it okay to continue from here?"));
        confirm_draw(menu);
        break;
    case C3_MENU_OPTIONS:
        printf("OPTIONS\n\n%s SOUND      <%s>\n\n%s RUMBLE     <%s>\n\n%s END\n\n",
               menu->selection == 0 ? ">" : " ", menu->options.sound_mono ? "MONO" : "STEREO",
               menu->selection == 1 ? ">" : " ", menu->options.rumble_disabled ? "OFF" : "ON",
               menu->selection == 2 ? ">" : " ");
        paragraph(message(menu, 0x43FB + menu->selection, menu->selection == 0 ? "Choose Mono or Stereo sound." :
                          menu->selection == 1 ? "Keep the original rumble preference." : "Apply settings and leave Options."));
        printf("\nEND applies for this session.\nUse in-game Save to keep settings.\n3DS has no Controller Rumble motor.\n\nD-Pad: choose/change   A: END\nB: cancel changes\n");
        break;
    case C3_MENU_ERROR:
        printf("Unable to continue\n\n");
        paragraph(menu->notice);
        printf("\nA / B: return to menu\n");
        break;
    case C3_MENU_NOTICE:
    case C3_MENU_PENDING:
        paragraph(menu->notice);
        if (menu->page == C3_MENU_NOTICE) printf("\nA / B: return to menu\n");
        break;
    }
}
