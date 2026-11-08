#ifndef COLOSSEUM3DS_WZX_TACKLE_H
#define COLOSSEUM3DS_WZX_TACKLE_H
#include <stddef.h>
#include <stdint.h>
#define C3_WZX_TACKLE_BYTES 1024u
#define C3_WZX_TACKLE_MAX_TICKS 3600u
typedef struct {
 uint32_t species,slot,owner_clip,owner_normal,owner_minimum,body_part,owner_end;
 float anchor[3]; /* Original body-map2 part in native .1 source units, hit clip frame0. */
 uint8_t pkx_sha1[20],hsd_sha1[20];
} C3WzxTackleOwner;
typedef struct {
 uint32_t attack_start,attack_stop,attack_sound_id,damage_sound_id;
 uint32_t particle_flags,bank_flags,root_generator,hp_start;
 float owner_rate,world_scale;
 uint8_t particles_sha1[20],caster_pkx_sha1[20],caster_hsd_sha1[20];
 C3WzxTackleOwner owners[2];
} C3WzxTackleProgram;
typedef struct {
 C3WzxTackleProgram program;
 uint64_t units; /* ms*60; phase clock, reset explicitly at defender start. */
 uint64_t handoff_units; /* Elapsed beyond133; retained after handoff for adapters. */
 uint32_t attack_sound_requests,attack_stops,defender_starts;
 uint32_t reset_except_requests,particle_starts,damage_sound_requests,hp_gates;
 uint32_t family_stop_requests,attack_sound_stops,damage_sound_stops;
 uint8_t active,phase,target,attack_complete,completed,miss;
 float owner_seconds;
} C3WzxTackle;
int c3_wzx_tackle_decode(C3WzxTackleProgram *,const void *,size_t,char *,size_t);
int c3_wzx_tackle_load(C3WzxTackleProgram *,const char *,char *,size_t);
int c3_wzx_tackle_begin(C3WzxTackle *,const C3WzxTackleProgram *,unsigned target);
int c3_wzx_tackle_step(C3WzxTackle *,uint32_t milliseconds);
/* Explicit hit callback/handoff: never start the damage bank for a miss.
 * reset_except_requests asks the caller to apply source owner16/flags18 guards
 * to every other active owner; it is not an unconditional caster idle setter.
 * Start consumers at0 before advancing them by handoff_units. The phase clock
 * already includes that remainder; do not step it a second time. */
int c3_wzx_tackle_defend(C3WzxTackle *,int hit);
/* Source B988: minimum + pending consumers. The externally enabled early-stop
 * branch is strict normal<time && !motion_busy && sequence_enabled. Normal
 * admitted DA9E8 clears both owner flags. Camera duration is not completion. */
int c3_wzx_tackle_finish(C3WzxTackle *,int particles_done,int sound_done,
                        int motion_busy,int sequence_enabled);
void c3_wzx_tackle_cancel(C3WzxTackle *);
#endif
