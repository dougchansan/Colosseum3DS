#ifndef C3_WZX_CAMERA_H
#define C3_WZX_CAMERA_H

#include "colosseum3ds/title.h"
#include <stddef.h>
#include <stdint.h>

/* The original battle camera for any waza sequence (WZX), as one general
 * module. Three paths, all driven by 60 Hz source ticks (1000 units each,
 * fractions retained):
 *
 * - procedural (battleCameraStartWaza 0x801D30BC without a camera DAT):
 *   _CalculateParams 0x801D4FB8, _SelectMotion 0x801D4DA0, _DoPosition
 *   0x801D349C (+ _DoDollyPosition 0x801D3F7C), _DoFOV 0x801D44C4 with
 *   GetPattern 0x801D2B4C and _SelectDuration 0x801D49D8, then per tick
 *   fn_801D2D28 (body-map part tracking with cameraMoveTargetOfs(7, ofs,
 *   0.2) and the two FOV keys) and camera.c's mode-7 cameraUpdate (linear
 *   target/position/rotation moves, eye = position + RotY(yaw)(0,H,D));
 * - camera DAT (the C3WX camera block): cameraPlayOffsetAnime 0x80176C78 +
 *   _cameraOffsetAnimeUpdate: samples scaled, rotated by the owner yaw,
 *   mirrored in z for negative-facing owners (unless flags 0x80) and offset
 *   by the owner (bound centre for 0x4000, root null y for damage), or in
 *   world space for flags 4;
 * - idle (battleCameraStartRandom 0x801C2D80): every 200 ticks with no
 *   camera active, behind a random gate, a random trainer/Pokemon other
 *   than the last runs the procedural camera with params flags 4.
 *
 * Geometry is computed in original GC units (native = GC * 0.1) from native
 * inputs. The random stream is COSMETIC and separate from the battle RNG:
 * it keeps the source draw order, not the original shared stream's values.
 * The DOL tables come from tools/prepare_wzx_camera.py (wzx_camera.c3k). */

#define C3_WZX_CAMERA_SHORT_ROWS 8u
#define C3_WZX_CAMERA_LONG_ROWS 13u
#define C3_WZX_CAMERA_ROW_WORDS 19u
#define C3_WZX_CAMERA_FILE_BYTES 1672u
#define C3_WZX_CAMERA_MAX_ACTORS 12u
#define C3_WZX_CAMERA_ENTRIES 17u
#define C3_WZX_CAMERA_MAX_STEP_TICKS 3600u
#define C3_WZX_CAMERA_IDLE_TICKS 200

typedef struct {
    /* lbl_80371F60 (short, 8 rows) / lbl_803721C0 (long, 13 rows): word 0 the
     * float weight, then two 9-word FOV segments {mode, duration mode,
     * thresholds[4] (float), initial mask, mask, timing mode}. */
    uint32_t short_rows[C3_WZX_CAMERA_SHORT_ROWS][C3_WZX_CAMERA_ROW_WORDS];
    uint32_t long_rows[C3_WZX_CAMERA_LONG_ROWS][C3_WZX_CAMERA_ROW_WORDS];
    /* fn_801DABAC scale per owner selector: [0]=-2 [1]=-1 [2]=1 [3]=2 [4]=3
     * [5]=other (lbl_8047E388..lbl_8047E39C). */
    float scale[6];
    float idle_gate;               /* lbl_8047DF58: idle cut when random <= gate */
    int32_t initial_previous_motion; /* lbl_80478CD8 .sdata initial value */
    uint32_t initial_idle_last;    /* lbl_80478CAC */
    int32_t initial_idle_timer;    /* lbl_80478CA8 */
} C3WzxCameraTables;

typedef struct {
    float position[3], yaw;      /* native root transform (GSmodelGetPosition, rotation y) */
    float bound_min[3], bound_max[3]; /* native, model bound relative to the root (GSmodelGetBound) */
    int32_t source_facing;       /* owner byte 0x76 < 0 -> -1, else 1 */
    int32_t scale_selector;      /* owner +0x10 (-2..3) */
    uint32_t model_id;           /* owner u16 +0x70; 0x13A forces motion 4 */
    uint8_t present;             /* idle candidates: occupied grid slot */
    uint8_t is_trainer;          /* owner byte 0x75 == 0: trainer count override, durations << 1 */
    uint16_t pokemon_count;      /* trainers: battleGridGetNumPokemonsForTrainer */
    uint8_t root_null;           /* owner byte 0x18 & 2 and GSmodelIsRootNullAdded */
    float root_null_y;           /* native, GSmodelGetRootPosition y */
    uint32_t entry;              /* current PKX entry (owner +0x32); the idle camera uses it */
    /* PKX timing rows (table row +0x0C..+0x18, 60 Hz ticks; index.c3n v2
     * timing) and value counts (row +0; 0 = 1 + last nonzero mark). */
    int32_t timing[C3_WZX_CAMERA_ENTRIES][4];
    uint8_t timing_count[C3_WZX_CAMERA_ENTRIES];
} C3WzxCameraActor;

typedef struct {
    C3WzxCameraActor actors[C3_WZX_CAMERA_MAX_ACTORS]; /* battle grid order: trainer, its Pokemon, ... */
    uint32_t count;
    /* World position (native) of an actor's body-map slot for its current
     * entry, sampled from the model's retained part bones. 0: no such part
     * (the original then skips that tick's retarget). */
    int (*part_world)(void *user, unsigned actor, unsigned body_slot, float out[3]);
    void *user;
    float grid_scale[3];         /* battleGridGetNormalisedScale (camera DAT flags 0x00800000) */
    C3TitleCamera base;          /* floor camera: the idle cuts' inherited camera */
} C3WzxCameraScene;

typedef struct {
    uint32_t flags, part_sel;    /* C3WX header (loader fix-ups applied) */
    uint32_t entry;              /* owner PKX entry after the loader's 0xB/0xC..0xF substitution */
    uint32_t kind;               /* sequence +0x2E: 1 attack, 2 damage, 3 field/self, 4 special */
    uint32_t id;                 /* sequence +0x2C: move or special id */
    const uint8_t *camera;       /* C3WX camera block (36-byte LE samples) or NULL: procedural */
    uint32_t camera_count;
} C3WzxCameraSequence;

typedef struct {
    float start[3], end[3];
    uint32_t ticks, duration;    /* source ticks; snaps when ticks >= duration */
    uint8_t active;
} C3WzxCameraMove;

enum { C3_WZX_CAMERA_NONE = 0, C3_WZX_CAMERA_PROCEDURAL = 1, C3_WZX_CAMERA_DAT = 2 };

typedef struct {
    const C3WzxCameraTables *tables;
    C3TitleCamera camera, restored; /* output; inherited camera restored by end */
    uint32_t rng, random_draws, cuts, ticks, remainder;
    int32_t previous_motion;     /* lbl_80478CD8, shared by every cut */
    uint8_t mode, idle_cut, holding, reverse, shift, special_origin;
    uint32_t owner, part, entry, motion, params_flags;
    /* _CalculateParams (GC units) */
    float size, height, rotation_min, rotation_max, rotation_base;
    float height_min, height_max, distance_min, distance_max, range[3];
    /* camera.c state, GC units: position P, view V, eye ("direction") E */
    float position[3], view[3], eye[3], yaw, cam_height, cam_distance;
    C3WzxCameraMove target_move, eye_move, yaw_move;
    uint8_t moving;
    /* FOV keys lbl_804673D4, clock lbl_8047B3E8, fov lbl_80478CDC */
    float fov, key_start[2], key_end[2];
    uint32_t key_begin[2], key_finish[2], fov_clock;
    /* camera DAT */
    const uint8_t *samples;
    uint32_t sample_count, sample;
    float dat_offset[3], dat_yaw, dat_scale[3];
    uint8_t dat_mirror;
    /* idle scheduler: lbl_8047B399 simple, lbl_8047B398 disabled, timer, last */
    uint8_t idle_simple, idle_disabled;
    int32_t idle_timer;
    uint32_t idle_last;
} C3WzxCamera;

int c3_wzx_camera_tables_decode(C3WzxCameraTables *out, const void *bytes, size_t size, char *error, size_t capacity);
int c3_wzx_camera_tables_load(C3WzxCameraTables *out, const char *path, char *error, size_t capacity);

/* tables must outlive the camera. The idle scheduler starts in full mode. */
void c3_wzx_camera_init(C3WzxCamera *camera, const C3WzxCameraTables *tables, uint32_t cosmetic_seed);
/* wazaSequenceStart -> battleCameraStartWaza(owner, sequence). Disables the
 * idle scheduler (battleCameraDisable) until c3_wzx_camera_idle_reset. A cut
 * while a camera is running keeps the earlier inherited camera. Failure
 * leaves the camera unchanged. */
int c3_wzx_camera_begin_sequence(C3WzxCamera *camera, const C3WzxCameraScene *scene, unsigned owner,
                                 const C3WzxCameraSequence *sequence, const C3TitleCamera *inherited,
                                 char *error, size_t capacity);
/* fn_801D2C74 -> battleCameraStartWaza(owner, NULL): procedural, params flags 4. */
int c3_wzx_camera_begin_idle(C3WzxCamera *camera, const C3WzxCameraScene *scene, unsigned owner,
                             const C3TitleCamera *inherited, char *error, size_t capacity);
/* Advance (1000 units per tick, max 3600 ticks per call): idle scheduler,
 * then the running camera. An idle cut that finishes its moves holds the
 * last pose (the original's scene mode 8 with no animation). */
int c3_wzx_camera_step_units(C3WzxCamera *camera, const C3WzxCameraScene *scene, uint64_t units,
                             char *error, size_t capacity);
int c3_wzx_camera_step(C3WzxCamera *camera, const C3WzxCameraScene *scene, uint32_t milliseconds,
                       char *error, size_t capacity);
/* Sequence finished or cancelled (fn_801D3034): restores the inherited camera. */
void c3_wzx_camera_end(C3WzxCamera *camera);
/* fn_801EF8F4 + fn_801C2F00: simple (command menu) or full mode, re-enable
 * the idle scheduler and reload its 200-tick timer. */
void c3_wzx_camera_idle_reset(C3WzxCamera *camera, int simple);
/* 1 while a camera (or a held idle pose) drives camera->camera. */
int c3_wzx_camera_active(const C3WzxCamera *camera);

/* The battle base camera (fn_801C2F00: cameraPlayOffsetAnime of
 * fight_common.fsys 0x007B1800, looped, resumed at its current frame every
 * command phase): one sample per 60 Hz tick from
 * tools/prepare_battle_home_camera.py (battle_home.c3h, "C3WH" v1). */
typedef struct {
    uint8_t *bytes;     /* owned copy of the pack */
    uint32_t count;     /* samples: eye xyz, target xyz (native units), roll, FOV */
    float scale[4];     /* lbl_8047DF5C/60/64 (largest field 1, 2, 3), lbl_8047DF68 otherwise */
} C3WzxHomeCamera;

int c3_wzx_home_camera_decode(C3WzxHomeCamera *out, const void *bytes, size_t size, char *error, size_t capacity);
int c3_wzx_home_camera_load(C3WzxHomeCamera *out, const char *path, char *error, size_t capacity);
void c3_wzx_home_camera_free(C3WzxHomeCamera *camera);
/* The pose `units` (1000 per tick) into the loop, scaled about the arena
 * origin; eye, target, roll (0: the source's offset animation keeps the up
 * vector) and FOV change, the lens planes and aspect of *out stay. */
void c3_wzx_home_camera_sample(const C3WzxHomeCamera *camera, uint64_t units, float scale, C3TitleCamera *out);

#endif
