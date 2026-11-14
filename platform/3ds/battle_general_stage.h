#ifndef C3_BATTLE_GENERAL_STAGE_H
#define C3_BATTLE_GENERAL_STAGE_H
#include "stage_renderer.h"
#include "colosseum3ds/battle_presentation.h"
#include "colosseum3ds/battle3d_index.h"
#include "colosseum3ds/battle_general.h"
#include "colosseum3ds/particle.h"
#include "colosseum3ds/particle_pack.h"
#include "colosseum3ds/wzx_damage.h"
#include <stdio.h>

/* The 3D stage of a general story battle: any battle floor's arena, the
 * four battle positions' Pokemon and both trainers, composed at run time
 * from the private battle3d assets (tools/prepare_battle_stages.py).
 *
 * It plays the presentation events of src/battle_general_view.c: send-outs,
 * attack clips (PKX entry 2 physical / entry 1 special, by the move's type),
 * hit and faint clips, trainer win/loss clips. Every damaging hit plays the
 * original generic damage effect (WZX 04E32000: hit model, particles, floor
 * flash and sound 0x86) on its target when its private pack (boot/battle/
 * bite_damage.*) is present. Move-specific effects, the original battle
 * camera sequences and Pokemon cries are not part of this stage.
 * Switching species needs a reload (close, then load the new cast). */
#define C3_GSTAGE_MODELS 40u

typedef struct {
    uint16_t floor;
    uint16_t species[4];   /* battle positions 0,1 hero; 2,3 foe; 0 empty */
    uint16_t trainer[2];   /* hero, opponent trainer ids */
    uint8_t active_per_side;
    uint16_t intro_wzx;    /* the battle floor's appear special (0: none) */
} C3BattleGeneralCast;

typedef struct {
    C3Title models[C3_GSTAGE_MODELS];
    const C3Title *draw[C3_GSTAGE_MODELS];
    uint32_t count;
    uint32_t slots[C3_GSTAGE_MODELS];      /* 0..3 Pokemon, 4..5 trainers, UINT32_MAX arena, UINT32_MAX-1 effect */
    uint32_t roles[C3_GSTAGE_MODELS][9];
    uint32_t clips[C3_GSTAGE_MODELS];
    float clocks[C3_GSTAGE_MODELS], transforms[C3_GSTAGE_MODELS][5], appear[C3_GSTAGE_MODELS];
    uint8_t immutable[C3_GSTAGE_MODELS], sampled[C3_GSTAGE_MODELS], hold[C3_GSTAGE_MODELS];
    char files[C3_GSTAGE_MODELS][C3_BATTLE3D_NAME];
    C3BattleGeneralCast cast;
    C3TitleCamera camera;
    C3BattlePresentation presentation;
    const C3BattlePack *pack;
    /* Generic damage effect. */
    C3WzxDamageProgram damage_program;
    C3WzxDamage damage;
    C3ParticlePack particle_pack;
    C3ParticleSystem particles;
    uint8_t *particle_bytes;
    uint32_t damage_model, damage_owner, damage_sequence, cue_serial;
    float damage_particle_local[3];
    uint8_t damage_available, particles_started, initialized;
    uint32_t message_ms, missing_sound;
    /* General WZX effects (battle_general_wzx.inc): per position the PKX
     * body-part bones, timing marks and entry clips from the index. */
    struct C3BattleWzxLayer *wzx;
    uint8_t part_bone[6][17];
    float timing[6][C3_BATTLE3D_ENTRIES][4];
    uint32_t entry_clip[6][C3_BATTLE3D_ENTRIES];
    char wzx_damage[48];
    uint32_t wzx_damage_target;
    void *wzx_damage_run;
    uint8_t wzx_attack_wait, wzx_hp_wait, wzx_move;
    uint32_t wzx_id; /* move id, or 0x1000|special, of the sequence being started */
    int (*sound_cue)(uint32_t, uint32_t, char *, size_t);
    char error[192];
    FILE *trace;
} C3BattleGeneralStage;

/* Which cast members lack prepared assets: 0 when the whole cast can be
 * shown, else a short reason (for the 2D fallback trace). */
const char *c3_battle_general_stage_missing(const C3Battle3dIndex *, const C3BattleGeneralCast *);
/* root: the battle3d directory; effects: the directory holding the generic
 * damage effect pack (optional, may be NULL). hp: per position at the
 * start of the first batch. */
int c3_battle_general_stage_load(C3BattleGeneralStage *, const C3Battle3dIndex *, const C3BattleGeneralCast *,
                                 const C3BattlePack *, const char *root, const char *effects,
                                 const uint16_t hp[4], FILE *, char *, size_t);
int c3_battle_general_stage_submit(C3BattleGeneralStage *, const C3BattlePreviewEvent *, uint32_t);
/* Put a different species at a Pokemon position without reloading the
 * stage: the caller drains the GPU first; only that position's pack is
 * loaded and the renderer restarts over the kept models. The position stays
 * hidden until its send-out plays. 0 (error set) when it cannot: reload. */
int c3_battle_general_stage_swap(C3BattleGeneralStage *, const C3Battle3dIndex *, unsigned slot, uint16_t species,
                                 const char *root, char *, size_t);
/* Load the effect sequences this batch's moves use (attack, damage and
 * special roots for the actual owners) and restart the renderer with them.
 * The caller drains the GPU first. Without a wzx.c3n nothing changes. */
int c3_battle_general_stage_prepare(C3BattleGeneralStage *, const C3BattlePreviewEvent *, uint32_t count,
                                    const char *root, char *, size_t);
/* Command menu open (1) or turn playback (0): the original camera switches
 * to its simple mode and the stage shows the command view. */
void c3_battle_general_stage_command_phase(C3BattleGeneralStage *, int on);
/* Whether the submitted batch has been fully presented. */
int c3_battle_general_stage_idle(const C3BattleGeneralStage *);
int c3_battle_general_stage_step(C3BattleGeneralStage *, uint32_t ms, char *, size_t);
int c3_battle_general_stage_draw(C3BattleGeneralStage *, C3TitleRenderStats *, char *, size_t);
/* Poll until true before releasing storage (GPU work may be in flight). */
int c3_battle_general_stage_close(C3BattleGeneralStage *);
#endif
