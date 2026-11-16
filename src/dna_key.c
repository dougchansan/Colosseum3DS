#include "colosseum3ds/dna_key.h"
#include <string.h>
static const uint16_t species[9]={0xb9,0xc8,0xf4,0x11f,0x9c,0xf3,0x99,0xf5,0x9f};
uint16_t c3_dna_key_species(uint32_t item){static const uint16_t map[9]={0x99,0x9c,0x9f,0xb9,0xc8,0x11f,0xf3,0xf4,0xf5};return item>=0x200&&item<=0x211?map[(item-0x200)/2]:0;}
int c3_dna_key_init(C3DnaKey *s,const uint16_t expected[3]){
    if(!s||!expected)return 0;
    memset(s,0,sizeof *s);s->x=s->y=1;s->result=-1;
    for(unsigned i=0;i<3;i++){unsigned j;for(j=0;j<9&&species[j]!=expected[i];j++);if(j==9){s->result=2;return 0;}s->expected[i]=expected[i];}return 1;
}
void c3_dna_key_sound_done(C3DnaKey *s){if(s&&s->reset_pending){memset(s->occupied,0,9);memset(s->clips,0,9);s->count=0;s->clips[11]=0;s->reset_pending=0;}}
void c3_dna_key_input(C3DnaKey *s,uint16_t repeat,uint16_t buttons){
    static const uint8_t cursor[9]={1,2,3,4,0,5,6,7,8};
    if(!s||s->result!=-1||s->reset_pending)return;
    s->sound=0;
    if(repeat&0x10){unsigned p=s->y*3+s->x;if(!s->occupied[p]){s->occupied[p]=1;s->clips[p]=1;s->choices[s->count++]=p;s->clips[11]=s->count;
        if(s->count==3){unsigned matches=0;for(unsigned j=0;j<9;j++)if(s->occupied[j])for(unsigned k=0;k<3;k++)if(species[j]==s->expected[k]){matches++;break;}
            if(matches<3){s->sound=0x26;s->reset_pending=1;}else{s->sound=0x4a1;s->result=0;}}
        else s->sound=0x3c6;}return;}
    if(buttons&0x20){if(!s->count){s->result=1;return;}s->sound=0x3c7;unsigned p=s->choices[--s->count];s->occupied[p]=0;s->clips[p]=0;s->clips[11]=s->count;return;}
    unsigned x=s->x,y=s->y;if((repeat&1)&&y>0)y--;if((repeat&2)&&y<2)y++;if((repeat&4)&&x>0)x--;if((repeat&8)&&x<2)x++;
    s->x=x;s->y=y;s->clips[9]=cursor[y*3+x];
}
