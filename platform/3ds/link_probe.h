#ifndef C3_LINK_PROBE_H
#define C3_LINK_PROBE_H
#include "colosseum3ds/link_protocol.h"
#include "link_uds.h"
#include "colosseum3ds/battle_general.h"
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

typedef struct C3LinkProbeConfig {
    char role[8], name[C3_LINK_NAME_SIZE], mode[16], type[8];
    unsigned players, rule, slot, turn_limit;
    uint32_t seed;
} C3LinkProbeConfig;
extern C3LinkProbeConfig c3_link_probe_config;

int c3_link_probe_run(void);
void c3_link_probe_log(FILE *log, const char *fmt, ...);
int c3_link_probe_pump(C3LinkSession *s);
void c3_link_probe_shot(FILE *log, const char *tag);
/* Battle Now, Colosseum Battle and trading probes (link_play_scene.c). */
int c3_link_probe_game(C3LinkSession *s, const char *mode, FILE *log);
int c3_link_probe_trade(C3LinkSession *s, FILE *log);
/* The save's party, read only (saves/pokemon_colosseum.gci). */
int c3_link_probe_load_party(uint8_t party[6][C3_GB_POKEMON_SIZE], FILE *log);
#endif
