#include "colosseum3ds/wzx_confusion_sequence.h"
#include "save_codec.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void checksum(uint8_t *b,size_t n){memset(b+72,0,20);c3_save_codec_sha1(b+72,b,(uint32_t)n);}
int main(int argc,char **argv)
{
 if(argc!=2){puts("Optional private source fixture: supply confusion_sequence.c3s");return 0;}
 C3WzxConfusionSequenceProgram p={0};char e[192];assert(c3_wzx_confusion_sequence_load(&p,argv[1],e,sizeof e));assert(p.length==13048);
 uint8_t ids[15];assert(c3_wzx_confusion_sequence_crossed(&p,1,0,0,1,ids)==4);assert(p.nodes[ids[0]].resource[0]==2&&p.nodes[ids[3]].resource[0]==0x472);
 assert(c3_wzx_confusion_sequence_crossed(&p,1,0,12000,0,ids)==2);assert(p.nodes[ids[0]].resource[0]==3&&p.nodes[ids[1]].resource[0]==0xE9);
 assert(c3_wzx_confusion_sequence_crossed(&p,1,12000,44000,0,ids)==4);assert(p.nodes[ids[0]].resource[0]==7&&p.nodes[ids[1]].resource[0]==4&&p.nodes[ids[2]].resource[0]==8&&p.nodes[ids[3]].resource[0]==0x473);
 assert(c3_wzx_confusion_sequence_crossed(&p,1,44000,44000,0,ids)==0);
 const float root[3]={4,2,-3};float idle[3],motion[3],rotated[3];
 for(unsigned species=196;species<=288;species+=92){unsigned entry=species==196?1:8;
  assert(c3_wzx_confusion_sequence_position(&p,species,0,.5f,root,0,1,idle,e,sizeof e));
  assert(c3_wzx_confusion_sequence_position(&p,species,entry,.5f,root,0,1,motion,e,sizeof e));
  assert(fabsf(idle[0]-motion[0])+fabsf(idle[1]-motion[1])+fabsf(idle[2]-motion[2])>.0001f);
  assert(c3_wzx_confusion_sequence_position(&p,species,entry,.5f,root,1.57079632679f,.75f,rotated,e,sizeof e));
  assert(fabsf(rotated[0]-(root[0]+.75f*(motion[2]-root[2])))<.0001f);
  assert(fabsf(rotated[2]-(root[2]-.75f*(motion[0]-root[0])))<.0001f);
 }
 float sentinel[3]={7,8,9};assert(!c3_wzx_confusion_sequence_position(&p,196,2,0,root,0,1,sentinel,e,sizeof e)&&sentinel[0]==7);
 uint8_t *bad=malloc(p.length);assert(bad);memcpy(bad,p.bytes,p.length);bad[448+128*13+13*4]=9;checksum(bad,p.length);
 const void *original=p.bytes;assert(!c3_wzx_confusion_sequence_decode(&p,bad,p.length,e,sizeof e)&&p.bytes==original);
 memcpy(bad,p.bytes,p.length);memset(bad+p.tracks[0].offset,0xFF,4);checksum(bad,p.length);assert(!c3_wzx_confusion_sequence_decode(&p,bad,p.length,e,sizeof e)&&p.bytes==original);
 free(bad);c3_wzx_confusion_sequence_close(&p);assert(!p.bytes);puts("Confusion full source-entry/track contract passed; native GPU/audio hardware acceptance remains external");return 0;
}
