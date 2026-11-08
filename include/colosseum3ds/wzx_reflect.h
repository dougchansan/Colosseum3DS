#ifndef COLOSSEUM3DS_WZX_REFLECT_H
#define COLOSSEUM3DS_WZX_REFLECT_H
#include <stddef.h>
#include <stdint.h>
#define C3_WZX_REFLECT_BYTES 288u
#define C3_WZX_REFLECT_PARTS_BYTES 3728u
#define C3_WZX_REFLECT_MAX_TICKS 3600u
#define C3_WZX_REFLECT_NO_ACTOR 4u
/* Exact C3WR v2 profile. Effect clip0 contains texture animation0 with static
 * rest joints (original joint animation-1); actor idle remains independently
 * sampled. Caller supplies live original source-part POSITION, not a compact
 * sampled bone index. One caster instance completes before the ally starts. */
typedef struct {
    uint32_t owner_clip,model_clip,owner_minimum,ally_minimum,texture_end;
    uint32_t floor_duration,darken_ticks,hold_ticks,restore_ticks,floor_from,floor_dark;
    uint32_t sound_id,root_generator,particle_flags,attachment,position_type;
    uint32_t body_map_index,espeon_part,umbreon_part;
    float owner_rate,model_rate;
    uint8_t model_sha1[20],particles_sha1[20],parts_sha1[20];
    uint8_t *parts_data; /* Owned immutable C3RP bytes; zero-init before decode. */
    size_t parts_bytes;
} C3WzxReflectProgram;
typedef struct {
    C3WzxReflectProgram program; /* Borrowed track storage, never close this copy. */
    uint64_t units;
    uint32_t species,owner_clip,owner_minimum,source_part;
    uint32_t model_starts,particle_starts,floor_starts,sound_requests;
    uint8_t active,model_visible,floor_enabled,floor_rgb[3];
    uint8_t model_finished,floor_finished,owner_minimum_complete,completed;
    float owner_seconds,model_seconds;
} C3WzxReflect;
int c3_wzx_reflect_decode(C3WzxReflectProgram *,const void *,size_t,char *,size_t);
int c3_wzx_reflect_load(C3WzxReflectProgram *,const char *,char *,size_t);
/* Parts decode/load preserve prior storage on failure. Program close must
 * happen only after all clock states borrowing its immutable tracks cancel.
 * No actor/world/model pack regeneration is required. */
int c3_wzx_reflect_parts_decode(C3WzxReflectProgram *,const void *,size_t,char *,size_t);
int c3_wzx_reflect_parts_load(C3WzxReflectProgram *,const char *,char *,size_t);
void c3_wzx_reflect_program_close(C3WzxReflectProgram *);
/* source_idle_seconds is the authored clock (local elapsed*.5), matching
 * actor idle sampling. Loop endpoint semantics match c3_title_animate.
 * Root position/rotation/uniform scale is applied after local interpolation.
 * Failure preserves outXYZ. Program/parts must outlive all calls. */
int c3_wzx_reflect_position(const C3WzxReflectProgram *,uint32_t internal_species,
    float source_idle_seconds,const float rootXYZ[3],float yaw,float scale,
    float outXYZ[3],char *error,size_t capacity);
int c3_wzx_reflect_begin(C3WzxReflect *,const C3WzxReflectProgram *,uint32_t internal_species);
/* Paused callers omit step. Overload rejects the elapsed time unchanged. */
int c3_wzx_reflect_step(C3WzxReflect *,uint32_t milliseconds);
int c3_wzx_reflect_finish(C3WzxReflect *,int model_done,int particles_done,int floor_done,int audio_done);
void c3_wzx_reflect_cancel(C3WzxReflect *);
/* Player actor slots0/1 only: returns caster then valid ally, sequentially.
 * alive_mask is the state at the committed source sequence start. */
unsigned c3_wzx_reflect_actor(unsigned caster,unsigned alive_mask,unsigned index);
#endif
