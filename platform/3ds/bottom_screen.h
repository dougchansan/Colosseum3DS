#ifndef C3_BOTTOM_SCREEN_H
#define C3_BOTTOM_SCREEN_H
#include "colosseum3ds/bottom_ui.h"
/* CPU-only bottom framebuffer. Never starts/swaps a C3D frame or allocates
 * linear memory. Console diagnostics can still take over on failure. */
void c3_bottom_screen_init(void);
void c3_bottom_screen_close(void);
int c3_bottom_screen_has_original_assets(void);
/* Begin draws into an ordinary CPU buffer. Present publishes one complete
 * image in the current console format without swapping graphics buffers. */
C3BottomUi *c3_bottom_screen_begin(void);
void c3_bottom_screen_present(void);
void c3_bottom_screen_title(int waiting,int accepted);
void c3_bottom_screen_notice(const char *title,const char *text,const char *footer);
#endif
