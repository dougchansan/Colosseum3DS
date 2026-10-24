#ifndef COLOSSEUM3DS_STAGE_RENDERER_H
#define COLOSSEUM3DS_STAGE_RENDERER_H

#include "title_renderer.h"
#include "colosseum3ds/particle.h"
#include "colosseum3ds/particle_pack.h"
#include "colosseum3ds/wzx_confusion.h"

#define C3_STAGE_MAX_MODELS 16u
#define C3_STAGE_MAX_VERTICES 131072u
#define C3_STAGE_MAX_TEXTURES 128u
#define C3_STAGE_MAX_PARTICLE_BANKS 4u

/* Pinned SDK steady particle draw uses 114 words; 192 also bounds setup.
 * Leave room for the subsequent HUD/fade and command-list finalization. */
#define C3_STAGE_PARTICLE_COMMAND_WORDS 192u
#define C3_STAGE_COMMAND_RESERVE_WORDS 8192u
static inline bool c3_stage_particle_segment_fits(uint32_t used,uint32_t visible,uint32_t capacity)
{
    return (uint64_t)used + (uint64_t)visible * C3_STAGE_PARTICLE_COMMAND_WORDS +
        C3_STAGE_COMMAND_RESERVE_WORDS <= capacity;
}
static inline bool c3_stage_particle_commands_fit(uint32_t used,uint32_t visible)
{
    return c3_stage_particle_segment_fits(used,visible,C3_RENDER_COMMAND_CAPACITY_WORDS);
}

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
/* Optional type12 capture resources; upload once before any scene draw. The
 * decoded program and live state are borrowed through guarded shutdown.
 * NULL state hides the effect. Allocation failure preserves the prior binding. */
int c3_stage_renderer_confusion_program(const C3WzxConfusionProgram *,char *,size_t);
int c3_stage_renderer_confusion(const C3ConfusionType12 *);
int c3_stage_renderer_draw(const C3Title *const *models, size_t count,
                          const C3TitleCamera *camera, float fade,
                          C3TitleRenderStats *stats, char *error, size_t capacity);
/* An opt-in immutable single-frame pack; disable before changing any sampled
 * vertices/materials/lights. Camera-dependent shading still invalidates. */
int c3_stage_renderer_set_static(size_t model_index, bool enabled);
/* Source GSmaterial RGB modulation is 2*color*previous, not an opaque overlay.
 * NULL disables. Render-side only, no asset/animation mutation. */
int c3_stage_renderer_set_modulation(size_t model_index, const uint8_t rgb[3]);
/* Upload once before first draw. Borrowed pack/system outlive guarded teardown.
 * Updating the live attachment only copies CPU state; GPU buffers are written
 * after the sole nonblocking FrameBegin completion barrier. */
/* Register up to four borrowed banks (Bite, Reflect, Confusion attack/damage)
 * before first draw. Re-registering the
 * same pack pointer is idempotent. Binding selects by bank.bytes identity. */
int c3_stage_renderer_particle_pack(const C3ParticlePack *,char *,size_t);
int c3_stage_renderer_particles(const C3ParticleSystem *,const float attachment[12]);
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
