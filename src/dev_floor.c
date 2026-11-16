#include "colosseum3ds/dev_floor.h"

#include <math.h>
#include <stdio.h>

/* Floor and map identities come from REL125 lifecycle row 2 and M1_out.fsys;
 * see docs/phenac-floors.md. */
static const C3DevFloorProfile profiles[] = {
    {"PHENAC CITY", "phenac", 0x03u, 0x00620200u},
};

unsigned c3_dev_floor_profile_count(void)
{
    return (unsigned)(sizeof profiles / sizeof profiles[0]);
}

const C3DevFloorProfile *c3_dev_floor_profile(unsigned index)
{
    return index < c3_dev_floor_profile_count() ? profiles + index : NULL;
}

int c3_dev_floor_path(char *out, size_t capacity, const char *root,
                      const C3DevFloorProfile *profile, C3DevFloorFile file, unsigned index)
{
    int written;
    if (!out || !capacity || !root || !profile) return 0;
    out[0] = '\0';
    switch (file) {
    case C3_DEV_FLOOR_FIELD: written = snprintf(out, capacity, "%s/%s.c3f", root, profile->prefix); break;
    case C3_DEV_FLOOR_MAP:
        if (index >= C3_FIELD_MAX_MODEL_PACKS) return 0;
        written = snprintf(out, capacity, "%s/%s_map%u.c3t", root, profile->prefix, index); break;
    case C3_DEV_FLOOR_WES: written = snprintf(out, capacity, "%s/%s_wes.c3t", root, profile->prefix); break;
    default: return 0;
    }
    if (written < 0 || (size_t)written >= capacity) { out[0] = '\0'; return 0; }
    return 1;
}

static int reject(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return 0;
}

int c3_dev_floor_validate(const C3FieldMap *map, const C3DevFloorProfile *profile,
                          char *error, size_t capacity)
{
    if (!map || !profile || !map->spawn_count || !map->walk_count)
        return reject(error, capacity, "The developer floor pack is not loaded.");
    if (map->floor != profile->floor || map->map_resource != profile->map_resource)
        return reject(error, capacity, "The developer floor pack is for a different floor.");
    if (map->arrival_pack_count)
        return reject(error, capacity, "Developer floors carry no story arrival sequence.");
    if (!map->map_pack_count || map->map_pack_count >= C3_FIELD_MAX_MODEL_PACKS + 1u)
        return reject(error, capacity, "The developer floor has an invalid map pack count.");
    if (map->actor_bone != UINT32_MAX || map->actor_first_vertex != 0u || !map->actor_vertex_count)
        return reject(error, capacity, "The developer floor needs a separate Wes pack.");
    if (error && capacity) error[0] = '\0';
    return 1;
}

void c3_dev_floor_place_actor(C3Title *model, const C3FieldPlayer *player)
{
    if (!model || !player) return;
    float sine = sinf(player->yaw), cosine = cosf(player->yaw);
    for (uint32_t i = 0; i < model->vertex_count; ++i) {
        C3TitleDrawVertex *v = model->draw_vertices + i;
        float x = v->pos[0], z = v->pos[2], nx = v->normal[0], nz = v->normal[2];
        v->pos[0] = cosine * x + sine * z + player->position[0];
        v->pos[1] += player->position[1];
        v->pos[2] = -sine * x + cosine * z + player->position[2];
        v->normal[0] = cosine * nx + sine * nz;
        v->normal[2] = -sine * nx + cosine * nz;
    }
    for (uint32_t i = 0; i < model->batch_count && i < C3_TITLE_MAX_BATCHES; ++i) {
        float *origin = model->pose.materials[i].specular_origin, x = origin[0], z = origin[2];
        origin[0] = cosine * x + sine * z + player->position[0];
        origin[1] += player->position[1];
        origin[2] = -sine * x + cosine * z + player->position[2];
    }
}

uint32_t c3_dev_floor_next_spawn(const C3FieldMap *map, uint32_t current, int step)
{
    if (!map || !map->spawn_count) return 0;
    uint32_t count = map->spawn_count, index = current % count;
    if (step > 0) return (index + 1u) % count;
    if (step < 0) return (index + count - 1u) % count;
    return index;
}
