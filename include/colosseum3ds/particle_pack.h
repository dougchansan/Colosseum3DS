#ifndef C3_PARTICLE_PACK_H
#define C3_PARTICLE_PACK_H
#include "colosseum3ds/particle.h"
#include <stddef.h>
#include <stdint.h>

#define C3_PARTICLE_PACK_MAX_TEXTURES 32u
#define C3_PARTICLE_PACK_MAX_ROOTS 32u
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
    /* Version 2 only: the source roots whose union closure `bank` holds. */
    uint32_t root_count, roots[C3_PARTICLE_PACK_MAX_ROOTS];
} C3ParticlePack;

/* Borrows all input storage, which must outlive the pack and particle system.
 * Does not allocate, relocate original references or create GPU textures.
 * expected_sha1 is mandatory. Failure leaves *out unchanged. */
int c3_particle_pack_decode(C3ParticlePack *out, const void *data, size_t bytes,
    const uint8_t expected_sha1[20], char *error, size_t capacity);
/* Explicit qualified profile; original C3PT/GPT1 bytes and root IDs remain.
 * Growl admits its sparse source table. RED/GREEN stat-down each pins its own
 * original source bank SHA-1 in addition to the caller's whole-pack hash.
 * Tackle v1 pins original91680-byte root0/8-descriptor F0-only bank. */
int c3_particle_pack_decode_reachable(C3ParticlePack *,const void *,size_t,
 const uint8_t expected_sha1[20],unsigned profile,char *,size_t);
/* C3PT version 2 from tools/prepare_particle.py --batch (wxp_<sha1>.c3p):
 * the original GPT1 bank, a root table and RGBA8 textures (any GX source
 * format 0-6, box-filtered to power-of-two edges 8..64). The bank is the
 * c3_particle_bank_decode_general closure of every listed root; begin any
 * listed root by source ID. Version 1 packs are refused here and vice versa. */
int c3_particle_pack_decode_general(C3ParticlePack *,const void *,size_t,
 const uint8_t expected_sha1[20],char *,size_t);
const C3ParticlePackTexture *c3_particle_pack_texture(const C3ParticlePack *,
    uint32_t group, uint32_t pose);
#endif
