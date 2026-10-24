#ifndef C3_PARTICLE_PACK_H
#define C3_PARTICLE_PACK_H
#include "colosseum3ds/particle.h"
#include <stddef.h>
#include <stdint.h>

#define C3_PARTICLE_PACK_MAX_TEXTURES 32u
#define C3_PARTICLE_PACK_MAX_BYTES (10u * 1024u * 1024u)

typedef struct {
    uint32_t group, pose, width, height, rgba_bytes, source_format;
    const uint8_t *rgba;
} C3ParticlePackTexture;

typedef struct {
    const uint8_t *bytes;
    size_t length;
    uint32_t root_generator, texture_count;
    C3ParticleBank bank;
    C3ParticlePackTexture textures[C3_PARTICLE_PACK_MAX_TEXTURES];
    /* SHA-256 provenance fields are borrowed metadata, bound by the caller's
     * trusted whole-pack SHA-1 (from C3WD), not independent authentication. */
    const uint8_t *source_sha256, *payload_sha256;
} C3ParticlePack;

/* Borrows all input storage, which must outlive the pack and particle system.
 * Does not allocate, relocate original references or create GPU textures.
 * expected_sha1 is mandatory. Failure leaves *out unchanged. */
int c3_particle_pack_decode(C3ParticlePack *out, const void *data, size_t bytes,
    const uint8_t expected_sha1[20], char *error, size_t capacity);
/* Explicit qualified profile; original C3PT/GPT1 bytes and root IDs remain.
 * Growl admits its sparse source table. Green-down pins its original source
 * bank SHA-1 in addition to the caller's whole-pack hash. */
int c3_particle_pack_decode_reachable(C3ParticlePack *,const void *,size_t,
 const uint8_t expected_sha1[20],unsigned profile,char *,size_t);
const C3ParticlePackTexture *c3_particle_pack_texture(const C3ParticlePack *,
    uint32_t group, uint32_t pose);
#endif
