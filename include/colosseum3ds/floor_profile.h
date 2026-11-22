#ifndef C3_FLOOR_PROFILE_H
#define C3_FLOOR_PROFILE_H

#include <stddef.h>
#include <stdint.h>

/* Prepared-pack naming for each original floor the native scenes can load.
 * The floor IDs and archive identities come from the original lifecycle
 * table (common REL data 0x8BCF4); see docs/phenac-floors.md. A profile only
 * names packs: whether a floor is reachable is decided by the original
 * scripts and by the packs being present on the SD card. */
enum {
    C3_FLOOR_ARRIVAL = 1u,     /* <prefix>_arrival%u.c3t opening arrival packs */
    C3_FLOOR_TV = 2u,          /* <prefix>_tv.c3t render-to-texture television */
    C3_FLOOR_ROOM_CAMERA = 4u, /* <prefix>_camera.c3c authored event camera */
    C3_FLOOR_LIGHTS = 8u,      /* <prefix>.c3l sampled light animation */
    C3_FLOOR_WORLD_MAP = 16u   /* menu-class floor 0x38E, not a walkable field */
};

typedef struct C3FloorProfile {
    uint32_t floor;
    const char *prefix; /* <prefix>.c3f, .c3a, _wes.c3t, _map%u.c3t */
    const char *story;  /* C3S1 story pack file name */
    const char *label;  /* loading notice */
    uint32_t features;
} C3FloorProfile;

#define C3_FLOOR_WORLD_MAP_ID 0x38Eu

/* NULL when the floor has no profile. */
const C3FloorProfile *c3_floor_profile(uint32_t floor);

/* Original place names (boot/place_names.c3pn, tools/prepare_place_names.py):
 * the message each floor record names at +0x24, as UTF-8 (e-acute kept; the
 * bottom-screen font draws it). The table loads once; a missing or invalid
 * pack leaves the profile labels in place. */
#define C3_FLOOR_NAME_MAX 48
/* 1 when the pack's names are in use. A later call returns the first result
 * without reading again. */
int c3_floor_names_load(const char *path);
/* 1 when data is a valid pack; its names replace the current table. */
int c3_floor_names_parse(const void *data, size_t size);
/* Forget the loaded names (host tests). */
void c3_floor_names_reset(void);
/* The area header: the original place name, else the profile label, else
 * NULL when the floor has neither. */
const char *c3_floor_label(uint32_t floor);

#endif
