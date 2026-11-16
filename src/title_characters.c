#include "colosseum3ds/title_characters.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HEADER=32, TEXTURES=HEADER, ITEMS=TEXTURES+17*16, PAYLOAD=ITEMS+19*32 };
static uint32_t u32(const uint8_t *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }

int c3_title_characters_open(C3TitleCharacters *pack,const void *memory,size_t size)
{
    const uint8_t *d=memory;
    unsigned pages[4]={0};
    const unsigned expected[4]={4,6,5,4};
    size_t next=PAYLOAD;
    if(!pack||!d||size<PAYLOAD||size>C3_TITLE_CHARACTER_MAX_BYTES||memcmp(d,"C3TC",4)||
       u32(d+4)!=1||u32(d+8)!=size||u32(d+12)!=17||u32(d+16)!=19||
       u32(d+20)!=4||u32(d+24)||u32(d+28))return 0;
    for(unsigned i=0;i<17;++i){
        const uint8_t *e=d+TEXTURES+i*16;
        uint32_t w=u32(e),h=u32(e+4),offset=u32(e+8),bytes=u32(e+12);
        if(!w||w>512||!h||h>512||bytes!=w*h*4u||offset!=next||
           offset>size||bytes>size-offset)return 0;
        next+=bytes;
    }
    if(next!=size)return 0;
    for(unsigned i=0;i<19;++i){
        const uint8_t *e=d+ITEMS+i*32;
        uint32_t texture=u32(e),page=u32(e+4),column=u32(e+8);
        uint32_t x=u32(e+12),y=u32(e+16),w=u32(e+20),h=u32(e+24);
        if(texture>=17||page>=4||column>=6||x>640||y>480||w>640-x||h>480-y||u32(e+28))return 0;
        const uint8_t *t=d+TEXTURES+texture*16;
        if(w!=u32(t)||h!=u32(t+4))return 0;
        ++pages[page];
    }
    for(unsigned i=0;i<4;++i)if(pages[i]!=expected[i])return 0;
    pack->data=d;pack->size=size;pack->owned=NULL;return 1;
}

int c3_title_characters_load(C3TitleCharacters *pack,const char *path)
{
    FILE *f;
    long length;
    uint8_t *data;
    C3TitleCharacters checked={0};
    if(!pack||!path||(f=fopen(path,"rb"))==NULL)return 0;
    if(fseek(f,0,SEEK_END)||(length=ftell(f))<PAYLOAD||(unsigned long)length>C3_TITLE_CHARACTER_MAX_BYTES||fseek(f,0,SEEK_SET)){
        fclose(f);return 0;
    }
    data=malloc((size_t)length);
    if(!data){fclose(f);return 0;}
    size_t got=fread(data,1,(size_t)length,f);
    int failed=ferror(f);
    fclose(f);
    if(failed||got!=(size_t)length||!c3_title_characters_open(&checked,data,(size_t)length)){
        free(data);return 0;
    }
    checked.owned=data;*pack=checked;return 1;
}

void c3_title_characters_close(C3TitleCharacters *pack)
{ if(pack){free(pack->owned);memset(pack,0,sizeof *pack);} }

int c3_title_characters_texture(const C3TitleCharacters *pack,unsigned index,C3TitleCharacterTexture *out)
{
    if(!pack||!pack->data||!out||index>=17)return 0;
    const uint8_t *e=pack->data+TEXTURES+index*16;
    out->width=u32(e);out->height=u32(e+4);out->pixels=pack->data+u32(e+8);return 1;
}
int c3_title_characters_item(const C3TitleCharacters *pack,unsigned index,C3TitleCharacterItem *out)
{
    if(!pack||!pack->data||!out||index>=19)return 0;
    const uint8_t *e=pack->data+ITEMS+index*32;
    out->texture=u32(e);out->page=u32(e+4);out->column=u32(e+8);
    out->x=(int)u32(e+12);out->y=(int)u32(e+16);out->w=(int)u32(e+20);out->h=(int)u32(e+24);return 1;
}
void c3_title_characters_enter(C3TitleCharacterState *state)
{
    if(!state)return;
    state->page=(state->page%4+1)%4;
    for(unsigned i=0;i<6;++i)state->offsets[i]=(i<3?1.0f:-1.0f)*(300.0f+100.0f*(i%3));
}
void c3_title_characters_tick(C3TitleCharacterState *state,float delta)
{
    if(!state||!isfinite(delta)||delta<=0)return;
    for(unsigned i=0;i<6;++i){
        float move=-4.0f*state->offsets[i]*delta;
        if(move>6)move=6;
        if(move<-6)move=-6;
        state->offsets[i]+=move;
        if(fabsf(state->offsets[i])<=fabsf(move)||fabsf(state->offsets[i])<1)state->offsets[i]=0;
    }
}
void c3_title_characters_rect(const C3TitleCharacterItem *item,const C3TitleCharacterState *state,
                              float *x,float *y,float *w,float *h)
{
    if(!item||!state||item->column>=6||!x||!y||!w||!h)return;
    *x=(item->column>=3?item->x*0.5f:400.0f-(640-item->x)*0.5f)+state->offsets[item->column]*0.5f;
    *y=item->y*0.5f;*w=item->w*0.5f;*h=item->h*0.5f;
}
