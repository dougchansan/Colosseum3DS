#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/field_story.h"
#include "colosseum3ds/inventory.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Private Under integration audit. Original packs and saves are required.
 * Each arrival, door, touch and talk starts from a fresh imported checkpoint;
 * explicitly logged earlier subway trainers may run before the target talk.
 * No save is written. Inventory/party services are production implementations.
 * Hero placement, automatic dialogue/default choices and synthetic battle wins
 * are reported shortcuts.
 * This proves script execution at checkpoints, not continuous story completion.
 * usage: test_under_story <newgame dir> <boot dir> <save.gci> [report] [trace] [--walkable]
 */
static char error[C3_STORY_ERROR];
static FILE *trace, *report;
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s (%s)\n",__FILE__,__LINE__,#x,error); exit(1); } } while (0)

enum { ARRIVAL, DOOR, TOUCH, TALK };
static const char *const KIND[] = {"arrival", "door", "touch", "talk"};

typedef struct Scene {
    C3FieldStory story;
    C3FieldMap map;
    C3Area area;
    C3FieldPlayer hero;
    C3Title wes, actors[C3_AREA_MAX_ACTORS];
    C3Title *models[C3_AREA_MAX_ACTORS];
    C3AreaCamera camera, cameras[7];
    unsigned camera_count;
    C3AreaDoors doors;
    C3Title door_models[C3_AREA_MAX_DOORS];
    C3Title *door_ptrs[C3_AREA_MAX_DOORS];
} Scene;

static const char *newgame, *root, *save;
static unsigned totals[5]; /* ok, failed, blocked, partial, skipped */
static unsigned synthetic_battles;
static int walkable_only;
static C3PdaAssets assets;

static void path(char out[1024], const char *dir, const char *name)
{ CHECK(snprintf(out, 1024, "%s/%s", dir, name) > 0 && strlen(out) < 1023); }
static int any_floor(uint32_t floor) { return floor >= 1 && floor <= 0xffff; }
static int present(const char *file)
{ FILE *f = fopen(file, "rb"); if (!f) return 0; fclose(f); return 1; }
static int receive(void *c, uint32_t item, uint32_t count, char *name, size_t capacity)
{
    C3GameState *game=c; C3GameSummary summary; C3InventoryItem info;
    if (!c3_game_state_summary(game,&summary) || !c3_inventory_item(&assets,item,&info) ||
        !c3_pda_text(&assets,info.name,summary.name,summary.partner,name,capacity)) return -1;
    return c3_inventory_receive(game,&assets,item,count);
}
static int change_item(void *c,uint32_t item,int32_t count)
{ return count<0?c3_inventory_remove(c,&assets,item,(unsigned)-count):c3_inventory_receive(c,&assets,item,(unsigned)count); }
static int restore(void *c) { return c3_inventory_restore_party(c,&assets); }
static int clear_poison(void *c) { return c3_inventory_clear_party_poison(c); }

/* Run the scene until the hero has control again (or the frame budget is
 * spent). Returns 0 on a script failure with the reason in error. */
static int run(Scene *s, unsigned frames, unsigned *used, uint32_t *next)
{
    for (unsigned frame = 0; frame < frames; ++frame) {
        const C3FieldStoryView *v = c3_field_story_view(&s->story);
        if (v->next_floor && !*next) *next = v->next_floor;
        if (v->next_floor || (frame > 30 && v->control && !v->dialogue && !v->choice)) { *used = frame; return 1; }
        uint16_t pressed = (v->dialogue || v->choice) && frame % 15 == 0 ? 0x100 : 0;
        uint32_t encounter;
        if (c3_field_story_battle_pending(&s->story, &encounter)) {
            ++synthetic_battles;
            fprintf(report, "under_battle encounter=%lu result=2 synthetic=1\n", (unsigned long)encounter);
            if (!c3_field_story_battle_result(&s->story, 2, error)) return 0;
        }
        if (v->shop && !c3_field_story_shop_done(&s->story, error)) return 0;
        if (v->name_entry && !c3_field_story_name_result(&s->story, "WES", error)) return 0;
        if (!c3_field_story_tick(&s->story, 1.0f/60.0f, pressed, error) || !c3_field_story_sample(&s->story, error)) return 0;
    }
    *used = frames;
    char waits[512]; c3_field_story_describe_waits(&s->story, waits, sizeof waits);
    fprintf(trace, "under_timeout waits=%s\n", waits);
    snprintf(error, sizeof error, "script exceeded %u frames without control or connector", frames);
    return 0;
}

/* One event on one floor in a fresh scene loaded from the save. */
static void event_attempt(const char *prefix, uint32_t floor, int kind, uint32_t id, uint32_t script, unsigned placement)
{
    Scene *s = calloc(1, sizeof *s);
    C3GameState game = {0};
    C3GameSummary summary;
    char file[1024], name[64], gerror[C3_GAME_ERROR_SIZE] = {0};
    const char *status = "failed";
    unsigned frames = 0;
    uint32_t next = 0;
    uint32_t before[0x1000] = {0};
    CHECK(s);
    error[0] = 0;
    fprintf(trace, "under_event floor=%03lx kind=%s id=%lu script=%08lx\n",
            (unsigned long)floor, KIND[kind], (unsigned long)id, (unsigned long)script);
    path(file, newgame, "newgame.c3g");
    CHECK(c3_game_state_load(&game, save, file, gerror));
    CHECK(c3_game_state_summary(&game, &summary));
    for (unsigned i = 0; i < 0x1000; ++i) c3_game_state_get_flag(&game, i, before + i);
    fprintf(report, "under_checkpoint save=%s saved_floor=%03lx event_floor=%03lx event=%s id=%lu\n", save, (unsigned long)summary.floor, (unsigned long)floor, KIND[kind], (unsigned long)id);
    CHECK(c3_game_state_set_floor(&game, floor, summary.floor, 0));
    snprintf(name, sizeof name, "%s.c3f", prefix); path(file, root, name);
    if (!c3_field_load(&s->map, file, error, sizeof error)) goto done;
    snprintf(name, sizeof name, "%s.c3a", prefix); path(file, root, name);
    if (!c3_area_load(&s->area, file, error, sizeof error)) goto done;
    snprintf(name, sizeof name, "%s_wes.c3t", prefix); path(file, root, name);
    if (!c3_title_load(&s->wes, file, error, sizeof error)) goto done;
    for (uint32_t i = 0; i < s->area.actor_count; ++i) {
        if (!s->area.actors[i].file[0]) continue;
        path(file, root, s->area.actors[i].file);
        if (!c3_title_load(s->actors + i, file, error, sizeof error)) goto done;
        s->models[i] = s->actors + i;
    }
    snprintf(name, sizeof name, "%s_camera.c3c", prefix); path(file, root, name);
    FILE *camera_file = fopen(file, "rb");
    if (camera_file) {
        fclose(camera_file);
        if (!c3_area_camera_load(&s->camera, file, error, sizeof error)) goto done;
        fprintf(trace, "under_camera floor=%03lx resource=%08lx frames=%lu\n",
                (unsigned long)floor, (unsigned long)s->camera.resource, (unsigned long)s->camera.frame_count);
    }
    for (unsigned i = 1; i <= 7; ++i) {
        snprintf(name, sizeof name, "%s_camera%u.c3c", prefix, i); path(file, root, name);
        if (!present(file)) break;
        if (!c3_area_camera_load(s->cameras + s->camera_count, file, error, sizeof error)) goto done;
        ++s->camera_count;
    }
    snprintf(name, sizeof name, "%s.c3d", prefix); path(file, root, name);
    if (present(file) && !c3_area_doors_load(&s->doors, file, error, sizeof error)) goto done;
    for (unsigned i = 0; i < s->doors.count; ++i) {
        if (!s->doors.rows[i].file[0]) continue;
        path(file, root, s->doors.rows[i].file);
        if (!c3_title_load(s->door_models + i, file, error, sizeof error)) goto done;
        s->door_ptrs[i] = s->door_models + i;
    }
    if (!c3_field_spawn(&s->map, 0, &s->hero)) { snprintf(error, sizeof error, "entrance 0 is unavailable"); goto done; }
    snprintf(name, sizeof name, "%s.c3s", prefix); path(file, root, name);
    if (!c3_field_story_load(&s->story, file, &game, &s->map, &s->area, &s->hero, &s->wes, s->models, &s->camera, trace, error)) goto done;
    if (!c3_field_story_bind_doors(&s->story, s->doors.rows, s->doors.count, s->door_ptrs, error)) goto done;
    c3_field_story_bind_cameras(&s->story, s->cameras, s->camera_count);
    c3_field_story_set_floor_support(&s->story, any_floor);
    c3_field_story_enable_name_entry(&s->story, 1);
    c3_field_story_set_item_service(&s->story, receive, &game);
    c3_field_story_set_item_change_service(&s->story, change_item, &game);
    c3_field_story_set_party_service(&s->story, restore, &game);
    c3_field_story_set_poison_service(&s->story, clear_poison, &game);
    if (!c3_field_story_prepare(&s->story, error)) goto done;
    for (frames = 0; frames < 3600 && !c3_field_story_view(&s->story)->prepared; ++frames)
        if (!c3_field_story_tick(&s->story, 1.0f/60.0f, 0, error)) goto done;
    if (!c3_field_story_view(&s->story)->prepared) { snprintf(error, sizeof error, "preparation did not finish"); goto done; }
    if (!c3_field_story_enter(&s->story, error)) goto done;
    if (!run(s, 1800, &frames, &next)) goto done;
    if (kind != ARRIVAL) {
        if (!c3_field_story_view(&s->story)->control) {
            /* An arrival that moves straight on to another floor never
             * hands control back here: the floor's other events are not
             * reachable from this entrance. */
            if (next) { status = "skipped"; snprintf(error, sizeof error, "arrival leaves for floor %03lx", (unsigned long)next); }
            else { status = "blocked"; snprintf(error, sizeof error, "arrival kept control"); }
            goto done;
        }
        next = 0;
        if (kind == DOOR) {
            if (!c3_field_story_transition(&s->story, id, error)) goto done;
        } else if (kind == TOUCH) {
            int started = c3_field_story_touch(&s->story, id, error);
            if (started < 0) goto done;
            if (!started) { status = "skipped"; snprintf(error, sizeof error, "%s", "not started"); goto done; }
        } else {
            C3StoryActor *actor = c3_field_story_actor(&s->story, id);
            if (!actor || !actor->visible) { status = "skipped"; snprintf(error, sizeof error, "actor not present"); goto done; }
            const C3AreaActor *source = NULL;
            for (unsigned i = 0; i < s->area.actor_count; ++i)
                if (s->area.actors[i].index == id) source = s->area.actors + i;
            if (!source || source->talk_start == 3 || (actor->flags & 1)) {
                status = "skipped";
                snprintf(error, sizeof error, "actor is not eligible for manual talk in this checkpoint");
                goto done;
            }
            /* The subway's original approach script can select an earlier
             * trainer first. Finish that actual conversation before retrying
             * this actor in the same scene/game; never grant its completion
             * flag or discard it by reimporting the checkpoint. */
            for (unsigned prerequisites = 0; ; ++prerequisites) {
                actor = c3_field_story_actor(&s->story, id);
                if (!actor || !actor->visible) {
                    snprintf(error, sizeof error, "intended actor vanished after prerequisite talk");
                    goto done;
                }
                float yaw = actor->pose.yaw + placement * .785398163f, reach = 0.6f;
                s->hero = actor->pose;
                s->hero.position[0] += sinf(yaw) * reach;
                s->hero.position[2] += cosf(yaw) * reach;
                s->hero.yaw = atan2f(actor->pose.position[0] - s->hero.position[0],
                                    actor->pose.position[2] - s->hero.position[2]);
                memcpy(s->hero.proposed, s->hero.position, sizeof s->hero.position);
                CHECK(fflush(trace) == 0);
                long start = ftell(trace);
                if (!c3_field_story_interact(&s->story, error)) goto done;
                CHECK(fflush(trace) == 0);
                long end = ftell(trace);
                char expected[96], line[1024]; int matched = 0, selected = 0;
                unsigned long actual_actor = 0, actual_script = 0;
                snprintf(expected, sizeof expected, "story_talk actor=%lu script=%08lx",
                         (unsigned long)id, (unsigned long)script);
                CHECK(start >= 0 && end >= start && fseek(trace, start, SEEK_SET) == 0);
                while (ftell(trace) < end && fgets(line, sizeof line, trace)) {
                    if (strstr(line, expected)) matched = 1;
                    if (sscanf(line, "story_talk actor=%lu script=%lx", &actual_actor, &actual_script) == 2)
                        selected = 1;
                }
                CHECK(fseek(trace, end, SEEK_SET) == 0);
                if (matched) break;
                /* Only the reviewed ordered subway trainers qualify as real
                 * prerequisites. An unrelated nearby talk remains a failure. */
                if (floor != 0x3a || id < 2 || id > 4 || !selected ||
                    actual_actor < 1 || actual_actor >= id || prerequisites >= 4) {
                    status = placement < 7 ? "retry" : "failed";
                    snprintf(error, sizeof error, "intended talk script was not started with placement %u", placement);
                    goto done;
                }
                fprintf(report, "under_prerequisite floor=%03lx intended_actor=%lu actual_actor=%lu script=%08lx attempt=%u original_flags=1 synthetic_battle_if_requested=1\n",
                        (unsigned long)floor, (unsigned long)id, actual_actor, actual_script, prerequisites + 1);
                if (!run(s, 3600, &frames, &next)) goto done;
                if (next || !c3_field_story_view(&s->story)->control) {
                    snprintf(error, sizeof error, "prerequisite talk did not return control on the intended floor");
                    goto done;
                }
            }
        }
        if (!run(s, 3600, &frames, &next)) goto done;
    }
    status = c3_field_story_view(&s->story)->blocked[0] ? "blocked" : "ok";
    if (!strcmp(status, "blocked")) snprintf(error, sizeof error, "%s", c3_field_story_view(&s->story)->blocked);
    /* A native menu program that needed an unported menu took the
     * original cancel branch: the event ran, but not every branch. */
    else if (c3_field_story_view(&s->story)->unported_menu) {
        status = "partial";
        snprintf(error, sizeof error, "menu step of native %02lx is not ported (cancel branch taken)",
                 (unsigned long)c3_field_story_view(&s->story)->unported_menu);
    }
done:
    for (unsigned i = 0; i < 0x1000; ++i) {
        uint32_t after;
        if (strcmp(status, "retry") && c3_game_state_get_flag(&game, i, &after) && after != before[i])
            fprintf(report, "under_flag floor=%03lx event=%s id=%lu flag=%04x before=%lu after=%lu\n",
                    (unsigned long)floor, KIND[kind], (unsigned long)id, i,
                    (unsigned long)before[i], (unsigned long)after);
    }
    if (!strcmp(status, "ok")) ++totals[0];
    else if (!strcmp(status, "failed")) ++totals[1];
    else if (!strcmp(status, "blocked")) ++totals[2];
    else if (!strcmp(status, "partial")) ++totals[3];
    else if (!strcmp(status, "skipped")) ++totals[4];
    fprintf(report, "under prefix=%s floor=%03lx event=%s id=%lu script=%08lx status=%s frames=%u next=%03lx reason=%s\n",
            prefix, (unsigned long)floor, KIND[kind], (unsigned long)id, (unsigned long)script, status, frames,
            (unsigned long)next, strcmp(status, "ok") ? error : "");
    fflush(report);
    c3_field_story_close(&s->story); c3_field_close(&s->map); c3_area_close(&s->area);
    c3_area_camera_close(&s->camera);
    for (unsigned i = 0; i < s->camera_count; ++i) c3_area_camera_close(s->cameras + i);
    for (unsigned i = 0; i < C3_AREA_MAX_DOORS; ++i) c3_title_close(s->door_models + i);
    c3_title_close(&s->wes);
    for (unsigned i = 0; i < C3_AREA_MAX_ACTORS; ++i) c3_title_close(s->actors + i);
    c3_game_state_free(&game); free(s);
    if (!strcmp(status, "retry")) event_attempt(prefix, floor, kind, id, script, placement + 1);
}

static void event(const char *prefix, uint32_t floor, int kind, uint32_t id, uint32_t script)
{ event_attempt(prefix, floor, kind, id, script, 0); }


static void floor_events(const char *prefix, uint32_t floor)
{
    char file[1024], name[64];
    C3Area area = {0};
    snprintf(name, sizeof name, "%s.c3a", prefix); path(file, root, name);
    if (!c3_area_load(&area, file, error, sizeof error)) {
        fprintf(report, "under prefix=%s floor=%03lx event=arrival id=0 script=00000000 status=failed frames=0 next=000 reason=%s\n", prefix, (unsigned long)floor, error);
        ++totals[1]; return;
    }
    if (area.floor != floor) {
        fprintf(report, "under prefix=%s floor=%03lx event=arrival id=0 script=00000000 status=failed frames=0 next=000 reason=pack floor identity mismatch\n", prefix, (unsigned long)floor);
        ++totals[1]; c3_area_close(&area); return;
    }
    snprintf(name, sizeof name, "%s.c3s", prefix); path(file, root, name);
    FILE *f = fopen(file, "rb");
    if (!f) {
        fprintf(report, "under prefix=%s floor=%03lx event=arrival id=0 script=00000000 status=failed frames=0 next=000 reason=original story pack missing\n", prefix, (unsigned long)floor);
        ++totals[1]; c3_area_close(&area); return;
    }
    fclose(f);
    event(prefix, area.floor, ARRIVAL, 0, 0);
    for (uint32_t i = 0; i < area.link_count; ++i) {
        const C3AreaLink *link = area.links + i;
        if (link->kind == 1) event(prefix, area.floor, DOOR, link->source_id, link->script);
        else if (link->kind == 2) event(prefix, area.floor, TOUCH, link->source_id, link->script);
    }
    for (uint32_t i = 0; i < area.actor_count; ++i)
        if (area.actors[i].talk_script) event(prefix, area.floor, TALK, area.actors[i].index, area.actors[i].talk_script);
    c3_area_close(&area);
}

int main(int argc, char **argv)
{
    static const uint16_t floors[] = {
        0x2f,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,
        0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x40,
        0x79,0x7a,0x7d,0x7f,0x8a,0x8b,0xd3,0xd4,0xd5
    };
    if (argc < 4) {
        puts("under-story: private integration skipped (needs <newgame dir> <boot dir> <save.gci>)");
        return 0;
    }
    newgame = argv[1]; root = argv[2]; save = argv[3];
    char asset_path[1024]; path(asset_path,root,"pda.c3pd");
    FILE *asset_file=fopen(asset_path,"rb"); CHECK(asset_file);
    CHECK(!fseek(asset_file,0,SEEK_END)); long asset_size=ftell(asset_file);
    CHECK(asset_size>=64 && asset_size<=8*1024*1024 && !fseek(asset_file,0,SEEK_SET));
    void *asset_data=malloc((size_t)asset_size); CHECK(asset_data);
    CHECK(fread(asset_data,1,(size_t)asset_size,asset_file)==(size_t)asset_size);
    CHECK(!fclose(asset_file) && c3_pda_assets_open(&assets,asset_data,(size_t)asset_size));
    report = argc > 4 ? fopen(argv[4], "w") : stdout; CHECK(report);
    trace = argc > 5 ? fopen(argv[5], "w+") : tmpfile(); CHECK(trace);
    walkable_only = argc > 6 && !strcmp(argv[6], "--walkable");
    fprintf(report, "under_shortcuts hero_placement=1 dialogue_auto=1 default_choice=1 inventory=production restore=production battle=%s continuous_story=0 models=original cameras_and_doors=bound_when_present\n",
            "synthetic_win");
    {
        C3GameState checkpoint = {0}; C3GameSummary summary; char file[1024];
        path(file, newgame, "newgame.c3g");
        CHECK(c3_game_state_load(&checkpoint, save, file, error));
        CHECK(c3_game_state_summary(&checkpoint, &summary));
        fprintf(report, "under_save floor=%03lx previous=%03lx\n", (unsigned long)summary.floor, (unsigned long)summary.previous_floor);
        for (unsigned id = 0x430; id <= 0x460; ++id) {
            uint32_t value;
            if (c3_game_state_get_flag(&checkpoint, id, &value))
                fprintf(report, "under_saved_flag flag=%04x value=%lu\n", id, (unsigned long)value);
        }
        { static const uint16_t ids[] = {0xa0e, 0xdcf};
          for (unsigned i = 0; i < sizeof ids / sizeof ids[0]; ++i) {
              uint32_t value;
              CHECK(c3_game_state_get_flag(&checkpoint, ids[i], &value));
              fprintf(report, "under_saved_flag flag=%04x value=%lu\n", ids[i], (unsigned long)value);
          }
        }
        c3_game_state_free(&checkpoint);
    }
    for (unsigned i = 0; i < sizeof floors / sizeof floors[0]; ++i) {
        char prefix[16]; snprintf(prefix, sizeof prefix, "f%03x", floors[i]);
        if (walkable_only && (floors[i] == 0x3f || floors[i] == 0x40 || floors[i] == 0x8a || floors[i] == 0x8b || floors[i] >= 0xd3)) {
            fprintf(report, "under_unqualified floor=%03x reason=non_field_set_requires_dedicated_runtime\n", floors[i]);
            continue;
        }
        floor_events(prefix, floors[i]);
    }
    fprintf(report, "under_summary floors=%u ok=%u failed=%u blocked=%u partial=%u skipped=%u synthetic_battles=%u\n",
            (unsigned)(sizeof floors / sizeof floors[0]), totals[0], totals[1], totals[2], totals[3], totals[4], synthetic_battles);
    printf("under-story: ok=%u failed=%u blocked=%u partial=%u\n", totals[0], totals[1], totals[2], totals[3]);
    fclose(trace);
    if (report != stdout) CHECK(fclose(report) == 0);
    free(asset_data);
    return totals[1] || totals[2] || totals[3] ? 1 : 0;
}
