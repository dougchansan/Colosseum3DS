#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/save.h"
#include "save_codec.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Offsets from the pinned memcard.c, not the provisional save.h. */
#define SLOT_BASE       0x6000u
#define HASHED_SIZE     0x1dfd8u
#define CIPHER_OFFSET   0x18u
#define RANDOM_OFFSET   0x1dfd8u
#define DIGEST_OFFSET   0x1dfecu
#define ICON_SUM_OFFSET 0x3a40u
#define PATH_LIMIT      1024u

static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void put_be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static uint32_t sum_words(const uint8_t *p, size_t bytes)
{
    uint32_t sum = 0;
    size_t i;
    for (i = 0; i < bytes; i += 4)
        sum += be32(p + i);
    return sum;
}

const char *c3_save_result_string(C3SaveResult result)
{
    switch (result) {
    case C3_SAVE_OK: return "Colosseum save format verified";
    case C3_SAVE_ARGUMENT: return "invalid save API argument";
    case C3_SAVE_SIZE: return "incorrect GameCube save file size";
    case C3_SAVE_GCI_HEADER: return "GCI is not a GC6E/01 pokemon_colosseum file";
    case C3_SAVE_SLOT_HEADER: return "invalid save slot flags or header checksum";
    case C3_SAVE_EMPTY: return "memory-card file contains no saved game";
    case C3_SAVE_INTEGRITY: return "no saved slot passes the game's SHA-1 check";
    case C3_SAVE_MEMORY: return "save work-buffer allocation failed";
    case C3_SAVE_IO: return "save file I/O failed";
    case C3_SAVE_COUNTER_OVERFLOW: return "save generation counter exhausted";
    case C3_SAVE_PENDING_FILE: return "pending save .tmp exists; preserve it for recovery";
    }
    return "unknown save error";
}

static C3SaveResult image_header(const uint8_t *image, size_t length,
                                const uint8_t **raw, C3SaveInfo *info)
{
    unsigned i;
    if (length != C3_SAVE_RAW_SIZE && length != C3_SAVE_GCI_SIZE)
        return C3_SAVE_SIZE;
    memset(info, 0, sizeof(*info));
    info->image_size = length;
    info->selected_slot = C3_SAVE_NO_SLOT;
    info->has_gci_header = length == C3_SAVE_GCI_SIZE;
    if (info->has_gci_header) {
        static const char filename[] = "pokemon_colosseum";
        if (memcmp(image, "GC6E01", 6) != 0 ||
            memcmp(image + 8, filename, sizeof(filename)) != 0 ||
            image[0x38] != 0 || image[0x39] != C3_SAVE_RAW_SIZE / 0x2000u)
            return C3_SAVE_GCI_HEADER;
    }
    *raw = image + (info->has_gci_header ? C3_SAVE_GCI_HEADER_SIZE : 0);
    info->icon_checksum_valid =
        sum_words(*raw, ICON_SUM_OFFSET) == be32(*raw + ICON_SUM_OFFSET);
    for (i = 0; i < C3_SAVE_SLOT_COUNT; ++i) {
        const uint8_t *slot = *raw + SLOT_BASE + i * C3_SAVE_SLOT_SIZE;
        uint32_t count = be32(slot + 4);
        if (slot[0] != 1 || slot[1] != 1 || sum_words(slot, 0x20) != 0)
            return C3_SAVE_SLOT_HEADER;
        /* Retail selects signed positive counters. Negative values are not
         * candidates; retain their raw value for diagnosis rather than wrap. */
        info->slot_counts[i] = count;
        if (count <= INT32_MAX && count > info->newest_count)
            info->newest_count = count;
    }
    return C3_SAVE_OK;
}

static int decode_slot(const uint8_t *slot, uint8_t *work)
{
    uint32_t expected[5];
    memcpy(work, slot, C3_SAVE_SLOT_SIZE);
    memcpy(expected, work + DIGEST_OFFSET, sizeof(expected));
    /* The first-eight-word checksum is zero during hashing/encryption. */
    memset(work + 0x0c, 0, 4);
    return c3_save_codec_decrypt(work, HASHED_SIZE, expected, CIPHER_OFFSET) != 0;
}

static C3SaveResult select_slot(const uint8_t *image, size_t length,
                               C3SaveInfo *info, uint8_t *work)
{
    const uint8_t *raw = NULL;
    unsigned attempted = 0;
    unsigned tries;
    C3SaveResult result = image_header(image, length, &raw, info);
    if (result != C3_SAVE_OK)
        return result;
    if (info->newest_count == 0)
        return C3_SAVE_EMPTY;
    for (tries = 0; tries < C3_SAVE_SLOT_COUNT; ++tries) {
        unsigned best = C3_SAVE_NO_SLOT;
        uint32_t best_count = 0;
        unsigned i;
        for (i = 0; i < C3_SAVE_SLOT_COUNT; ++i) {
            uint32_t count = info->slot_counts[i];
            if (!(attempted & (1u << i)) && count <= INT32_MAX && count > best_count) {
                best = i;
                best_count = count;
            }
        }
        if (best == C3_SAVE_NO_SLOT)
            break;
        attempted |= 1u << best;
        if (decode_slot(raw + SLOT_BASE + best * C3_SAVE_SLOT_SIZE, work)) {
            info->selected_slot = best;
            info->selected_count = best_count;
            info->recovered_older_slot = info->rejected_slots != 0;
            return C3_SAVE_OK;
        }
        info->rejected_slots |= 1u << best;
    }
    return C3_SAVE_INTEGRITY;
}

C3SaveResult c3_save_inspect(const void *image, size_t length, C3SaveInfo *info)
{
    C3SaveInfo local;
    C3SaveResult result;
    uint8_t *work;
    if (image == NULL)
        return C3_SAVE_ARGUMENT;
    if (length != C3_SAVE_RAW_SIZE && length != C3_SAVE_GCI_SIZE)
        return C3_SAVE_SIZE;
    work = malloc(C3_SAVE_SLOT_SIZE);
    if (work == NULL)
        return C3_SAVE_MEMORY;
    result = select_slot(image, length, &local, work);
    if (info != NULL && (result == C3_SAVE_OK || result == C3_SAVE_EMPTY ||
                         result == C3_SAVE_INTEGRITY))
        *info = local;
    free(work);
    return result;
}

C3SaveResult c3_save_decode(const void *image, size_t length,
                          void *body, size_t body_capacity, C3SaveInfo *info)
{
    C3SaveInfo local;
    C3SaveResult result;
    uint8_t *work;
    if (image == NULL || body == NULL)
        return C3_SAVE_ARGUMENT;
    if (body_capacity < C3_SAVE_BODY_SIZE ||
        (length != C3_SAVE_RAW_SIZE && length != C3_SAVE_GCI_SIZE))
        return C3_SAVE_SIZE;
    work = malloc(C3_SAVE_SLOT_SIZE);
    if (work == NULL)
        return C3_SAVE_MEMORY;
    result = select_slot(image, length, &local, work);
    if (result == C3_SAVE_OK)
        memcpy(body, work + 8, C3_SAVE_BODY_SIZE);
    if (info != NULL && (result == C3_SAVE_OK || result == C3_SAVE_EMPTY ||
                         result == C3_SAVE_INTEGRITY))
        *info = local;
    free(work);
    return result;
}

C3SaveResult c3_save_encode_slot(const void *body, size_t body_length,
                               uint32_t count, const uint8_t random[20],
                               void *slot, size_t slot_capacity)
{
    uint8_t *work;
    uint8_t padding[20];
    uint32_t checksum;
    if (body == NULL || random == NULL || slot == NULL || count == 0)
        return C3_SAVE_ARGUMENT;
    if (count > INT32_MAX)
        return C3_SAVE_COUNTER_OVERFLOW;
    if (body_length != C3_SAVE_BODY_SIZE || slot_capacity < C3_SAVE_SLOT_SIZE)
        return C3_SAVE_SIZE;
    work = calloc(1, C3_SAVE_SLOT_SIZE);
    if (work == NULL)
        return C3_SAVE_MEMORY;
    memcpy(padding, random, sizeof(padding));
    work[0] = work[1] = 1;
    put_be32(work + 4, count);
    memcpy(work + 8, body, C3_SAVE_BODY_SIZE);
    memset(work + 0x0c, 0, 4);
    memset(work + 0x20, 0, 4);
    c3_save_codec_encrypt(work, HASHED_SIZE, work + DIGEST_OFFSET, CIPHER_OFFSET);
    checksum = sum_words(work, 0x20);
    put_be32(work + 0x0c, 0u - checksum);
    /* Retail overwrites the 20-byte random trailer after encryption; its
     * first four bytes overlap the final feedback block, outside the hash. */
    memcpy(work + RANDOM_OFFSET, padding, sizeof(padding));
    memcpy(slot, work, C3_SAVE_SLOT_SIZE);
    free(work);
    return C3_SAVE_OK;
}

C3SaveResult c3_save_update(void *image, size_t length,
                          const void *body, size_t body_length,
                          const uint8_t random[20], C3SaveInfo *info)
{
    C3SaveInfo current;
    C3SaveResult result;
    uint8_t *slot;
    uint8_t *raw;
    unsigned next;
    if (image == NULL || body == NULL || random == NULL)
        return C3_SAVE_ARGUMENT;
    result = c3_save_inspect(image, length, &current);
    if (result != C3_SAVE_OK && result != C3_SAVE_EMPTY)
        return result;
    if (current.newest_count == INT32_MAX)
        return C3_SAVE_COUNTER_OVERFLOW;
    next = current.selected_slot == C3_SAVE_NO_SLOT ? 0 :
           (current.selected_slot + 1) % C3_SAVE_SLOT_COUNT;
    slot = malloc(C3_SAVE_SLOT_SIZE);
    if (slot == NULL)
        return C3_SAVE_MEMORY;
    result = c3_save_encode_slot(body, body_length, current.newest_count + 1,
                                 random, slot, C3_SAVE_SLOT_SIZE);
    if (result == C3_SAVE_OK) {
        raw = (uint8_t *)image + (current.has_gci_header ? C3_SAVE_GCI_HEADER_SIZE : 0);
        memcpy(raw + SLOT_BASE + next * C3_SAVE_SLOT_SIZE, slot, C3_SAVE_SLOT_SIZE);
        current.slot_counts[next] = current.newest_count + 1;
        current.selected_slot = next;
        current.selected_count = ++current.newest_count;
        current.rejected_slots = 0;
        current.recovered_older_slot = 0;
        if (info != NULL)
            *info = current;
    }
    free(slot);
    return result;
}

static C3SaveResult suffix_path(const char *path, const char *suffix, char out[PATH_LIMIT])
{
    size_t length;
    size_t suffix_length = strlen(suffix);
    if (path == NULL || path[0] == '\0')
        return C3_SAVE_ARGUMENT;
    length = strlen(path);
    if (length >= PATH_LIMIT || suffix_length >= PATH_LIMIT - length)
        return C3_SAVE_ARGUMENT;
    memcpy(out, path, length);
    memcpy(out + length, suffix, suffix_length + 1);
    return C3_SAVE_OK;
}

static C3SaveResult read_file(const char *path, uint8_t *buffer, size_t *length,
                             int *missing)
{
    struct stat status;
    FILE *file;
    size_t got;
    int error;
    *missing = 0;
    if (stat(path, &status) != 0) {
        *missing = errno == ENOENT;
        return C3_SAVE_IO;
    }
    if (!S_ISREG(status.st_mode))
        return C3_SAVE_IO;
    if (status.st_size != C3_SAVE_RAW_SIZE && status.st_size != C3_SAVE_GCI_SIZE)
        return C3_SAVE_SIZE;
    file = fopen(path, "rb");
    if (file == NULL)
        return C3_SAVE_IO;
    got = fread(buffer, 1, C3_SAVE_GCI_SIZE + 1u, file);
    error = ferror(file);
    if (fclose(file) != 0)
        error = 1;
    if (error)
        return C3_SAVE_IO;
    if (got != C3_SAVE_RAW_SIZE && got != C3_SAVE_GCI_SIZE)
        return C3_SAVE_SIZE;
    *length = got;
    return C3_SAVE_OK;
}

C3SaveResult c3_save_load_file(const char *path, void *image, size_t capacity,
                             size_t *length, C3SaveInfo *info, int *used_backup)
{
    char backup[PATH_LIMIT];
    uint8_t *buffer;
    size_t got = 0;
    C3SaveInfo local;
    C3SaveResult result;
    C3SaveResult primary_result;
    int missing;
    int recovered = 0;
    if (image == NULL || length == NULL)
        return C3_SAVE_ARGUMENT;
    result = suffix_path(path, ".bak", backup);
    if (result != C3_SAVE_OK)
        return result;
    buffer = malloc(C3_SAVE_GCI_SIZE + 1u);
    if (buffer == NULL)
        return C3_SAVE_MEMORY;
    result = read_file(path, buffer, &got, &missing);
    if (result == C3_SAVE_OK)
        result = c3_save_inspect(buffer, got, &local);
    primary_result = result;
    if (result != C3_SAVE_OK) {
        result = read_file(backup, buffer, &got, &missing);
        if (result == C3_SAVE_OK)
            result = c3_save_inspect(buffer, got, &local);
        if (result == C3_SAVE_OK)
            recovered = 1;
        else if (missing)
            result = primary_result;
    }
    if (result == C3_SAVE_OK) {
        if (capacity < got) {
            result = C3_SAVE_SIZE;
        } else {
            memcpy(image, buffer, got);
            *length = got;
            if (info != NULL)
                *info = local;
            if (used_backup != NULL)
                *used_backup = recovered;
        }
    }
    free(buffer);
    return result;
}

static C3SaveResult write_temporary(const char *path, const void *image, size_t length)
{
    int descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    FILE *file;
    int error = 0;
    if (descriptor < 0)
        return errno == EEXIST ? C3_SAVE_PENDING_FILE : C3_SAVE_IO;
    file = fdopen(descriptor, "wb");
    if (file == NULL) {
        close(descriptor);
        unlink(path);
        return C3_SAVE_IO;
    }
    if (fwrite(image, 1, length, file) != length || fflush(file) != 0 ||
        fsync(fileno(file)) != 0)
        error = 1;
    if (fclose(file) != 0)
        error = 1;
    if (error) {
        unlink(path);
        return C3_SAVE_IO;
    }
    return C3_SAVE_OK;
}

C3SaveResult c3_save_store_file(const char *path, const void *image, size_t length)
{
    char backup[PATH_LIMIT];
    char temporary[PATH_LIMIT];
    uint8_t *readback;
    C3SaveResult result;
    size_t got = 0;
    int missing;
    int have_current;
    if (image == NULL)
        return C3_SAVE_ARGUMENT;
    result = suffix_path(path, ".bak", backup);
    if (result != C3_SAVE_OK)
        return result;
    result = suffix_path(path, ".tmp", temporary);
    if (result != C3_SAVE_OK)
        return result;
    result = c3_save_inspect(image, length, NULL);
    if (result != C3_SAVE_OK)
        return result;
    readback = malloc(C3_SAVE_GCI_SIZE + 1u);
    if (readback == NULL)
        return C3_SAVE_MEMORY;
    result = read_file(path, readback, &got, &missing);
    have_current = !missing;
    if (have_current) {
        if (result == C3_SAVE_OK)
            result = c3_save_inspect(readback, got, NULL);
        /* Never silently replace an unreadable or unverified current file. */
        if (result != C3_SAVE_OK)
            goto done;
    }
    result = write_temporary(temporary, image, length);
    if (result != C3_SAVE_OK)
        goto done;
    result = read_file(temporary, readback, &got, &missing);
    if (result != C3_SAVE_OK || got != length || memcmp(readback, image, length) != 0) {
        result = C3_SAVE_IO;
        unlink(temporary);
        goto done;
    }
    if (have_current) {
        /* A validated current copy remains intact until this succeeds.
         * Destinations of both following renames must be absent on libctru. */
        if (unlink(backup) != 0 && errno != ENOENT) {
            result = C3_SAVE_IO;
            unlink(temporary);
            goto done;
        }
        if (rename(path, backup) != 0) {
            result = C3_SAVE_IO;
            unlink(temporary);
            goto done;
        }
    }
    if (rename(temporary, path) != 0) {
        /* Keep .bak and .tmp. load_file recovers the last committed save. */
        result = C3_SAVE_IO;
        goto done;
    }
    result = read_file(path, readback, &got, &missing);
    if (result != C3_SAVE_OK || got != length || memcmp(readback, image, length) != 0)
        result = C3_SAVE_IO;
done:
    free(readback);
    return result;
}
