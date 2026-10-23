#ifndef COLOSSEUM3DS_STAGE_RENDERER_H
#define COLOSSEUM3DS_STAGE_RENDERER_H

#include "title_renderer.h"

#define C3_STAGE_MAX_MODELS 16u
#define C3_STAGE_MAX_VERTICES 131072u
#define C3_STAGE_MAX_TEXTURES 128u

/* Development HUD uses the libctru font, not the original game's font/UI.
 * Copied commands are drawn inside the sole stage frame, never CPU framebuffer
 * writes. Set while idle before the first frame; later updates only copy data. */
typedef struct {
    char lines[8][49];
    uint16_t hp[4], maximum_hp[4];
    uint8_t enabled;
} C3StageBattleHud;
int c3_stage_renderer_set_battle_hud(const C3StageBattleHud *hud, char *error, size_t capacity);

/* Implemented by the shared renderer in title_renderer.c. Packs retain their
 * independent animation clocks and bounded C3Title storage. All packs must
 * outlive this renderer, and init/draw must use the same ordered identities.
 * The renderer owns one GPU frame and disjoint arena ranges for every pack.
 * It cannot coexist with an initialized title renderer. */
int c3_stage_renderer_init(const C3Title *const *models, size_t count,
                          char *error, size_t capacity);
/* Source field-camera texture entry0, fixed 256x256 RGBA8 plus depth. Call
 * after init and before the first draw. The TV model stays excluded from the
 * main camera even when disabled. Repeated calls may toggle enabled but may
 * not change identities. The model's sampled camera drives the offscreen
 * pass. Returns 1 on success, 0 on failure; failure preserves prior binding. */
int c3_stage_renderer_set_tv(size_t model_index, size_t screen_model_index,
                             uint32_t screen_texture_index, bool enabled,
                             char *error, size_t capacity);
int c3_stage_renderer_draw(const C3Title *const *models, size_t count,
                          const C3TitleCamera *camera, float fade,
                          C3TitleRenderStats *stats, char *error, size_t capacity);
void c3_stage_renderer_shutdown(void);

/* Application shutdown only, after the scene's guarded teardown completed.
 * Releases the persistent shader program and Citro3D context. */
void c3_stage_renderer_fini(void);

/* Nonblocking completion barrier. Busy leaves all renderer ownership intact.
 * try_shutdown deletes resources only after this barrier succeeds. Requires
 * sole Citro3D ownership and no frame-end hook that submits GPU work. */
bool c3_stage_renderer_is_idle(void);
bool c3_stage_renderer_try_shutdown(void);

#ifdef C3_GPU_DIAGNOSTICS
/* Standalone diagnostic executable only: memory clear and display transfer,
 * with no shader command list. Normal game builds do not expose this path. */
int c3_stage_renderer_probe_clear(void);
#endif

#endif
