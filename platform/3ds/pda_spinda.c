#include "pda_spinda.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BASE_PIXELS 16384u
#define SPOT_PIXELS 1024u
#define SOURCE_BYTES (32u+2u*(BASE_PIXELS+SPOT_PIXELS))
static uint32_t word(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t half(const uint8_t *p){return (uint16_t)((unsigned)p[0]|(unsigned)p[1]<<8);}
static uint32_t hash(const uint8_t *p,size_t n){uint32_t h=2166136261u;while(n--)h=(h^*p++)*16777619u;return h;}
static void rgba(uint16_t v,uint8_t *p){p[0]=(uint8_t)((v>>11)*8);p[1]=(uint8_t)(((v>>5)&63)*4);p[2]=(uint8_t)((v&31)*8);p[3]=255;}
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
int c3_pda_spinda_apply(C3Title *models,unsigned count,uint32_t pid,const char *path,char *e,size_t n)
{
    static const uint8_t anchors[8]={6,7,91,6,30,65,68,65};
    uint8_t *source=NULL,*base=NULL,*output=NULL;uint16_t *values=NULL;unsigned matches=0;int result=0;
    if(!models||!count||count>16||!path)return fail(e,n,"Invalid Spinda preview input.");
    FILE *f=fopen(path,"rb");if(!f)return fail(e,n,"Original Spinda source textures are missing.");
    source=malloc(SOURCE_BYTES);if(!source){fclose(f);return fail(e,n,"Spinda allocation failed.");}
    size_t got=fread(source,1,SOURCE_BYTES,f);int extra=fgetc(f),io=ferror(f);fclose(f);
    if(io||got!=SOURCE_BYTES||extra!=EOF||memcmp(source,"C3SP",4)||word(source+4)!=1||word(source+8)!=SOURCE_BYTES||
       word(source+12)!=1994825157u||word(source+16)!=2050583703u||word(source+20)!=1387699653u||memcmp(source+24,anchors,8)||
       hash(source+32,32768)!=1994825157u||hash(source+32800,2048)!=2050583703u){fail(e,n,"Invalid original Spinda source textures.");goto done;}
    base=malloc(BASE_PIXELS*4);output=malloc(BASE_PIXELS*4);values=malloc(BASE_PIXELS*sizeof *values);
    if(!base||!output||!values){fail(e,n,"Spinda allocation failed.");goto done;}
    for(unsigned i=0;i<BASE_PIXELS;++i)rgba(half(source+32+i*2),base+i*4);
    /* Verify every replacement's ownership before changing any texture. */
    for(unsigned m=0;m<count;++m){C3Title *model=models+m;
        if(model->texture_count>C3_TITLE_MAX_TEXTURES||(!model->textures&&model->texture_count)){fail(e,n,"Invalid Spinda model textures.");goto done;}
        for(unsigned t=0;t<model->texture_count;++t){C3TitleTexture *tex=model->textures+t;
            if(tex->width!=128||tex->height!=128||tex->size!=BASE_PIXELS*4||!tex->pixels)continue;
            if(hash(tex->pixels,tex->size)!=1387699653u||memcmp(tex->pixels,base,BASE_PIXELS*4))continue;
            uintptr_t p=(uintptr_t)tex->pixels,b=(uintptr_t)model->file_data;
            if(model->borrowed_storage||!b||p<b||model->file_bytes<BASE_PIXELS*4||p-b>model->file_bytes-BASE_PIXELS*4){fail(e,n,"Spinda texture storage is not owned.");goto done;}
            ++matches;
        }
    }
    if(!matches){fail(e,n,"Original Spinda model texture does not match.");goto done;}
    memcpy(output,base,BASE_PIXELS*4);
    /* Keep original unsigned RGB16 max, signed linear row bounds, and float32 shifts. */
    for(unsigned i=0;i<BASE_PIXELS;++i)values[i]=half(source+32+i*2);
    for(unsigned spot=0;spot<4;++spot){unsigned byte=(pid>>(spot*8))&255;
        int x=anchors[spot*2]+(int)(3.4f*((int)(byte&15)-8));
        int y=anchors[spot*2+1]+(int)(3.4f*((int)(byte>>4)-8));
        for(int row=0;row<32;++row){int start=x+(y+row)*128;
            if(start>=0&&start<(int)BASE_PIXELS)for(int col=0;col<32;++col){int i=start+col;
                uint16_t v=half(source+32800+(row*32+col)*2);
                if(i<(int)BASE_PIXELS&&v>values[i])values[i]=v;
            }
        }
    }
    for(unsigned i=0;i<BASE_PIXELS;++i)rgba(values[i],output+i*4);
    for(unsigned m=0;m<count;++m)for(unsigned t=0;t<models[m].texture_count;++t){C3TitleTexture *tex=models[m].textures+t;
        if(tex->width==128&&tex->height==128&&tex->size==BASE_PIXELS*4&&tex->pixels&&hash(tex->pixels,tex->size)==1387699653u&&!memcmp(tex->pixels,base,BASE_PIXELS*4))memcpy((uint8_t*)tex->pixels,output,BASE_PIXELS*4);
    }
    result=1;
done:free(values);free(output);free(base);free(source);return result;
}
