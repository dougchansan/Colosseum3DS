#include "pda_spinda.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char **argv){
    assert(argc==4);FILE *f=fopen(argv[1],"rb");assert(f);unsigned char source[34848];assert(fread(source,1,sizeof source,f)==sizeof source);fclose(f);
    unsigned char *pixels=malloc(65536),*expected=malloc(65536);assert(pixels&&expected);
    for(unsigned i=0;i<16384;++i){unsigned v=source[32+i*2]|(unsigned)source[33+i*2]<<8;pixels[i*4]=(v>>11)*8;pixels[i*4+1]=((v>>5)&63)*4;pixels[i*4+2]=(v&31)*8;pixels[i*4+3]=255;}
    unsigned char untouched[4]={1,2,3,4};C3TitleTexture tex[2]={{128,128,1,1,pixels,65536},{1,1,0,0,untouched,4}};
    C3Title model={0};model.file_data=pixels;model.file_bytes=65536;model.textures=tex;model.texture_count=2;char error[128];
    char bad[1024];assert(snprintf(bad,sizeof bad,"%s.bad",argv[2])<(int)sizeof bad);
    source[32]^=1;f=fopen(bad,"wb");assert(f);assert(fwrite(source,1,sizeof source,f)==sizeof source);fclose(f);
    unsigned char first=pixels[0];assert(!c3_pda_spinda_apply(&model,1,0,bad,error,sizeof error));assert(pixels[0]==first);assert(!remove(bad));
    model.borrowed_storage=1;assert(!c3_pda_spinda_apply(&model,1,0,argv[1],error,sizeof error));model.borrowed_storage=0;
    pixels[0]^=1;assert(!c3_pda_spinda_apply(&model,1,0,argv[1],error,sizeof error));pixels[0]^=1;
    assert(c3_pda_spinda_apply(&model,1,(uint32_t)strtoul(argv[3],NULL,16),argv[1],error,sizeof error));
    f=fopen(argv[2],"rb");assert(f);assert(fread(expected,1,65536,f)==65536);fclose(f);assert(!memcmp(pixels,expected,65536));assert(!memcmp(untouched,(unsigned char[]){1,2,3,4},4));
    free(expected);free(pixels);return 0;
}
