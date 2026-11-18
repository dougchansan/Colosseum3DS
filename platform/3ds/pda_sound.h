#ifndef C3_PDA_SOUND_H
#define C3_PDA_SOUND_H
#include <stdint.h>
/* In-game menu / P*DA button sounds (C3IngameMenu.sound: 0x23 cursor, 0x24
 * decide, 0x25 cancel) from the prepared original sound pack
 * (battle3d/sfx.c3s, tools/prepare_battle_sfx.py UI_SOUNDS). The pack opens
 * on the first sound and closes with c3_pda_sound_close when the menu does;
 * a pack another owner already opened is used and left open. Missing pack or
 * NDSP: silent. */
void c3_pda_sound_play(uint32_t sound);
void c3_pda_sound_close(void);
#endif
