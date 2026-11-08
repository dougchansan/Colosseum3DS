#ifndef COLOSSEUM3DS_WZX_DAMAGE_H
#define COLOSSEUM3DS_WZX_DAMAGE_H
#include <stddef.h>
#include <stdint.h>
#define C3_WZX_DAMAGE_BYTES 256u
#define C3_WZX_DAMAGE_MAX_TICKS 3600u
/* Exact admitted Bite defender C3WD v2 profile, not a general WZX VM.
 * Body-map particle part12 resolves to sampled native bone11 in this pack.
 * SHA1 bindings must be checked against the actual loaded model/particle packs. */
typedef struct {
    uint32_t owner_clip,model_clip,model_start,particle_start,sound_start;
    uint32_t floor_start,floor_duration,hp_start,owner_end,model_frames;
    uint32_t owner_start,normalization_shift;
    float owner_rate,model_rate,anchor[3];
    uint32_t model_part,particle_part,particle_bone;
    uint32_t floor_from,floor_hold,floor_to,floor_hold_ticks,floor_ramp_ticks;
    uint32_t sound_id,particle_flags,root_generator;
    uint8_t model_sha1[20],particles_sha1[20];
} C3WzxDamageProgram;
typedef struct {
    C3WzxDamageProgram program;
    uint64_t units; /* milliseconds*60;1000units per original source tick. */
    uint32_t model_starts,particle_starts,floor_starts,sound_requests,hp_gates;
    uint8_t active,model_visible,floor_enabled,floor_rgb[3];
    uint8_t model_finished,floor_finished,owner_minimum_complete,completed;
    float owner_seconds,model_seconds;
} C3WzxDamage;
/* Failures preserve outputs. Error storage must not alias inputs/outputs. */
int c3_wzx_damage_decode(C3WzxDamageProgram *,const void *,size_t,char *,size_t);
int c3_wzx_damage_load(C3WzxDamageProgram *,const char *,char *,size_t);
int c3_wzx_damage_begin(C3WzxDamage *,const C3WzxDamageProgram *);
/* Paused callers omit step. Oversized elapsed time fails without mutation,
 * never truncates elapsed time or silently drops crossed authored events. */
int c3_wzx_damage_step(C3WzxDamage *,uint32_t milliseconds);
/* Owner tick380 is only a minimum: caller must confirm all three consumers
 * finished (including surviving particles). Returns1 only on completion. */
int c3_wzx_damage_finish(C3WzxDamage *,int model_done,int particles_done,int floor_done);
void c3_wzx_damage_cancel(C3WzxDamage *);
#endif
