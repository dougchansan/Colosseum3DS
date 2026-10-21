#ifndef COLOSSEUM3DS_DATA_H
#define COLOSSEUM3DS_DATA_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Allocation-free, endian-explicit readers for the original FSYS format. */
typedef enum C3DataResult {
    C3_DATA_OK = 0,
    C3_DATA_INVALID_ARGUMENT,
    C3_DATA_IO,
    C3_DATA_FORMAT,
    C3_DATA_RANGE,
    C3_DATA_LIMIT,
    C3_DATA_NOT_FOUND,
    C3_DATA_TRUNCATED
} C3DataResult;

/* Return zero only when all requested bytes were read. No concurrent calls. */
typedef struct C3FsysReader {
    int (*read_at)(void *context, uint32_t offset, void *destination, size_t size);
    void *context;
    uint32_t size;
} C3FsysReader;

typedef struct C3FsysArchive {
    C3FsysReader reader;
    uint32_t archive_id;
    uint32_t member_count;
    uint32_t archive_size;
    uint32_t entry_table;
} C3FsysArchive;

#define C3_FSYS_NAME_CAPACITY 256u
#define C3_FSYS_MAX_MEMBERS 65535u
#define C3_FSYS_FLAG_PACKED UINT32_C(0x80000000)

typedef struct C3FsysEntry {
    uint32_t index;
    uint32_t id;
    uint32_t data_offset;
    uint32_t unpacked_size;
    uint32_t packed_size;
    uint32_t flags;
    uint32_t group;
    char name[C3_FSYS_NAME_CAPACITY];
} C3FsysEntry;

/* open validates the directory extent; entries are checked lazily. */
C3DataResult c3_fsys_open(C3FsysArchive *archive, const C3FsysReader *reader);
C3DataResult c3_fsys_entry(const C3FsysArchive *archive, uint32_t index,
                         C3FsysEntry *entry);
C3DataResult c3_fsys_find(const C3FsysArchive *archive, const char *name,
                        C3FsysEntry *entry);

/*
 * Decode/copy one member to caller-owned storage. At most capacity bytes are
 * written. written is set to zero on entry and the complete size on success;
 * discard the output on failure. No whole-archive allocation is performed.
 * LZSS decoding uses about 4.6 KiB of stack, independent of member size.
 */
C3DataResult c3_fsys_extract(const C3FsysArchive *archive, uint32_t index,
                           void *destination, size_t capacity, size_t *written);

/* Decode an LZSS member including its 16-byte big-endian header. */
C3DataResult c3_lzss_decode(const void *source, size_t source_size,
                          void *destination, size_t capacity, size_t *written);
const char *c3_data_result_string(C3DataResult result);

#ifdef __cplusplus
}
#endif
#endif
