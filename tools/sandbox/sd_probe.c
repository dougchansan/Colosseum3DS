/* Isolated emulator SD persistence/input probe; not a Colosseum save format. */
#include <3ds.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>

static const char *path = "sdmc:/3ds/Colosseum3DS/harness-probe.bin";
int main(void) {
    gfxInitDefault(); gfxSetDoubleBuffering(GFX_BOTTOM, false); consoleInit(GFX_BOTTOM, NULL);
    mkdir("sdmc:/3ds", 0777); mkdir("sdmc:/3ds/Colosseum3DS", 0777);
    uint32_t record[3] = {0x50533343, 0, UINT32_MAX};
    FILE *f = fopen(path, "rb");
    int okay = 1, wrote = 0;
    if (f) {
        okay = fread(record, sizeof(record), 1, f) == 1 && record[0] == 0x50533343 && record[2] == ~record[1];
        fclose(f);
    }
    uint32_t initial = record[1], last_keys = UINT32_MAX;
    for (unsigned frame = 0; aptMainLoop(); ++frame) {
        hidScanInput(); uint32_t keys = hidKeysHeld();
        if (okay && (hidKeysDown() & KEY_A)) {
            record[1]++; record[2] = ~record[1]; f = fopen(path, "wb");
            if (!f) okay = 0;
            else {
                okay = fwrite(record, sizeof(record), 1, f) == 1;
                if (fclose(f)) okay = 0;
                if (okay) {
                    uint32_t verify[3]; f = fopen(path, "rb");
                    if (!f) okay = 0;
                    else { okay = fread(verify, sizeof(verify), 1, f) == 1 && !memcmp(verify, record, sizeof(record)); fclose(f); }
                }
                if (okay) wrote++;
            }
        }
        if (keys != last_keys || frame % 15 == 0) {
            consoleClear();
            printf("Sandbox SD/input probe\nNot a Colosseum save\n\n");
            printf("Read at launch: %lu\nCurrent count: %lu\nVerified writes: %d\nStatus: %s\n\n", (unsigned long)initial, (unsigned long)record[1], wrote, okay ? "PASS" : "FAIL");
            printf("HID %08lx\nA writes once on press\nSTART exits\n", (unsigned long)keys);
            last_keys = keys;
        }
        if (hidKeysDown() & KEY_START) break;
        gfxFlushBuffers(); gfxSwapBuffers(); gspWaitForVBlank();
    }
    gfxExit(); return okay ? 0 : 1;
}
