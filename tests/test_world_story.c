#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/field_story.h"

#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Optional PRIVATE-DATA whole-game story test. For every floor that
 * tools/prepare_world.py prepared (f%03x.c3f/.c3a/.c3s, no models) it loads
 * a player's own save, places the hero on that floor and runs the original
 * scripts headlessly: the arrival (load/init/pre/main), then each door link,
 * each touch link and each NPC conversation in a fresh scene. Dialogue is
 * advanced with A, choices take the default, battles are reported as the
 * hero's win (result 2) and shops close at once. Nothing is rendered.
 *
 * Every event ends in one line "world floor=.. event=.. status=.. reason=.."
 * so the report groups what the port still needs (unported natives, missing
 * data, engine gaps). A floor change requested by a script is recorded as a
 * connector (next=..). Without arguments the test checks nothing private.
 *
 * usage: test_world_story <newgame dir> <world boot dir> <save.gci> [report]
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
    C3Title wes, npc; /* stand-ins: one shared by every actor */
    C3Title *models[C3_AREA_MAX_ACTORS];
    C3AreaCamera camera;
} Scene;

static const char *newgame, *root, *save;
static unsigned totals[4]; /* ok, failed, blocked, partial */

static void path(char out[1024], const char *dir, const char *name)
{ CHECK(snprintf(out, 1024, "%s/%s", dir, name) > 0 && strlen(out) < 1023); }
static int any_floor(uint32_t floor) { return floor >= 1 && floor <= 0xffff; }
static int receive(void *c, uint32_t item, uint32_t count, char *name, size_t capacity)
{ (void)c; (void)item; (void)count; snprintf(name, capacity, "ITEM"); return 0; }
static int restore(void *c) { (void)c; return 6; }

static void le32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static uint32_t bits(float f) { uint32_t v; memcpy(&v, &f, 4); return v; }
/* A one-vertex stand-in model: every clip slot holds one 31-frame clip, plus a
 * camera block, the shape c3_field_story_load requires. */
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

/* Run the scene until the hero has control again (or the frame budget is
 * spent). Returns 0 on a script failure with the reason in error. */
static int run(Scene *s, unsigned frames, unsigned *used, uint32_t *next)
{
    for (unsigned frame = 0; frame < frames; ++frame) {
        const C3FieldStoryView *v = c3_field_story_view(&s->story);
        if (v->next_floor && !*next) *next = v->next_floor;
        if (frame > 30 && v->control && !v->dialogue) { *used = frame; return 1; }
        uint16_t pressed = (v->dialogue || v->choice) && frame % 15 == 0 ? 0x100 : 0;
        if (c3_field_story_battle_pending(&s->story, NULL) && !c3_field_story_battle_result(&s->story, 2, error)) return 0;
        if (v->shop && !c3_field_story_shop_done(&s->story, error)) return 0;
        if (v->name_entry && !c3_field_story_name_result(&s->story, "WES", error)) return 0;
        if (!c3_field_story_tick(&s->story, 1.0f/60.0f, pressed, error) || !c3_field_story_sample(&s->story, error)) return 0;
    }
    *used = frames;
    return 1;
}

/* One event on one floor in a fresh scene loaded from the save. */
static void event(const char *prefix, uint32_t floor, int kind, uint32_t id, uint32_t script)
{
    Scene *s = calloc(1, sizeof *s);
    C3GameState game = {0};
    C3GameSummary summary;
    char file[1024], name[64], gerror[C3_GAME_ERROR_SIZE] = {0};
    const char *status = "failed";
    unsigned frames = 0;
    uint32_t next = 0;
    CHECK(s);
    error[0] = 0;
    path(file, newgame, "newgame.c3g");
    CHECK(c3_game_state_load(&game, save, file, gerror));
    CHECK(c3_game_state_summary(&game, &summary));
    CHECK(c3_game_state_set_floor(&game, floor, summary.floor, 0));
    snprintf(name, sizeof name, "%s.c3f", prefix); path(file, root, name);
    if (!c3_field_load(&s->map, file, error, sizeof error)) goto done;
    snprintf(name, sizeof name, "%s.c3a", prefix); path(file, root, name);
    if (!c3_area_load(&s->area, file, error, sizeof error)) goto done;
    stand_in(&s->wes); stand_in(&s->npc);
    for (uint32_t i = 0; i < s->area.actor_count && i < C3_AREA_MAX_ACTORS; ++i) s->models[i] = &s->npc;
    if (!c3_field_spawn(&s->map, 0, &s->hero)) { snprintf(error, sizeof error, "entrance 0 is unavailable"); goto done; }
    snprintf(name, sizeof name, "%s.c3s", prefix); path(file, root, name);
    if (!c3_field_story_load(&s->story, file, &game, &s->map, &s->area, &s->hero, &s->wes, s->models, &s->camera, trace, error)) goto done;
    c3_field_story_set_floor_support(&s->story, any_floor);
    c3_field_story_enable_name_entry(&s->story, 1);
    c3_field_story_set_item_service(&s->story, receive, NULL);
    c3_field_story_set_party_service(&s->story, restore, NULL);
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
            /* Stand just in front of the NPC and face it. */
            float yaw = actor->pose.yaw, reach = 0.6f;
            s->hero = actor->pose;
            s->hero.position[0] += sinf(yaw) * reach; s->hero.position[2] += cosf(yaw) * reach;
            s->hero.yaw = yaw + 3.14159265f;
            memcpy(s->hero.proposed, s->hero.position, sizeof s->hero.position);
            if (!c3_field_story_interact(&s->story, error)) goto done;
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
    if (!strcmp(status, "ok")) ++totals[0];
    else if (!strcmp(status, "failed")) ++totals[1];
    else if (!strcmp(status, "blocked")) ++totals[2];
    else if (!strcmp(status, "partial")) ++totals[3];
    fprintf(report, "world prefix=%s floor=%03lx event=%s id=%lu script=%08lx status=%s frames=%u next=%03lx reason=%s\n",
            prefix, (unsigned long)floor, KIND[kind], (unsigned long)id, (unsigned long)script, status, frames,
            (unsigned long)next, strcmp(status, "ok") ? error : "");
    fflush(report);
    c3_field_story_close(&s->story); c3_field_close(&s->map); c3_area_close(&s->area);
    c3_title_close(&s->wes); c3_title_close(&s->npc); c3_game_state_free(&game); free(s);
}

static void floor_events(const char *prefix)
{
    char file[1024], name[64];
    C3Area area = {0};
    snprintf(name, sizeof name, "%s.c3a", prefix); path(file, root, name);
    if (!c3_area_load(&area, file, error, sizeof error)) {
        fprintf(report, "world prefix=%s floor=000 event=arrival id=0 script=00000000 status=failed frames=0 next=000 reason=%s\n", prefix, error);
        ++totals[1]; return;
    }
    snprintf(name, sizeof name, "%s.c3s", prefix); path(file, root, name);
    FILE *f = fopen(file, "rb");
    if (!f) { c3_area_close(&area); return; }
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

static int by_name(const void *a, const void *b) { return strcmp(a, b); }

int main(int argc, char **argv)
{
    if (argc < 4) { puts("world-story: private integration skipped (needs <newgame dir> <world boot dir> <save.gci>)"); return 0; }
    newgame = argv[1]; root = argv[2]; save = argv[3];
    report = argc > 4 ? fopen(argv[4], "w") : stdout; CHECK(report);
    trace = tmpfile(); CHECK(trace);
    /* Every prepared area pack names one floor; sorted for a stable report. */
    static char names[512][64];
    unsigned floors = 0;
    DIR *dir = opendir(root); CHECK(dir);
    for (struct dirent *e; (e = readdir(dir)) && floors < 512;) {
        size_t n = strlen(e->d_name);
        if (n > 4 && n < 60 && !strcmp(e->d_name + n - 4, ".c3a")) { memcpy(names[floors], e->d_name, n - 4); names[floors++][n - 4] = 0; }
    }
    closedir(dir);
    qsort(names, floors, sizeof names[0], by_name);
    for (unsigned i = 0; i < floors; ++i) {
        /* A long trace per floor would fill the disk: start it afresh. */
        fclose(trace); trace = tmpfile(); CHECK(trace);
        floor_events(names[i]);
    }
    printf("world-story: floors=%u ok=%u failed=%u blocked=%u partial=%u\n", floors, totals[0], totals[1], totals[2], totals[3]);
    if (report != stdout) fclose(report);
    return 0;
}
