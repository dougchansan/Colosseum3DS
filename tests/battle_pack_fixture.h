/* Synthetic C3B2 battle pack and party records (no original data), shared
 * by tests/test_battle_general.c and tests/test_link_battle.c. */
#ifndef C3_TEST_BATTLE_PACK_FIXTURE_H
#define C3_TEST_BATTLE_PACK_FIXTURE_H
#include "save_codec.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
enum { MOVES = 166, SPECIES = 3, ENCOUNTERS = 2, ENTRY = 672 };
static inline void p16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static inline void p32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static inline unsigned g16(const uint8_t *p) { return (unsigned)p[0] << 8 | p[1]; }
static inline uint32_t g32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }

static inline size_t pack_size(void)
{ return 128 + MOVES * 56 + SPECIES * 284 + 18 * 44 + 8 * 0x194 + ENCOUNTERS * ENTRY + (SPECIES + MOVES + 2 * ENCOUNTERS) * 16 + 0x61 * 0x38; }

/* opponent_level/species configure encounter 0's two-Pokemon party. */
static inline uint8_t *make_pack(unsigned opponent_species, unsigned opponent_level, size_t *size)
{
    size_t n = pack_size();
    uint8_t *b = calloc(1, n);
    uint32_t moves = 128, species = moves + MOVES * 56, types = species + SPECIES * 284,
             growth = types + 18 * 44, entries = growth + 8 * 0x194, names = entries + ENCOUNTERS * ENTRY;
    memcpy(b, "C3B2", 4); p32(b + 4, 2); p32(b + 8, (uint32_t)n); p32(b + 12, ENCOUNTERS);
    p32(b + 16, MOVES); p32(b + 20, SPECIES); p32(b + 24, 18); p32(b + 28, 8);
    p32(b + 112, species); p32(b + 116, types); p32(b + 120, growth); p32(b + 124, entries);
    /* move 1 TACKLE (hit), 2 GROWL (Attack down, both foes), 3 EMBER-like
     * burn hit, 165 STRUGGLE (recoil). Row: pri, pp, type, range, acc, sec, .., power +22, effect +26. */
    uint8_t *m = b + moves + 1 * 56; m[1] = 35; m[2] = 0; m[3] = 0; m[4] = 100; p16(m + 22, 40); p16(m + 26, 0);
    m = b + moves + 2 * 56; m[1] = 40; m[2] = 0; m[3] = 4; m[4] = 100; p16(m + 26, 18);
    m = b + moves + 3 * 56; m[1] = 25; m[2] = 10; m[3] = 0; m[4] = 100; m[5] = 10; p16(m + 22, 40); p16(m + 26, 4);
    m = b + moves + 165 * 56; m[1] = 1; m[2] = 0; m[3] = 0; p16(m + 22, 50); p16(m + 26, 48);
    for (unsigned s = 1; s < SPECIES; ++s) {
        uint8_t *r = b + species + s * 284;
        r[0] = 0; p16(r + 6, s == 1 ? 100 : 50); r[0x30] = r[0x31] = s == 1 ? 0 : 10;
        for (unsigned j = 0; j < 6; ++j) p16(r + 0x84 + 2 * j, s == 1 ? 100 : 40);
        r[0xba] = 1; p16(r + 0xbc, 1); /* level-up: TACKLE at 1 */
        if (s == 1) { r[0xbe] = 6; p16(r + 0xc0, 2); } /* HERO learns GROWL at 6 */
        if (s == 2) p16(r + 0x90 + 2 * 5, 2);          /* FOE gives 2 Speed effort */
    }
    for (unsigned t = 0; t < 18; ++t) for (unsigned d = 0; d < 18; ++d) p16(b + types + t * 44 + 8 + d * 2, 0x3f);
    p16(b + types + 0 * 44 + 8 + 7 * 2, 0x43);      /* Normal vs Ghost: immune */
    p16(b + types + 10 * 44 + 8 + 0 * 2, 0x41);     /* Fire vs Normal: 2x (synthetic) */
    for (unsigned g = 0; g < 8; ++g) for (unsigned l = 0; l <= 100; ++l) p32(b + growth + g * 0x194 + l * 4, l * l * l);
    for (unsigned e = 0; e < ENCOUNTERS; ++e) {
        uint8_t *r = b + entries + e * ENTRY; p16(r, e);
        uint8_t *enc = r + 8;
        enc[0] = 2; enc[1] = 1; enc[2] = 1; p16(enc + 4, 5);
        p16(enc + 24, 1); p32(enc + 28, 1); p16(enc + 32, 10);
        p16(r + 64 + 2, 200); r[120] = 1; r[121] = 6; r[122] = 2;
        p16(r + 128 + 2, 26); p16(r + 180, 5);
        /* fight kind 2 (story trainer): back-save, items, EXP, money, criticals, Hyper Mode */
        r[88] = 1; r[92] = 1; r[96] = 1; r[97] = 1; r[102] = 1; r[110] = 1;
        if (e == 0) {
            r[2] = 2;
            for (unsigned k = 0; k < 2; ++k) {
                uint8_t *t = r + 192 + k * 80;
                t[4] = (uint8_t)opponent_level; p16(t + 10, opponent_species); t[3] = k == 0;
                p32(t + 0x34, 1); /* TACKLE */
            }
        } else p16(r + 4, 3);
    }
    const char *sn[SPECIES] = {"", "HERO", "FOE"};
    for (unsigned s = 0; s < SPECIES; ++s) strcpy((char *)b + names + s * 16, sn[s]);
    strcpy((char *)b + names + (SPECIES + 1) * 16, "TACKLE");
    strcpy((char *)b + names + (SPECIES + MOVES) * 16, "WILLIE");
    strcpy((char *)b + names + (SPECIES + MOVES + 1) * 16, "RIDER");
    {   /* version 2 Shadow table: id 1 is FOE, catch rate 45, gauge 30, snag flag 0x84E */
        uint8_t *sh = b + n - 0x61 * 0x38 + 1 * 0x38;
        sh[0] = 45; p16(sh + 2, 2); p16(sh + 8, 30); p16(sh + 0x12, 0x84e);
    }
    c3_save_codec_sha1(b + 72, b + 128, n - 128);
    *size = n;
    return b;
}

static inline void make_party(uint8_t party[6][0x138], unsigned species, unsigned level, unsigned count, int shadow)
{
    memset(party, 0, 6 * 0x138);
    for (unsigned i = 0; i < count; ++i) {
        uint8_t *r = party[i];
        unsigned base = species == 1 ? 100 : 40;
        p16(r, species); r[0x60] = (uint8_t)level; p32(r + 0x5c, level * level * level);
        for (unsigned j = 0; j < 6; ++j) {
            unsigned core = (2 * base + 31) * level / 100;
            p16(r + 0xa4 + 2 * j, 31);
            p16(r + 0x8c + 2 * j, j ? core + 5 : core + level + 10);
        }
        p16(r + 0x8a, g16(r + 0x8c));
        p16(r + 0x78, 1); r[0x7a] = 35; /* TACKLE */
        p16(r + 0x7c, 3); r[0x7e] = 25; /* burn hit */
        if (shadow) p16(r + 0xd8, 1);
    }
}

#endif
