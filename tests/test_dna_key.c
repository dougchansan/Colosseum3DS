#include "colosseum3ds/dna_key.h"
#include <assert.h>
#include <stdio.h>
int main(void){C3DnaKey s;uint16_t e[3]={0x99,0x9c,0x9f};static const unsigned mapped[9]={0x99,0x9c,0x9f,0xb9,0xc8,0x11f,0xf3,0xf4,0xf5};for(unsigned i=0;i<18;i++)assert(c3_dna_key_species(0x200+i)==mapped[i/2]);
    assert(c3_dna_key_species(0x200)==0x99);
    assert(c3_dna_key_species(0x211)==0xf5);
    assert(!c3_dna_key_species(0x212));
    assert(c3_dna_key_init(&s,e));
    c3_dna_key_input(&s,0x10,0x20);
    assert(s.count==1);
    c3_dna_key_input(&s,0x10,0);
    assert(s.count==1);
    c3_dna_key_input(&s,0,0x20);
    assert(!s.count&&!s.occupied[4]);
    c3_dna_key_input(&s,0,0x20);
    assert(s.result==1);
    assert(c3_dna_key_init(&s,e));
    c3_dna_key_input(&s,0x10,0);
    c3_dna_key_input(&s,2|4,0);
    c3_dna_key_input(&s,0x10,0);
    c3_dna_key_input(&s,8,0);
    c3_dna_key_input(&s,8,0);
    c3_dna_key_input(&s,0x10,0);
    assert(s.result==0&&s.count==3);
    assert(c3_dna_key_init(&s,e));
    c3_dna_key_input(&s,1|4,0);
    c3_dna_key_input(&s,0x10,0);
    c3_dna_key_input(&s,8,0);
    c3_dna_key_input(&s,0x10,0);
    c3_dna_key_input(&s,8,0);
    c3_dna_key_input(&s,0x10,0);
    assert(s.result==-1&&s.count==3&&s.sound==0x26&&s.reset_pending);c3_dna_key_sound_done(&s);
    assert(!s.count&&!s.reset_pending);
    e[0]=1;
    assert(!c3_dna_key_init(&s,e)&&s.result==2);
    puts("DNA key: 32 assertions passed");
    return 0;
}

