#include "colosseum3ds/core.h"
#include "colosseum3ds/data.h"
#include "colosseum3ds/pokemon.h"
#include <limits.h>
#include <stdio.h>
#include <string.h>

int c3_port_selftest(void)
{
    static const uint32_t expected[] = {41, 51235, 6334, 59268, 51937};
    const float n[] = {0.0f, 1.0f, 0.0f};
    const float a[] = {1.0f, 2.0f, 3.0f};
    const float b[] = {5.0f, 6.0f, 7.0f};
    /* Three literals, a final partial flag group, and the real 16-byte header. */
    const unsigned char lzss[] = {
        'L','Z','S','S', 0,0,0,3, 0,0,0,20, 0,0,0,0, 7,'A','B','C'
    };
    unsigned char decoded[3];
    size_t written = 0;
    C3Pokemon pokemon = {0};
    c3_rng_seed(1);
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i)
        if (c3_rng_next() != expected[i]) return 1;
    if (c3_side_plane(n, a, b) != 4.0f) return 2;
    if (c3_lzss_decode(lzss, sizeof(lzss), decoded, sizeof(decoded), &written)
            != C3_DATA_OK || written != 3 || memcmp(decoded, "ABC", 3)) return 3;
    pokemon.species_id = 1; /* Synthetic record; no species database is loaded. */
    for (unsigned i = 0; i < C3_STAT_COUNT; ++i) {
        pokemon.base_stats[i] = 100;
        pokemon.iv[i] = 31;
    }
    c3_pokemon_neutral_nature(&pokemon);
    if (c3_pokemon_recalculate(&pokemon, 50) != C3_POKEMON_OK ||
            pokemon.current_hp != 175 || pokemon.stats[C3_STAT_HP] != 175) return 4;
    for (unsigned i = 1; i < C3_STAT_COUNT; ++i)
        if (pokemon.stats[i] != 120) return 4;
    c3_rng_seed(1);
    return 0;
}

static int file_read_at(void *context, uint32_t offset, void *destination, size_t size)
{
    FILE *file = context;
#if LONG_MAX < UINT32_MAX
    if (offset > (uint32_t)LONG_MAX) return -1;
#endif
    if (fseek(file, (long)offset, SEEK_SET) != 0) return -1;
    return fread(destination, 1, size, file) == size ? 0 : -1;
}

int c3_data_probe(const char *root, char *diagnostic, size_t capacity)
{
    char path[1024];
    FILE *file;
    long length;
    C3FsysArchive archive;
    C3FsysEntry entry;
    C3FsysReader reader;
    C3DataResult result;
    if (!root || !diagnostic || capacity == 0) return -1;
    int count = snprintf(path, sizeof(path), "%s/common.fsys", root);
    if (count < 0 || (size_t)count >= sizeof(path)) {
        snprintf(diagnostic, capacity, "Data path is too long");
        return -1;
    }
    file = fopen(path, "rb");
    if (!file) {
        snprintf(diagnostic, capacity, "common.fsys missing/unreadable");
        return -1;
    }
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
            (unsigned long)length > UINT32_MAX) {
        fclose(file);
        snprintf(diagnostic, capacity, "common.fsys size/read error");
        return -1;
    }
    reader.read_at = file_read_at;
    reader.context = file;
    reader.size = (uint32_t)length;
    result = c3_fsys_open(&archive, &reader);
    if (result == C3_DATA_OK && archive.member_count == 0) result = C3_DATA_FORMAT;
    for (uint32_t i = 0; result == C3_DATA_OK && i < archive.member_count; ++i)
        result = c3_fsys_entry(&archive, i, &entry);
    fclose(file);
    if (result != C3_DATA_OK) {
        snprintf(diagnostic, capacity, "common.fsys: %s", c3_data_result_string(result));
        return -1;
    }
    snprintf(diagnostic, capacity, "FSYS directory OK: %lu members",
             (unsigned long)archive.member_count);
    return 0;
}
