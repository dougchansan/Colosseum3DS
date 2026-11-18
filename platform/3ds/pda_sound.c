#include "pda_sound.h"
#include "sfx_player.h"
#include "battle_general_3d.h"

static int owned, failed;

void c3_pda_sound_play(uint32_t sound)
{
    char error[128];
    if (!sound) return;
    /* c3_sfx_has is 0 while closed: a pack opened elsewhere is borrowed. */
    if (!owned && !failed && !c3_sfx_has(0x23) && !c3_sfx_has(0x24) && !c3_sfx_has(0x25)) {
        if (c3_sfx_open(C3_BATTLE3D_ROOT "/sfx.c3s", error, sizeof error)) owned = 1;
        else failed = 1; /* tried once per menu session */
    }
    if (c3_sfx_has(sound)) c3_sfx_play(sound, 127);
}

void c3_pda_sound_close(void)
{
    if (owned) { c3_sfx_stop_all(); c3_sfx_close(); }
    owned = failed = 0;
}
