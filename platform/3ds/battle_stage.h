#ifndef C3_BATTLE_STAGE_H
#define C3_BATTLE_STAGE_H
#include "stage_renderer.h"
#include "colosseum3ds/battle_presentation.h"
#include <stdio.h>
#include "colosseum3ds/wzx_bite.h"
#include "colosseum3ds/wzx_damage.h"
#include "colosseum3ds/wzx_reflect.h"
#include "colosseum3ds/wzx_confusion_sequence.h"
#include "colosseum3ds/wzx_confusion.h"
#include "colosseum3ds/battle_camera.h"
#include "colosseum3ds/wzx_tail_whip.h"
#include "colosseum3ds/wzx_growl.h"
#include "colosseum3ds/wzx_stat_down.h"
#include "colosseum3ds/wzx_tackle.h"
#define C3_BATTLE_STAGE_MODELS 16u
typedef struct {
    C3Title models[C3_BATTLE_STAGE_MODELS];
    const C3Title *draw[C3_BATTLE_STAGE_MODELS];
    uint32_t count, slots[C3_BATTLE_STAGE_MODELS], roles[C3_BATTLE_STAGE_MODELS][9];
    uint32_t clips[C3_BATTLE_STAGE_MODELS];
    float transforms[C3_BATTLE_STAGE_MODELS][5], clocks[C3_BATTLE_STAGE_MODELS];
    C3TitleCamera camera;
    C3BattlePresentation presentation;
    uint8_t initialized, fresh[C3_BATTLE_STAGE_MODELS];
    uint8_t immutable[C3_BATTLE_STAGE_MODELS], sampled[C3_BATTLE_STAGE_MODELS];
    C3WzxBiteProgram bite_program;
    C3WzxBite bite;
    uint32_t bite_model, bite_owner;
    uint8_t bite_available;
    C3WzxDamageProgram damage_program;
    C3WzxDamage damage;
    C3ParticlePack particle_pack;
    C3ParticleSystem particles;
    uint8_t *particle_bytes;
    uint32_t damage_model, damage_owner, bite_sequence, bite_group;
    float damage_particle_local[3];
    uint8_t damage_available, damage_pending, damage_hit, particles_started;
    C3BattleCameraProgram camera_program;
    C3BattleCamera source_camera;
    uint8_t camera_available;
    C3WzxReflectProgram reflect_program;
    C3WzxReflect reflect;
    C3ParticlePack reflect_pack;
    uint8_t *reflect_bytes;
    uint32_t reflect_model, reflect_owner, reflect_event, reflect_group;
    uint32_t cue_serial, reflect_cue;
    unsigned reflect_alive, reflect_index;
    uint8_t reflect_available, reflect_particles;
    C3BattleCameraProgram confusion_camera_program;
    C3BattleCamera confusion_camera;
    uint8_t confusion_camera_available;
    C3WzxConfusionSequenceProgram confusion_program;
    C3WzxConfusionProgram confusion_mask;
    C3ConfusionType12 confusion_type12;
    C3ParticlePack confusion_pack[2];
    C3ParticleSystem *confusion_damage_particles;
    uint8_t *confusion_bytes[2];
    uint64_t confusion_units;
    uint32_t confusion_owner, confusion_target, confusion_event, confusion_group;
    uint32_t confusion_seen, confusion_cues[4], confusion_roots[5];
    float confusion_idle;
    uint8_t confusion_available, confusion_active, confusion_phase;
    uint8_t confusion_pending, confusion_hit, confusion_hp_gate;
    uint8_t confusion_particles_started, confusion_floor_enabled, confusion_floor_rgb[3];
    C3WzxTailWhipProgram tail_program;
    C3WzxTailWhip tail;
    C3BattleCameraProgram tail_camera_program;
    C3BattleCamera tail_camera;
    uint32_t tail_model, tail_owner, tail_cue, tail_event;
    uint8_t tail_available, tail_camera_available;
    C3WzxGrowlProgram growl_program;
    C3WzxGrowl growl;
    C3ParticlePack growl_pack;
    uint8_t *growl_bytes;
    C3BattleCameraProgram growl_camera_program;
    C3BattleCamera growl_camera;
    uint32_t growl_model, growl_owner, growl_cue, growl_event;
    uint8_t growl_available, growl_camera_available, growl_particles_started;
    uint8_t growl_teardown_started, growl_keyoff_sent;
    uint32_t growl_audio_wait_ms;
    uint8_t growl_motion[C3_BATTLE_STAGE_MODELS]; /* Historical name: common half-rate carrier. 1=Growl,2=idle,3=Tackle cast,4=Tackle hit. */
    uint64_t growl_motion_units[C3_BATTLE_STAGE_MODELS];
    uint32_t owner_motion_end[C3_BATTLE_STAGE_MODELS]; /* Source ticks; Tackle natural end, zero for legacy Growl. */
    C3WzxTackleProgram tackle_program;
    C3WzxTackle tackle;
    C3ParticlePack tackle_pack;
    uint8_t *tackle_bytes;
    C3BattleCameraProgram tackle_camera_program;
    C3BattleCamera tackle_camera;
    uint32_t tackle_owner,tackle_target,tackle_event,tackle_group,tackle_cues[2];
    uint8_t tackle_available,tackle_camera_available,tackle_hit,tackle_particles_started,tackle_keyoff[2];
    C3WzxStatDownProgram attack_down_program;
    C3ParticlePack attack_down_pack;
    uint8_t *attack_down_bytes;
    C3BattleCameraProgram attack_down_camera_program;
    C3BattleCamera attack_down_camera;
    uint8_t attack_down_available, attack_down_camera_available;
    C3WzxStatDownProgram stat_down_program;
    C3WzxStatDown stat_down;
    C3ParticlePack stat_down_pack;
    uint8_t *stat_down_bytes;
    C3BattleCameraProgram stat_down_camera_program;
    C3BattleCamera stat_down_camera;
    uint32_t stat_down_owner, stat_down_event, stat_down_cue;
    uint8_t stat_down_available, stat_down_camera_available;
    uint8_t stat_down_prior_enabled, stat_down_color_owned, stat_down_motion[2];
    uint64_t stat_down_motion_units[2];
    uint32_t stat_down_motion_period[2];
    float stat_down_motion_rate[2];
    uint8_t stat_down_teardown_started, stat_down_keyoff_sent;
    uint32_t stat_down_audio_wait_ms;
    int (*sound_done)(uint32_t,uint32_t,char *,size_t);
    int (*sound_cue)(uint32_t,uint32_t,char *,size_t);
    int (*sound_status)(uint32_t,uint32_t,char *,size_t);
    int (*sound_stop)(uint32_t,uint32_t,char *,size_t);
    char error[192];
    FILE *trace;
} C3BattleStage;
/* Descriptor and all paths are private prepared assets. No guessed fallback. */
int c3_battle_stage_load(C3BattleStage *,const char *root,const uint16_t hp[4],FILE *,char *,size_t);
int c3_battle_stage_submit(C3BattleStage *,const C3BattlePreviewEvent *,uint32_t);
int c3_battle_stage_step(C3BattleStage *,uint32_t,char *,size_t);
int c3_battle_stage_draw(C3BattleStage *,C3TitleRenderStats *,char *,size_t);
/* Caller polls until true before releasing storage or restoring field. */
int c3_battle_stage_close(C3BattleStage *);
#endif
