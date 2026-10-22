#define _POSIX_C_SOURCE 200809L
/* Format fixtures only: these bytes are not fabricated playable progress.
 * Independent byte-wise Python/hashlib vectors: tests/save_reference.py. */
#include "colosseum3ds/save.h"
#include "../src/save_codec.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned failures;
static unsigned checks;
static const char *fail_rename_source;

/* Host-only fault injection: save.c's rename is intercepted without a
 * production test hook. renameat implements the real operation otherwise. */
int rename(const char *source, const char *destination)
{
    if (fail_rename_source != NULL && strcmp(source, fail_rename_source) == 0) {
        errno = EIO;
        return -1;
    }
    return renameat(AT_FDCWD, source, AT_FDCWD, destination);
}

#define CHECK(expression) do { \
    ++checks; \
    if (!(expression)) { \
        fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #expression); \
        ++failures; \
    } \
} while (0)

static uint8_t *allocate(size_t length)
{
    uint8_t *p = malloc(length);
    if (p == NULL) {
        fputs("fixture allocation failed\n", stderr);
        exit(2);
    }
    return p;
}

static void put32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24); p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8); p[3] = (uint8_t)value;
}

static int hex_equal(const uint8_t *bytes, const char *hex)
{
    unsigned value;
    while (*hex) {
        if (sscanf(hex, "%2x", &value) != 1 || *bytes++ != value)
            return 0;
        hex += 2;
    }
    return 1;
}

static void fill_body(uint8_t *body)
{
    size_t i;
    for (i = 0; i < C3_SAVE_BODY_SIZE; ++i)
        body[i] = (uint8_t)(i * 37 + (i >> 8) * 11 + 13);
}

static void normalize_body(uint8_t *body)
{
    memset(body + 4, 0, 4);
    memset(body + 0x18, 0, 4);
}

static void make_image(uint8_t *image, const uint8_t *body, const uint8_t random[20])
{
    unsigned i;
    memset(image, 0, C3_SAVE_GCI_SIZE);
    memcpy(image, "GC6E01", 6);
    image[7] = 2;
    memcpy(image + 8, "pokemon_colosseum", 17);
    image[0x39] = 48;
    for (i = 0; i < 3; ++i)
        CHECK(c3_save_encode_slot(body, C3_SAVE_BODY_SIZE, 10 + i, random,
              image + 0x40 + 0x6000 + i * C3_SAVE_SLOT_SIZE,
              C3_SAVE_SLOT_SIZE) == C3_SAVE_OK);
}

static void test_known_vectors(uint8_t *body, const uint8_t random[20])
{
    uint8_t digest[20];
    uint8_t *slot = allocate(C3_SAVE_SLOT_SIZE + 1);
    static const char long_message[] =
        "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    c3_save_codec_sha1(digest, (const uint8_t *)"", 0);
    CHECK(hex_equal(digest, "da39a3ee5e6b4b0d3255bfef95601890afd80709"));
    c3_save_codec_sha1(digest, (const uint8_t *)"abc", 3);
    CHECK(hex_equal(digest, "a9993e364706816aba3e25717850c26c9cd0d89d"));
    c3_save_codec_sha1(digest, (const uint8_t *)long_message, sizeof(long_message) - 1);
    CHECK(hex_equal(digest, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
    /* Unaligned destination deliberately tests the wrapper's alignment. */
    CHECK(c3_save_encode_slot(body, C3_SAVE_BODY_SIZE, 0x01020304, random,
                             slot + 1, C3_SAVE_SLOT_SIZE) == C3_SAVE_OK);
    CHECK(hex_equal(slot + 1 + 0x1dfec, "a54a31da326f8cfaeea2d405f243660dcb152641"));
    CHECK(hex_equal(slot + 1,
        "01010000010203040d32577cadc46056355a7fa4c9ee1338073769e93c864865"
        "115d2bfa1482fa7a99382ea2837e433b5c2b96729bfd8750160569666be14005"));
    CHECK(hex_equal(slot + 1 + C3_SAVE_SLOT_SIZE - 64,
        "dc10b142272fe2c120c68b9b7e396ec84414e599fae50b78a0a1a2a3a4a5a6a7"
        "a8a9aaabacadaeafb0b1b2b3a54a31da326f8cfaeea2d405f243660dcb152641"));
    c3_save_codec_sha1(digest, slot + 1, C3_SAVE_SLOT_SIZE);
    CHECK(hex_equal(digest, "a2c9db0642fcf4c2a981b504f2e23e38dc32040e"));
    free(slot);
}

static void test_decode_and_validation(uint8_t *image, uint8_t *body,
                                       const uint8_t random[20])
{
    C3SaveInfo info;
    uint8_t *out = allocate(C3_SAVE_BODY_SIZE + 1);
    uint8_t *expected = allocate(C3_SAVE_BODY_SIZE);
    uint8_t *copy = allocate(C3_SAVE_GCI_SIZE);
    unsigned i;
    memcpy(expected, body, C3_SAVE_BODY_SIZE);
    normalize_body(expected);
    make_image(image, body, random);
    CHECK(c3_save_inspect(image, C3_SAVE_GCI_SIZE, &info) == C3_SAVE_OK);
    CHECK(info.has_gci_header && info.selected_slot == 2 && info.selected_count == 12);
    CHECK(info.newest_count == 12 && !info.recovered_older_slot && info.icon_checksum_valid);
    CHECK(c3_save_decode(image, C3_SAVE_GCI_SIZE, out + 1, C3_SAVE_BODY_SIZE, NULL) == C3_SAVE_OK);
    CHECK(memcmp(out + 1, expected, C3_SAVE_BODY_SIZE) == 0);
    CHECK(c3_save_inspect(image + 0x40, C3_SAVE_RAW_SIZE, &info) == C3_SAVE_OK);
    CHECK(!info.has_gci_header && info.selected_count == 12);
    CHECK(c3_save_inspect(image, C3_SAVE_GCI_SIZE - 1, NULL) == C3_SAVE_SIZE);
    CHECK(c3_save_inspect(NULL, C3_SAVE_GCI_SIZE, NULL) == C3_SAVE_ARGUMENT);

    memcpy(copy, image, C3_SAVE_GCI_SIZE);
    copy[3] = 'P';
    CHECK(c3_save_inspect(copy, C3_SAVE_GCI_SIZE, NULL) == C3_SAVE_GCI_HEADER);
    copy[3] = 'E'; copy[0x39] = 47;
    CHECK(c3_save_inspect(copy, C3_SAVE_GCI_SIZE, NULL) == C3_SAVE_GCI_HEADER);
    copy[0x39] = 48; copy[8] = 'X';
    CHECK(c3_save_inspect(copy, C3_SAVE_GCI_SIZE, NULL) == C3_SAVE_GCI_HEADER);
    memcpy(copy, image, C3_SAVE_GCI_SIZE);
    copy[0x40 + 0x6000 + 0x10] ^= 1;
    CHECK(c3_save_inspect(copy, C3_SAVE_GCI_SIZE, NULL) == C3_SAVE_SLOT_HEADER);

    memcpy(copy, image, C3_SAVE_GCI_SIZE);
    copy[0x40 + 0x6000 + 2 * C3_SAVE_SLOT_SIZE + 0x100] ^= 1;
    CHECK(c3_save_decode(copy, C3_SAVE_GCI_SIZE, out, C3_SAVE_BODY_SIZE, &info) == C3_SAVE_OK);
    CHECK(info.selected_slot == 1 && info.selected_count == 11 && info.newest_count == 12);
    CHECK(info.recovered_older_slot && info.rejected_slots == 4);
    CHECK(memcmp(out, expected, C3_SAVE_BODY_SIZE) == 0);
    for (i = 0; i < 2; ++i)
        copy[0x40 + 0x6000 + i * C3_SAVE_SLOT_SIZE + 0x100] ^= 1;
    memset(out, 0x5a, C3_SAVE_BODY_SIZE);
    CHECK(c3_save_decode(copy, C3_SAVE_GCI_SIZE, out, C3_SAVE_BODY_SIZE, &info) == C3_SAVE_INTEGRITY);
    CHECK(info.selected_slot == C3_SAVE_NO_SLOT && info.rejected_slots == 7);
    CHECK(out[0] == 0x5a && out[C3_SAVE_BODY_SIZE - 1] == 0x5a);
    memcpy(copy, image, C3_SAVE_GCI_SIZE);
    copy[0x40 + 0x100] = 1;
    CHECK(c3_save_inspect(copy, C3_SAVE_GCI_SIZE, &info) == C3_SAVE_OK);
    CHECK(!info.icon_checksum_valid);

    memset(copy + 0x40 + 0x6000, 0, 3 * C3_SAVE_SLOT_SIZE);
    for (i = 0; i < 3; ++i) {
        uint8_t *slot = copy + 0x40 + 0x6000 + i * C3_SAVE_SLOT_SIZE;
        slot[0] = slot[1] = 1;
        put32(slot + 12, 0u - 0x01010000u);
    }
    CHECK(c3_save_inspect(copy, C3_SAVE_GCI_SIZE, &info) == C3_SAVE_EMPTY);
    CHECK(info.selected_slot == C3_SAVE_NO_SLOT && info.newest_count == 0);
    CHECK(c3_save_update(copy, C3_SAVE_GCI_SIZE, body, C3_SAVE_BODY_SIZE, random, &info) == C3_SAVE_OK);
    CHECK(info.selected_count == 1 && info.selected_slot == 0);
    CHECK(c3_save_decode(copy, C3_SAVE_GCI_SIZE, out, C3_SAVE_BODY_SIZE, NULL) == C3_SAVE_OK);
    CHECK(memcmp(out, expected, C3_SAVE_BODY_SIZE) == 0);
    free(copy); free(expected); free(out);
}

static void test_updates(uint8_t *image, const uint8_t *body, const uint8_t random[20])
{
    uint8_t *copy = allocate(C3_SAVE_GCI_SIZE);
    C3SaveInfo info;
    make_image(image, body, random);
    memcpy(copy, image, C3_SAVE_GCI_SIZE);
    CHECK(c3_save_update(image, C3_SAVE_GCI_SIZE, body, C3_SAVE_BODY_SIZE, random, &info) == C3_SAVE_OK);
    CHECK(info.selected_count == 13 && info.selected_slot == 0);
    CHECK(memcmp(copy, image, 0x6040) == 0);
    CHECK(memcmp(copy + 0x6040 + C3_SAVE_SLOT_SIZE, image + 0x6040 + C3_SAVE_SLOT_SIZE,
                 2 * C3_SAVE_SLOT_SIZE) == 0);
    CHECK(c3_save_inspect(image, C3_SAVE_GCI_SIZE, &info) == C3_SAVE_OK && info.selected_count == 13);
    CHECK(c3_save_encode_slot(body, C3_SAVE_BODY_SIZE, 0x7fffffff, random,
                             image + 0x6040, C3_SAVE_SLOT_SIZE) == C3_SAVE_OK);
    memcpy(copy, image, C3_SAVE_GCI_SIZE);
    CHECK(c3_save_update(image, C3_SAVE_GCI_SIZE, body, C3_SAVE_BODY_SIZE, random, NULL) == C3_SAVE_COUNTER_OVERFLOW);
    CHECK(memcmp(copy, image, C3_SAVE_GCI_SIZE) == 0);
    CHECK(c3_save_encode_slot(body, C3_SAVE_BODY_SIZE, 0, random, image, C3_SAVE_SLOT_SIZE) == C3_SAVE_ARGUMENT);
    CHECK(c3_save_encode_slot(body, C3_SAVE_BODY_SIZE, 0x80000000, random,
                             image, C3_SAVE_SLOT_SIZE) == C3_SAVE_COUNTER_OVERFLOW);
    CHECK(c3_save_encode_slot(body, C3_SAVE_BODY_SIZE - 1, 1, random,
                             image, C3_SAVE_SLOT_SIZE) == C3_SAVE_SIZE);
    CHECK(memcmp(copy, image, C3_SAVE_GCI_SIZE) == 0);
    free(copy);
}

static void test_storage(uint8_t *image, const uint8_t *body, const uint8_t random[20])
{
    char directory[] = "/tmp/c3-save-test-XXXXXX";
    char path[256], backup[260], temporary[260];
    uint8_t *loaded = allocate(C3_SAVE_GCI_SIZE);
    uint8_t *old = allocate(C3_SAVE_GCI_SIZE);
    C3SaveInfo info;
    size_t length = 0;
    int recovered = -1;
    FILE *file;
    if (mkdtemp(directory) == NULL) {
        perror("mkdtemp"); exit(2);
    }
    snprintf(path, sizeof(path), "%s/pokemon_colosseum.gci", directory);
    snprintf(backup, sizeof(backup), "%s.bak", path);
    snprintf(temporary, sizeof(temporary), "%s.tmp", path);
    make_image(image, body, random);
    memcpy(old, image, C3_SAVE_GCI_SIZE);
    CHECK(c3_save_store_file(path, image, C3_SAVE_GCI_SIZE) == C3_SAVE_OK);
    CHECK(c3_save_load_file(path, loaded, C3_SAVE_GCI_SIZE, &length, &info, &recovered) == C3_SAVE_OK);
    CHECK(length == C3_SAVE_GCI_SIZE && !recovered && info.selected_count == 12);
    CHECK(memcmp(loaded, image, length) == 0);
    CHECK(c3_save_update(image, C3_SAVE_GCI_SIZE, body, C3_SAVE_BODY_SIZE, random, NULL) == C3_SAVE_OK);
    CHECK(c3_save_store_file(path, image, C3_SAVE_GCI_SIZE) == C3_SAVE_OK);
    CHECK(c3_save_load_file(backup, loaded, C3_SAVE_GCI_SIZE, &length, &info, NULL) == C3_SAVE_OK);
    CHECK(memcmp(loaded, old, length) == 0);
    memcpy(old, image, C3_SAVE_GCI_SIZE);

    /* Existing .tmp is never overwritten, nor does it authorize a commit. */
    file = fopen(temporary, "wb");
    if (!file) { perror("fopen"); exit(2); }
    CHECK(fwrite("pending", 1, 7, file) == 7);
    CHECK(fclose(file) == 0);
    CHECK(c3_save_store_file(path, image, C3_SAVE_GCI_SIZE) == C3_SAVE_PENDING_FILE);
    CHECK(c3_save_load_file(path, loaded, C3_SAVE_GCI_SIZE, &length, NULL, NULL) == C3_SAVE_OK);
    CHECK(memcmp(loaded, old, length) == 0);
    CHECK(unlink(temporary) == 0);

    /* Fail the second rename after the old committed copy became .bak. */
    CHECK(c3_save_update(image, C3_SAVE_GCI_SIZE, body, C3_SAVE_BODY_SIZE, random, NULL) == C3_SAVE_OK);
    fail_rename_source = temporary;
    CHECK(c3_save_store_file(path, image, C3_SAVE_GCI_SIZE) == C3_SAVE_IO);
    fail_rename_source = NULL;
    CHECK(access(path, F_OK) != 0);
    CHECK(c3_save_load_file(path, loaded, C3_SAVE_GCI_SIZE, &length, &info, &recovered) == C3_SAVE_OK);
    CHECK(recovered && info.selected_count == 13 && memcmp(loaded, old, length) == 0);
    CHECK(c3_save_store_file(path, image, C3_SAVE_GCI_SIZE) == C3_SAVE_PENDING_FILE);
    CHECK(unlink(temporary) == 0);
    CHECK(c3_save_store_file(path, image, C3_SAVE_GCI_SIZE) == C3_SAVE_OK);

    /* Invalid replacement never changes the current valid file. */
    image[0] = 'X';
    CHECK(c3_save_store_file(path, image, C3_SAVE_GCI_SIZE) == C3_SAVE_GCI_HEADER);
    image[0] = 'G';
    CHECK(c3_save_load_file(path, loaded, C3_SAVE_GCI_SIZE, &length, &info, &recovered) == C3_SAVE_OK);
    CHECK(!recovered && info.selected_count == 14 && memcmp(loaded, image, length) == 0);
    memset(loaded, 0x5a, C3_SAVE_GCI_SIZE);
    CHECK(c3_save_load_file(path, loaded, C3_SAVE_RAW_SIZE, &length, NULL, NULL) == C3_SAVE_SIZE);
    CHECK(loaded[0] == 0x5a && loaded[C3_SAVE_GCI_SIZE - 1] == 0x5a);
    CHECK(unlink(path) == 0);
    CHECK(unlink(backup) == 0);
    CHECK(rmdir(directory) == 0);
    free(old); free(loaded);
}

int main(void)
{
    uint8_t *body = allocate(C3_SAVE_BODY_SIZE);
    uint8_t *image = allocate(C3_SAVE_GCI_SIZE);
    uint8_t random[20];
    unsigned i;
    for (i = 0; i < sizeof(random); ++i)
        random[i] = (uint8_t)(0xa0 + i);
    fill_body(body);
    test_known_vectors(body, random);
    test_decode_and_validation(image, body, random);
    test_updates(image, body, random);
    test_storage(image, body, random);
    free(image); free(body);
    printf("save: %u checks, %u failures (synthetic format fixtures)\n", checks, failures);
    return failures ? 1 : 0;
}
