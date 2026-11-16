#ifndef C3_DNA_KEY_H
#define C3_DNA_KEY_H
#include <stdint.h>
/* Retail menu/CardGrid state. Input bits are the original window key bits. */
typedef struct { uint16_t expected[3]; uint8_t occupied[9],choices[3],count,x,y,reset_pending; int result; uint32_t sound; uint8_t clips[12]; } C3DnaKey;
uint16_t c3_dna_key_species(uint32_t item);
int c3_dna_key_init(C3DnaKey *state,const uint16_t expected[3]);
void c3_dna_key_sound_done(C3DnaKey *state);
void c3_dna_key_input(C3DnaKey *state,uint16_t repeat,uint16_t buttons);
#endif
