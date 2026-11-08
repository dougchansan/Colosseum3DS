#ifndef COLOSSEUM3DS_SAVE_H
#define COLOSSEUM3DS_SAVE_H

#include <stddef.h>
#include <stdint.h>

/* GC6E01 memory-card file and its optional standard GCI directory entry.
 * All game-data fields remain in their original big-endian representation.
 * This module does not initialize a new game or serialize native structs. */
#define C3_SAVE_RAW_SIZE       0x60000u
#define C3_SAVE_GCI_HEADER_SIZE 0x40u
#define C3_SAVE_GCI_SIZE       (C3_SAVE_RAW_SIZE + C3_SAVE_GCI_HEADER_SIZE)
#define C3_SAVE_SLOT_SIZE      0x1e000u
#define C3_SAVE_BODY_SIZE      0x1dfd0u
#define C3_SAVE_SLOT_COUNT     3u
#define C3_SAVE_NO_SLOT        3u

typedef enum C3SaveResult {
    C3_SAVE_OK = 0,
    C3_SAVE_ARGUMENT,
    C3_SAVE_SIZE,
    C3_SAVE_GCI_HEADER,
    C3_SAVE_SLOT_HEADER,
    C3_SAVE_EMPTY,
    C3_SAVE_INTEGRITY,
    C3_SAVE_MEMORY,
    C3_SAVE_IO,
    C3_SAVE_COUNTER_OVERFLOW,
    C3_SAVE_PENDING_FILE
} C3SaveResult;

typedef struct C3SaveInfo {
    size_t image_size;
    unsigned has_gci_header;
    unsigned selected_slot;  /* C3_SAVE_NO_SLOT when no valid payload exists. */
    uint32_t selected_count;
    uint32_t newest_count;   /* Includes a newer slot rejected by SHA-1. */
    uint32_t slot_counts[C3_SAVE_SLOT_COUNT];
    unsigned rejected_slots; /* Bit mask of non-empty slots failing SHA-1. */
    unsigned recovered_older_slot;
    unsigned icon_checksum_valid;
} C3SaveInfo;

const char *c3_save_result_string(C3SaveResult result);

/* Accept exact-sized raw card-file data or GC6E/01 pokemon_colosseum GCI.
 * Check all three slot headers, then authenticate the newest nonempty slot,
 * falling back to older slots as the original game does. Never modify image.
 * info is filled for OK/EMPTY/INTEGRITY; incomplete headers are not accepted.
 * Icon checksum failure is reported separately: the game can repair artwork. */
C3SaveResult c3_save_inspect(const void *image, size_t length, C3SaveInfo *info);

/* Decode the selected slot's genuine serialized body. On error, body is
 * unchanged. The body contains bytes, not an endian-converted native struct. */
C3SaveResult c3_save_decode(const void *image, size_t length,
                          void *body, size_t body_capacity, C3SaveInfo *info);

/* Low-level format encoder for a real serialized game body supplied by the
 * caller. count must be 1..INT32_MAX. random is the game's 20-byte padding.
 * Like fn_801CF320, clears the checksum placeholder and reserved word at
 * body offsets 4 and 0x18. Does not invent trainer/Pokemon/progress data,
 * update the body's save counter/card serial, or construct a GCI envelope.
 * On failure slot is unchanged. Buffers may be unaligned and may overlap. */
C3SaveResult c3_save_encode_slot(const void *body, size_t body_length,
                               uint32_t count, const uint8_t random[20],
                               void *slot, size_t slot_capacity);

/* Replace the next slot of an already valid game file in memory. Keeps the
 * GCI header, artwork and other slots byte-for-byte. Uses newest_count+1;
 * caller must prepare the genuine serialized body and its internal fields.
 * On failure image is unchanged. Does not write a file. */
C3SaveResult c3_save_update(void *image, size_t length,
                          const void *body, size_t body_length,
                          const uint8_t random[20], C3SaveInfo *info);

/* Persistent byte-compatible import/export backend. Caller selects the SD
 * path (for example sdmc:/3ds/Colosseum3DS/saves/pokemon_colosseum.gci).
 * Parent directory must already exist. load validates before copying and
 * tries path.bak if current is absent/invalid. used_backup is 0 or 1.
 * store validates data, writes exclusive path.tmp, flushes and compares its
 * readback, and retains the previous verified save as path.bak. It never
 * relies on rename overwriting an existing file (libctru is not atomic for
 * that operation). Recoverable errors can leave .bak/.tmp; never run two
 * stores or mutate these paths concurrently. No external console I/O occurs.
 * An orphan .tmp is preserved and returns PENDING_FILE for explicit recovery.
 * File-system/power failures are recoverable only while one copy survives;
 * this is not a guarantee against physical SD corruption. */
C3SaveResult c3_save_load_file(const char *path, void *image, size_t capacity,
                             size_t *length, C3SaveInfo *info, int *used_backup);
C3SaveResult c3_save_store_file(const char *path, const void *image, size_t length);

#endif
