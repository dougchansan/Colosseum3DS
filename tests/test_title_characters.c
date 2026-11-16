#include "colosseum3ds/title_characters.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { ITEMS=32+17*16, PAYLOAD=ITEMS+19*32, SIZE=PAYLOAD+17*4 };
static void put32(unsigned char *p,unsigned v)
{ for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(8*i)); }
static unsigned char *fixture(void)
{
    unsigned char *d=calloc(1,SIZE);
    assert(d);memcpy(d,"C3TC",4);
    put32(d+4,1);put32(d+8,SIZE);put32(d+12,17);put32(d+16,19);put32(d+20,4);
    for(unsigned i=0;i<17;++i){
        unsigned char *t=d+32+i*16;
        put32(t,1);put32(t+4,1);put32(t+8,PAYLOAD+i*4);put32(t+12,4);
        d[PAYLOAD+i*4]=(unsigned char)i;
    }
    for(unsigned i=0;i<19;++i){
        unsigned char *e=d+ITEMS+i*32;
        put32(e,i%17);put32(e+4,i<4?0:i<10?1:i<15?2:3);put32(e+8,i%6);
        put32(e+12,34);put32(e+16,249);put32(e+20,1);put32(e+24,1);
    }
    return d;
}
static void rejected(unsigned char *d,unsigned at,unsigned value)
{
    C3TitleCharacters pack={0};
    unsigned char old[4];memcpy(old,d+at,4);put32(d+at,value);
    assert(!c3_title_characters_open(&pack,d,SIZE));assert(!pack.data);
    memcpy(d+at,old,4);
}
int main(void)
{
    unsigned char *d=fixture();
    C3TitleCharacters pack={0};
    C3TitleCharacterTexture texture;
    C3TitleCharacterItem item;
    assert(c3_title_characters_open(&pack,d,SIZE));assert(!pack.owned);
    assert(c3_title_characters_texture(&pack,16,&texture));
    assert(texture.width==1&&texture.height==1&&texture.pixels[0]==16);
    assert(c3_title_characters_item(&pack,18,&item)&&item.page==3&&item.texture==1);
    assert(!c3_title_characters_texture(&pack,17,&texture));
    assert(!c3_title_characters_item(&pack,19,&item));
    assert(!c3_title_characters_open(NULL,d,SIZE));
    assert(!c3_title_characters_open(&pack,NULL,SIZE));
    assert(!c3_title_characters_open(&pack,d,SIZE-1));
    assert(!c3_title_characters_open(&pack,d,C3_TITLE_CHARACTER_MAX_BYTES+1));
    rejected(d,0,0);rejected(d,4,2);rejected(d,8,SIZE+1);
    rejected(d,12,16);rejected(d,16,18);rejected(d,20,3);rejected(d,24,1);rejected(d,28,1);
    rejected(d,32,0);rejected(d,32,513);rejected(d,36,0);rejected(d,36,513);
    rejected(d,40,PAYLOAD+1);rejected(d,44,8);rejected(d,40,0xffffffffu);
    rejected(d,ITEMS,17);rejected(d,ITEMS+4,4);rejected(d,ITEMS+4,1);
    rejected(d,ITEMS+8,6);rejected(d,ITEMS+12,640);rejected(d,ITEMS+12,0xffffffffu);
    rejected(d,ITEMS+16,480);rejected(d,ITEMS+20,2);rejected(d,ITEMS+24,2);rejected(d,ITEMS+28,1);
    /* Failed replacement leaves the existing borrowed pack valid. */
    assert(pack.data==d);c3_title_characters_close(&pack);assert(!pack.data&&!pack.owned&&!pack.size);
    c3_title_characters_close(&pack);
    assert(!c3_title_characters_load(&pack,"does-not-exist-title-characters.c3tc"));
    const char *path="test_title_characters.tmp";
    FILE *f=fopen(path,"wb");assert(f);assert(fwrite(d,1,SIZE,f)==SIZE);assert(!fclose(f));
    assert(c3_title_characters_load(&pack,path));assert(pack.owned&&pack.data!=d);
    assert(c3_title_characters_texture(&pack,16,&texture)&&texture.pixels[0]==16);
    c3_title_characters_close(&pack);
    f=fopen(path,"wb");assert(f);assert(fwrite(d,1,20,f)==20);assert(!fclose(f));
    assert(!c3_title_characters_load(&pack,path));assert(!pack.data);
    assert(!remove(path));
    C3TitleCharacterState state={3,{0}};
    for(unsigned page=0;page<5;++page){
        c3_title_characters_enter(&state);assert(state.page==page%4);
        for(unsigned i=0;i<6;++i)assert(state.offsets[i]==(i<3?1:-1)*(float)(300+100*(i%3)));
    }
    c3_title_characters_tick(&state,1.0f/60.0f);
    assert(state.offsets[0]==294&&state.offsets[3]==-294);
    float before=state.offsets[0];
    c3_title_characters_tick(&state,-1);c3_title_characters_tick(&state,NAN);
    assert(state.offsets[0]==before);
    state.offsets[0]=10;c3_title_characters_tick(&state,1.0f/60.0f);
    assert(fabsf(state.offsets[0]-(10-2.0f/3.0f))<0.0001f);
    for(unsigned i=0;i<300;++i)c3_title_characters_tick(&state,1.0f/60.0f);
    for(unsigned i=0;i<6;++i)assert(state.offsets[i]==0);
    float x,y,w,h;
    item=(C3TitleCharacterItem){0,0,3,34,249,145,231};
    c3_title_characters_rect(&item,&state,&x,&y,&w,&h);
    assert(x==17&&y==124.5f&&w==72.5f&&h==115.5f);
    item.column=0;item.x=454;
    c3_title_characters_rect(&item,&state,&x,&y,&w,&h);assert(x==307);
    c3_title_characters_enter(&state);
    c3_title_characters_rect(&item,&state,&x,&y,&w,&h);assert(x==457);
    item.column=3;item.x=34;
    c3_title_characters_rect(&item,&state,&x,&y,&w,&h);assert(x==-133);
    free(d);puts("title characters tests passed");return 0;
}
