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
 * there. boot/ itself always wins over packs/. Files are only read.
 *
 * Listing all 127 groups takes ~8 s on the console (~60 ms per directory).
 * The full listing is saved to sdmc:/3ds/Colosseum3DS/boot_index.c3x; on
 * the next boot boot/ and packs/ are listed fresh (~0.2 s) and, when packs/
 * holds exactly the saved groups, lookups start at once (provisional): a
 * name is looked up in boot/, else in its group (the one the saved copy puts
 * it in, or for an unknown name the group whose files share its prefix up
 * to '_' or '.'), and that group is listed again the first time it is
 * needed, so answers come from fresh listings. A name the provisional index
 * cannot place waits for the full listing, which still runs (paused while
 * packs load), replaces the provisional index, and rewrites the saved copy
 * when anything changed. A missing, damaged or stale copy means a plain
 * full listing, as before. */

/* Start listing on a background thread (idempotent). Before the list is
 * ready, lookups behave as without the index. */
void c3_boot_index_start(void);

/* Compressed size of a boot file from the index (0 when unknown; may be
 * the saved size before the full listing). */
uint64_t c3_boot_index_size(const char *path);

/* Pack loads call this with 1 when they start and 0 when they end: the
 * background listing yields the card meanwhile unless a lookup waits. */
void c3_boot_index_busy(int loading);

/* SD lookups that reached the card (fopen/stat of boot files) and the time
 * they took, plus lookups the index answered without the card. */
typedef struct {
    unsigned lookups, answered;
    uint64_t lookup_us, max_us;
    char max_name[64];
} C3BootFsStats;
void c3_boot_fs_stats(C3BootFsStats *out);

/* One "boot_index" trace line per state reached (provisional, ready). */
void c3_boot_index_trace(FILE *trace);
#endif
