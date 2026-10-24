#ifndef C3_BATTLE_CAMERA_H
#define C3_BATTLE_CAMERA_H
#include "colosseum3ds/title.h"
#include <stddef.h>
#include <stdint.h>
#define C3_BATTLE_CAMERA_HEADER_BYTES 512u
#define C3_BATTLE_CAMERA_MAX_TRACK_FRAMES 1025u
#define C3_BATTLE_CAMERA_MAX_BYTES (1500u + 6u * C3_BATTLE_CAMERA_MAX_TRACK_FRAMES * 12u)
#define C3_BATTLE_CAMERA_ACTORS 3u
#define C3_BATTLE_CAMERA_MAX_TICKS 3600u
typedef enum { C3_BATTLE_CAMERA_ATTACK=0, C3_BATTLE_CAMERA_DAMAGE=1 } C3BattleCameraPhase;
typedef struct {
    uint32_t species, timing[2][4], part[2], animation_kind[2], track_offset[2], track_count[2];
    int32_t scale_selector;
    float minimum[3], maximum[3]; /* Original GC units, from GSmodel bound sampling. */
} C3BattleCameraBinding;
typedef struct {
    uint32_t move_id, patterns[13][19]; /* move0: Bite; move93: long13; move39: short8 (exact LE source tables). */
    uint32_t flags[2], pattern[2], fov_mask[2], entry[2], owner_start[2];
    C3BattleCameraBinding actors[C3_BATTLE_CAMERA_ACTORS];
    float (*samples)[3]; /* Owned immutable source JObj camera-part local positions, native units. */
    uint32_t sample_count;
    uint8_t owned;
} C3BattleCameraProgram;
typedef struct {
    uint32_t slot, species;
    int32_t source_facing; /* Original owner byte0x76: battleGrid group.arg1 ? -1 : 1. */
    float position[3], yaw; /* Native root transform; source body-map tracks are internal. */
} C3BattleCameraActor;
typedef struct {
    C3TitleCamera camera, restored;
    uint32_t rng, random_draws, motion, previous_motion, owner, target, cuts, duration, ticks;
    uint64_t units; /* milliseconds * 60: fractional source60Hz ticks retained. */
    float origin[3], view[3], distance, height, yaw_start, yaw_end, fov;
    const float (*track)[3];
    uint32_t track_count, track_delay;
    uint32_t move_id, motion_duration;
    float eye_from[3],eye_to[3]; /* TailWhip relative native eye endpoints; motion starts immediately. */
    float fov_start[2], fov_end[2];
    uint32_t fov_begin[2], fov_finish[2], fov_pattern;
    uint8_t dynamic_fov, active, paused;
} C3BattleCamera;
/* Zero-initialize before load/decode. Program must outlive all cameras using its
 * immutable part tracks; cancel camera before close. Failures preserve output. */
int c3_battle_camera_decode(C3BattleCameraProgram *,const void *,size_t,char *,size_t);
int c3_battle_camera_confusion_decode(C3BattleCameraProgram *,const void *,size_t,char *,size_t);
int c3_battle_camera_tailwhip_decode(C3BattleCameraProgram *,const void *,size_t,char *,size_t);
int c3_battle_camera_tailwhip_load(C3BattleCameraProgram *,const char *,char *,size_t);
int c3_battle_camera_confusion_load(C3BattleCameraProgram *,const char *,char *,size_t);
int c3_battle_camera_load(C3BattleCameraProgram *,const char *,char *,size_t);
/* Cosmetic stream is deliberately independent from the original shared RNG.
 * init once; begin retains the stream and source previous-motion exclusion. */
void c3_battle_camera_close(C3BattleCameraProgram *);
void c3_battle_camera_init(C3BattleCamera *,uint32_t cosmetic_seed);
int c3_battle_camera_begin(C3BattleCamera *,const C3BattleCameraProgram *,C3BattleCameraPhase,
                          unsigned owner,unsigned target,const C3BattleCameraActor actors[4],
                          const C3TitleCamera *inherited,char *,size_t);
/* Separate checked move93/move39 programs/states; use independent cosmetic streams.
 * Move39 admits its source idle attack phase only, never a guessed damage phase. */
int c3_battle_camera_begin_move(C3BattleCamera *,const C3BattleCameraProgram *,uint32_t move,
                              C3BattleCameraPhase,unsigned owner,unsigned target,
                              const C3BattleCameraActor actors[4],const C3TitleCamera *,char *,size_t);
/* Source body-map[2] (original JObj42/62/53) is followed on source60Hz ticks with source0.2s move.
 * Max3600ticks per call/sequence; overload is explicit and transactional. */
int c3_battle_camera_step(C3BattleCamera *,uint32_t milliseconds,
                         const C3BattleCameraActor actors[4],char *,size_t);
void c3_battle_camera_pause(C3BattleCamera *,int paused);
void c3_battle_camera_cancel(C3BattleCamera *); /* Restores inherited camera. */
#endif
