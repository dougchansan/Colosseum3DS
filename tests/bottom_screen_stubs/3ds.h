#ifndef C3_TEST_3DS_H
#define C3_TEST_3DS_H
#include <stdint.h>
enum { GFX_BOTTOM, GFX_LEFT, GSP_RGB565_OES, GSP_BGR8_OES };
typedef struct { const unsigned char *gfx; unsigned asciiOffset,numChars; } ConsoleFont;
typedef struct { ConsoleFont font; } PrintConsole;
PrintConsole *consoleGetDefault(void);
int gfxGetScreenFormat(int screen);
unsigned char *gfxGetFramebuffer(int screen,int side,uint16_t *width,uint16_t *height);
void gfxFlushBuffers(void);
#endif
