#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/field_story.h"
#include "colosseum3ds/inventory.h"
#include "colosseum3ds/pda.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Optional PRIVATE-DATA story route test (docs/agate-story.md). It plays a
 * main-story chapter headlessly through the production field adapter with
 * one game state that persists across floors: a route file lists what a
 * player does (talk to a person, touch or cross a surface, stand somewhere,
 * battle results) and the scripts' own floor changes are followed. Every
 * flag an original script writes is reported, so a route is checked against
 * the original flags it must reach. Nothing is rendered; models are
 * one-vertex stand-ins and dialogue is advanced with A.
 *
 * usage: test_story_route <newgame dir> <boot dir> <save.gci> <route> [report]
 *
 * Route lines (# starts a comment):
 *   clear <flag> [flag ...]   write 0 (the chapter checkpoint)
 *   set <flag> <value>        write a flag (recorded as a shortcut)
 *   floor <floor> <entrance>  load a floor at an entrance and run its arrival
 *   talk <person index>       stand in front of the person and press A
 *   touch <surface>           touch a surface (event kind 2)
 *   door <surface>            cross a surface (event kind 1); a floor change follows
 *   at <x> <z>                put the hero at a source position (main-loop boxes)
 *   run <frames>              tick with no input (a floor change is followed)
 *   pad <mask>                hold these buttons for ten ticks (0x1-0x8 D-pad),
 *                             then run until the hero has control
 *   result <id>               result ID reported for the next battles (default 2, win)
 *   expect <flag> <value>     fail unless the flag holds the value
 *   expect_floor <floor>      fail unless the scene is on that floor
 *   save <path>               write the game state as a card file (a chapter-point
 *                             save for emulator checks; keep it private)
 *   talkall                   talk to every present person with a talk script, one
 *                             "route_talk" line each; a failed or floor-changing
 *                             conversation reloads this floor and the sweep goes on
 *   mail <id>                 read a received PDA mail: its original read
 *                             transaction (flags, hints) from the PDA pack
 *                             named by C3_ROUTE_PDA (tools/prepare_pda.py)
 *   inspect <surface>         press A at a surface (event kind 3): stand half a
 *                             unit in front of its first enabled triangle
 *   press <x> <z> <yaw>       press A from a source position facing yaw degrees
 *   use <item>                when a script next opens the pause menu at Items
 *                             (native D0), use this key item (0 closes the menu)
 *                             (talk scripts that check where the hero stands)
 * door and touch refuse a surface whose CCD objects are all disabled. Item
 * and species names in messages come from the C3_ROUTE_PDA pack; native DB
 * (direct bag change) is reported as a "route_item_change" line.
 * Each step ends in one "route" line; flag writes, messages, battles and
 * floor changes are "route_flag", "route_message", "route_battle" and
 * "route_floor" lines. Without arguments nothing private is checked. */
static char error[C3_STORY_ERROR];
static FILE *report, *trace;
static unsigned failures, steps;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s (%s)\n",__FILE__,__LINE__,#x,error); exit(1); } } while (0)

typedef struct Scene {
    C3FieldStory story;
    C3FieldMap map;
    C3Area area;
    C3FieldPlayer hero;
    C3Title wes, npc;
    C3Title *models[C3_AREA_MAX_ACTORS];
    C3AreaCamera camera;
    uint32_t floor, entrance, previous;
    int loaded;
} Scene;

enum { FLAG_FIRST = 0x001, FLAG_LAST = 0x1000 };
static const char *newgame, *root;
static C3GameState game;
static uint32_t shadow[FLAG_LAST];
static uint8_t valid[FLAG_LAST];
static uint32_t result_id = 2, last_message, use_item;
static Scene *scene;
static C3PdaAssets pda;
static uint8_t *pda_data;

static void path(char out[1024], const char *dir, const char *name)
{ CHECK(snprintf(out, 1024, "%s/%s", dir, name) > 0 && strlen(out) < 1023); }
static int any_floor(uint32_t floor) { return floor >= 1 && floor <= 0xffff; }
static int receive(void *c, uint32_t item, uint32_t count, char *name, size_t capacity)
{ (void)c; fprintf(report, "route_item item=%lu count=%lu\n", (unsigned long)item, (unsigned long)count);
  snprintf(name, capacity, "ITEM"); return 0; }
static int clear_poison(void *c) { return c3_inventory_clear_party_poison(c); }
static int restore(void *c) { (void)c; return 6; }
static int item_change(void *c, uint32_t item, int32_t count)
{ (void)c; fprintf(report, "route_item_change item=%lu count=%ld\n", (unsigned long)item, (long)count); return 0; }
/* Item and species names for message controls, from the PDA pack's tables
 * (platform/3ds/field_scene.c story_namer). */
static int namer(void *c, unsigned kind, uint32_t id, C3StoryValue *value)
{
    static char text[64]; C3InventoryItem item; C3PdaSpecies species; int ok = 0; (void)c;
    if (!pda_data) return 0;
    if (kind == C3_STORY_NAME_ITEM && c3_inventory_item(&pda, id, &item)) ok = c3_pda_text(&pda, item.name, "WES", "RUI", text, sizeof text);
    else if (kind == C3_STORY_NAME_SPECIES && c3_pda_species(&pda, id, &species)) ok = c3_pda_text(&pda, species.name, "", "", text, sizeof text);
    if (ok) value->text = text;
    return ok;
}

static void le32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static uint32_t bits(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
/* The one-vertex stand-in of tests/test_world_story.c. */
static void stand_in(C3Title *m)
{
    m->fps=30;m->clip_count=C3_TITLE_MAX_CLIPS;m->frame_count=31;m->bone_count=1;m->batch_count=1;m->vertex_count=1;
    m->format_version=3;m->material_frame_bytes=92;m->frame_bytes=48+92+44;
    m->file_bytes=m->frame_bytes*31;m->file_data=calloc(1,m->file_bytes);
    m->vertices=calloc(1,sizeof(*m->vertices));m->draw_vertices=calloc(1,sizeof(*m->draw_vertices));
    m->batches=calloc(1,sizeof(*m->batches));CHECK(m->file_data&&m->vertices&&m->draw_vertices&&m->batches);
    m->vertices[0].pos[0]=1;m->vertices[0].normal[2]=1;m->batches[0].count=1;
    for(unsigned i=0;i<C3_TITLE_MAX_CLIPS;++i)m->clips[i]=(C3TitleClip){0,31,1};
    for(unsigned i=0;i<31;++i){uint8_t *f=m->file_data+i*m->frame_bytes;
        for(unsigned j=0;j<3;++j){le32(f+(j*4+j)*4,bits(1));le32(f+48+(20+j)*4,bits((float)(j+1)));}
        le32(f+48+20,bits(1));le32(f+140+4*7,bits(45));le32(f+140+4*8,bits(.1f));
        le32(f+140+4*9,bits(100));le32(f+140+4*10,bits(1));
    }
}

static void snapshot(void)
{ for (uint32_t id = FLAG_FIRST; id < FLAG_LAST; ++id) valid[id] = (uint8_t)c3_game_state_get_flag(&game, id, &shadow[id]); }
/* Report every flag that changed since the last snapshot. */
static unsigned flag_writes(void)
{
    unsigned changed = 0;
    for (uint32_t id = FLAG_FIRST; id < FLAG_LAST; ++id) {
        uint32_t value;
        if (!valid[id] || !c3_game_state_get_flag(&game, id, &value) || value == shadow[id]) continue;
        fprintf(report, "route_flag floor=%03lx flag=%03lx %lu->%lu\n", (unsigned long)scene->floor,
                (unsigned long)id, (unsigned long)shadow[id], (unsigned long)value);
        shadow[id] = value; ++changed;
    }
    return changed;
}

static void unload(void)
{
    if (!scene->loaded) return;
    c3_field_story_close(&scene->story); c3_field_close(&scene->map); c3_area_close(&scene->area);
    c3_title_close(&scene->wes); c3_title_close(&scene->npc);
    memset(scene, 0, sizeof *scene);
}

static int run(unsigned frames, int until_control);
/* Load a floor at an entrance with the persistent game state and run its
 * load/init/pre scripts and arrival until the hero has control. */
static int load(uint32_t floor, uint32_t entrance, uint32_t previous)
{
    char file[1024], name[64];
    unload();
    error[0] = 0;
    scene->floor = floor; scene->entrance = entrance; scene->previous = previous;
    CHECK(c3_game_state_set_floor(&game, floor, previous, (uint8_t)entrance));
    snprintf(name, sizeof name, "f%03lx.c3f", (unsigned long)floor); path(file, root, name);
    if (!c3_field_load(&scene->map, file, error, sizeof error)) return 0;
    snprintf(name, sizeof name, "f%03lx.c3a", (unsigned long)floor); path(file, root, name);
    if (!c3_area_load(&scene->area, file, error, sizeof error)) { c3_field_close(&scene->map); return 0; }
    stand_in(&scene->wes); stand_in(&scene->npc);
    for (uint32_t i = 0; i < scene->area.actor_count && i < C3_AREA_MAX_ACTORS; ++i) scene->models[i] = &scene->npc;
    if (!c3_field_spawn(&scene->map, entrance, &scene->hero) && !c3_field_spawn(&scene->map, 0, &scene->hero)) {
        snprintf(error, sizeof error, "entrance %lu is unavailable", (unsigned long)entrance); return 0;
    }
    snprintf(name, sizeof name, "f%03lx.c3s", (unsigned long)floor); path(file, root, name);
    if (!c3_field_story_load(&scene->story, file, &game, &scene->map, &scene->area, &scene->hero, &scene->wes,
                             scene->models, &scene->camera, trace, error)) return 0;
    scene->loaded = 1;
    c3_field_story_set_floor_support(&scene->story, any_floor);
    c3_field_story_enable_name_entry(&scene->story, 1);
    c3_field_story_set_item_service(&scene->story, receive, NULL);
    c3_field_story_set_party_service(&scene->story, restore, NULL);
    c3_field_story_set_poison_service(&scene->story, clear_poison, &game);
    c3_field_story_set_item_change_service(&scene->story, item_change, NULL);
    c3_field_story_set_namer(&scene->story, namer, NULL);
    c3_field_story_enable_item_menu(&scene->story, 1);
    if (!c3_field_story_prepare(&scene->story, error)) return 0;
    for (unsigned frame = 0; frame < 3600 && !c3_field_story_view(&scene->story)->prepared; ++frame)
        if (!c3_field_story_tick(&scene->story, 1.0f/60.0f, 0, error)) return 0;
    if (!c3_field_story_view(&scene->story)->prepared) { snprintf(error, sizeof error, "preparation did not finish"); return 0; }
    if (!c3_field_story_enter(&scene->story, error)) return 0;
    fprintf(report, "route_floor floor=%03lx entrance=%lu previous=%03lx actors=%lu links=%lu\n", (unsigned long)floor,
            (unsigned long)entrance, (unsigned long)previous, (unsigned long)scene->area.actor_count,
            (unsigned long)scene->area.link_count);
    int arrived = run(6000, 1);
    /* Where each person stands after the arrival (for writing routes). */
    if (scene->loaded && scene->floor == floor)
        for (uint32_t i = 0; i < scene->area.actor_count; ++i) {
            C3StoryActor *a = c3_field_story_actor(&scene->story, scene->area.actors[i].index);
            if (a) fprintf(trace, "route_actor floor=%03lx person=%lu talk=%08lx visible=%d x=%.2f z=%.2f yaw=%.0f\n",
                           (unsigned long)floor, (unsigned long)scene->area.actors[i].index,
                           (unsigned long)scene->area.actors[i].talk_script, a->visible,
                           (double)(a->pose.position[0] / scene->map.scale), (double)(a->pose.position[2] / scene->map.scale),
                           (double)(a->pose.yaw * 57.29578f));
        }
    return arrived;
}

/* Tick until the hero has control again (until_control) or for `frames`.
 * A floor change requested by a script loads the next floor. */
static int run(unsigned frames, int until_control)
{
    unsigned steady = 0;
    for (unsigned frame = 0; frame < frames; ++frame) {
        const C3FieldStoryView *v = c3_field_story_view(&scene->story);
        if (v->next_floor) {
            uint32_t next = v->next_floor, spawn = v->next_spawn, from = scene->floor;
            flag_writes();
            if (next == 0x38e) { fprintf(report, "route_world_map from=%03lx\n", (unsigned long)from); unload(); return 1; }
            return load(next, spawn, from);
        }
        if (v->message_id && v->message_id != last_message) {
            last_message = v->message_id;
            /* An unsupported control code fails the script (src/story.c);
             * a glyph outside ASCII is drawn as '?' by the field font. */
            int wide = 0;
            for (const char *c = v->message; *c; ++c) wide |= (unsigned char)*c >= 0x80;
            fprintf(report, "route_message floor=%03lx id=%05lx non_ascii=%d\n", (unsigned long)scene->floor,
                    (unsigned long)v->message_id, wide);
        }
        /* Control must hold for a second: some events hand it back
         * between two parts (Mt. Battle area 1, function 72). */
        if (v->control && !v->dialogue && !c3_field_story_battle_pending(&scene->story, NULL)) ++steady; else steady = 0;
        if (until_control && frame > 30 && steady >= 60) return 1;
        uint16_t pressed = (v->dialogue || v->choice) && frame % 15 == 0 ? 0x100 : 0;
        uint32_t encounter;
        if (c3_field_story_battle_pending(&scene->story, &encounter)) {
            fprintf(report, "route_battle floor=%03lx encounter=%lu result=%lu skipped_as_result=1\n",
                    (unsigned long)scene->floor, (unsigned long)encounter, (unsigned long)result_id);
            if (!c3_field_story_battle_result(&scene->story, result_id, error)) return 0;
        }
        if (v->shop && !c3_field_story_shop_done(&scene->story, error)) return 0;
        if (v->item_menu) {
            fprintf(report, "route_item_menu floor=%03lx use=%lu\n", (unsigned long)scene->floor, (unsigned long)use_item);
            uint32_t item = use_item; use_item = 0;
            if (!c3_field_story_item_menu_result(&scene->story, item, error)) return 0;
        }
        if (v->name_entry && !c3_field_story_name_result(&scene->story, "RUI", error)) return 0;
        if (v->relive_pick && !c3_field_story_relive_pick(&scene->story, -1, error)) return 0;
        if (!c3_field_story_tick(&scene->story, 1.0f/60.0f, pressed, error) || !c3_field_story_sample(&scene->story, error)) return 0;
        if (v->blocked[0] && !c3_field_story_battle_pending(&scene->story, NULL)) { snprintf(error, sizeof error, "blocked: %.160s", v->blocked); return 0; }
    }
    if (until_control) { char waits[512]; c3_field_story_describe_waits(&scene->story, waits, sizeof waits);
        snprintf(error, sizeof error, "no control after %u frames: %.150s", frames, waits); return 0; }
    return 1;
}

/* A surface the player can reach: one triangle of it on an enabled object. */
static int reachable(const C3FieldTriangle *t, uint32_t count, uint32_t source)
{
    for (uint32_t i = 0; i < count; ++i)
        if (t[i].source_id == source && c3_field_object_enabled(&scene->map, t[i].object)) return 1;
    return 0;
}

static int step(char *line)
{
    char word[32]; unsigned long a = 0, b = 0; float x, z; int n;
    if (sscanf(line, "%31s%n", word, &n) != 1) return 1;
    line += n;
    if (!strcmp(word, "clear")) {
        char *p = line;
        while (sscanf(p, "%li%n", (long *)&a, &n) == 1) { CHECK(c3_game_state_set_flag(&game, (uint32_t)a, 0)); p += n; }
        snapshot(); return 1;
    }
    if (!strcmp(word, "set")) {
        CHECK(sscanf(line, "%li %li", (long *)&a, (long *)&b) == 2 && c3_game_state_set_flag(&game, (uint32_t)a, (uint32_t)b));
        fprintf(report, "route_shortcut flag=%03lx value=%lu\n", a, b); snapshot(); return 1;
    }
    if (!strcmp(word, "result")) { CHECK(sscanf(line, "%li", (long *)&a) == 1); result_id = (uint32_t)a; return 1; }
    if (!strcmp(word, "expect")) {
        uint32_t value = 0;
        CHECK(sscanf(line, "%li %li", (long *)&a, (long *)&b) == 2);
        if (!c3_game_state_get_flag(&game, (uint32_t)a, &value) || value != b) {
            snprintf(error, sizeof error, "flag %03lx is %lu, expected %lu", a, (unsigned long)value, b); return 0;
        }
        return 1;
    }
    if (!strcmp(word, "expect_floor")) {
        CHECK(sscanf(line, "%li", (long *)&a) == 1);
        if (!scene->loaded || scene->floor != a) { snprintf(error, sizeof error, "on floor %03lx", (unsigned long)scene->floor); return 0; }
        return 1;
    }
    if (!strcmp(word, "save")) {
        char out[1024], gerror[C3_GAME_ERROR_SIZE] = {0};
        CHECK(sscanf(line, " %1023s", out) == 1);
        if (scene->loaded) CHECK(c3_game_state_set_floor(&game, scene->floor, scene->previous, (uint8_t)scene->entrance));
        if (!c3_game_state_save(&game, out, gerror)) { snprintf(error, sizeof error, "save: %.150s", gerror); return 0; }
        fprintf(report, "route_save path=%s floor=%03lx entrance=%lu\n", out, (unsigned long)scene->floor, (unsigned long)scene->entrance);
        return 1;
    }
    if (!strcmp(word, "mail")) {
        C3PdaMailAction action;
        CHECK(sscanf(line, "%li", (long *)&a) == 1);
        if (!pda_data) { snprintf(error, sizeof error, "C3_ROUTE_PDA names no PDA pack"); return 0; }
        if (!c3_pda_mail_action(&pda, &game, (unsigned)a, &action) || !c3_game_state_mail_read(&game, &action.save, 1)) {
            snprintf(error, sizeof error, "mail %lu cannot be read", a); return 0;
        }
        fprintf(report, "route_mail id=%lu flags=%u hints=%u script=%08lx\n", a, action.save.flag_count,
                action.save.hint_count, (unsigned long)action.script);
        /* A checkpoint made from a later save may hold the mail as read
         * already; the original transaction is then a no-op, so the pack's
         * own read flags are written and the line says so. */
        unsigned count = 0; int already = 0; C3GameMail mail;
        CHECK(c3_game_state_mail_count(&game, &count));
        for (unsigned i = 0; i < count; ++i) if (c3_game_state_mail(&game, i, &mail) && mail.id == a && mail.read) already = 1;
        for (unsigned i = 0; i < action.save.flag_count; ++i) {
            uint32_t value = 0;
            if (already && c3_game_state_get_flag(&game, action.save.flags[i].id, &value) && value != action.save.flags[i].value)
                CHECK(c3_game_state_set_flag(&game, action.save.flags[i].id, action.save.flags[i].value));
            fprintf(report, "route_mail_flag id=%lu flag=%03lx value=%lu checkpoint_already_read=%d\n", a,
                    (unsigned long)action.save.flags[i].id, (unsigned long)action.save.flags[i].value, already);
        }
        if (scene->loaded) flag_writes(); else snapshot();
        return 1;
    }
    if (!strcmp(word, "floor")) {
        CHECK(sscanf(line, "%li %li", (long *)&a, (long *)&b) == 2);
        return load((uint32_t)a, (uint32_t)b, scene->loaded ? scene->floor : 0x38e);
    }
    if (!scene->loaded) { snprintf(error, sizeof error, "no floor is loaded"); return 0; }
    if (!strcmp(word, "pad")) {
        CHECK(sscanf(line, "%li", (long *)&a) == 1);
        for (unsigned held = 0; held < 10 && scene->loaded; ++held)
            if (!c3_field_story_tick(&scene->story, 1.0f/60.0f, (uint16_t)a, error) || !c3_field_story_sample(&scene->story, error)) return 0;
        return run(30000, 1);
    }
    if (!strcmp(word, "run")) { CHECK(sscanf(line, "%li", (long *)&a) == 1); return run((unsigned)a, 0); }
    if (!strcmp(word, "at")) {
        CHECK(sscanf(line, "%f %f", &x, &z) == 2);
        scene->hero.position[0] = x * scene->map.scale; scene->hero.position[2] = z * scene->map.scale;
        memcpy(scene->hero.proposed, scene->hero.position, sizeof scene->hero.position);
        return run(120, 0) && run(6000, 1);
    }
    if (!strcmp(word, "talkall")) {
        uint32_t floor = scene->floor, entrance = scene->entrance, previous = scene->previous, count = scene->area.actor_count;
        unsigned failed = 0, talked = 0;
        for (uint32_t i = 0; i < count && i < C3_AREA_MAX_ACTORS; ++i) {
            if (!scene->loaded || scene->floor != floor) { if (!load(floor, entrance, previous)) return 0; }
            const C3AreaActor *who = scene->area.actors + i;
            if (!who->talk_script) continue;
            uint32_t index = who->index, script = who->talk_script;
            C3StoryActor *actor = c3_field_story_actor(&scene->story, index);
            if (!actor || !actor->visible) {
                fprintf(report, "route_talk floor=%03lx person=%lu script=%08lx status=absent\n", (unsigned long)floor,
                        (unsigned long)index, (unsigned long)script);
                continue;
            }
            char sub[32]; snprintf(sub, sizeof sub, "talk %lu", (unsigned long)index);
            error[0] = 0; ++talked;
            uint32_t menu_before = c3_field_story_view(&scene->story)->unported_menu;
            int ok = step(sub);
            if (scene->loaded) flag_writes();
            /* A menu the port lacks took the original cancel branch (the
             * view keeps the last such native, so a change marks this talk). */
            uint32_t menu = scene->loaded && scene->floor == floor ? c3_field_story_view(&scene->story)->unported_menu : 0;
            if (menu == menu_before) menu = 0;
            fprintf(report, "route_talk floor=%03lx person=%lu script=%08lx status=%s unported_menu=%02lx next=%03lx reason=%s\n",
                    (unsigned long)floor, (unsigned long)index, (unsigned long)script, ok ? (menu ? "partial" : "ok") : "failed",
                    (unsigned long)menu,
                    (unsigned long)(scene->loaded && scene->floor != floor ? scene->floor : 0), ok ? "" : error);
            if (!ok) { ++failed; unload(); }
        }
        if (!scene->loaded || scene->floor != floor) { if (!load(floor, entrance, previous)) return 0; }
        if (failed) { snprintf(error, sizeof error, "%u of %u conversations failed", failed, talked); return 0; }
        return 1;
    }
    if (!strcmp(word, "talk")) {
        CHECK(sscanf(line, "%li", (long *)&a) == 1);
        C3StoryActor *actor = c3_field_story_actor(&scene->story, (uint32_t)a);
        if (!actor || !actor->visible) { snprintf(error, sizeof error, "person %lu is not present", a); return 0; }
        float yaw = actor->pose.yaw, reach = 0.6f;
        scene->hero.position[0] = actor->pose.position[0] + sinf(yaw) * reach;
        scene->hero.position[1] = actor->pose.position[1];
        scene->hero.position[2] = actor->pose.position[2] + cosf(yaw) * reach;
        scene->hero.yaw = yaw + 3.14159265f;
        memcpy(scene->hero.proposed, scene->hero.position, sizeof scene->hero.position);
        if (!c3_field_story_interact(&scene->story, error)) return 0;
        return run(30000, 1);
    }
    if (!strcmp(word, "use")) { CHECK(sscanf(line, "%li", (long *)&a) == 1); use_item = (uint32_t)a; return 1; }
    if (!strcmp(word, "press")) {
        float yaw;
        CHECK(sscanf(line, "%f %f %f", &x, &z, &yaw) == 3);
        scene->hero.position[0] = x * scene->map.scale; scene->hero.position[2] = z * scene->map.scale;
        scene->hero.yaw = yaw * 3.14159265f / 180.0f;
        memcpy(scene->hero.proposed, scene->hero.position, sizeof scene->hero.position);
        if (!c3_field_story_interact(&scene->story, error)) return 0;
        return run(30000, 1);
    }
    if (!strcmp(word, "inspect")) {
        CHECK(sscanf(line, "%li", (long *)&a) == 1);
        const C3FieldTriangle *t = NULL;
        for (uint32_t i = 0; i < scene->area.interaction_count && !t; ++i)
            if (scene->area.interactions[i].source_id == a && c3_field_object_enabled(&scene->map, scene->area.interactions[i].object))
                t = scene->area.interactions + i;
        if (!t) { snprintf(error, sizeof error, "surface %lu has no enabled A-button triangle", a); return 0; }
        float c[3], d[2];
        for (unsigned k = 0; k < 3; ++k) c[k] = (t->vertices[k] + t->vertices[3 + k] + t->vertices[6 + k]) / 3.0f;
        d[0] = t->normal[0]; d[1] = t->normal[2];
        float len = sqrtf(d[0] * d[0] + d[1] * d[1]);
        if (len < 1e-4f) { d[0] = 0; d[1] = 1; } else { d[0] /= len; d[1] /= len; }
        scene->hero.position[0] = c[0] + d[0] * 0.5f; scene->hero.position[1] = c[1] - 0.5f;
        scene->hero.position[2] = c[2] + d[1] * 0.5f; scene->hero.yaw = atan2f(-d[0], -d[1]);
        memcpy(scene->hero.proposed, scene->hero.position, sizeof scene->hero.position);
        if (!c3_field_story_interact(&scene->story, error)) return 0;
        return run(30000, 1);
    }
    if (!strcmp(word, "touch")) {
        CHECK(sscanf(line, "%li", (long *)&a) == 1);
        if (!reachable(scene->area.transitions, scene->area.transition_count, (uint32_t)a)) {
            snprintf(error, sizeof error, "touch surface %lu is on a disabled CCD object", a); return 0;
        }
        int started = c3_field_story_touch(&scene->story, (uint32_t)a, error);
        if (started <= 0) { if (!started) snprintf(error, sizeof error, "touch %lu did not start", a); return 0; }
        return run(30000, 1);
    }
    if (!strcmp(word, "door")) {
        CHECK(sscanf(line, "%li", (long *)&a) == 1);
        if (!reachable(scene->area.transitions, scene->area.transition_count, (uint32_t)a)) {
            snprintf(error, sizeof error, "door surface %lu is on a disabled CCD object", a); return 0;
        }
        if (!c3_field_story_transition(&scene->story, (uint32_t)a, error)) return 0;
        return run(30000, 1);
    }
    snprintf(error, sizeof error, "unknown route step %s", word);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 5) { puts("story-route: private integration skipped (needs <newgame dir> <boot dir> <save.gci> <route>)"); return 0; }
    newgame = argv[1]; root = argv[2];
    report = argc > 5 ? fopen(argv[5], "w") : stdout; CHECK(report);
    trace = argc > 6 ? fopen(argv[6], "w") : tmpfile(); CHECK(trace);
    scene = calloc(1, sizeof *scene); CHECK(scene);
    char file[1024], gerror[C3_GAME_ERROR_SIZE] = {0}, line[4096];
    path(file, newgame, "newgame.c3g");
    if (!c3_game_state_load(&game, argv[3], file, gerror)) { fprintf(stderr, "save: %s\n", gerror); return 1; }
    snapshot();
    const char *pda_path = getenv("C3_ROUTE_PDA");
    if (pda_path && *pda_path) {
        FILE *f = fopen(pda_path, "rb"); long size; CHECK(f && fseek(f, 0, SEEK_END) == 0 && (size = ftell(f)) > 0);
        rewind(f); pda_data = malloc((size_t)size); CHECK(pda_data && fread(pda_data, 1, (size_t)size, f) == (size_t)size);
        fclose(f); CHECK(c3_pda_assets_open(&pda, pda_data, (size_t)size));
    }
    FILE *route = fopen(argv[4], "r"); CHECK(route);
    for (unsigned number = 1; fgets(line, sizeof line, route); ++number) {
        char *hash = strchr(line, '#'); if (hash) *hash = 0;
        line[strcspn(line, "\r\n")] = 0;
        char *p = line; while (*p == ' ' || *p == '\t') ++p;
        if (!*p) continue;
        error[0] = 0; ++steps;
        int ok = step(p);
        if (scene->loaded) flag_writes();
        fprintf(report, "route line=%u step=\"%s\" floor=%03lx status=%s reason=%s\n", number, p,
                (unsigned long)scene->floor, ok ? "ok" : "failed", ok ? "" : error);
        fflush(report);
        if (!ok) { ++failures; break; }
    }
    fclose(route);
    unload(); free(scene); c3_game_state_free(&game); free(pda_data);
    printf("story-route: steps=%u failures=%u\n", steps, failures);
    if (report != stdout) fclose(report);
    return failures ? 1 : 0;
}
