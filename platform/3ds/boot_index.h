#ifndef C3_BOOT_INDEX_H
#define C3_BOOT_INDEX_H
#include <stdint.h>
#include <stdio.h>

/* Name index of sdmc:/3ds/Colosseum3DS/boot/ (and of the optional grouped
 * tree sdmc:/3ds/Colosseum3DS/boot/packs/<group>/, tools/sd_pack_layout.py).
 *
 * On the console every path lookup in boot/ scans that FAT directory from
 * its start (about 1900 entries): a file late in it costs ~0.4 s to open and
 * a missing one ~0.85 s, the whole directory (hardware traces, 2026-11-20).
 * The directory is listed once; fopen and stat of "boot/<name>" (linked with
 * --wrap=fopen,--wrap=stat in Makefile.3ds) then answer missing files and
 * existence checks from memory, and open a file the grouped tree holds from
 * there. boot/ itself always wins over packs/. Files are only read. */

/* Start listing on a background thread (idempotent). Before the list is
 * ready, lookups behave as without the index. */
void c3_boot_index_start(void);

/* Compressed size of a boot file from the index (0 when unknown). */
uint64_t c3_boot_index_size(const char *path);

/* SD lookups that reached the card (fopen/stat of boot files) and the time
 * they took, plus lookups the index answered without the card. */
typedef struct {
    unsigned lookups, answered;
    uint64_t lookup_us, max_us;
    char max_name[64];
} C3BootFsStats;
void c3_boot_fs_stats(C3BootFsStats *out);

/* One "boot_index" trace line once the list is ready (later calls: none). */
void c3_boot_index_trace(FILE *trace);
#endif
