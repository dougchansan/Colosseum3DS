#ifndef C3_BATTLE_GENERAL_3D_H
#define C3_BATTLE_GENERAL_3D_H
#include "colosseum3ds/battle_general.h"
#include <stdio.h>
#include <stddef.h>

#define C3_BATTLE3D_ROOT "sdmc:/3ds/Colosseum3DS/boot/battle3d"

/* A general story battle on its original stage: the top screen shows the
 * battle floor, both sides' Pokemon and trainers (battle_general_stage.c),
 * the bottom screen the commands and messages (battle_general_scene.c).
 *
 * ready: whether the prepared 3D assets cover this battle's floor, its
 * opening Pokemon and both trainers. Cheap (reads the small index only);
 * call it before releasing the field's models. why receives the reason
 * when it returns 0.
 * run: caller has drained the GPU and released the field's models. Returns
 * 0 when the battle finished, 1 when the application must exit, -1 when
 * the 3D stage failed before the battle finished (the battle state is
 * intact and the caller may continue it on the 2D scene). */
int c3_battle_general_3d_ready(const C3GeneralBattle *battle, FILE *trace, char *why, size_t capacity);
int c3_battle_general_3d_run(const C3BattlePack *pack, C3GeneralBattle *battle, FILE *trace);

/* A link battle (docs/link-play.md) on the same stage. view is this
 * console's copy of the battle as it shows it (side 0 its own); the
 * caller's hooks carry commands to the other consoles and bring back each
 * agreed turn. */
typedef struct C3BattleLinkHooks {
    void *ctx;
    unsigned control_mask;   /* view positions (bits 0/1) this console commands */
    int autoplay;            /* probe: choose with a local generator, never the battle's */
    const char *shot_prefix; /* probe frames: sdmc:/3ds/Colosseum3DS/<prefix>_<frame>_top.bmp */
    /* Every frame; 0 when the link failed (error filled). */
    int (*pump)(void *ctx, char *error, size_t capacity);
    /* This console's commands, in view numbering (positions 0/1). */
    int (*submit)(void *ctx, const C3GeneralCommand commands[2]);
    /* 1 when the next agreed turn was applied; view then holds it. */
    int (*turn_ready)(void *ctx, C3GeneralBattle *view);
} C3BattleLinkHooks;
int c3_battle_general_3d_run_link(const C3BattlePack *pack, C3GeneralBattle *view, C3BattleLinkHooks *hooks, FILE *trace);
#ifdef C3_GENERAL_3D_PROBE
int c3_battle_general_3d_probe_run(uint32_t encounter);
#endif
#endif
