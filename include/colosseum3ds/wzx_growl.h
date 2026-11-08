#ifndef COLOSSEUM3DS_WZX_GROWL_H
#define COLOSSEUM3DS_WZX_GROWL_H
#include <stddef.h>
#include <stdint.h>
#define C3_WZX_GROWL_BYTES 1280u
#define C3_WZX_GROWL_MAX_TICKS 3600u
typedef struct {uint32_t key,kind,start,attachment,position_type,link,source,target,timing_index,flags,part,state;uint32_t timing[16],resource[19];} C3WzxGrowlNode;
typedef struct {
 uint8_t model_sha1[20],particles_sha1[20],owner_sha1[20];
 float model_rate,class_scale,owner_anchor[3],emitter_anchor[3];
 uint32_t owner_part,model_part,retained_bone,owner_start,owner_normal,owner_minimum,stop_tick,model_end,owner_end;
 uint32_t floor_start,floor_duration,model_start,particle_start,sound_start,control_start,control_delay,root_generator;
 C3WzxGrowlNode nodes[5];
} C3WzxGrowlProgram;
typedef enum {C3_GROWL_AUDIO_PENDING=0,C3_GROWL_AUDIO_STOPPED=1,C3_GROWL_AUDIO_FINISHED=2} C3WzxGrowlAudio;
typedef struct {
 C3WzxGrowlProgram program; /* Value copy: contains no file/PPC pointers. */
 uint64_t units;
 float owner_seconds,model_seconds;
 uint32_t model_starts,particle_starts,sound_requests,control_starts,floor_starts;
 uint32_t stop_requests,particle_kills,sound_keyoffs,restore_requests;
 uint8_t active,model_visible,floor_enabled,floor_rgb[3],model_finished;
 uint8_t owner_normal_complete,owner_minimum_complete,stop_requested,teardown_acknowledged,completed,cancelled;
 C3WzxGrowlAudio audio_status;
} C3WzxGrowl;
int c3_wzx_growl_decode(C3WzxGrowlProgram *,const void *,size_t,char *,size_t);
int c3_wzx_growl_load(C3WzxGrowlProgram *,const char *,char *,size_t);
void c3_wzx_growl_program_close(C3WzxGrowlProgram *);
int c3_wzx_growl_begin(C3WzxGrowl *,const C3WzxGrowlProgram *);
/* Pause by omitting step. Exact60*ms retained units; overload preserves state.
 * Admitted control0 triggers whole-sequence stop at130+100, even while owner
 * normal244/min366 remain pending. It does not imply natural audio completion. */
int c3_wzx_growl_step(C3WzxGrowl *,uint32_t milliseconds);
/* Caller must acknowledge actual family cancellation, native STOPPED/FINISHED
 * audio status, owner release and camera restoration. The Pokemon motion
 * continues independently until its animation-end callback/override. STOPPED is
 * dry native adaptation,
 * distinct from original SDK keyoff/release and full natural PCM completion. */
int c3_wzx_growl_acknowledge(C3WzxGrowl *,int family_cancelled,C3WzxGrowlAudio,int owner_released,int camera_restored);
int c3_wzx_growl_finish(C3WzxGrowl *);
void c3_wzx_growl_cancel(C3WzxGrowl *);
/* One-shot tick130 anchors. POSITION only: inherit no sourcepart9 basis;
 * owner anchor uses actor scale, emitter translation uses source class .75.
 * Failure preserves both outputs. */
int c3_wzx_growl_anchors(const C3WzxGrowlProgram *,const float root[3],float yaw,float actor_scale,float model_position[3],float emitter_position[3],char *,size_t);
#endif
