/* Host message sweep: renders every message of every original message group
 * (tools/prepare_message_sweep.py builds the private C3MS pack) and reports
 * control codes the decoder cannot interpret.
 *
 *   build/test_message_sweep <messages.c3ms> [ui.c3ui]
 *
 * Every message is rendered twice:
 *   1. with a source that answers every value control, so any failure is a
 *      control (or text) the decoder itself does not understand. The count
 *      must be zero.
 *   2. for groups the field scene shows (floor archives, the world map and
 *      common_rel), with exactly the value controls the field message source
 *      answers (c3_field_story_message_control). This count must be zero too.
 * Menu and battle groups are reported by owner and control as information:
 * their own scenes supply those values.
 *
 * With the private UI pack (tools/prepare_bottom_ui.py) every hard line of
 * every field message page is also laid out in the original font at the
 * bottom-screen message window's text width (C3_UI_MESSAGE_WIDTH): each must
 * take one row, so no original line wraps and leaves an orphan word. The
 * speaker control (59) prints SPEAKER_BUDGET, the other names their default
 * (WES, RUI) and every other value "<XX>"; widths with the widest original
 * person name are reported as information.
 * Without the private packs the sweep runs only its synthetic checks. */
#include "colosseum3ds/bottom_ui.h"
#include "colosseum3ds/field_story.h"
#include "colosseum3ds/story.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks, failures;
#ifndef SPEAKER_BUDGET
#define SPEAKER_BUDGET "FUN OLD LADY"
#endif
#define SPEAKER_WIDEST "MYSTERY TROOP"
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); } } while (0)

static uint32_t le32(const unsigned char *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint32_t be32(const unsigned char *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }

typedef struct { const char *name; const unsigned char *data; uint32_t size; } Group;
enum { OWNER_FIELD, OWNER_MENU, OWNER_BATTLE, OWNERS };
static const char *const owner_names[OWNERS] = {"field", "menu", "battle"};

static int owner(const char *name)
{
    /* The fight message tables, wherever they are stored (D2 archives carry
     * a copy of 0D5E0A00). */
    if (strstr(name, ":0d5e0a00") || strstr(name, ":013d0a00") || !strncmp(name, "fight_", 6) ||
        !strncmp(name, "waza_viewer", 11))
        return OWNER_BATTLE;
    if ((name[0] == 'M' || name[0] == 'D' || name[0] == 'S') && name[1] >= '1' && name[1] <= '9' && name[2] == '_')
        return OWNER_FIELD;
    if (!strncmp(name, "world_map", 9) || !strncmp(name, "common_rel", 10)) return OWNER_FIELD;
    return OWNER_MENU;
}

typedef struct { int field_only; unsigned asked[256]; } Source;
static int answer(void *context, uint8_t control, C3StoryValue *value)
{
    Source *s = context;
    static char text[8];
    if (s->field_only && !c3_field_story_message_control(control)) { ++s->asked[control]; return 0; }
    snprintf(text, sizeof text, "<%02X>", control);
    value->text = text;
    return 1;
}

/* Field text as the window shows it, for the width check. */
static const char *speaker_name;
static int field_values(void *context, uint8_t control, C3StoryValue *value)
{
    (void)context;
    value->text = control == 0x59 ? speaker_name : control == 0x2b ? "WES" : control == 0x2c ? "RUI" : "<XX>";
    return 1;
}

/* Hard lines of every page (split at '\n' and '\f') that need more than one
 * row; prints up to `report` of them. *pages_over counts pages of more than
 * five rows (the dialogue window's height). */
static unsigned long wrapped_lines(const C3BottomUi *ui, const char *text, int report, unsigned long *pages_over, unsigned long *lines)
{
    unsigned long wrapped = 0;
    int page_rows = 0;
    char line[C3_STORY_MESSAGE];
    for (const char *p = text;;) {
        size_t n = strcspn(p, "\n\f");
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n); line[n] = 0;
        int rows = c3_ui_text_rows(ui, C3_UI_MESSAGE_WIDTH, line);
        ++*lines;
        page_rows += rows ? rows : 1;
        if (rows > 1) { if (report > 0 && wrapped < (unsigned long)report) fprintf(stderr, "  wraps: %s\n", line); ++wrapped; }
        if (!p[n] || p[n] == '\f') { if (page_rows > 5) ++*pages_over; page_rows = 0; }
        if (!p[n]) break;
        p += n + 1;
    }
    return wrapped;
}

static int values(void *context, uint8_t control, C3StoryValue *value)
{
    static uint32_t v[256];
    static char number[24];
    (void)context;
    v[0x2f] = 7; v[0x4b] = 1234567;
    if (control == 0x31) { value->message = 2; return 1; }
    if (c3_story_format_number(control, v, number, sizeof number)) { value->text = number; return 1; }
    return 0;
}

static void synthetic(void)
{
    /* One group: every structural control, a nested message and an unknown. */
    unsigned char g[512] = {0};
    unsigned at = 16 + 3 * 8;
    g[5] = 3;
    const unsigned char m1[] = {0,'A', 255,255,7,1, 255,255,8,0xff,0,0,0xff, 0,'B', 255,255,9,30, 255,255,0x38,2,
        255,255,0x52,1, 255,255,0x53,1, 255,255,0x54,1, 255,255,0x5b,1, 255,255,0x5c,1, 255,255,0x3d, 255,255,0x5a,
        255,255,4, 0,'K', 255,255,5, 0,'k', 255,255,6, 255,255,0x31, 255,255,0x5d, 255,255,0x5e,
        255,255,0x4b, 255,255,1, 255,255,2, 0,'X', 0,0};
    const unsigned char m2[] = {0,'N', 255,255,0x2f, 0,0};
    const unsigned char m3[] = {0,'Z', 255,255,0x5f, 0,0};
    const unsigned char *texts[3] = {m1, m2, m3};
    const unsigned sizes[3] = {sizeof m1, sizeof m2, sizeof m3};
    for (unsigned i = 0; i < 3; ++i) {
        g[16 + i * 8 + 3] = (unsigned char)(i + 1);
        g[20 + i * 8 + 2] = (unsigned char)(at >> 8); g[20 + i * 8 + 3] = (unsigned char)at;
        memcpy(g + at, texts[i], sizes[i]); at += sizes[i];
    }
    C3Story story = {0};
    char out[256], error[C3_STORY_ERROR];
    CHECK(c3_story_add_messages(&story, g, at, error));
    CHECK(c3_story_render(&story, 1, values, NULL, C3_STORY_TEXT_MARKUP, out, sizeof out, error));
    CHECK(!strcmp(out, "A\x1b" "F01\x1b" "Cff0000ffB\x1b" "W1e\x1b" "P02\x1b" "A01KN7\x1b" "S0\x1b" "S11,234,567\n"));
    CHECK(c3_story_text_plain(out) == strlen("ABKN71,234,567\n") && !strcmp(out, "ABKN71,234,567\n"));
    CHECK(c3_story_render(&story, 1, values, NULL, 0, out, sizeof out, error) && !strcmp(out, "ABKN71,234,567\n"));
    CHECK(!c3_story_render(&story, 3, values, NULL, 0, out, sizeof out, error) && strstr(error, "control 5f"));
    CHECK(c3_story_has_message(&story, 2) && !c3_story_has_message(&story, 4));
    for (unsigned c = 0; c < 0x5f; ++c) CHECK(c3_story_control_known((uint8_t)c));
    CHECK(!c3_story_control_known(0x5f) && !c3_story_control_known(0xff));
    uint32_t v[256] = {0};
    char text[32];
    v[0x4c] = 100u * 3600u + 5u * 60u; CHECK(c3_story_format_number(0x4c, v, text, sizeof text) && !strcmp(text, "100:05"));
    v[0x34] = 0x2a; CHECK(c3_story_format_number(0x47, v, text, sizeof text) && !strcmp(text, "0000002A"));
    CHECK(c3_story_format_number(0x49, v, text, sizeof text) && !strcmp(text, "0000000042"));
    v[0x2f] = (uint32_t)-12; CHECK(c3_story_format_number(0x2f, v, text, sizeof text) && !strcmp(text, "-12"));
    v[0x30] = 0; CHECK(c3_story_format_number(0x30, v, text, sizeof text) && !strcmp(text, "0"));
    CHECK(!c3_story_format_number(0x2b, v, text, sizeof text));
    c3_story_free(&story);
}

int main(int argc, char **argv)
{
    synthetic();
    /* Arguments in either order: the C3MS message pack and the C3UI pack. */
    unsigned char *pack = NULL, *ui_pack = NULL;
    long size = 0, ui_size = 0;
    for (int a = 1; a < argc; ++a) {
        FILE *file = fopen(argv[a], "rb");
        if (!file) { fprintf(stderr, "cannot open %s\n", argv[a]); return 1; }
        fseek(file, 0, SEEK_END); long n = ftell(file); fseek(file, 0, SEEK_SET);
        unsigned char *data = malloc((size_t)n);
        if (!data || n < 4 || fread(data, 1, (size_t)n, file) != (size_t)n) { fprintf(stderr, "read failed\n"); return 1; }
        fclose(file);
        if (!memcmp(data, "C3UI", 4)) { ui_pack = data; ui_size = n; } else { pack = data; size = n; }
    }
    C3UiAssets assets;
    C3BottomUi ui = {0};
    if (ui_pack) { CHECK(c3_ui_assets_open(&assets, ui_pack, (size_t)ui_size)); ui.assets = &assets; }
    if (!pack) {
        printf("message sweep: %u synthetic checks, %u failures; private pack not supplied (tools/prepare_message_sweep.py)\n",
               checks, failures);
        return failures != 0;
    }
    if (size < 16 || memcmp(pack, "C3MS", 4) || le32(pack + 4) != 1 || le32(pack + 8) != (uint32_t)size) { fprintf(stderr, "bad C3MS\n"); return 1; }
    uint32_t count = le32(pack + 12), at = 16;
    Group *groups = calloc(count, sizeof *groups);
    char (*names)[96] = calloc(count, 96);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t n = le32(pack + at); at += 4;
        memcpy(names[i], pack + at, n < 95 ? n : 95); at += n;
        groups[i].name = names[i]; groups[i].size = le32(pack + at); at += 4;
        groups[i].data = pack + at; at += groups[i].size;
    }
    CHECK(at == (uint32_t)size);
    const Group *common[2] = {0};
    for (uint32_t i = 0; i < count; ++i) {
        if (!strcmp(groups[i].name, "common_rel.rel:names")) common[0] = &groups[i];
        if (!strcmp(groups[i].name, "common_rel.rel:messages")) common[1] = &groups[i];
    }
    unsigned long messages = 0, unknown = 0, missing[OWNERS] = {0}, non_ascii = 0;
    unsigned long field_lines = 0, wrapped = 0, wrapped_widest = 0, pages_over = 0, pages_over_widest = 0, unused = 0;
    unsigned pending[OWNERS][256] = {{0}};
    static char out[C3_STORY_MESSAGE * 4];
    char error[C3_STORY_ERROR];
    for (uint32_t i = 0; i < count; ++i) {
        C3Story story = {0};
        int ok = c3_story_add_messages(&story, groups[i].data, groups[i].size, error);
        for (unsigned c = 0; c < 2 && ok; ++c)
            if (common[c] && common[c] != &groups[i]) ok = c3_story_add_messages(&story, common[c]->data, common[c]->size, error);
        CHECK(ok);
        if (!ok) { fprintf(stderr, "%s: %s\n", groups[i].name, error); c3_story_free(&story); continue; }
        int who = owner(groups[i].name);
        unsigned entries = be16(groups[i].data + 4);
        for (unsigned e = 0; e < entries; ++e) {
            uint32_t id = be32(groups[i].data + 16 + e * 8) | (uint32_t)be16(groups[i].data) << 20;
            Source all = {0}, field = {1, {0}};
            ++messages;
            if (!c3_story_render(&story, id, answer, &all, C3_STORY_TEXT_MARKUP, out, sizeof out, error)) {
                if (unknown < 20) fprintf(stderr, "unknown: %s %05x: %s\n", groups[i].name, (unsigned)id, error);
                ++unknown; continue;
            }
            if (who == OWNER_FIELD) for (const char *p = out; *p; ++p) if ((unsigned char)*p >= 0x80) ++non_ascii;
            if (who == OWNER_FIELD && ui.assets) {
                char shown[C3_STORY_MESSAGE * 4];
                speaker_name = SPEAKER_BUDGET;
                if (c3_story_render(&story, id, field_values, NULL, C3_STORY_TEXT_MARKUP, shown, sizeof shown, error))
                    wrapped += wrapped_lines(&ui, shown, 20, &pages_over, &field_lines);
                speaker_name = SPEAKER_WIDEST;
                if (c3_story_render(&story, id, field_values, NULL, C3_STORY_TEXT_MARKUP, shown, sizeof shown, error))
                    wrapped_widest += wrapped_lines(&ui, shown, 0, &pages_over_widest, &unused);
            }
            Source *owner_source = who == OWNER_FIELD ? &field : &all;
            if (who != OWNER_FIELD) { owner_source->field_only = 1; }
            if (!c3_story_render(&story, id, answer, owner_source, C3_STORY_TEXT_MARKUP, out, sizeof out, error)) {
                ++missing[who];
                for (unsigned c = 0; c < 256; ++c) if (owner_source->asked[c]) ++pending[who][c];
                if (who == OWNER_FIELD && missing[who] <= 20)
                    fprintf(stderr, "field source lacks a value: %s %05x: %s\n", groups[i].name, (unsigned)id, error);
            }
        }
        c3_story_free(&story);
    }
    printf("message sweep: %u groups, %lu messages, %lu unknown-control messages\n", (unsigned)count, messages, unknown);
    for (int w = 0; w < OWNERS; ++w) {
        printf("  %s: %lu messages need values the field source does not give", owner_names[w], missing[w]);
        for (unsigned c = 0; c < 256; ++c) if (pending[w][c]) printf(" %02x:%u", c, pending[w][c]);
        printf("\n");
    }
    printf("  field text bytes outside ASCII (font coverage): %lu\n", non_ascii);
    if (ui.assets) {
        printf("  dialogue width %d: %lu field lines, %lu wrap with speaker \"%s\"; %lu pages exceed five rows\n",
               C3_UI_MESSAGE_WIDTH, field_lines, wrapped, SPEAKER_BUDGET, pages_over);
        printf("  with speaker \"%s\" (widest original person name): %lu lines wrap, %lu pages exceed five rows\n",
               SPEAKER_WIDEST, wrapped_widest, pages_over_widest);
        CHECK(field_lines > 10000 && wrapped == 0);
    } else {
        printf("  dialogue widths not checked: UI pack not supplied (tools/prepare_bottom_ui.py)\n");
    }
    CHECK(unknown == 0);
    CHECK(missing[OWNER_FIELD] == 0);
    printf("message sweep: %u checks, %u failures\n", checks, failures);
    free(names); free(groups); free(pack);
    return failures != 0;
}
