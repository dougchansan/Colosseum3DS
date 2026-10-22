#ifndef C3_SAVE_CODEC_H
#define C3_SAVE_CODEC_H

#include <stdint.h>

/* Internal pinned-code entry points. Buffers used by encrypt/decrypt must
 * be aligned to uint32_t and contain the complete final 20-byte block.
 * save.c provides those bounds/alignment; callers use colosseum3ds/save.h. */
void c3_save_codec_sha1(uint8_t digest[20], const uint8_t *input, uint32_t length);
void c3_save_codec_encrypt(uint8_t *data, uint32_t size, uint8_t *hash, uint32_t offset);
uint8_t c3_save_codec_decrypt(uint8_t *data, uint32_t size, uint32_t *expected,
                            uint32_t offset);

#endif
