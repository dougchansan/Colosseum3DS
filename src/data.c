/*
 * Native FSYS/LZSS reader. Format and algorithm reference:
 * Count-Sheep/pkmn-colosseum @ 51bfe4314a218dfaf9592fc80723c50498d0ee55
 * tools/fsys_extract.py and src/game/gs_range_8017F2C4.c (fn_8017F2C4).
 * This implementation adds input/output bounds and streams archive reads.
 */
#include "colosseum3ds/data.h"

#include <string.h>

static uint32_t be32(const unsigned char *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | (uint32_t)data[3];
}

static int within(uint32_t offset, size_t count, uint32_t limit)
{
    return offset <= limit && count <= (size_t)(limit - offset);
}

static C3DataResult read_bytes(const C3FsysReader *reader, uint32_t limit,
                               uint32_t offset, void *out, size_t count)
{
    if (!within(offset, count, limit) || !within(offset, count, reader->size))
        return C3_DATA_RANGE;
    if (count && reader->read_at(reader->context, offset, out, count) != 0)
        return C3_DATA_IO;
    return C3_DATA_OK;
}

static int valid_archive(const C3FsysArchive *archive)
{
    return archive && archive->reader.read_at &&
           archive->archive_size >= 0x40u &&
           archive->archive_size <= archive->reader.size &&
           archive->member_count <= C3_FSYS_MAX_MEMBERS &&
           within(archive->entry_table, (size_t)archive->member_count * 4u,
                  archive->archive_size);
}

C3DataResult c3_fsys_open(C3FsysArchive *archive, const C3FsysReader *reader)
{
    unsigned char header[0x40], tables[12];
    uint32_t archive_size, count, tables_offset, entry_table;
    C3DataResult result;
    C3FsysReader input;
    if (!archive || !reader || !reader->read_at)
        return C3_DATA_INVALID_ARGUMENT;
    input = *reader;
    reader = &input;
    memset(archive, 0, sizeof(*archive));
    if (reader->size < sizeof(header))
        return C3_DATA_TRUNCATED;
    result = read_bytes(reader, reader->size, 0, header, sizeof(header));
    if (result != C3_DATA_OK)
        return result;
    if (memcmp(header, "FSYS", 4) != 0)
        return C3_DATA_FORMAT;
    archive_size = be32(header + 0x20);
    count = be32(header + 0x0c);
    tables_offset = be32(header + 0x18);
    if (archive_size < sizeof(header) || archive_size > reader->size)
        return C3_DATA_RANGE;
    if (count > C3_FSYS_MAX_MEMBERS)
        return C3_DATA_LIMIT;
    result = read_bytes(reader, archive_size, tables_offset, tables, sizeof(tables));
    if (result != C3_DATA_OK)
        return result;
    entry_table = be32(tables);
    if (!within(entry_table, (size_t)count * 4u, archive_size))
        return C3_DATA_RANGE;
    archive->reader = *reader;
    archive->archive_id = be32(header + 8);
    archive->archive_size = archive_size;
    archive->member_count = count;
    archive->entry_table = entry_table;
    return C3_DATA_OK;
}

C3DataResult c3_fsys_entry(const C3FsysArchive *archive, uint32_t index,
                         C3FsysEntry *entry)
{
    unsigned char offset_bytes[4], raw[0x28];
    uint32_t offset, name_offset;
    size_t i, name_bytes;
    C3DataResult result;
    C3FsysEntry found;
    if (!valid_archive(archive) || !entry)
        return C3_DATA_INVALID_ARGUMENT;
    if (index >= archive->member_count)
        return C3_DATA_RANGE;
    result = read_bytes(&archive->reader, archive->archive_size,
                        archive->entry_table + index * 4u,
                        offset_bytes, sizeof(offset_bytes));
    if (result != C3_DATA_OK)
        return result;
    offset = be32(offset_bytes);
    result = read_bytes(&archive->reader, archive->archive_size, offset,
                        raw, sizeof(raw));
    if (result != C3_DATA_OK)
        return result;
    memset(&found, 0, sizeof(found));
    found.index = index;
    found.id = be32(raw);
    found.data_offset = be32(raw + 4);
    found.unpacked_size = be32(raw + 8);
    found.flags = be32(raw + 0x0c);
    found.packed_size = be32(raw + 0x14);
    found.group = be32(raw + 0x20);
    name_offset = be32(raw + 0x24);
    if (!within(found.data_offset, found.packed_size, archive->archive_size))
        return C3_DATA_RANGE;
    if (found.flags & C3_FSYS_FLAG_PACKED) {
        if (found.packed_size < 16u)
            return C3_DATA_TRUNCATED;
    } else if (found.unpacked_size > found.packed_size) {
        return C3_DATA_FORMAT;
    }
    if (name_offset >= archive->archive_size)
        return C3_DATA_RANGE;
    name_bytes = archive->archive_size - name_offset;
    if (name_bytes > sizeof(found.name))
        name_bytes = sizeof(found.name);
    result = read_bytes(&archive->reader, archive->archive_size, name_offset,
                        found.name, name_bytes);
    if (result != C3_DATA_OK)
        return result;
    for (i = 0; i < name_bytes; ++i) {
        if (found.name[i] == '\0') {
            memset(found.name + i + 1, 0, sizeof(found.name) - i - 1);
            *entry = found;
            return C3_DATA_OK;
        }
    }
    return name_bytes < sizeof(found.name) ? C3_DATA_TRUNCATED : C3_DATA_LIMIT;
}

C3DataResult c3_fsys_find(const C3FsysArchive *archive, const char *name,
                        C3FsysEntry *entry)
{
    uint32_t index;
    C3FsysEntry found;
    C3DataResult result;
    if (!valid_archive(archive) || !name || !entry)
        return C3_DATA_INVALID_ARGUMENT;
    for (index = 0; index < archive->member_count; ++index) {
        result = c3_fsys_entry(archive, index, &found);
        if (result != C3_DATA_OK)
            return result;
        if (strcmp(found.name, name) == 0) {
            *entry = found;
            return C3_DATA_OK;
        }
    }
    return C3_DATA_NOT_FOUND;
}

typedef struct InputStream {
    const C3FsysReader *reader;
    uint32_t base, size, position;
    size_t cached, next;
    unsigned char cache[512];
} InputStream;

/* 1 = byte read, 0 = end, -1 = read failure. */
static int take(InputStream *stream, unsigned char *value)
{
    size_t count;
    if (stream->position == stream->size)
        return 0;
    if (stream->next == stream->cached) {
        count = stream->size - stream->position;
        if (count > sizeof(stream->cache))
            count = sizeof(stream->cache);
        if (read_bytes(stream->reader, stream->reader->size,
                       stream->base + stream->position, stream->cache,
                       count) != C3_DATA_OK)
            return -1;
        stream->next = 0;
        stream->cached = count;
    }
    *value = stream->cache[stream->next++];
    ++stream->position;
    return 1;
}

static C3DataResult decode(const C3FsysReader *reader, uint32_t base,
                           uint32_t available, uint32_t expected_size,
                           int check_expected, void *destination,
                           size_t capacity, size_t *written)
{
    unsigned char header[16], window[4096], byte, first, second;
    unsigned char *output = (unsigned char *)destination;
    uint32_t packed_size, unpacked_size, r = 0xfee, flags = 0;
    size_t produced = 0;
    InputStream stream;
    C3DataResult result;
    if (available < sizeof(header))
        return C3_DATA_TRUNCATED;
    if (!within(base, available, reader->size))
        return C3_DATA_RANGE;
    result = read_bytes(reader, reader->size, base, header, sizeof(header));
    if (result != C3_DATA_OK)
        return result;
    if (memcmp(header, "LZSS", 4) != 0)
        return C3_DATA_FORMAT;
    unpacked_size = be32(header + 4);
    packed_size = be32(header + 8);
    if (packed_size < sizeof(header) || packed_size > available)
        return C3_DATA_RANGE;
    if (check_expected && unpacked_size != expected_size)
        return C3_DATA_FORMAT;
    if (unpacked_size > capacity)
        return C3_DATA_LIMIT;
    if (unpacked_size && !destination)
        return C3_DATA_INVALID_ARGUMENT;
    memset(window, 0, sizeof(window));
    memset(&stream, 0, sizeof(stream));
    stream.reader = reader;
    stream.base = base;
    stream.size = packed_size;
    stream.position = sizeof(header);
    while (stream.position < stream.size) {
        int got;
        flags >>= 1;
        if (!(flags & 0x100u)) {
            got = take(&stream, &byte);
            if (got != 1)
                return got < 0 ? C3_DATA_IO : C3_DATA_TRUNCATED;
            flags = (uint32_t)byte | 0xff00u;
        }
        got = take(&stream, &first);
        if (got != 1)
            return got < 0 ? C3_DATA_IO : C3_DATA_TRUNCATED;
        if (flags & 1u) {
            if (produced == unpacked_size)
                return C3_DATA_FORMAT;
            output[produced++] = first;
            window[r] = first;
            r = (r + 1u) & 0xfffu;
        } else {
            uint32_t match, length, k;
            got = take(&stream, &second);
            if (got != 1)
                return got < 0 ? C3_DATA_IO : C3_DATA_TRUNCATED;
            match = (uint32_t)first | (((uint32_t)second & 0xf0u) << 4);
            length = ((uint32_t)second & 0x0fu) + 3u;
            if (length > unpacked_size - produced)
                return C3_DATA_FORMAT;
            for (k = 0; k < length; ++k) {
                byte = window[(match + k) & 0xfffu];
                output[produced++] = byte;
                window[r] = byte;
                r = (r + 1u) & 0xfffu;
            }
        }
    }
    if (produced != unpacked_size)
        return C3_DATA_TRUNCATED;
    if (written)
        *written = produced;
    return C3_DATA_OK;
}

C3DataResult c3_fsys_extract(const C3FsysArchive *archive, uint32_t index,
                           void *destination, size_t capacity, size_t *written)
{
    C3FsysEntry entry;
    C3DataResult result;
    size_t copied = 0;
    if (written)
        *written = 0;
    if (!valid_archive(archive))
        return C3_DATA_INVALID_ARGUMENT;
    result = c3_fsys_entry(archive, index, &entry);
    if (result != C3_DATA_OK)
        return result;
    if (entry.unpacked_size > capacity)
        return C3_DATA_LIMIT;
    if (entry.unpacked_size && !destination)
        return C3_DATA_INVALID_ARGUMENT;
    if (entry.flags & C3_FSYS_FLAG_PACKED)
        return decode(&archive->reader, entry.data_offset, entry.packed_size,
                       entry.unpacked_size, 1, destination, capacity, written);
    while (copied < entry.unpacked_size) {
        size_t count = entry.unpacked_size - copied;
        if (count > 512u)
            count = 512u;
        result = read_bytes(&archive->reader, archive->archive_size,
                            entry.data_offset + (uint32_t)copied,
                            (unsigned char *)destination + copied, count);
        if (result != C3_DATA_OK)
            return result;
        copied += count;
    }
    if (written)
        *written = copied;
    return C3_DATA_OK;
}

static int memory_read(void *context, uint32_t offset, void *destination,
                        size_t size)
{
    memcpy(destination, (const unsigned char *)context + offset, size);
    return 0;
}

C3DataResult c3_lzss_decode(const void *source, size_t source_size,
                          void *destination, size_t capacity, size_t *written)
{
    C3FsysReader reader;
    if (written)
        *written = 0;
    if (!source)
        return C3_DATA_INVALID_ARGUMENT;
    if (source_size > UINT32_MAX)
        return C3_DATA_LIMIT;
    reader.read_at = memory_read;
    reader.context = (void *)source;
    reader.size = (uint32_t)source_size;
    return decode(&reader, 0, reader.size, 0, 0, destination, capacity, written);
}

const char *c3_data_result_string(C3DataResult result)
{
    switch (result) {
    case C3_DATA_OK: return "ok";
    case C3_DATA_INVALID_ARGUMENT: return "invalid argument";
    case C3_DATA_IO: return "read failed";
    case C3_DATA_FORMAT: return "invalid data format";
    case C3_DATA_RANGE: return "offset or length out of range";
    case C3_DATA_LIMIT: return "configured limit exceeded";
    case C3_DATA_NOT_FOUND: return "member not found";
    case C3_DATA_TRUNCATED: return "truncated data";
    default: return "unknown data error";
    }
}
