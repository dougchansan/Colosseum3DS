#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/field_story.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Optional PRIVATE-DATA story crawler. Starting from a player's own save on
 * one floor and entrance, it plays the original scripts headlessly with ONE
 * persistent game state, the way a player who talks to everyone would:
 *
 *   arrival -> every visible person (talk), every kind-2 touch surface,
 *   every kind-3 A-button surface -> when a whole pass changes no flag, a
 *   door (kind 1) to the floor visited longest ago -> the floor the door's
 *   script requests, at the entrance it requests.
 *
 * Floor changes requested by any script (arrival, talk, touch) are followed
 * when the boot directory holds that floor's packs; otherwise the crawl
 * records the exit and goes back. Battles are reported as the hero's win
 * (result 2, the field scene's "skip as win"); shops close at once; the
 * partner keyboard answers "RUI". Dialogue is advanced with A and every
 * shown text is written with its ID, so unknown control codes are visible.
 * Each flag a script changes is written as "crawl_flag".
 *
 * usage: test_story_crawl <newgame dir> <boot dir> <save.gci> <floor hex>
 *        <entrance> <report> [out.gci] [steps]
 * Without arguments it checks nothing private. */
static char error[C3_STORY_ERROR];
static FILE *trace, *report;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s (%s)\n",__FILE__,__LINE__,#x,error); exit(1); } } while (0)
#define FLAGS 3593u

typedef struct Scene {
    C3FieldStory story;
    C3FieldMap map;
    C3Area area;
    C3FieldPlayer hero;
    C3Title wes, npc;
    C3Title *models[C3_AREA_MAX_ACTORS];
    C3AreaCamera camera;
    C3AreaDoors doors;
    uint32_t floor;
    int loaded;
} Scene;

static const char *newgame, *root;
static C3GameState game;
static uint32_t flags[FLAGS];
static unsigned battles, messages, flag_changes, steps_left = 400;
static uint32_t last_visit[0x400];
static unsigned clock_tick, loads_since_change;
static uint32_t current_floor;
static uint32_t door_turn[0x400];

static void path(char out[1024], const char *dir, const char *name)
{ CHECK(snprintf(out, 1024, "%s/%s", dir, name) > 0 && strlen(out) < 1023); }
static int any_floor(uint32_t floor) { return floor >= 1 && floor <= 0xffff; }
/* Items received this run (the bag itself needs the PDA item data). */
static uint32_t received[256]; static unsigned received_count;
static int receive(void *c, uint32_t item, uint32_t count, char *name, size_t capacity)
{
    (void)c; (void)count;
    if (received_count < 256) received[received_count++] = item;
    snprintf(name, capacity, "ITEM %lu", (unsigned long)item);
    fprintf(report, "crawl_item item=%lu count=%lu\n", (unsigned long)item, (unsigned long)count);
    return 0;
}
static int restore(void *c) { (void)c; return 6; }
static int species_name(void *c, unsigned species, char *out, size_t capacity)
{ (void)c; snprintf(out, capacity, "SPECIES %u", species); return 1; }
/* scriptAddItem (native DB) and the DNA disc exchange: signed change. */
static int change(void *c, uint32_t item, int32_t count)
{
    (void)c;
    if (count > 0) { if (received_count < 256) received[received_count++] = item; }
    else for (unsigned i = 0; i < received_count; ++i)
        if (received[i] == item) { received[i] = received[--received_count]; break; }
    fprintf(report, "crawl_item_change item=%lu count=%ld\n", (unsigned long)item, (long)count);
    return 0;
}
static int holds(uint32_t id)
{
    C3GameItem item;
    for (unsigned i = 0; i < received_count; ++i) if (received[i] == id) return 1;
    for (unsigned pocket = 0; pocket < 8; ++pocket)
        for (unsigned slot = 0; slot < 256 && c3_game_state_item(&game, pocket, slot, &item); ++slot)
            if (item.id == id && item.quantity) return 1;
    return 0;
}
static int prepared(uint32_t floor)
{
    char file[1024], name[64];
    snprintf(name, sizeof name, "f%03lx.c3s", (unsigned long)floor); path(file, root, name);
    FILE *f = fopen(file, "rb");
    if (!f) return 0;
    fclose(f);
    return 1;
}

static void le32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static uint32_t bits(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
/* The one-vertex stand-in model of test_world_story.c. */
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
{
    for (uint32_t id = 1; id < FLAGS; ++id) { uint32_t v = 0; c3_game_state_get_flag(&game, id, &v); flags[id] = v; }
}
/* Writes every flag that changed since the last snapshot; returns the count. */
static unsigned diff(const char *what)
{
    unsigned n = 0;
    for (uint32_t id = 1; id < FLAGS; ++id) {
        uint32_t v = 0;
        c3_game_state_get_flag(&game, id, &v);
        if (v != flags[id]) {
            fprintf(report, "crawl_flag floor=%03lx after=%s flag=%03lx %lu->%lu\n", (unsigned long)current_floor, what,
                    (unsigned long)id, (unsigned long)flags[id], (unsigned long)v);
            flags[id] = v; ++n;
        }
    }
    flag_changes += n;
    if (n) loads_since_change = 0;
    return n;
}

static void close_scene(Scene *s)
{
    if (!s->loaded) return;
    c3_field_story_close(&s->story); c3_field_close(&s->map); c3_area_close(&s->area);
    c3_title_close(&s->wes); c3_title_close(&s->npc);
    memset(s, 0, sizeof *s);
}

/* Ticks until the hero has control again. Returns 0 on a script failure. */
static int run(Scene *s, unsigned frames, uint32_t *next, uint32_t *spawn)
{
    uint32_t shown = 0;
    for (unsigned frame = 0; frame < frames; ++frame) {
        const C3FieldStoryView *v = c3_field_story_view(&s->story);
        if (v->next_floor && !*next) { *next = v->next_floor; *spawn = v->next_spawn; }
        if (v->dialogue && v->message_id != shown) {
            char text[C3_STORY_MESSAGE];
            shown = v->message_id; ++messages;
            snprintf(text, sizeof text, "%s", v->message);
            for (char *c = text; *c; ++c) if (*c == '\n' || *c == '\r') *c = '|';
            fprintf(report, "crawl_text floor=%03lx id=%05lx text=%s\n", (unsigned long)s->floor,
                    (unsigned long)v->message_id, text);
        }
        if (!v->dialogue) shown = 0;
        if (frame > 30 && v->control && !v->dialogue) return 1;
        if (*next && frame > 30 && !v->dialogue && v->fade >= 1.0f) return 1;
        uint16_t pressed = (v->dialogue || v->choice) && frame % 15 == 0 ? 0x100 : 0;
        uint32_t encounter;
        if (c3_field_story_battle_pending(&s->story, &encounter)) {
            ++battles;
            fprintf(report, "crawl_battle floor=%03lx encounter=%lu result=2 skipped_as_win=1\n",
                    (unsigned long)s->floor, (unsigned long)encounter);
            if (!c3_field_story_battle_result(&s->story, 2, error)) return 0;
        }
        if (v->shop && !c3_field_story_shop_done(&s->story, error)) return 0;
        if (v->name_entry && !c3_field_story_name_result(&s->story, "RUI", error)) return 0;
        if (v->item_menu) {
            /* Use the first registered item the hero holds, else close. */
            uint32_t items[8], use = 0;
            unsigned n = c3_field_story_item_menu_items(&s->story, items);
            for (unsigned i = 0; i < n && !use; ++i) if (holds(items[i])) use = items[i];
            fprintf(report, "crawl_item_menu floor=%03lx registered=%u used=%lu\n", (unsigned long)s->floor, n, (unsigned long)use);
            if (!c3_field_story_item_menu_result(&s->story, use, error)) return 0;
        }
        if (!c3_field_story_tick(&s->story, 1.0f/60.0f, pressed, error) || !c3_field_story_sample(&s->story, error)) return 0;
    }
    if (*next) return 1;
    char waits[512]; c3_field_story_describe_waits(&s->story, waits, sizeof waits);
    fprintf(report, "crawl_wait floor=%03lx waits=%s\n", (unsigned long)s->floor, waits);
    return 1;
}

/* Loads a floor at an entrance and plays its arrival. */
static int load(Scene *s, uint32_t floor, uint32_t spawn, uint32_t *next, uint32_t *next_spawn)
{
    char file[1024], name[64];
    C3GameSummary summary;
    CHECK(c3_game_state_summary(&game, &summary));
    CHECK(c3_game_state_set_floor(&game, floor, summary.floor, spawn));
    s->floor = floor; current_floor = floor;
    snprintf(name, sizeof name, "f%03lx.c3f", (unsigned long)floor); path(file, root, name);
    if (!c3_field_load(&s->map, file, error, sizeof error)) return 0;
    snprintf(name, sizeof name, "f%03lx.c3a", (unsigned long)floor); path(file, root, name);
    if (!c3_area_load(&s->area, file, error, sizeof error)) { c3_field_close(&s->map); return 0; }
    stand_in(&s->wes); stand_in(&s->npc);
    for (uint32_t i = 0; i < s->area.actor_count && i < C3_AREA_MAX_ACTORS; ++i) s->models[i] = &s->npc;
    if (!c3_field_spawn(&s->map, spawn, &s->hero) && !c3_field_spawn(&s->map, 0, &s->hero)) {
        snprintf(error, sizeof error, "entrance %lu is unavailable", (unsigned long)spawn); return 0;
    }
    s->loaded = 1;
    snprintf(name, sizeof name, "f%03lx.c3s", (unsigned long)floor); path(file, root, name);
    if (!c3_field_story_load(&s->story, file, &game, &s->map, &s->area, &s->hero, &s->wes, s->models, &s->camera, trace, error)) return 0;
    snprintf(name, sizeof name, "f%03lx.c3d", (unsigned long)floor); path(file, root, name);
    FILE *door_file = fopen(file, "rb");
    if (door_file) {
        fclose(door_file);
        if (!c3_area_doors_load(&s->doors, file, error, sizeof error)) return 0;
        static C3Title *const none[C3_AREA_MAX_DOORS];
        if (!c3_field_story_bind_doors(&s->story, s->doors.rows, s->doors.count, none, error)) return 0;
    }
    snprintf(name, sizeof name, "f%03lx.c3r", (unsigned long)floor); path(file, root, name);
    if (!c3_field_story_load_treasure(&s->story, file, error)) return 0;
    c3_field_story_set_floor_support(&s->story, any_floor);
    c3_field_story_enable_name_entry(&s->story, 1);
    c3_field_story_enable_item_menu(&s->story, 1);
    c3_field_story_set_item_service(&s->story, receive, NULL);
    c3_field_story_set_item_change_service(&s->story, change, NULL);
    c3_field_story_set_species_namer(&s->story, species_name, NULL);
    c3_field_story_set_party_service(&s->story, restore, NULL);
    c3_field_story_set_random(&s->story, c3_game_state_random16, &game);
    if (!c3_field_story_prepare(&s->story, error)) return 0;
    for (unsigned f = 0; f < 3600 && !c3_field_story_view(&s->story)->prepared; ++f)
        if (!c3_field_story_tick(&s->story, 1.0f/60.0f, 0, error)) return 0;
    if (!c3_field_story_view(&s->story)->prepared) { snprintf(error, sizeof error, "preparation did not finish"); return 0; }
    if (!c3_field_story_enter(&s->story, error)) return 0;
    fprintf(report, "crawl_floor floor=%03lx entrance=%lu\n", (unsigned long)floor, (unsigned long)spawn);
    last_visit[floor & 0x3ff] = ++clock_tick;
    return run(s, 3600, next, next_spawn);
}

enum { TALK, TOUCH, SEARCH, TREASURE, REGION, GAZE, DOOR };
/* Common scripts that change floor: 05960004 (door crossing), 05960006
 * (elevator touch). These are the crawl's exploration choices. */
static int moves(const C3AreaLink *l) { return l->script == 0x05960004u || l->script == 0x05960006u; }
static const char *const KIND[] = {"talk", "touch", "search", "treasure", "region", "gaze", "door"};

/* One event on the loaded floor. Returns -1 failure, 0 not started, 1 run. */
static int event(Scene *s, int kind, uint32_t index, uint32_t *next, uint32_t *spawn)
{
    const C3FieldStoryView *v = c3_field_story_view(&s->story);
    if (!v->control) return 0;
    if (kind == TALK) {
        C3StoryActor *a = c3_field_story_actor(&s->story, s->area.actors[index].index);
        if (!a || !a->visible) return 0;
        float yaw = a->pose.yaw, reach = 0.6f;
        s->hero = a->pose;
        s->hero.position[0] += sinf(yaw) * reach; s->hero.position[2] += cosf(yaw) * reach;
        s->hero.yaw = yaw + 3.14159265f;
        memcpy(s->hero.proposed, s->hero.position, sizeof s->hero.position);
        if (!c3_field_story_interact(&s->story, error)) return -1;
    } else if (kind == SEARCH) {
        const C3FieldTriangle *t = s->area.interactions + index;
        float c[3] = {0, 0, 0}, nx = t->normal[0], nz = t->normal[2], len = sqrtf(nx*nx + nz*nz);
        for (unsigned i = 0; i < 9; ++i) c[i % 3] += t->vertices[i] / 3.0f;
        if (len < 1e-3f) return 0;
        nx /= len; nz /= len;
        /* Stand half a unit in front of the surface (either side) facing it. */
        for (int side = 0; side < 2 && v->control; ++side) {
            float d = side ? -0.5f : 0.5f;
            s->hero.position[0] = c[0] + nx * d; s->hero.position[1] = c[1] - 0.5f; s->hero.position[2] = c[2] + nz * d;
            s->hero.yaw = atan2f(c[0] - s->hero.position[0], c[2] - s->hero.position[2]);
            memcpy(s->hero.proposed, s->hero.position, sizeof s->hero.position);
            if (!c3_field_story_interact(&s->story, error)) return -1;
        }
    } else if (kind == TREASURE) {
        const C3StoryTreasure *r = c3_field_story_treasure(&s->story, index);
        if (!r || !r->visible || r->taken) return 0;
        s->hero.position[0] = r->position[0] + sinf(r->yaw) * 0.8f; s->hero.position[1] = r->position[1];
        s->hero.position[2] = r->position[2] + cosf(r->yaw) * 0.8f;
        s->hero.yaw = atan2f(r->position[0] - s->hero.position[0], r->position[2] - s->hero.position[2]);
        memcpy(s->hero.proposed, s->hero.position, sizeof s->hero.position);
        if (!c3_field_story_interact(&s->story, error)) return -1;
    } else if (kind == GAZE) {
        /* Stand in a person's line of sight (native A9, peopleGazeHeroCheck:
         * within 30 units and 15 degrees of its facing), a trainer's spot. */
        C3StoryActor *a = c3_field_story_actor(&s->story, s->area.actors[index].index);
        if (!a || !a->visible) return 0;
        float yaw = a->pose.yaw, reach = 2.0f;
        s->hero = a->pose;
        s->hero.position[0] += sinf(yaw) * reach; s->hero.position[2] += cosf(yaw) * reach;
        s->hero.yaw = yaw + 3.14159265f;
        memcpy(s->hero.proposed, s->hero.position, sizeof s->hero.position);
        for (unsigned f = 0; f < 20 && c3_field_story_view(&s->story)->control; ++f)
            if (!c3_field_story_tick(&s->story, 1.0f/60.0f, 0, error) || !c3_field_story_sample(&s->story, error)) return -1;
    } else if (kind == REGION) {
        /* Step into a box the floor's main loop tests the hero against
         * (native 3A) and give the polling scripts a moment. */
        float boxes[32][4];
        unsigned n = c3_field_story_hero_regions(&s->story, boxes, 32);
        if (index >= n) return 0;
        s->hero.position[0] = (boxes[index][0] + boxes[index][2]) / 2;
        s->hero.position[2] = (boxes[index][1] + boxes[index][3]) / 2;
        memcpy(s->hero.proposed, s->hero.position, sizeof s->hero.position);
        for (unsigned f = 0; f < 20 && c3_field_story_view(&s->story)->control; ++f)
            if (!c3_field_story_tick(&s->story, 1.0f/60.0f, 0, error) || !c3_field_story_sample(&s->story, error)) return -1;
    } else if (kind == TOUCH) {
        int started = c3_field_story_touch(&s->story, index, error);
        if (started <= 0) return started;
    } else if (!c3_field_story_transition(&s->story, index, error)) return -1;
    if (c3_field_story_view(&s->story)->control) return 0;
    return run(s, 7200, next, spawn) ? 1 : -1;
}

int main(int argc, char **argv)
{
    if (argc < 7) { puts("story-crawl: private integration skipped (needs <newgame dir> <boot dir> <save.gci> <floor> <entrance> <report>)"); return 0; }
    newgame = argv[1]; root = argv[2];
    uint32_t floor = (uint32_t)strtoul(argv[4], NULL, 16), spawn = (uint32_t)strtoul(argv[5], NULL, 10);
    report = fopen(argv[6], "w"); CHECK(report);
    if (argc > 8) steps_left = (unsigned)strtoul(argv[8], NULL, 10);
    char file[1024], gerror[C3_GAME_ERROR_SIZE] = {0};
    path(file, newgame, "newgame.c3g");
    CHECK(c3_game_state_load(&game, argv[3], file, gerror));
    trace = fopen(getenv("C3_CRAWL_TRACE") ? getenv("C3_CRAWL_TRACE") : "/dev/null", "w"); CHECK(trace);
    /* C3_CRAWL_FLAGS="884=1,885=1": flags another engine part writes (for
     * example the battle engine's Shadow Pokemon rows) preset, and recorded. */
    for (const char *p = getenv("C3_CRAWL_FLAGS"); p && *p;) {
        char *end; unsigned long id = strtoul(p, &end, 16), value = 1;
        if (*end == '=') value = strtoul(end + 1, &end, 0);
        CHECK(c3_game_state_set_flag(&game, (uint32_t)id, (uint32_t)value));
        fprintf(report, "crawl_preset flag=%03lx value=%lu\n", id, value);
        p = *end == ',' ? end + 1 : end;
        if (*p && *p != ',' && !(*p >= '0' && *p <= '9') && !(*p >= 'a' && *p <= 'f')) break;
    }
    snapshot();
    Scene *s = calloc(1, sizeof *s); CHECK(s);
    uint32_t back_floor = 0, back_spawn = 0;
    int stop = 0;
    while (!stop && steps_left) {
        uint32_t next = 0, next_spawn = 0;
        close_scene(s);
        if (!load(s, floor, spawn, &next, &next_spawn)) { fprintf(report, "crawl_fail floor=%03lx step=load reason=%s\n", (unsigned long)floor, error); break; }
        diff("arrival");
        /* Events on this floor until one leaves it or a pass changes nothing. */
        int moved = 0;
        while (!next && !moved && steps_left) {
            unsigned changed = 0;
            for (int kind = TALK; kind <= GAZE && !next; ++kind) {
                float boxes[32][4];
                uint32_t count = kind == TALK ? s->area.actor_count : kind == TOUCH ? s->area.link_count :
                                 kind == SEARCH ? s->area.interaction_count : kind == TREASURE ? c3_field_story_treasure_count(&s->story) :
                                 kind == REGION ? c3_field_story_hero_regions(&s->story, boxes, 32) : s->area.actor_count;
                for (uint32_t i = 0; i < count && !next && steps_left; ++i) {
                    uint32_t id = i;
                    if (kind == TALK && !s->area.actors[i].talk_script) continue;
                    if (kind == TOUCH) { if (s->area.links[i].kind != 2 || moves(s->area.links + i)) continue; id = s->area.links[i].source_id; }
                    if (kind == SEARCH) {
                        unsigned seen = 0; /* one try per surface ID */
                        for (uint32_t j = 0; j < i; ++j) seen |= s->area.interactions[j].source_id == s->area.interactions[i].source_id;
                        if (seen || !c3_area_link(&s->area, 3, s->area.interactions[i].source_id)) continue;
                    }
                    int r = event(s, kind, id, &next, &next_spawn);
                    if (r < 0) { fprintf(report, "crawl_fail floor=%03lx step=%s id=%lu reason=%s\n", (unsigned long)floor, KIND[kind], (unsigned long)id, error); stop = 1; break; }
                    if (!r) continue;
                    --steps_left;
                    char what[48]; snprintf(what, sizeof what, "%s:%lu", KIND[kind], (unsigned long)id);
                    unsigned n = diff(what);
                    fprintf(report, "crawl_event floor=%03lx %s flags=%u next=%03lx\n", (unsigned long)floor, what, n, (unsigned long)next);
                    changed += n;
                    if (c3_field_story_view(&s->story)->blocked[0]) {
                        fprintf(report, "crawl_blocked floor=%03lx %s reason=%s\n", (unsigned long)floor, what, c3_field_story_view(&s->story)->blocked);
                    }
                }
                if (stop) break;
            }
            if (stop || next) break;
            if (changed) continue;
            /* Nothing new here: the next door of this floor in turn. */
            uint32_t best = door_turn[floor & 0x3ff]++;
            uint32_t tried = 0;
            for (uint32_t i = 0; i < s->area.link_count && !next; ++i) {
                const C3AreaLink *l = s->area.links + (best + i) % s->area.link_count;
                if (l->kind != 1 && !moves(l)) continue;
                /* A door whose destination (the common door script's first
                 * argument) has no packs would only come straight back. */
                if (l->script == 0x05960004u && l->args[0] && l->args[0] != 0x38e && !prepared(l->args[0])) continue;
                ++tried;
                int r = event(s, l->kind == 1 ? DOOR : TOUCH, l->source_id, &next, &next_spawn);
                if (r < 0) { fprintf(report, "crawl_fail floor=%03lx step=door id=%lu reason=%s\n", (unsigned long)floor, (unsigned long)l->source_id, error); stop = 1; break; }
                if (next && (!prepared(next) || next == 0x38e)) {
                    fprintf(report, "crawl_exit floor=%03lx door=%lu next=%03lx (not followed)\n", (unsigned long)floor, (unsigned long)l->source_id, (unsigned long)next);
                    last_visit[next & 0x3ff] = ++clock_tick;
                    next = 0; moved = 1; /* reload this floor at the same entrance */
                    back_floor = floor; back_spawn = spawn;
                    break;
                }
            }
            if (!tried) { fprintf(report, "crawl_end floor=%03lx reason=no door\n", (unsigned long)floor); stop = 1; }
        }
        if (next) {
            if (!prepared(next)) { fprintf(report, "crawl_exit floor=%03lx next=%03lx (no packs)\n", (unsigned long)floor, (unsigned long)next); floor = back_floor ? back_floor : floor; spawn = back_floor ? back_spawn : spawn; continue; }
            floor = next; spawn = next_spawn;
        }
        /* An epoch with no flag change anywhere ends the crawl after every
         * floor was revisited once. */
        if (++loads_since_change > 40) { fprintf(report, "crawl_end reason=no flag change in 40 floor loads\n"); break; }
    }
    fprintf(report, "crawl_done floors=%u flag_changes=%u battles=%u texts=%u steps_left=%u\n", clock_tick, flag_changes, battles, messages, steps_left);
    if (argc > 7) {
        C3GameSummary summary;
        CHECK(c3_game_state_summary(&game, &summary));
        CHECK(c3_game_state_set_floor(&game, floor, summary.previous_floor, spawn));
        CHECK(c3_game_state_save(&game, argv[7], gerror));
    }
    printf("story-crawl: floors=%u flag_changes=%u battles=%u texts=%u\n", clock_tick, flag_changes, battles, messages);
    close_scene(s); free(s);
    c3_game_state_free(&game);
    fclose(report); fclose(trace);
    return 0;
}
