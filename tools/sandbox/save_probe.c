/* Synthetic format-only ARM persistence test. Never a playable game save. */
#include <3ds.h>
#include "colosseum3ds/save.h"
#include "../../src/save_codec.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static const char *save_path = "sdmc:/3ds/Colosseum3DS/test-save/synthetic-format.gci";
static const char *report_path = "sdmc:/3ds/Colosseum3DS/test-save/result.txt";
static uint8_t body[C3_SAVE_BODY_SIZE];
static uint8_t image[C3_SAVE_GCI_SIZE];
static uint8_t slot[C3_SAVE_SLOT_SIZE];

int main(void)
{
    static const uint8_t expected[20] = {
        0xa2,0xc9,0xdb,0x06,0x42,0xfc,0xf4,0xc2,0xa9,0x81,
        0xb5,0x04,0xf2,0xe2,0x3e,0x38,0xdc,0x32,0x04,0x0e
    };
    uint8_t random[20], hash[20];
    C3SaveInfo info = {0};
    C3SaveResult load_result, store_result = C3_SAVE_OK;
    size_t length = 0;
    int used_backup = 0, writes = 0;
    uint32_t initial_count = 0;
    unsigned i;
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_BOTTOM, false);
    consoleInit(GFX_BOTTOM, NULL);
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/Colosseum3DS", 0777);
    mkdir("sdmc:/3ds/Colosseum3DS/test-save", 0777);
    for (i = 0; i < sizeof(body); ++i)
        body[i] = (uint8_t)(i * 37 + (i >> 8) * 11 + 13);
    for (i = 0; i < sizeof(random); ++i) random[i] = (uint8_t)(0xa0 + i);
    int golden = c3_save_encode_slot(body, sizeof(body), 0x01020304, random,
                                    slot, sizeof(slot)) == C3_SAVE_OK;
    c3_save_codec_sha1(hash, slot, sizeof(slot));
    golden = golden && !memcmp(hash, expected, sizeof(hash));
    load_result = c3_save_load_file(save_path, image, sizeof(image), &length, &info, &used_backup);
    if (load_result == C3_SAVE_OK) {
        initial_count = info.selected_count;
    } else {
        memset(image, 0, sizeof(image));
        memcpy(image, "GC6E01", 6);
        image[7] = 2;
        memcpy(image + 8, "pokemon_colosseum", 17);
        image[0x39] = 48;
        for (i = 0; i < 3; ++i)
            c3_save_encode_slot(body, sizeof(body), 10 + i, random,
                               image + 0x6040 + i * C3_SAVE_SLOT_SIZE, C3_SAVE_SLOT_SIZE);
        c3_save_inspect(image, sizeof(image), &info);
    }
    for (unsigned frame = 0; aptMainLoop(); ++frame) {
        hidScanInput();
        uint32_t down = hidKeysDown();
        if (golden && (down & KEY_A)) {
            if (initial_count || writes)
                store_result = c3_save_update(image, sizeof(image), body, sizeof(body), random, &info);
            if (store_result == C3_SAVE_OK)
                store_result = c3_save_store_file(save_path, image, sizeof(image));
            if (store_result == C3_SAVE_OK) ++writes;
        }
        if (frame == 0 || frame % 15 == 0 || down) {
            consoleClear();
            printf("Colosseum save codec ARM test\nSYNTHETIC FORMAT FIXTURE\nNot playable game progress\n\n");
            printf("Independent golden: %s\n", golden ? "PASS" : "FAIL");
            printf("Read at launch: %lu\nCurrent generation: %lu\n", (unsigned long)initial_count,
                   (unsigned long)info.selected_count);
            printf("Writes: %d\nLoad result: %d backup:%d\nStore result: %d\n\n", writes,
                   load_result, used_backup, store_result);
            printf("A writes fixture\nSTART exits\n");
            FILE *report = fopen(report_path, "w");
            if (report) {
                fprintf(report, "synthetic_format_fixture=1\ngolden_pass=%d\ninitial_count=%lu\ncurrent_count=%lu\nwrites=%d\nload_result=%d\nstore_result=%d\nused_backup=%d\n",
                        golden, (unsigned long)initial_count, (unsigned long)info.selected_count,
                        writes, load_result, store_result, used_backup);
                fclose(report);
            }
        }
        if (down & KEY_START) break;
        gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
    }
    gfxExit();
    return golden && store_result == C3_SAVE_OK ? 0 : 1;
}
