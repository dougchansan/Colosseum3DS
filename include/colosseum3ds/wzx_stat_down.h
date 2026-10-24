#ifndef COLOSSEUM3DS_WZX_STAT_DOWN_H
#define COLOSSEUM3DS_WZX_STAT_DOWN_H
#include <stddef.h>
#include <stdint.h>
#define C3_WZX_STAT_DOWN_BYTES 1024u
#define C3_WZX_STAT_DOWN_MAX_TICKS 3600u
/* Admitted GREEN Defense-down special0x29/kind4, not move29. */
typedef struct {
 uint32_t species,slot,entry,clip,authored_end,minimum,root_part,camera_part;
 uint8_t owner_sha1[20],member_sha1[20];float root_translation[3];
} C3WzxStatDownOwner;
typedef struct {uint32_t key,kind,start,attachment,position_type,link,source,target,timing_index,flags,part,state;uint32_t timing[16],resource[19];} C3WzxStatDownNode;
typedef struct {
 uint8_t particles_sha1[20];float class_scale,owner_rate;
 uint32_t sound_id,root_generator,particle_flags,particle_mode,color_duration,target_model;
 C3WzxStatDownOwner owners[2];C3WzxStatDownNode nodes[3];
} C3WzxStatDownProgram;
typedef enum {C3_STAT_DOWN_AUDIO_ACTIVE=0,C3_STAT_DOWN_AUDIO_FINISHED=1,C3_STAT_DOWN_AUDIO_STOPPED=2} C3WzxStatDownAudio;
typedef struct {
 C3WzxStatDownProgram program; /* Pointer-free value; closing source is safe. */
 uint64_t units;uint32_t slot,owner_clip,owner_minimum;
 float owner_seconds;
 uint32_t particle_starts,color_starts,sound_requests,family_kills,sound_keyoffs,restore_requests;
 uint8_t active,color_enabled,color_finished,color_rgb[3],prior_rgb[3];
 uint8_t owner_minimum_complete,particles_finished,audio_finished;
 uint8_t teardown_requested,teardown_acknowledged,completed,cancelled;
 C3WzxStatDownAudio cleanup_audio_status;
} C3WzxStatDown;
int c3_wzx_stat_down_decode(C3WzxStatDownProgram *,const void *,size_t,char *,size_t);
int c3_wzx_stat_down_load(C3WzxStatDownProgram *,const char *,char *,size_t);
void c3_wzx_stat_down_program_close(C3WzxStatDownProgram *);
int c3_wzx_stat_down_begin(C3WzxStatDown *,const C3WzxStatDownProgram *,unsigned slot,const uint8_t prior_rgb[3]);
/* Exact60*ms clock; pause by omitting step, overload preserves state. Owner
 * starts original idle entry0/frame0/rate.5; owner_seconds wraps at source
 * period118/180 ticks, absoluteunits neverwrap. Actor color alone lasts50 ticks;
 * its end never implies sequence completion. */
int c3_wzx_stat_down_step(C3WzxStatDown *,uint32_t milliseconds);
/* Admitted fn801DA9E8 clears owner17: normal early-abort branch is disabled.
 * Natural completion requires owner minimum + color end + no PS generator or
 * children + direct SE FINISHED. Raw sound payload[19B,3,0] has runtimeFlags0,
 * not threaded playback. Unexpected STOPPED is rejected; explicitly cancel. */
int c3_wzx_stat_down_observe(C3WzxStatDown *,int particles_finished,C3WzxStatDownAudio);
/* ApplyStop whole-family release/keyoff and prior actor-color/camera restoration
 * must be acknowledged. STOPPED is permitted only as cleanup after natural
 * FINISHED was already observed, or after explicit cancellation. */
int c3_wzx_stat_down_acknowledge(C3WzxStatDown *,int family_cancelled,C3WzxStatDownAudio,int actor_color_restored,int camera_restored);
int c3_wzx_stat_down_finish(C3WzxStatDown *);
void c3_wzx_stat_down_cancel(C3WzxStatDown *);
#endif
