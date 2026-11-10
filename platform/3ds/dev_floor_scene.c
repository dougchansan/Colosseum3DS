#include "dev_floor_scene.h"

#ifdef C3_DEV_FLOOR
#include "bottom_screen.h"
#include "n3ds_platform.h"
#include "render_recovery.h"
#include "stage_renderer.h"
#include "colosseum3ds/dev_floor.h"
#include "colosseum3ds/field.h"
#include "colosseum3ds/title.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Developer packs live apart from the story boot assets. */
#define DEV_ROOT "sdmc:/3ds/Colosseum3DS/" C3_DEV_FLOOR_DIRECTORY

static void draw_status(const C3DevFloorProfile *profile, const C3FieldMap *map,
                        const C3FieldPlayer *player, uint32_t spawn)
{
    char text[320];
    snprintf(text, sizeof text,
             "%s  (original floor 0x%02lX)\n%s\n"
             "Map, collision, camera and Wes only.\n"
             "No NPCs, scripts, doors, battles or audio.\n\n"
             "Entrance %lu of %lu (source index %lu)\n"
             "Position %.2f %.2f %.2f",
             profile->name, (unsigned long)profile->floor, C3_DEV_FLOOR_NOTICE,
             (unsigned long)spawn + 1u, (unsigned long)map->spawn_count,
             (unsigned long)map->spawns[spawn].source_index,
             (double)player->position[0], (double)player->position[1], (double)player->position[2]);
    c3_bottom_screen_notice(C3_DEV_FLOOR_LABEL, text, "Stick/D-pad move  L/R entrance  START leave");
}

int c3_dev_floor_scene_run(FILE *trace, char *error, size_t capacity)
{
    const C3DevFloorProfile *profile = c3_dev_floor_profile(0);
    C3FieldMap map = {0};
    C3FieldPlayer player = {0};
    C3Title *storage = NULL;
    const C3Title *models[C3_STAGE_MAX_MODELS] = {0};
    C3TitleCamera camera;
    C3TitleRenderStats stats = {0};
    C3N3dsInput input = {0};
    uint32_t opened = 0, count = 0, spawn = 0, frames = 0;
    uint64_t previous, next_status = 0, next_trace = 0;
    float elapsed = 0;
    int result = -1;
    char path[192];

    c3_bottom_screen_notice(C3_DEV_FLOOR_LABEL, "Loading the developer floor...\n" C3_DEV_FLOOR_NOTICE, "Please wait...");
    if (trace) { fprintf(trace, "dev_floor_begin label=developer profile=%s floor=%lu game_state=none save_writes=0 story_flags=0\n",
                         profile ? profile->prefix : "none", profile ? (unsigned long)profile->floor : 0ul); fflush(trace); }
    if (!profile || !c3_dev_floor_path(path, sizeof path, DEV_ROOT, profile, C3_DEV_FLOOR_FIELD, 0) ||
        !c3_field_load(&map, path, error, capacity)) {
        if (profile && error && capacity && !error[0]) snprintf(error, capacity, "Developer floor pack is unavailable.");
        goto done;
    }
    if (!c3_dev_floor_validate(&map, profile, error, capacity)) goto done;
    count = map.map_pack_count + 1u;
    if (count > C3_STAGE_MAX_MODELS) { snprintf(error, capacity, "The developer floor exceeds the model bound."); goto done; }
    storage = calloc(count, sizeof *storage);
    if (!storage) { snprintf(error, capacity, "Developer floor allocation failed."); goto done; }
    for (uint32_t i = 0; i < count; ++i) {
        int is_wes = i == map.map_pack_count;
        if (!c3_dev_floor_path(path, sizeof path, DEV_ROOT, profile,
                               is_wes ? C3_DEV_FLOOR_WES : C3_DEV_FLOOR_MAP, i)) {
            snprintf(error, capacity, "Developer floor path is too long."); goto done;
        }
        if (!c3_title_load(storage + i, path, error, capacity)) goto done;
        models[opened++] = storage + i;
        if (!c3_title_animate(storage + i, 0, 0)) { snprintf(error, capacity, "Initial developer floor animation failed."); goto done; }
    }
    C3Title *wes = storage + map.map_pack_count;
    if (wes->vertex_count != map.actor_vertex_count || wes->clip_count != 4u ||
        map.idle_clip >= wes->clip_count || map.walk_clip >= wes->clip_count || map.run_clip >= wes->clip_count) {
        snprintf(error, capacity, "The prepared Wes motions have an unsupported layout."); goto done;
    }
    if (!c3_field_spawn(&map, map.spawns[spawn].source_index, &player)) {
        snprintf(error, capacity, "The developer floor entrance is unavailable."); goto done;
    }
    if (trace) {
        size_t resident = 0;
        for (uint32_t i = 0; i < count; ++i) resident += storage[i].resident_bytes;
        fprintf(trace, "dev_floor_models floor=%lu maps=%lu models=%lu entrances=%lu cpu_resident_bytes=%lu\n",
                (unsigned long)map.floor, (unsigned long)map.map_pack_count, (unsigned long)count,
                (unsigned long)map.spawn_count, (unsigned long)resident);
        fflush(trace);
    }
    if (c3_stage_renderer_init(models, count, error, capacity)) goto done;
    /* Static packs carry one sample; let the renderer keep their arrays. */
    for (uint32_t i = 0; i < map.map_pack_count; ++i)
        if (storage[i].frame_count == 1u) c3_stage_renderer_set_static(i, true);
    c3_n3ds_log_memory(trace, "dev_floor_ready");
    previous = c3_n3ds_active_time_us();
    result = 1;
    while (c3_n3ds_poll(&input)) {
        uint64_t now = c3_n3ds_active_time_us();
        float delta = (float)(now - previous) / 1000000.0f;
        previous = now; elapsed += delta;
        if (input.pressed & C3_PAD_START) { result = 0; break; }
        if (input.pressed & (C3_PAD_L | C3_PAD_R)) {
            spawn = c3_dev_floor_next_spawn(&map, spawn, (input.pressed & C3_PAD_R) ? 1 : -1);
            if (!c3_field_spawn(&map, map.spawns[spawn].source_index, &player)) {
                snprintf(error, capacity, "The developer floor entrance is unavailable."); result = -1; break;
            }
            if (trace) { fprintf(trace, "dev_floor_entrance index=%lu source=%lu xyz=%.4f,%.4f,%.4f\n", (unsigned long)spawn,
                                 (unsigned long)map.spawns[spawn].source_index, (double)player.position[0],
                                 (double)player.position[1], (double)player.position[2]); fflush(trace); }
            next_status = 0;
        } else if (!c3_field_move(&map, &player, input.stick_x, input.stick_y, input.held, map.camera_yaw, delta)) {
            snprintf(error, capacity, "Field movement failed."); result = -1; break;
        }
        int sampled = 1;
        for (uint32_t i = 0; i < map.map_pack_count; ++i)
            if (storage[i].frame_count > 1u) sampled &= c3_title_animate(storage + i, 0, elapsed);
        uint32_t clip = player.motion == 2 ? map.run_clip : player.motion == 1 ? map.walk_clip : map.idle_clip;
        sampled &= c3_title_animate(wes, clip, player.animation_seconds);
        if (!sampled) { snprintf(error, capacity, "A developer floor animation sample failed."); result = -1; break; }
        c3_dev_floor_place_actor(wes, &player);
        c3_field_camera(&map, &player, &camera);
        if (now >= next_status || input.pressed) {
            draw_status(profile, &map, &player, spawn);
            next_status = now + UINT64_C(250000);
        }
        int draw_result = c3_stage_renderer_draw(models, count, &camera, 1.0f, &stats, error, capacity);
        if (draw_result == C3_RENDER_BUSY) {
            C3RenderRecoveryResult recovery = c3_render_recover(trace, "dev_floor", frames, C3_RECOVER_WAIT);
            if (recovery == C3_RECOVERY_EXIT) { result = 1; break; }
            if (recovery == C3_RECOVERY_RESUME_PAUSED) { previous = c3_n3ds_active_time_us(); next_status = 0; }
            continue;
        }
        if (draw_result) { result = -1; break; }
        ++frames;
        if (trace && (now >= next_trace || input.pressed)) {
            fprintf(trace, "dev_floor_tick floor=%lu entrance=%lu input=%04x stick=%d,%d xyz=%.4f,%.4f,%.4f yaw=%.4f motion=%lu hits=%lu draws=%lu gpu_bytes=%lu\n",
                    (unsigned long)map.floor, (unsigned long)spawn, input.pressed, input.stick_x, input.stick_y,
                    (double)player.position[0], (double)player.position[1], (double)player.position[2],
                    (double)player.yaw, (unsigned long)player.motion, (unsigned long)player.collision_count,
                    (unsigned long)stats.draw_calls, (unsigned long)stats.gpu_buffer_bytes);
            fflush(trace);
            next_trace = now + UINT64_C(1000000);
        }
    }
done:
    if (result < 0) c3_n3ds_log_memory(trace, "dev_floor_failed");
    if (c3_render_recover(trace, "dev_floor", frames, result == 1 ? C3_RECOVER_EXIT : C3_RECOVER_DRAIN) == C3_RECOVERY_EXIT)
        result = 1;
    for (uint32_t i = 0; i < opened; ++i) c3_title_close(storage + i);
    free(storage);
    c3_field_close(&map);
    if (trace) { fprintf(trace, "dev_floor_end result=%d frames=%lu save_writes=0 story_flags=0 error=%s\n",
                         result, (unsigned long)frames, result < 0 && error ? error : ""); fflush(trace); }
    return result;
}

#else
/* Normal builds contain no developer floor path. */
int c3_dev_floor_scene_run(FILE *trace, char *error, size_t capacity)
{
    (void)trace;
    if (error && capacity) snprintf(error, capacity, "Developer floor loader is not built.");
    return -1;
}
#endif
