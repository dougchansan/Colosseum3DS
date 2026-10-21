/* Compile the pinned source unchanged, with private symbol names. */
#include "colosseum3ds/core.h"
#define fn_801A6990 c3_hsd_seed_released
#define lbl_80478C90 c3_hsd_seed
#define lbl_80478C94 c3_hsd_seed_pointer
#define fn_801ADCD8 c3_hsd_rand
#define fn_801ADC7C c3_hsd_randf
#define fn_801ADC3C c3_hsd_randi
#define _HSD_RandForgetMemory c3_hsd_forget_memory
#include "../vendor/colosseum/src/hsd/random.c"

/* This isolated slice owns a static seed; no HSD heap exists yet. */
int c3_hsd_seed_released(void *pointer)
{
    return pointer != &c3_hsd_seed;
}

void c3_rng_seed(uint32_t seed)
{
    c3_hsd_seed_pointer = &c3_hsd_seed;
    c3_hsd_seed = seed;
}

uint32_t c3_rng_next(void)
{
    return (uint32_t)c3_hsd_rand();
}
