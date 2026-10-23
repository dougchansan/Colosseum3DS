#ifndef C3_PARTICLE_H
#define C3_PARTICLE_H
#include <stddef.h>
#include <stdint.h>
#define C3_PARTICLE_MAX_GENERATORS 16u
#define C3_PARTICLE_MAX_TEXTURE_GROUPS 16u
#define C3_PARTICLE_MAX_EMITTERS 16u
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
 * Positions/velocities/size remain source-local units; the WZX owner supplies its
 * explicitly qualified one-shot/follow attachment transform at rendering. */
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
} C3ParticleBank;
typedef struct {
 uint8_t active,depth;uint16_t generator,life;
 uint32_t kind,serial;float count,position[3];
} C3ParticleEmitter;
typedef struct {
 uint8_t active,depth,tex_group,pose;
 uint16_t generator,emitter,life,wait,pc,size_count,rotate_count,loop_pc,mark_pc;
 uint8_t loop_count;uint32_t serial;
 uint16_t color_count[2],color_remain[2];
 uint32_t kind;
 float tornado_radius,tornado_angle,tornado_speed,tornado_turn,tornado_origin[3];
 float position[3],velocity[3],size,size_target,rotation,rotate_target,rotate_accel,grav,fric,trail;
 uint8_t prim[4],env[4],color_base[2][4],color_target[2][4];
} C3Particle;
typedef struct {
 C3ParticleBank bank;
 C3ParticleEmitter emitters[C3_PARTICLE_MAX_EMITTERS];
 C3Particle particles[C3_PARTICLE_MAX_PARTICLES];
 uint32_t cosmetic_seed;uint64_t cosmetic_draws,ticks,remainder;
 uint32_t emitted,spawned_emitters,peak_particles;
 uint8_t failed,cancelled;
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
/* begin commits only after validation. Cosmetic RNG is per-system HSD LCG,
 * deliberately separate from combat; not a claim of original shared-RNG parity. */
int c3_particle_begin(C3ParticleSystem *,const C3ParticleBank *,unsigned generator,
                     uint32_t cosmetic_seed,char *,size_t);
/* Fixed60Hz source simulation, retained millisecond fraction; paused consumes no
 * time. Catchup >600ticks fails explicitly; elapsed time is never silently lost.
 * Failure latches until cancel/new begin; diagnostics identify generator/PC/op. */
int c3_particle_step_ticks(C3ParticleSystem *,uint32_t ticks,int paused,char *,size_t);
int c3_particle_step(C3ParticleSystem *,uint32_t milliseconds,int paused,char *,size_t);
void c3_particle_cancel(C3ParticleSystem *);
int c3_particle_finished(const C3ParticleSystem *);
#endif
