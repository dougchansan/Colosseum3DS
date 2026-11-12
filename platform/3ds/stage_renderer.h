#ifndef COLOSSEUM3DS_STAGE_RENDERER_H
#define COLOSSEUM3DS_STAGE_RENDERER_H

#include "title_renderer.h"
#include "colosseum3ds/particle.h"
#include "colosseum3ds/particle_pack.h"
#include "colosseum3ds/wzx_confusion.h"

/* Sized for Phenac City's exterior: 8 map packs, Wes and 26 people are 35
 * models, 128,358 vertices and 75 unique textures (7.19 MB of arena plus
 * 8.79 MB of RGBA8, 15.98 MB of the 32 MiB linear heap). A model is only a
 * range of the shared arena plus a texture remap, so the model bound costs
 * static bookkeeping (about 6 KiB per model), not GPU objects. The arena is
 * allocated for the scene's actual vertex total; at the bound it is 11.0 MB. */
#define C3_STAGE_MAX_MODELS 48u
#define C3_STAGE_MAX_VERTICES 196608u
#define C3_STAGE_MAX_TEXTURES 128u
#define C3_STAGE_MAX_PARTICLE_BANKS 8u

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
/* Before init: models (bit per index) that may get a GPU skin stream. Each
 * costs 48 bytes of linear memory per vertex; the request applies to the
 * next init only. */
void c3_stage_renderer_request_gpu_skin(uint64_t models);
/* After init: the batches of model the GPU can skin and light
 * (colosseum3ds/skin_uniforms.h). A caller may copy it into that retained
 * title's gpu_batches; zero when nothing qualifies. */
uint64_t c3_stage_renderer_gpu_skin_mask(size_t model);
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
/* Source GSmodelEnableColorSwap texture RGB swap table. Set before the first
 * draw. Source CPU assets and alpha remain unchanged; route3 samples alpha.
 * Intended for a sole PDA actor split across the initialized model parts. */
int c3_stage_renderer_set_texture_routing(const uint8_t route[3],char *,size_t);
/* Snapshot exact enabled state before a source owner-color sequence. */
int c3_stage_renderer_get_modulation(size_t model_index, uint8_t rgb[3], int *enabled);
/* Upload once before first draw. Borrowed pack/system outlive guarded teardown.
 * Updating the live attachment only copies CPU state; GPU buffers are written
 * after the sole nonblocking FrameBegin completion barrier. */
/* Register up to eight borrowed banks (including qualified move profiles)
 * before first draw. Re-registering the
 * same pack pointer is idempotent. Binding selects by bank.bytes identity. */
int c3_stage_renderer_particle_pack(const C3ParticlePack *,char *,size_t);
int c3_stage_renderer_particles(const C3ParticleSystem *,const float attachment[12]);
/* Explicit source-world Growl adapter. Only AppSRT render caches may change;
 * simulation coordinates, clocks and cosmetic RNG remain independent of draws. */
int c3_stage_renderer_particles_app_srt(C3ParticleSystem *);
/* Tackle only: display commits source list sort through explicit mutable
 * ownership. Does not change clocks, RNG or particle fields. */
int c3_stage_renderer_particles_source_order(C3ParticleSystem *);
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
