#include "colosseum3ds/core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char diagnostic[128];
    assert(c3_port_selftest() == 0);
    c3_rng_seed(0xffffffffu);
    assert(c3_rng_next() == 35);
    assert(c3_data_probe("/path/that/does/not/exist", diagnostic, sizeof(diagnostic)) != 0);
    assert(strstr(diagnostic, "common.fsys") != NULL);
    assert(c3_data_probe(NULL, diagnostic, sizeof(diagnostic)) != 0);
    puts("Native decomp startup checks passed");
    return 0;
}
