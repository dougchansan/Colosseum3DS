#include "colosseum3ds/data.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct TestFile {
    unsigned char bytes[8192];
    uint32_t size;
    size_t largest_read;
    int fail_reads;
} TestFile;

static void put32(unsigned char *bytes, uint32_t value)
{
    bytes[0] = (unsigned char)(value >> 24);
    bytes[1] = (unsigned char)(value >> 16);
    bytes[2] = (unsigned char)(value >> 8);
    bytes[3] = (unsigned char)value;
}

static int test_read(void *context, uint32_t offset, void *destination, size_t size)
{
    TestFile *file = (TestFile *)context;
    assert(offset <= file->size);
    assert(size <= file->size - offset);
    if (file->fail_reads)
        return -1;
    if (size > file->largest_read)
        file->largest_read = size;
    memcpy(destination, file->bytes + offset, size);
    return 0;
}

static C3FsysReader make_reader(TestFile *file)
{
    C3FsysReader reader;
    reader.read_at = test_read;
    reader.context = file;
    reader.size = file->size;
    return reader;
}

static void init_archive(TestFile *file)
{
    memset(file, 0, sizeof(*file));
    file->size = 512;
    memcpy(file->bytes, "FSYS", 4);
    put32(file->bytes + 8, 0x12345678);
    put32(file->bytes + 0x0c, 1);
    put32(file->bytes + 0x18, 0x40);
    put32(file->bytes + 0x20, file->size);
    put32(file->bytes + 0x40, 0x60);
    put32(file->bytes + 0x44, 0xc0);
    put32(file->bytes + 0x48, 0x100);
    put32(file->bytes + 0x60, 0x80);
    put32(file->bytes + 0x80, 0x87651c00);
    put32(file->bytes + 0x84, 0x100);
    put32(file->bytes + 0x88, 4);
    put32(file->bytes + 0x94, 4);
    put32(file->bytes + 0xa0, 0xabcde123);
    put32(file->bytes + 0xa4, 0xc0);
    memcpy(file->bytes + 0xc0, "native_test", 12);
    memcpy(file->bytes + 0x100, "DATA", 4);
}

static void lzss_header(unsigned char *bytes, uint32_t output, uint32_t packed)
{
    memset(bytes, 0, 16);
    memcpy(bytes, "LZSS", 4);
    put32(bytes + 4, output);
    put32(bytes + 8, packed);
}

static void test_stored_archive(void)
{
    TestFile file;
    C3FsysReader reader;
    C3FsysArchive archive;
    C3FsysEntry entry;
    unsigned char output[4];
    size_t written = 99;
    init_archive(&file);
    reader = make_reader(&file);
    assert(c3_fsys_open(&archive, &reader) == C3_DATA_OK);
    assert(archive.archive_id == 0x12345678 && archive.member_count == 1);
    assert(c3_fsys_entry(&archive, 0, &entry) == C3_DATA_OK);
    assert(entry.id == 0x87651c00 && entry.group == 0xabcde123);
    assert(strcmp(entry.name, "native_test") == 0);
    assert(c3_fsys_find(&archive, "native_test", &entry) == C3_DATA_OK);
    assert(c3_fsys_find(&archive, "missing", &entry) == C3_DATA_NOT_FOUND);
    assert(c3_fsys_entry(&archive, 1, &entry) == C3_DATA_RANGE);
    assert(c3_fsys_extract(&archive, 0, output, sizeof(output), &written) == C3_DATA_OK);
    assert(written == 4 && memcmp(output, "DATA", 4) == 0);
    assert(file.largest_read <= 512);
    memset(output, 0xa5, sizeof(output));
    assert(c3_fsys_extract(&archive, 0, output, 3, &written) == C3_DATA_LIMIT);
    assert(written == 0 && output[0] == 0xa5);
    assert(c3_fsys_extract(&archive, 0, NULL, 4, &written) == C3_DATA_INVALID_ARGUMENT);
}

static void test_compressed_archive(void)
{
    TestFile file;
    C3FsysReader reader;
    C3FsysArchive archive;
    unsigned char output[8];
    size_t written;
    init_archive(&file);
    put32(file.bytes + 0x88, 5);
    put32(file.bytes + 0x8c, C3_FSYS_FLAG_PACKED);
    put32(file.bytes + 0x94, 24); /* two trailing alignment bytes */
    lzss_header(file.bytes + 0x100, 5, 22);
    file.bytes[0x110] = 0x1f;
    memcpy(file.bytes + 0x111, "HELLO", 5);
    reader = make_reader(&file);
    assert(c3_fsys_open(&archive, &reader) == C3_DATA_OK);
    assert(c3_fsys_extract(&archive, 0, output, sizeof(output), &written) == C3_DATA_OK);
    assert(written == 5 && memcmp(output, "HELLO", 5) == 0);
    put32(file.bytes + 0x104, 6);
    assert(c3_fsys_extract(&archive, 0, output, sizeof(output), &written) == C3_DATA_FORMAT);
    assert(written == 0);
}

static void test_lzss_window(void)
{
    unsigned char packed[64], output[64];
    size_t written, i, pos;
    lzss_header(packed, 3, 19);
    packed[16] = 0; packed[17] = 0; packed[18] = 0;
    memset(output, 0xa5, sizeof(output));
    assert(c3_lzss_decode(packed, 19, output, sizeof(output), &written) == C3_DATA_OK);
    assert(written == 3 && output[0] == 0 && output[1] == 0 && output[2] == 0);
    lzss_header(packed, 10, 20);
    packed[16] = 1; packed[17] = 'A'; packed[18] = 0xee; packed[19] = 0xf6;
    assert(c3_lzss_decode(packed, 20, output, sizeof(output), &written) == C3_DATA_OK);
    assert(written == 10);
    for (i = 0; i < written; ++i)
        assert(output[i] == 'A');

    /* Cross the ring boundary, then copy an overlapping QRST pattern. */
    lzss_header(packed, 38, 41);
    pos = 16;
    for (i = 0; i < 20; ++i) {
        if (i % 8 == 0)
            packed[pos++] = i == 16 ? 0x0f : 0xff;
        packed[pos++] = (unsigned char)('A' + i);
    }
    packed[pos++] = 0xfe; packed[pos++] = 0xff;
    assert(pos == 41);
    assert(c3_lzss_decode(packed, pos, output, sizeof(output), &written) == C3_DATA_OK);
    assert(written == 38);
    for (i = 0; i < 20; ++i)
        assert(output[i] == 'A' + i);
    for (i = 20; i < written; ++i)
        assert(output[i] == 'Q' + (i - 20) % 4);
}

static void test_lzss_errors(void)
{
    unsigned char packed[32], output[8];
    size_t written = 99;
    lzss_header(packed, 0, 16);
    assert(c3_lzss_decode(packed, 16, NULL, 0, &written) == C3_DATA_OK);
    assert(written == 0);
    assert(c3_lzss_decode(NULL, 0, output, sizeof(output), &written) == C3_DATA_INVALID_ARGUMENT);
    assert(c3_lzss_decode(packed, 15, output, sizeof(output), &written) == C3_DATA_TRUNCATED);
    packed[0] = 'X';
    assert(c3_lzss_decode(packed, 16, output, sizeof(output), &written) == C3_DATA_FORMAT);
    lzss_header(packed, 1, 19);
    assert(c3_lzss_decode(packed, 18, output, sizeof(output), &written) == C3_DATA_RANGE);
    put32(packed + 8, 15);
    assert(c3_lzss_decode(packed, 18, output, sizeof(output), &written) == C3_DATA_RANGE);
    lzss_header(packed, 1, 17);
    packed[16] = 1;
    assert(c3_lzss_decode(packed, 17, output, sizeof(output), &written) == C3_DATA_TRUNCATED);
    lzss_header(packed, 1, 18);
    packed[16] = 0; packed[17] = 0;
    assert(c3_lzss_decode(packed, 18, output, sizeof(output), &written) == C3_DATA_TRUNCATED);
    lzss_header(packed, 2, 19);
    packed[16] = 0; packed[17] = 0; packed[18] = 0;
    assert(c3_lzss_decode(packed, 19, output, sizeof(output), &written) == C3_DATA_FORMAT);
    assert(written == 0);
    lzss_header(packed, 4, 18);
    packed[16] = 1; packed[17] = 'A';
    assert(c3_lzss_decode(packed, 18, output, sizeof(output), &written) == C3_DATA_TRUNCATED);
    memset(output, 0xa5, sizeof(output));
    assert(c3_lzss_decode(packed, 18, output, 3, &written) == C3_DATA_LIMIT);
    assert(written == 0 && output[0] == 0xa5);
    assert(c3_lzss_decode(packed, 18, NULL, 4, &written) == C3_DATA_INVALID_ARGUMENT);
}

static void test_archive_errors(void)
{
    TestFile file;
    C3FsysReader reader;
    C3FsysArchive archive;
    C3FsysEntry entry;
    init_archive(&file);
    reader = make_reader(&file);
    assert(c3_fsys_open(NULL, &reader) == C3_DATA_INVALID_ARGUMENT);
    reader.size = 63;
    assert(c3_fsys_open(&archive, &reader) == C3_DATA_TRUNCATED);
    reader = make_reader(&file);
    file.fail_reads = 1;
    assert(c3_fsys_open(&archive, &reader) == C3_DATA_IO);
    file.fail_reads = 0;
    file.bytes[0] = 'X';
    assert(c3_fsys_open(&archive, &reader) == C3_DATA_FORMAT);
    file.bytes[0] = 'F';
    put32(file.bytes + 0x18, UINT32_MAX - 3);
    assert(c3_fsys_open(&archive, &reader) == C3_DATA_RANGE);
    put32(file.bytes + 0x18, 0x40);
    put32(file.bytes + 0x0c, UINT32_MAX);
    assert(c3_fsys_open(&archive, &reader) == C3_DATA_LIMIT);
    put32(file.bytes + 0x0c, 1);
    put32(file.bytes + 0x40, file.size - 2);
    assert(c3_fsys_open(&archive, &reader) == C3_DATA_RANGE);
    put32(file.bytes + 0x40, 0x60);
    assert(c3_fsys_open(&archive, &reader) == C3_DATA_OK);
    put32(file.bytes + 0x60, file.size - 8);
    assert(c3_fsys_entry(&archive, 0, &entry) == C3_DATA_RANGE);
    put32(file.bytes + 0x60, 0x80);
    put32(file.bytes + 0x84, UINT32_MAX - 1);
    assert(c3_fsys_entry(&archive, 0, &entry) == C3_DATA_RANGE);
    put32(file.bytes + 0x84, 0x100);
    put32(file.bytes + 0x88, 5);
    assert(c3_fsys_entry(&archive, 0, &entry) == C3_DATA_FORMAT);
    put32(file.bytes + 0x88, 4);
    put32(file.bytes + 0x8c, C3_FSYS_FLAG_PACKED);
    assert(c3_fsys_entry(&archive, 0, &entry) == C3_DATA_TRUNCATED);
    put32(file.bytes + 0x8c, 0);
    put32(file.bytes + 0xa4, file.size - 1);
    file.bytes[file.size - 1] = 'X';
    assert(c3_fsys_entry(&archive, 0, &entry) == C3_DATA_TRUNCATED);
    put32(file.bytes + 0xa4, 0xc0);
    memset(file.bytes + 0xc0, 'X', C3_FSYS_NAME_CAPACITY);
    assert(c3_fsys_entry(&archive, 0, &entry) == C3_DATA_LIMIT);
    file.fail_reads = 1;
    assert(c3_fsys_entry(&archive, 0, &entry) == C3_DATA_IO);
}

static void test_streamed_member(void)
{
    TestFile file;
    C3FsysReader reader;
    C3FsysArchive archive;
    unsigned char output[5000];
    size_t pos, i, written;
    init_archive(&file);
    file.size = sizeof(file.bytes);
    put32(file.bytes + 0x20, file.size);
    put32(file.bytes + 0x88, sizeof(output));
    put32(file.bytes + 0x8c, C3_FSYS_FLAG_PACKED);
    pos = 0x110;
    for (i = 0; i < sizeof(output); ++i) {
        if (i % 8 == 0)
            file.bytes[pos++] = 0xff;
        file.bytes[pos++] = (unsigned char)i;
    }
    lzss_header(file.bytes + 0x100, sizeof(output), (uint32_t)(pos - 0x100));
    put32(file.bytes + 0x94, (uint32_t)(pos - 0x100));
    reader = make_reader(&file);
    assert(c3_fsys_open(&archive, &reader) == C3_DATA_OK);
    assert(c3_fsys_extract(&archive, 0, output, sizeof(output), &written) == C3_DATA_OK);
    assert(written == sizeof(output));
    for (i = 0; i < written; ++i)
        assert(output[i] == (unsigned char)i);
    assert(file.largest_read == 512);
}

int main(void)
{
    test_stored_archive();
    test_compressed_archive();
    test_lzss_window();
    test_lzss_errors();
    test_archive_errors();
    test_streamed_member();
    puts("FSYS/LZSS: 6 test groups passed (bounds, streaming, overlap, ring wrap, malformed input)");
    return 0;
}
