#include "world_map_scene.h"
#include "bottom_screen.h"
#include "field_scene.h"
#include "n3ds_platform.h"
#include "colosseum3ds/floor_profile.h"
#include "colosseum3ds/world_map.h"
#include "colosseum3ds/title.h"
#include "colosseum3ds/field.h"
#include "pack_loader.h"
#include "pack_cache.h"
#include "area_prefetch.h"
#include "render_recovery.h"
#include "stage_renderer.h"
#include <math.h>

#include <3ds.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ROW_TOP 52
#define ROW_HEIGHT 18

/* Port labels for the nine destination floors of the pinned table. The
 * original menu strings are not extracted yet (docs/world-map.md). */
static const char *label(uint32_t floor)
{
    switch (floor) {
    case 0x01: return "OUTSKIRT STAND";
    case 0x03: return "PHENAC CITY";
    case 0x0F: return "PYRITE TOWN";
    case 0x24: return "AGATE VILLAGE";
    case 0x4A: return "MT. BATTLE";
    case 0x7A: return "THE UNDER";
    case 0x66: return "REALGAM TOWER";
    case 0x41: return "SHADOW PKMN LAB";
    case 0x88: return "SNAGEM HIDEOUT";
    default: return "UNKNOWN AREA";
    }
}

static int read_flag(void *context, uint32_t flag, uint32_t *value)
{
    return c3_game_state_get_flag(context, flag, value);
}

int c3_world_map_scene_available(void) { return 1; }

/* The original world-map menu (boot/worldmap.c3w, tools/prepare_world_map.py,
 * made from the user's own disc): the map image, the destination marker and
 * the selection cursor, and each destination item's place on the map. */
#define WORLDMAP_PACK "sdmc:/3ds/Colosseum3DS/boot/worldmap.c3w"
typedef struct { const uint8_t *rgba; uint32_t w, h; } MapImage;
typedef struct { uint16_t item; int16_t x, y; uint16_t w, h; } MapItem;
typedef struct {
    uint8_t *data; MapImage image[3]; MapItem items[16]; uint32_t item_count;
} MapPack;

static uint32_t le32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t le16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int pack_load(MapPack *pack)
{
    memset(pack, 0, sizeof *pack);
    FILE *file = fopen(WORLDMAP_PACK, "rb");
    if (!file) return 0;
    long size = -1;
    if (fseek(file, 0, SEEK_END) == 0) size = ftell(file);
    if (size < 12 || size > 4 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) { fclose(file); return 0; }
    pack->data = malloc((size_t)size);
    int read = pack->data && fread(pack->data, 1, (size_t)size, file) == (size_t)size;
    fclose(file);
    const uint8_t *d = pack->data;
    if (!read || memcmp(d, "C3W1", 4)) { free(pack->data); pack->data = NULL; return 0; }
    uint32_t images = le32(d + 4), items = le32(d + 8);
    size_t at = 12;
    for (uint32_t i = 0; i < images; ++i) {
        if (at + 12 > (size_t)size) goto bad;
        uint32_t key = le32(d + at), w = le32(d + at + 4), h = le32(d + at + 8);
        at += 12;
        if (!w || !h || w > 640 || h > 480 || at + (size_t)w * h * 4 > (size_t)size) goto bad;
        if (key < 3) { pack->image[key].rgba = d + at; pack->image[key].w = w; pack->image[key].h = h; }
        at += (size_t)w * h * 4;
    }
    for (uint32_t i = 0; i < items && i < 16; ++i, at += 10) {
        if (at + 10 > (size_t)size) goto bad;
        MapItem *m = pack->items + pack->item_count++;
        m->item = le16(d + at); m->x = (int16_t)le16(d + at + 2); m->y = (int16_t)le16(d + at + 4);
        m->w = le16(d + at + 6); m->h = le16(d + at + 8);
    }
    if (!pack->image[0].rgba || !pack->image[1].rgba || !pack->image[2].rgba) goto bad;
    return 1;
bad:
    free(pack->data); memset(pack, 0, sizeof *pack); return 0;
}

static const MapItem *pack_item(const MapPack *pack, uint32_t item)
{
    for (uint32_t i = 0; i < pack->item_count; ++i) if (pack->items[i].item == item) return pack->items + i;
    return NULL;
}

/* Visible destination under a touch, or -1 (markers are small: 14 px reach). */
static int pack_hit(const MapPack *pack, const C3WorldMap *map, int x, int y)
{
    int best = -1, best_d = 14 * 14;
    for (uint32_t i = 0; i < map->visible_count; ++i) {
        C3WorldMapEntry entry; int index = c3_world_map_visible(map, i);
        const MapItem *m;
        if (index < 0 || !c3_world_map_entry((uint32_t)index, &entry) || !(m = pack_item(pack, entry.item))) continue;
        int dx = x - (m->x + m->w / 2), dy = y - (m->y + m->h / 2), d = dx * dx + dy * dy;
        if (d < best_d) { best_d = d; best = (int)i; }
    }
    return best;
}

/* Top screen: the highlighted destination's exterior map packs (already on
 * the SD card for the field), streamed on the second core by the area
 * prefetch and orbited by a slow camera. A port presentation; the original
 * shows a still snapshot. The prefetch keeps the packs: when the player
 * travels to this destination, the field load takes them. */
#define PREVIEW_REST 0.35f   /* seconds the cursor rests before loading */
#define PREVIEW_SPEED 0.30f  /* orbit, radians per second */
typedef struct {
    uint32_t floor, wanted;
    const C3Title *models[C3_AREA_PREFETCH_MAX];
    unsigned count;
    int streaming, ready;
    float center[3], radius, angle, clock, rest;
    int walk_bounds; float walk_lo[3], walk_hi[3]; /* the area's walkable ground */
} Preview;
static Preview *preview;

/* Stop drawing the preview (the renderer is drained); the packs stay with
 * the prefetch. */
static void preview_release(FILE *trace)
{
    if (!preview) return;
    if (preview->ready) { (void)c3_render_recover(trace, "world-map-preview", 0, C3_RECOVER_DRAIN); preview->ready = 0; }
    preview->count = 0; preview->floor = 0; preview->streaming = 0;
}

static void preview_start(uint32_t floor, FILE *trace)
{
    const C3FloorProfile *profile = c3_floor_profile(floor);
    preview_release(trace);
    preview->floor = floor;
    if (!profile) { c3_area_prefetch_release(); return; }
    {   /* Frame the orbit on the walkable ground (the collision file), not
         * on the sky dome and far terrain that the map packs also hold. */
        char path[160], error[128]; C3FieldMap field; memset(&field, 0, sizeof field);
        snprintf(path, sizeof path, "sdmc:/3ds/Colosseum3DS/boot/%s.c3f", profile->prefix);
        preview->walk_bounds = 0;
        if (c3_field_load(&field, path, error, sizeof error) && field.walk_count) {
            for (int k = 0; k < 3; ++k) { preview->walk_lo[k] = 1e30f; preview->walk_hi[k] = -1e30f; }
            for (uint32_t i = 0; i < field.walk_count; ++i)
                for (int v = 0; v < 3; ++v)
                    for (int k = 0; k < 3; ++k) {
                        float x = field.walk[i].vertices[v * 3 + k];
                        if (x < preview->walk_lo[k]) preview->walk_lo[k] = x;
                        if (x > preview->walk_hi[k]) preview->walk_hi[k] = x;
                    }
            preview->walk_bounds = 1;
        }
        c3_field_close(&field);
    }
    preview->streaming = c3_area_prefetch_start(floor, trace) > 0;
    if (trace) { fprintf(trace, "world_map_preview floor=%03lx packs=%u\n", (unsigned long)floor, c3_area_prefetch_count()); fflush(trace); }
}

/* Once every pack arrived: bounds, first pose, renderer. */
static void preview_poll(FILE *trace)
{
    if (!preview->streaming || c3_area_prefetch_done() < c3_area_prefetch_count()) return;
    c3_area_prefetch_finish(); preview->streaming = 0;
    float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
    unsigned loaded = 0;
    for (unsigned i = 0; i < c3_area_prefetch_count(); ++i) {
        C3Title *title = c3_area_prefetch_title(i);
        if (!c3_area_prefetch_ok(i) || !c3_title_animate(title, 0, 0)) continue;
        for (uint32_t v = 0; v < title->vertex_count; ++v)
            for (int k = 0; k < 3; ++k) {
                float x = title->vertices[v].pos[k];
                if (x < lo[k]) lo[k] = x;
                if (x > hi[k]) hi[k] = x;
            }
        title->retained = 1;
        preview->models[loaded++] = title;
    }
    char error[192] = "";
    if (!loaded || c3_stage_renderer_init(preview->models, loaded, error, sizeof error)) {
        if (trace) { fprintf(trace, "world_map_preview floor=%03lx ready=0 error=%s\n", (unsigned long)preview->floor, error); fflush(trace); }
        return;
    }
    preview->count = loaded;
    if (preview->walk_bounds) for (int k = 0; k < 3; ++k) { lo[k] = preview->walk_lo[k]; hi[k] = preview->walk_hi[k]; }
    for (int k = 0; k < 3; ++k) preview->center[k] = (lo[k] + hi[k]) * 0.5f;
    float dx = hi[0] - lo[0], dz = hi[2] - lo[2];
    preview->radius = 0.5f * sqrtf(dx * dx + dz * dz);
    if (preview->radius < 1.0f) preview->radius = 1.0f;
    preview->ready = 1;
    if (trace) { fprintf(trace, "world_map_preview floor=%03lx ready=1 packs=%u radius=%.1f\n", (unsigned long)preview->floor, loaded, (double)preview->radius); fflush(trace); }
}

static void preview_frame(float seconds, FILE *trace)
{
    if (!preview) return;
    preview->rest += seconds;
    if (preview->wanted && preview->wanted != preview->floor && !preview->streaming && preview->rest >= PREVIEW_REST)
        preview_start(preview->wanted, trace);
    preview_poll(trace);
    if (!preview->ready) {
        unsigned total = c3_area_prefetch_count();
        c3_n3ds_top_loading(total ? (float)c3_area_prefetch_done() / (float)total : 0.0f);
        return;
    }
    preview->angle += PREVIEW_SPEED * seconds;
    preview->clock += seconds;
    for (unsigned i = 0; i < preview->count; ++i) (void)c3_title_animate((C3Title *)preview->models[i], 0, preview->clock);
    float r = preview->radius;
    C3TitleCamera camera;memset(&camera,0,sizeof camera);
    camera.target[0] = preview->center[0]; camera.target[1] = preview->center[1]; camera.target[2] = preview->center[2];
    camera.eye[0] = preview->center[0] + sinf(preview->angle) * r * 1.4f;
    camera.eye[1] = preview->center[1] + r * 0.9f;
    camera.eye[2] = preview->center[2] + cosf(preview->angle) * r * 1.4f;
    camera.fov_y_degrees = 40.0f; camera.near_plane = r * 0.02f; camera.far_plane = r * 8.0f; camera.aspect = 400.0f / 240.0f;
    C3TitleRenderStats stats; char error[192] = "";
    if (c3_stage_renderer_draw(preview->models, preview->count, &camera, 1.0f, &stats, error, sizeof error) && trace) {
        fprintf(trace, "world_map_preview draw_error=%s\n", error); fflush(trace);
        preview_release(trace);
    }
}

static void draw_native(const MapPack *pack, const C3WorldMap *map, const char *note)
{
    C3BottomUi *ui = c3_bottom_screen_begin();
    const MapImage *board = pack->image, *cursor = pack->image + 1, *marker = pack->image + 2;
    c3_ui_rgba(ui, board->rgba, board->w, board->h, 0, 0, 320, 240);
    const char *name = NULL;
    for (uint32_t i = 0; i < map->visible_count; ++i) {
        C3WorldMapEntry entry; int index = c3_world_map_visible(map, i);
        const MapItem *m;
        if (index < 0 || !c3_world_map_entry((uint32_t)index, &entry) || !(m = pack_item(pack, entry.item))) continue;
        int cx = m->x + m->w / 2, cy = m->y + m->h / 2;
        c3_ui_rgba(ui, marker->rgba, marker->w, marker->h, cx - (int)marker->w / 2, cy - (int)marker->h / 2,
                   (int)marker->w, (int)marker->h);
        if (i == map->cursor) {
            c3_ui_rgba(ui, cursor->rgba, cursor->w, cursor->h, cx - (int)cursor->w / 2, cy - (int)cursor->h / 2,
                       (int)cursor->w, (int)cursor->h);
            name = label(entry.floor);
        }
    }
    if (name) { c3_ui_panel(ui, 8, 196, 304, 24); c3_ui_text(ui, 18, 202, 284, 1, name, C3_UI_WHITE); }
    c3_ui_footer(ui, note ? note : "D-pad / tap: choose   A: go   B: back");
    c3_bottom_screen_present();
}

static void draw(const C3WorldMap *map, const char *note)
{
    C3BottomUi *ui = c3_bottom_screen_begin();
    c3_ui_header(ui, "WORLD MAP", "Choose a destination");
    c3_ui_panel(ui, 7, 46, 306, 170);
    for (uint32_t i = 0; i < map->visible_count; ++i) {
        C3WorldMapEntry entry;
        int index = c3_world_map_visible(map, i);
        if (index < 0 || !c3_world_map_entry((uint32_t)index, &entry)) continue;
        c3_ui_row_value(ui, ROW_TOP + (int)i * ROW_HEIGHT, label(entry.floor),
                        c3_field_scene_supports(entry.floor) ? NULL : "not prepared",
                        i == map->cursor, !c3_field_scene_supports(entry.floor));
    }
    c3_ui_footer(ui, note ? note : "Touch or A: go   B: back");
    c3_bottom_screen_present();
}

/* The journey before the floor link: gs_title.c fn_80025730 opens menu 0xC3
 * and fn_80025F84 plays move_demo (tools/prepare_world_journey.py). The
 * scenery and ground (journey_map%u.c3t) stream in when the map opens, as
 * the original registers 0xC6A1000-1002 in fn_800259B0; the bike rides
 * route animation frame_a of the path joint 0xC6A1000 under camera frame_b,
 * one row of lbl_3_data_270 picked by direction and a session-long index.
 * The bike is bike_hizuki (Wes with Rui) once flag 0x3E5 is set, else
 * bike_pokemon. 0xAE frames after the camera starts, the world map's exit
 * (fn_800255A4) fades for one second. The port holds the fade until the
 * destination's map packs (the area prefetch) have arrived, so the ride
 * covers the load; the last route frame holds if the load is longer. */
#define JOURNEY_PARTS 4u
#define JOURNEY_FADE 1.0f
#define JOURNEY_MAX_WAIT 12.0f /* seconds; the prefetch cannot outlast it in practice */
typedef struct {
    C3Title maps[JOURNEY_PARTS]; C3PackJob map_jobs[JOURNEY_PARTS]; unsigned map_count;
    C3PackStream *map_stream;
    C3Title bike[JOURNEY_PARTS]; C3PackJob bike_jobs[JOURNEY_PARTS]; unsigned bike_count;
} Journey;
static Journey *journey;
static C3WorldMapJourney journey_rows; /* lbl_8047A394/398: kept for the session */
static int journey_shown;
int c3_world_map_scene_take_journey_shown(void) { int shown = journey_shown; journey_shown = 0; return shown; }

static void journey_open(FILE *trace)
{
    journey = calloc(1, sizeof *journey);
    if (!journey) return;
    for (unsigned i = 0; i < JOURNEY_PARTS; ++i) {
        C3PackJob *job = journey->map_jobs + journey->map_count;
        FILE *f;
        snprintf(job->path, sizeof job->path, "sdmc:/3ds/Colosseum3DS/boot/journey_map%u.c3t", i);
        if (!(f = fopen(job->path, "rb"))) { job->path[0] = 0; break; }
        fclose(f);
        job->title = journey->maps + journey->map_count++;
    }
    if (journey->map_count) journey->map_stream = c3_pack_stream_start(journey->map_jobs, journey->map_count);
    if (trace) { fprintf(trace, "world_map_journey_packs maps=%u\n", journey->map_count); fflush(trace); }
}

static void journey_close(void)
{
    if (!journey) return;
    if (journey->map_stream) c3_pack_stream_finish(journey->map_stream);
    for (unsigned i = 0; i < JOURNEY_PARTS; ++i) { c3_title_close(journey->maps + i); c3_title_close(journey->bike + i); }
    free(journey); journey = NULL;
}

/* Map-pack centre of a floor's menu item (bottom-screen x); 0 if none. */
static int item_x(const MapPack *pack, uint32_t floor, int *x)
{
    for (uint32_t i = 0; pack->data && i < c3_world_map_entry_count(); ++i) {
        C3WorldMapEntry entry; const MapItem *m;
        if (!c3_world_map_entry(i, &entry) || entry.floor != floor || !(m = pack_item(pack, entry.item))) continue;
        *x = m->x + m->w / 2;
        return 1;
    }
    return 0;
}

/* Returns 1 when the journey was shown, 0 when its packs are missing (the
 * link goes straight on), -1 on an application exit. */
static int journey_play(const MapPack *pack, const C3WorldMap *map, uint32_t floor, int rui, FILE *trace)
{
    int ox, dx, west = 0; /* fn_800259B0 resets lbl_8047A380 when the map opens */
    if (item_x(pack, map->origin_floor, &ox) && item_x(pack, floor, &dx)) west = c3_world_map_journey_west(0, ox, dx);
    uint32_t anim = 0, camera_id = 0;
    uint32_t row = c3_world_map_journey_pick(&journey_rows, west, &anim, &camera_id);
    const char *bike = rui ? "hizuki" : "pokemon";
    if (!journey || !journey->map_count) return 0;
    for (unsigned i = 0; i < JOURNEY_PARTS; ++i) {
        C3PackJob *job = journey->bike_jobs + journey->bike_count;
        FILE *f;
        snprintf(job->path, sizeof job->path, "sdmc:/3ds/Colosseum3DS/boot/journey_%s_a%02lu_%u.c3t", bike,
                 (unsigned long)anim, i);
        if (!(f = fopen(job->path, "rb"))) { job->path[0] = 0; break; }
        fclose(f);
        job->title = journey->bike + journey->bike_count++;
    }
    if (trace) {
        fprintf(trace, "world_map_journey row=%lu route=%lu camera=%08lx bike=%s west=%d packs=%u+%u\n",
                (unsigned long)row, (unsigned long)anim, (unsigned long)camera_id, bike, west,
                journey->map_count, journey->bike_count);
        fflush(trace);
    }
    if (!journey->bike_count) return 0;
    uint64_t load_start = c3_n3ds_time_us();
    size_t failed = c3_pack_load_all(journey->bike_jobs, journey->bike_count, NULL);
    if (journey->map_stream) { c3_pack_stream_finish(journey->map_stream); journey->map_stream = NULL; }
    const C3Title *models[2 * JOURNEY_PARTS];
    unsigned count = 0, first_bike;
    uint64_t skin = 0;
    for (unsigned i = 0; i < journey->map_count; ++i) {
        if (!journey->map_jobs[i].ok) failed = failed ? failed : 100 + i;
        journey->maps[i].retained = 1;
        models[count++] = journey->maps + i;
    }
    first_bike = count;
    for (unsigned i = 0; i < journey->bike_count; ++i) { skin |= UINT64_C(1) << count; models[count++] = journey->bike + i; }
    for (unsigned i = 0; !failed && i < count; ++i) if (!c3_title_animate((C3Title *)models[i], 0, 0)) failed = 200 + i;
    if (failed) {
        if (trace) { fprintf(trace, "world_map_journey ready=0 failed=%lu\n", (unsigned long)failed); fflush(trace); }
        return 0;
    }
    /* The orbit preview's packs stay with the prefetch; the GPU lets go. */
    preview_release(trace);
    char error[192] = "";
#ifndef C3_NO_GPU_SKIN
    c3_stage_renderer_request_gpu_skin(skin);
#endif
    if (c3_stage_renderer_init(models, count, error, sizeof error)) {
        c3_pack_cache_clear();
        if (c3_stage_renderer_init(models, count, error, sizeof error)) {
            if (trace) { fprintf(trace, "world_map_journey ready=0 error=%s\n", error); fflush(trace); }
            return 0;
        }
    }
#ifndef C3_NO_GPU_SKIN
    for (unsigned i = first_bike; i < count; ++i) {
        C3Title *part = journey->bike + (i - first_bike);
        part->gpu_batches = c3_stage_renderer_gpu_skin_mask(i);
        part->retained = part->gpu_batches != 0;
    }
#else
    (void)skin;
#endif
    const C3Title *rider = journey->bike;
    float route_end = rider->clip_count && rider->clips[0].frame_count > 1u
        ? (float)(rider->clips[0].frame_count - 1u) / rider->fps : 0.0f;
    float ride = (float)C3_WORLD_MAP_JOURNEY_FRAMES / 60.0f, fade_start = -1.0f, t = 0.0f;
    uint64_t previous = c3_n3ds_active_time_us();
    unsigned frames = 0, prefetched = 0;
    int result = 1;
    C3N3dsInput input = {0};
    if (trace) {
        fprintf(trace, "world_map_journey ready=1 load_us=%llu route_seconds=%.3f prefetch=%u/%u\n",
                (unsigned long long)(c3_n3ds_time_us() - load_start), (double)route_end,
                c3_area_prefetch_done(), c3_area_prefetch_count());
        fflush(trace);
    }
    for (;;) {
        if (!c3_n3ds_poll(&input)) { result = -1; break; }
        uint64_t now = c3_n3ds_active_time_us();
        float delta = (float)(now - previous) / 1e6f; previous = now;
        if (delta > 0.1f) delta = 0.1f; /* a stall does not skip the ride */
        t += delta;
        prefetched = c3_area_prefetch_done();
        if (fade_start < 0 && t >= ride && (prefetched >= c3_area_prefetch_count() || t >= JOURNEY_MAX_WAIT))
            fade_start = t;
        float fade = fade_start < 0 ? 1.0f : 1.0f - (t - fade_start) / JOURNEY_FADE;
        if (fade <= 0.0f) break;
        int sampled = 1;
        for (unsigned i = 0; i < journey->map_count; ++i) sampled &= c3_title_animate(journey->maps + i, 0, t);
        for (unsigned i = 0; i < journey->bike_count; ++i)
            sampled &= c3_title_animate(journey->bike + i, 0, t < route_end ? t : route_end);
        if (!sampled) { if (trace) { fprintf(trace, "world_map_journey sample_error=1\n"); fflush(trace); } break; }
        C3TitleCamera camera = rider->pose.camera;
        C3TitleRenderStats stats;
        int drawn = c3_stage_renderer_draw(models, count, &camera, fade, &stats, error, sizeof error);
        if (drawn == C3_RENDER_BUSY) {
            C3RenderRecoveryResult recovery = c3_render_recover(trace, "world-map-journey", frames, C3_RECOVER_WAIT);
            if (recovery == C3_RECOVERY_EXIT) { result = -1; break; }
            if (recovery == C3_RECOVERY_RESUME_PAUSED) previous = c3_n3ds_active_time_us();
            continue;
        }
        if (drawn) { if (trace) { fprintf(trace, "world_map_journey draw_error=%s\n", error); fflush(trace); } break; }
        ++frames;
#ifdef C3_ROUNDTRIP_PROBE
        {   /* Review frames of each ride (the capture reads the previous frame). */
            static unsigned ride; char shot[96];
            if (frames == 1) { ++ride; mkdir("sdmc:/3ds/Colosseum3DS/probe", 0777); }
            if (frames == 40 || frames == 100 || frames == 160) {
                snprintf(shot, sizeof shot, "sdmc:/3ds/Colosseum3DS/probe/journey%u_%03u.bmp", ride, frames);
                (void)c3_n3ds_capture_top(shot);
            }
            if (frames == 100) {
                snprintf(shot, sizeof shot, "sdmc:/3ds/Colosseum3DS/probe/journey%u_bottom.bmp", ride);
                (void)c3_n3ds_capture_bottom(shot);
            }
        }
#endif
    }
    if (c3_render_recover(trace, "world-map-journey", frames, result < 0 ? C3_RECOVER_EXIT : C3_RECOVER_DRAIN) ==
        C3_RECOVERY_EXIT) result = -1;
    if (trace) {
        fprintf(trace, "world_map_journey_end frames=%u seconds=%.2f fade_start=%.2f prefetch=%u/%u\n", frames,
                (double)t, (double)fade_start, prefetched, c3_area_prefetch_count());
        fflush(trace);
    }
    return result < 0 ? -1 : frames > 0;
}

/* Everything the scene owns; keep_prefetch leaves the destination's map
 * packs for the next field load. */
static void scene_close(MapPack *pack, FILE *trace, int keep_prefetch)
{
    preview_release(trace);
    if (!keep_prefetch) c3_area_prefetch_release();
    journey_close();
    free(pack->data); pack->data = NULL;
    free(preview); preview = NULL;
}

int c3_world_map_scene_run(C3GameState *state, FILE *trace, char *error, size_t capacity)
{
    C3GameSummary summary;
    C3WorldMap map;
    C3N3dsInput input = {0};
    const char *note = NULL;
    int dirty = 1, released = 0, armed = -1, stick_ready = 1;
    MapPack pack; int native = pack_load(&pack);
    preview = calloc(1, sizeof *preview);
    uint64_t preview_clock = c3_n3ds_time_us();
    if (!state || !c3_game_state_summary(state, &summary) || summary.floor != C3_FLOOR_WORLD_MAP_ID ||
        !c3_world_map_open(&map, summary.previous_floor, read_flag, state)) {
        snprintf(error, capacity, "The world map could not be opened from this state.");
        scene_close(&pack, trace, 0);
        return -1;
    }
    journey_open(trace);
    if (trace) {
        fprintf(trace, "world_map_begin previous=%lu enabled=%03lx visible=%lu cursor=%lu save_writes=0\n",
                (unsigned long)summary.previous_floor, (unsigned long)map.enabled,
                (unsigned long)map.visible_count, (unsigned long)map.cursor);
        fflush(trace);
    }
    while (c3_n3ds_poll(&input)) {
        int choice = -2; /* -2 none, -1 cancel, otherwise visible index */
        if (!input.held && !input.touch_held) released = 1;
        if (released) {
            if (native) {
                /* The original cursor-link table; the circle pad acts as the
                 * D-pad once per push. */
                uint16_t dpad = input.pressed & (C3_PAD_LEFT | C3_PAD_RIGHT | C3_PAD_UP | C3_PAD_DOWN);
                int sx = input.stick_x, sy = input.stick_y;
                if (sx * sx + sy * sy < 40 * 40) stick_ready = 1;
                else if (stick_ready) {
                    stick_ready = 0;
                    if (abs(sx) > abs(sy)) dpad |= sx > 0 ? C3_PAD_RIGHT : C3_PAD_LEFT;
                    else dpad |= sy > 0 ? C3_PAD_UP : C3_PAD_DOWN;
                }
                if (dpad && c3_world_map_move(&map, dpad)) { dirty = 1; note = NULL; }
            } else {
                if ((input.pressed & C3_PAD_UP) && map.cursor) { --map.cursor; dirty = 1; note = NULL; }
                if ((input.pressed & C3_PAD_DOWN) && map.cursor + 1u < map.visible_count) { ++map.cursor; dirty = 1; note = NULL; }
            }
            if (input.pressed & C3_PAD_A) choice = (int)map.cursor;
#ifdef C3_CAMPAIGN
            /* Campaign: visit the destinations in turn, one per world-map stop. */
            { extern uint32_t c3_campaign_launch; static uint32_t rotation; static int seeded;
              if (!seeded) { rotation = c3_campaign_launch; seeded = 1; }
              /* Review aid: show the map a moment and save the bottom screen
               * before choosing. */
              static unsigned shown; ++shown;
              if (shown == 30) (void)c3_n3ds_capture_bottom("sdmc:/3ds/Colosseum3DS/campaign/worldmap_bottom.bmp");
              if (shown == 140) (void)c3_n3ds_capture_top("sdmc:/3ds/Colosseum3DS/campaign/worldmap_top.bmp");
              if (shown >= 150 && map.visible_count && choice < 0 && !(input.pressed & C3_PAD_B)) {
                shown = 0; choice = (int)(rotation++ % map.visible_count); } }
#endif
#ifdef C3_ROUNDTRIP_PROBE
            /* Probe: once the preview is up, go to the other destination. */
            { static unsigned waited; ++waited;
              if (waited >= 120 && preview && preview->ready) {
                  waited = 0;
                  for (uint32_t i = 0; i < map.visible_count; ++i) {
                      C3WorldMapEntry e; int idx = c3_world_map_visible(&map, i);
                      if (idx >= 0 && c3_world_map_entry((uint32_t)idx, &e) && e.floor != summary.previous_floor &&
                          c3_field_scene_supports(e.floor)) { map.cursor = i; choice = (int)i; break; }
                  } } }
#endif
            if (input.pressed & C3_PAD_B) choice = -1;
            /* A tap selects a row; lifting the stylus on the same row goes. */
            if (native && input.touch_pressed) {
                /* A tap selects a marker; tapping the selected one goes. */
                int hit = pack_hit(&pack, &map, input.touch_x, input.touch_y);
                if (hit >= 0 && (uint32_t)hit == map.cursor) choice = hit;
                else if (hit >= 0) { map.cursor = (uint32_t)hit; dirty = 1; note = NULL; }
            } else if (!native && (input.touch_held || input.touch_released)) {
                int row = input.touch_y >= ROW_TOP && input.touch_x >= 15 && input.touch_x < 305
                    ? (input.touch_y - ROW_TOP) / ROW_HEIGHT : -1;
                if (row < 0 || (uint32_t)row >= map.visible_count) row = -1;
                if (input.touch_pressed) armed = row;
                if (row >= 0 && input.touch_held && (uint32_t)row != map.cursor && armed == row) {
                    map.cursor = (uint32_t)row; dirty = 1; note = NULL;
                }
                if (input.touch_released) {
                    if (row >= 0 && row == armed) choice = row;
                    armed = -1;
                }
            }
        }
        if (choice != -2) {
            uint32_t floor = 0;
            int travel = 0;
            if (!c3_world_map_choose(&map, choice, read_flag, state, &floor, &travel)) {
                snprintf(error, capacity, "The world-map destination is invalid.");
                scene_close(&pack, trace, 0);
                return -1;
            }
            if (!c3_field_scene_supports(floor)) {
                note = "That area is not prepared in this build.";
                dirty = 1;
            } else {
                /* floorLink(floor, 0): entrance 0, arriving from floor 0x38E. */
                if (!c3_game_state_set_floor(state, floor, C3_FLOOR_WORLD_MAP_ID, 0)) {
                    snprintf(error, capacity, "The world-map destination could not be recorded.");
                    scene_close(&pack, trace, 0);
                    return -1;
                }
                int presented = 0;
                uint32_t rui = 0;
                /* The destination's map packs load during the ride (or are
                 * already here from the preview); the field takes them. */
                if (c3_area_prefetch_floor() != floor) { preview_release(trace); (void)c3_area_prefetch_start(floor, trace); }
                if (travel && !read_flag(state, C3_WORLD_MAP_JOURNEY_RUI_FLAG, &rui)) rui = 0;
                /* The bottom screen keeps the world map, as in the original,
                 * with the cursor on the chosen destination. */
                if (choice >= 0) map.cursor = (uint32_t)choice;
                if (native) draw_native(&pack, &map, NULL); else draw(&map, NULL);
                if (travel) presented = journey_play(&pack, &map, floor, rui == 1, trace);
                journey_shown = presented > 0;
                if (trace) {
                    fprintf(trace, "world_map_link floor=%lu entrance=0 cancel=%d journey=%d journey_presented=%d save_writes=0\n",
                            (unsigned long)floor, choice < 0, travel, presented > 0);
                    fflush(trace);
                }
                scene_close(&pack, trace, presented >= 0);
                return presented < 0 ? 1 : 2; /* 1: exit requested during the ride */
            }
        }
        if (dirty) {
            if (native) draw_native(&pack, &map, note); else draw(&map, note);
            /* The highlighted destination becomes the top screen's preview. */
            C3WorldMapEntry entry; int index = c3_world_map_visible(&map, map.cursor);
            if (preview && index >= 0 && c3_world_map_entry((uint32_t)index, &entry) && entry.floor != preview->wanted) {
                preview->wanted = entry.floor; preview->rest = 0;
            }
            dirty = 0;
        }
        { uint64_t now = c3_n3ds_time_us(); float seconds = (float)(now - preview_clock) / 1e6f; preview_clock = now;
          if (seconds > 0.25f) seconds = 0.25f;
          preview_frame(seconds, trace); }
        gspWaitForVBlank();
    }
    scene_close(&pack, trace, 0);
    return 1;
}
