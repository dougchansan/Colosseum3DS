/* Optional private-pack dialogue audit; no game bytes are part of this test.
 * Usage: test_realgam_messages <boot-directory>. Every original local message
 * is decoded with explicit fixture values for the supported dynamic variables.
 * This establishes decoder coverage only, not bottom-screen rendering. */
#include "colosseum3ds/story.h"
#include <stdio.h>
#include <stdlib.h>

static unsigned be16(const unsigned char *p){return p[0]*256u+p[1];}
static unsigned be32(const unsigned char *p){return (unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3];}
static unsigned le32(const unsigned char *p){return (unsigned)p[3]<<24|(unsigned)p[2]<<16|(unsigned)p[1]<<8|p[0];}
static const char *value(void *context,uint8_t code)
{
    (void)context;
    switch(code){
    case 0x2b:return "WES";case 0x2c:return "RUI";case 0x59:return "SPEAKER";
    case 0x2d:return "ITEM";case 0x32:return "POKEMON";case 0x39:return "MOVE";
    case 0x2f:case 0x4b:return "123";
    default:return NULL;
    }
}
int main(int argc,char **argv)
{
    if(argc!=2){puts("realgam-message audit skipped: needs a private boot directory");return 0;}
    static const unsigned floors[]={0x66,0x67,0x68,0x69,0x6a,0x6b,0x6c,0x6d,0x6e,0x6f,0x70,0x71,0x72,0x73,0x74,0x75,0x76,0x7b,0x7c};
    unsigned total=0,failed=0;
    for(unsigned floor=0;floor<sizeof floors/sizeof floors[0];++floor){
        char path[1024],error[C3_STORY_ERROR],text[C3_STORY_MESSAGE];
        snprintf(path,sizeof path,"%s/f%03x.c3s",argv[1],floors[floor]);
        FILE *file=fopen(path,"rb");
        if(!file){fprintf(stderr,"missing %s\n",path);return 1;}
        if(fseek(file,0,SEEK_END)){fclose(file);return 1;}
        long length=ftell(file);
        if(length<64||length>1048576||fseek(file,0,SEEK_SET)){fclose(file);return 1;}
        unsigned char *data=malloc((size_t)length);
        if(!data||fread(data,1,(size_t)length,file)!=(size_t)length){free(data);fclose(file);return 1;}
        fclose(file);C3Story story={0};
        if(!c3_story_decode(&story,data,(size_t)length,error)){puts(error);free(data);return 1;}
        unsigned banks=le32(data+16),groups=le32(data+20),count=0,bad=0;
        /* The final two groups are shared system strings/names, audited by
         * the systems lane. Each preceding group belongs to this floor. */
        if(groups<2){c3_story_free(&story);free(data);return 1;}
        for(unsigned group=0;group<groups-2;++group){
            const unsigned char *g=data+le32(data+64+(banks+group)*16+4);
            for(unsigned i=0;i<be16(g+4);++i){
                unsigned id=be16(g)<<20|be32(g+16+i*8);
                if(!id)continue;
                ++count;
                if(!c3_story_message_with(&story,id,value,NULL,text,sizeof text,error)){
                    ++bad;printf("message_failure floor=%03x id=%05x error=%s\n",floors[floor],id,error);
                }
            }
        }
        printf("message_floor floor=%03x checked=%u failed=%u\n",floors[floor],count,bad);
        total+=count;failed+=bad;c3_story_free(&story);free(data);
    }
    printf("message_total checked=%u failed=%u\n",total,failed);
    return failed?1:0;
}
