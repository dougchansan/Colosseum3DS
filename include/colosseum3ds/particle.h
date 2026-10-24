#ifndef C3_PARTICLE_H
#define C3_PARTICLE_H
#include <stddef.h>
#include <stdint.h>
#define C3_PARTICLE_MAX_GENERATORS 16u
#define C3_PARTICLE_MAX_TEXTURE_GROUPS 16u
#define C3_PARTICLE_MAX_EMITTERS 16u
#define C3_PARTICLE_MAX_APP_SRT 16u
#define C3_PARTICLE_PROFILE_GROWL_V1 1u
#define C3_PARTICLE_PROFILE_GREEN_DOWN_V1 2u
#define C3_PARTICLE_PROFILE_RED_DOWN_V1 3u
#define C3_PARTICLE_PROFILE_TACKLE_V1 4u
#ifndef C3_PARTICLE_MAX_PARTICLES
/* Measured admitted Reflect closure peak1369; bounded1536 live slots. */
#define C3_PARTICLE_MAX_PARTICLES 1536u
#endif
#define C3_PARTICLE_MAX_DEPTH 4u
#define C3_PARTICLE_MAX_INSTRUCTIONS 128u
#define C3_PARTICLE_MAX_CATCHUP 600u
/* Checked native handles, not original PPC pointers. Raw GPT1 bytes must remain immutable and outlive bank/system.
 * Source: psstructs.h PSCmdList, particle.c, generator.c, psinterpret.c.
 * Admits reached Bite, Reflect and Confusion instruction/shape closures;
 * unsupported reached instructions and render modes fail explicitly.
 * Legacy begin retains source-local coordinates. POSITION begin simulates in
 * source-world units; render that mode with native conversion only. */
typedef struct {
 uint16_t type,tex_group,gen_life,life;
 uint32_t kind,script_offset,script_bytes;
 float grav,fric,velocity[3],radius,angle,rate,size,param[3];
} C3ParticleGenerator;
typedef struct {
 const uint8_t *bytes;size_t length;
 uint32_t generator_count,texture_group_count,refs[C3_PARTICLE_MAX_GENERATORS];
 uint32_t texture_frames[C3_PARTICLE_MAX_TEXTURE_GROUPS];
 C3ParticleGenerator generators[C3_PARTICLE_MAX_GENERATORS];
 /* Explicit sparse profile only: immutable original table IDs and references.
  * generator_count remains decoded slots; old banks retain identity mapping. */
 uint32_t source_generator_count,reference_offset;
 uint16_t source_ids[C3_PARTICLE_MAX_GENERATORS];
 uint8_t profile;
} C3ParticleBank;
typedef struct {
 uint8_t active,depth;uint16_t generator,life;
 uint32_t kind,serial;float count,position[3];
 float attachment_yaw,attachment_scale,radius_scale;
 uint16_t source_flags;
 uint32_t app_srt_handle;
 uint8_t deferred;
} C3ParticleEmitter;
typedef struct {
 uint8_t active,depth,tex_group,pose;
 /* generator is a native decoded slot; source_ids[generator] preserves the
  * original ID for an explicit sparse profile (identity for legacy banks). */
 uint16_t generator,emitter,life,wait,pc,size_count,rotate_count,loop_pc,mark_pc;
 uint8_t loop_count;uint32_t serial;
 uint16_t color_count[2],color_remain[2];
 uint32_t kind;
 float attachment_yaw,attachment_scale,radius_scale;
 uint16_t source_flags;
 uint32_t app_srt_handle,emitter_serial;
 float tornado_radius,tornado_angle,tornado_speed,tornado_turn,tornado_origin[3];
 float position[3],velocity[3],size,size_target,rotation,rotate_target,rotate_accel,grav,fric,trail;
 uint8_t prim[4],env[4],color_base[2][4],color_target[2][4];
} C3Particle;

/* Reached Growl owns camera-relative SRTs with identity rotation/scale.
 * Only source status0 is admitted; no general SRT setter is exposed.
 * Handles encode slot+generation and are scoped to one begin/cancel session. */
typedef struct {
 uint32_t generation,owner_serial;
 uint16_t references;
 uint8_t active,status,cache_valid;
 uint64_t render_frame;
 float translate[3],matrix[12];
} C3ParticleAppSRT;
typedef struct {
 C3ParticleBank bank;
 C3ParticleEmitter emitters[C3_PARTICLE_MAX_EMITTERS];
 C3Particle particles[C3_PARTICLE_MAX_PARTICLES];
 C3ParticleAppSRT app_srt[C3_PARTICLE_MAX_APP_SRT];
 uint32_t app_srt_generation;
 uint32_t cosmetic_seed;uint64_t cosmetic_draws,ticks,remainder;
 uint32_t emitted,spawned_emitters,peak_particles;
 uint8_t failed,cancelled,position_attachment;
 /* Tackle only: source head-insertion list; display sorting persists into
  * subsequent simulation passes, including multiple ticks without display. */
 uint16_t source_order[C3_PARTICLE_MAX_PARTICLES],source_order_count;
 uint32_t root_emitter_serial;
 uint16_t source_attachment_flags;
 char diagnostic[160];
} C3ParticleSystem;
typedef struct {
 float right[3],up[3]; /* Orthonormal inverse-view columns in native world space. */
 float clip[16];       /* Row-major world-to-clip; used for projected trail direction. */
} C3ParticleCamera;
typedef struct {float position[3],uv[2];uint8_t rgba[4];} C3ParticleQuadVertex;
typedef struct {
 C3ParticleQuadVertex vertices[4]; /* GX quad order, triangulate 0,1,2 / 0,2,3. */
 uint8_t prim[4],env[4],blend,depth_test,depth_write,mirror_s,mirror_t;
 uint8_t filter_nearest,alpha_reference,texture_group,texture_frame,prim_env,trail;
} C3ParticleQuad;
/* Source psDispSub/MakePolygon no-form branch. Attachment is row-major3x4,
 * uniform scale plus rotation/translation. Caller owns one-shot versus live
 * bone lookup and source-part mapping. Returns1 drawn,0 invisible,-1 invalid.
 * UV includes original mirror scale/offset: renderer must use mirror wrapping.
 * TEV: env*(1-texture)+prim*texture; trail multiplies alpha by vertex alpha.
 * blend:0 alpha,1 additive-alpha,2 subtract,3 destination-color/zero. */
int c3_particle_quad(const C3Particle *,const float attachment[12],
                     const C3ParticleCamera *,C3ParticleQuad *,char *,size_t);
int c3_particle_bank_decode(C3ParticleBank *,const void *,size_t,char *,size_t);
/* Explicit Growl v1 root18/113 source descriptors, or stat-down v1 root0/3
 * dense descriptors. Original IDs/bytes remain unchanged. Stat-down source hash
 * authentication is enforced by the opt-in pack decoder; raw decode checks
 * the admitted generator/bytecode contract. */
int c3_particle_bank_decode_reachable(C3ParticleBank *,const void *,size_t,
 unsigned source_root,unsigned profile,char *,size_t);
/* Active draw slots; Tackle uses source static buckets then newest birth.
 * Other profiles retain ascending native slots. Tackle commits source list
 * sorting as original display does; subsequent updates traverse this order.
 * No texture/primitive regroup. */
int c3_particle_render_indices(C3ParticleSystem *,uint16_t *,size_t,size_t *,char *,size_t);
int c3_particle_bank_source_slot(const C3ParticleBank *,unsigned source_id,unsigned *slot);
/* Source-world outputs; C is the inverse-view rotation orthonormalized Z,X.
 * Does not mutate simulation coordinates or bake camera motion into particles. */
int c3_particle_app_srt_geometry(C3ParticleSystem *,const C3Particle *,
 const C3ParticleCamera *,uint64_t render_frame,float position[3],float velocity[3],char *,size_t);
int c3_particle_quad_app_srt(C3ParticleSystem *,const C3Particle *,
 const C3ParticleCamera *,uint64_t render_frame,float native_per_source_unit,
 C3ParticleQuad *,char *,size_t);
/* begin commits only after validation. Cosmetic RNG is per-system HSD LCG,
 * deliberately separate from combat. Legacy native queue visitation is retained.
 * Tackle alone preserves the bounded source F0/list visitation; original
 * global shared-RNG and retail frame parity remain unqualified. */
int c3_particle_begin(C3ParticleSystem *,const C3ParticleBank *,unsigned generator,
                     uint32_t cosmetic_seed,char *,size_t);
/* Reflect POSITION adapter: source world units (native world *10), radians.
 * Captures rotation and uniform generator/billboard and angle/radius scales.
 * Future root emissions follow position; emitted particles/child generators do
 * not follow. Reflect flags 0x18A9; no motion/tornado scaling or local offsets. */
int c3_particle_begin_position(C3ParticleSystem *,const C3ParticleBank *,unsigned,
 uint32_t,const float source_world_position[3],float start_yaw,
 float billboard_scale,float radius_scale,char *,size_t);
/* Checked source profiles: Confusion attack 0x0001, Bite/Confusion damage
 * 0x10A1, Reflect 0x18A9. Bit5 controls radius_scale; bit7 billboards. */
int c3_particle_begin_position_flags(C3ParticleSystem *,const C3ParticleBank *,unsigned,
 uint32_t,const float source_world_position[3],float start_yaw,
 float billboard_scale,float radius_scale,uint16_t source_flags,char *,size_t);
/* Append a root to an existing POSITION system without resetting time, RNG or
 * live particles. Bank identity must match the same immutable bytes/length.
 * Root handle is its checked native emitter serial; source launch order owns RNG. */
int c3_particle_append_position_flags(C3ParticleSystem *,const C3ParticleBank *,unsigned,
 const float source_world_position[3],float start_yaw,float billboard_scale,
 float radius_scale,uint16_t source_flags,uint32_t *root_handle,char *,size_t);
int c3_particle_position_root(C3ParticleSystem *,uint32_t root_handle,
 const float source_world_position[3],char *,size_t);
/* RED's LIVE field is admitted only for a constant source-world root point:
 * identical checked updates succeed until cancel/new begin, including after
 * root0's emission ends. Changed roots fail without translating descendants
 * or emitted particles. General source LIVE child/JObj following is unqualified.
 * GREEN remains one-shot and rejects all position updates. */
int c3_particle_position(C3ParticleSystem *,const float source_world_position[3],char *,size_t);
/* Fixed60Hz source simulation, retained millisecond fraction; paused consumes no
 * time. Catchup >600ticks fails explicitly; elapsed time is never silently lost.
 * Failure latches until cancel/new begin; diagnostics identify generator/PC/op. */
int c3_particle_step_ticks(C3ParticleSystem *,uint32_t ticks,int paused,char *,size_t);
/* Exact source clock fractions:1000units per60Hz tick,60units perms.
 * Retains the same fractional remainder and bounded catchup as step(ms). */
int c3_particle_step_units(C3ParticleSystem *,uint64_t source_units,int paused,char *,size_t);
int c3_particle_step(C3ParticleSystem *,uint32_t milliseconds,int paused,char *,size_t);
void c3_particle_cancel(C3ParticleSystem *);
int c3_particle_finished(const C3ParticleSystem *);
#endif
