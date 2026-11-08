#ifndef COLOSSEUM3DS_WZX_TAIL_WHIP_H
#define COLOSSEUM3DS_WZX_TAIL_WHIP_H
#include <stddef.h>
#include <stdint.h>
#define C3_WZX_TAIL_WHIP_BYTES 512u
#define C3_WZX_TAIL_WHIP_MOTION_BYTES 6176u
#define C3_WZX_TAIL_WHIP_MAX_TICKS 3600u
/* Only original move39/default Zigzagoon288 is admitted. The reached controller
 * is yaw-only; nonzero translation, pitch/roll, or nonunit scale fails closed.
 * This qualification preserves all reached rotation, without PPC handles. */
typedef struct {
    float center[3],model_rate,class_scale;
    uint32_t owner_gate,sound_id;
    uint8_t controller_sha1[20],effect_sha1[20],owner_sha1[20];
    uint8_t *motion_data; /* Owned immutable C3TM; zero-init before decode. */
    size_t motion_bytes;
} C3WzxTailWhipProgram;
typedef struct {
    float owner_position[3],owner_yaw,owner_scale;
    float effect_position[3],effect_yaw,effect_scale;
} C3WzxTailWhipPose;
typedef struct {
    C3WzxTailWhipProgram program; /* Borrowed tracks; cancel before program close. */
    uint64_t units;
    float root[3],root_yaw,actor_scale;
    float controller_seconds,effect_seconds,owner_seconds;
    uint32_t controller_starts,effect_starts,sound_requests;
    uint8_t active,owner_attached,effect_visible,owner_gate_complete;
    uint8_t controller_ended,effect_ended,audio_done,completed;
} C3WzxTailWhip;
int c3_wzx_tail_whip_decode(C3WzxTailWhipProgram *,const void *,size_t,char *,size_t);
int c3_wzx_tail_whip_load(C3WzxTailWhipProgram *,const char *,char *,size_t);
int c3_wzx_tail_whip_motion_decode(C3WzxTailWhipProgram *,const void *,size_t,char *,size_t);
int c3_wzx_tail_whip_motion_load(C3WzxTailWhipProgram *,const char *,char *,size_t);
void c3_wzx_tail_whip_program_close(C3WzxTailWhipProgram *);
int c3_wzx_tail_whip_begin(C3WzxTailWhip *,const C3WzxTailWhipProgram *,const float root[3],float yaw,float actor_scale);
/* Exact retained 60*milliseconds units. Pause by omitting step; overload preserves
 * state. Authored seconds are local elapsed*.5. No invented fixed total/end. */
int c3_wzx_tail_whip_step(C3WzxTailWhip *,uint32_t milliseconds);
/* Supply the actual model animation-ended and live audio completion results.
 * Controller stop detaches/restores saved owner transform immediately. */
int c3_wzx_tail_whip_signals(C3WzxTailWhip *,int controller_done,int effect_done,int audio_done);
int c3_wzx_tail_whip_finish(C3WzxTailWhip *);
/* POSITION+ROTATION owner attachment keeps actor scale; center-null effect is
 * POSITION-only, keeps initial yaw and source class scale .75. Failure preserves
 * output. Cancel restores owner root; pose remains queryable after cancel. */
int c3_wzx_tail_whip_pose(const C3WzxTailWhip *,C3WzxTailWhipPose *,char *,size_t);
void c3_wzx_tail_whip_cancel(C3WzxTailWhip *);
#endif
