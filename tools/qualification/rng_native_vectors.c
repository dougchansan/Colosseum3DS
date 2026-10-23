/* Native API comparison lane; never linked into the Dolphin executor. */
#include "colosseum3ds/battle_rng.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    C3BattleRng rng;
    uint32_t seed, value = 0xdeadbeefu;
    int ok;
    if (argc < 3) return 2;
    seed = (uint32_t)strtoull(argv[2], NULL, 0);
    if (!c3_battle_rng_init(&rng, seed)) return 2;
    if (strcmp(argv[1], "next16") == 0) {
        uint16_t small = 0xbeefu;
        ok = c3_battle_rng_next16(&rng, &small);
        value = small;
    } else if (strcmp(argv[1], "bounded32") == 0 && argc == 4) {
        uint32_t modulus = (uint32_t)strtoull(argv[3], NULL, 0);
        ok = c3_battle_rng_bounded32(&rng, modulus, &value);
    } else return 2;
    printf("{\"ok\":%d,\"value\":%" PRIu32 ",\"seed\":%" PRIu32 ",\"draws\":%" PRIu64 "}\n",
           ok, value, rng.seed, rng.draws);
    return 0;
}
