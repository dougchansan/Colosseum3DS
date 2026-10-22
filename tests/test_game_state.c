#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/game_state.h"
#include "colosseum3ds/save.h"
#include "../src/save_codec.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* This fixture is deliberately synthetic and never installed as game data.
 * Supply a privately prepared pack as argv[1] for the retail-data checks. */
#define PACK_SIZE 176722u
#define BODY 128u
#define PREFIX 122960u
#define STARTERS 147600u
#define NATURES 147728u
#define FLAGS 147978u
#define PARTY 0xa0u
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)

static unsigned r16(const unsigned char *p) { return p[0]*256u+p[1]; }
static unsigned r32(const unsigned char *p)
{ return (unsigned)p[0]<<24|(unsigned)p[1]<<16|(unsigned)p[2]<<8|p[3]; }
static void w16(unsigned char *p,unsigned v) { p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v; }
static void w32(unsigned char *p,unsigned v)
{ p[0]=(unsigned char)(v>>24);p[1]=(unsigned char)(v>>16);p[2]=(unsigned char)(v>>8);p[3]=(unsigned char)v; }

static void write_file(const char *path,const void *data,size_t size)
{
    FILE *f=fopen(path,"wb"); CHECK(f!=NULL);
    CHECK(fwrite(data,1,size,f)==size); CHECK(fclose(f)==0);
}

static void sign_pack(unsigned char *p)
{ c3_save_codec_sha1(p+64,p+128,PACK_SIZE-128); }

static unsigned char *synthetic_pack(void)
{
    static const unsigned fields[]={1,PACK_SIZE,BODY,C3_SAVE_BODY_SIZE,PREFIX,0x6040,
                                   STARTERS,2,64,NATURES,25,10,FLAGS,3593,8};
    static const unsigned char pin[]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,
        0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    unsigned char *p=calloc(1,PACK_SIZE+1);
    unsigned i,j,bit=0;
    CHECK(p!=NULL); memcpy(p,"C3GS",4); memcpy(p+84,pin,20); w32(p+104,1);
    for (i=0;i<15;++i) w32(p+4+i*4,fields[i]);
    memcpy(p+BODY,"\x0b\x03\x02\x02",4);
    w32(p+BODY+0x1c,2); w32(p+BODY+0x24,1); p[BODY+0x28]=1;
    w32(p+BODY+0xaf4,10000);
    memcpy(p+PREFIX,"GC6E01",6);
    memcpy(p+PREFIX+8,"pokemon_colosseum",18); w16(p+PREFIX+0x38,48);
    for (i=0;i<2;++i) {
        unsigned char *q=p+STARTERS+i*64;
        w16(q,i?196:197); q[2]=(unsigned char)(i?25:26);q[3]=31;
        w32(q+4,i?0x3ff4:0x4a6f);w16(q+8,220);
        for (j=0;j<6;++j) w16(q+10+j*2,100);
        for (j=0;j<4;++j) { w16(q+22+j*3,1+j);q[24+j*3]=10; }
        w16(q+34,'S'); q[56]=1;
    }
    for (i=0;i<25*5;++i) p[NATURES+i*2]=p[NATURES+i*2+1]=100;
    for (i=0;i<3593;++i) {
        unsigned width=(i==31)?32:1;
        p[FLAGS+i*8]=(unsigned char)(0xc0|width);
        w16(p+FLAGS+i*8+4,bit); bit+=width;
    }
    sign_pack(p);return p;
}

static unsigned char *read_file(const char *path,size_t *length)
{
    FILE *f=fopen(path,"rb"); long size; unsigned char *p;
    CHECK(f!=NULL); CHECK(fseek(f,0,SEEK_END)==0);size=ftell(f);CHECK(size>=0);
    CHECK(fseek(f,0,SEEK_SET)==0);p=malloc((size_t)size+1);CHECK(p!=NULL);
    CHECK(fread(p,1,(size_t)size,f)==(size_t)size); CHECK(fclose(f)==0);
    *length=(size_t)size;return p;
}

static void retail_checks(const char *pack_path,const char *directory)
{
    static const unsigned iv[2][6]={{3,15,18,28,25,28},{30,21,19,5,25,21}};
    static const unsigned stats[2][6]={{86,37,66,47,79,46},{75,42,39,71,52,71}};
    static const unsigned pid[2]={0x3f5932e4,0x0990627b};
    static const unsigned items[]={0x16,0x0d,0x0e,0x0f,0x10,0x12,0x11,0x17};
    C3GameState state={0},reload={0};C3GameSummary summary;
    char error[C3_GAME_ERROR_SIZE],path[1024]; const unsigned char *body;
    unsigned i,j,value;
    CHECK(c3_game_state_new(&state,NULL,0x12345678,pack_path,error));
    CHECK(c3_game_state_summary(&state,&summary));
    CHECK(!strcmp(summary.name,"WES"));CHECK(summary.trainer_id==0x1c465252);
    CHECK(summary.money==10000 && summary.saved_count==0 && summary.party_count==2);
    CHECK(summary.floor==2 && summary.previous_floor==1 && summary.spawn==1);
    body=c3_game_state_body(&state,NULL);
    for (i=0;i<2;++i) {
        const unsigned char *p=body+PARTY+i*0x138;
        CHECK(r32(p+4)==pid[i]); CHECK(r32(p+0x14)==summary.trainer_id);
        CHECK(!memcmp(p+0x18,body+0x70,22)); CHECK(r16(p+0x0c)==0xfe && p[0xf]==4);
        CHECK(r16(p+0x8a)==stats[i][0]); CHECK(r16(p+0xb0)==220);
        CHECK(r32(p+0xdc)==0xffffff9c && p[0xd0]==255);
        for (j=0;j<6;++j) { CHECK(r16(p+0xa4+j*2)==iv[i][j]);CHECK(r16(p+0x8c+j*2)==stats[i][j]); }
    }
    CHECK(r16(body+0x82a8)==2 && r16(body+0x82ac)==0x80c5 && r16(body+0x82b8)==0x80c4);
    CHECK(r32(body+0x82a8+36)==0xb7955b96);
    for (i=0;i<8;++i) { CHECK(r16(body+0x7f0+i*4)==items[i]);CHECK(r16(body+0x7f2+i*4)==(i==1?5u:2u)); }
    CHECK(!memcmp(body+0xb32,"\0N\0/\0A\0\0",8));
    for (i=0;i<3;++i) {
        const unsigned char *box=body+0xb88+i*0x24a4;
        CHECK(r16(box)=='B' && r16(box+2)=='O' && r16(box+4)=='X' && r16(box+6)=='1'+i);
        CHECK(r32(box+0x14+0xdc)==0xffffff9c && box[0x14+0xd0]==255);
    }
    CHECK(r16(body+0x7d20)==255 && r16(body+0x7d20+0x3fe)==255 && r16(body+0x8120)==0);
    CHECK(c3_game_state_get_flag(&state,0x8d0,&value) && value==0);
    CHECK(c3_game_state_set_flag(&state,0x8d0,1));
    CHECK(r32(c3_game_state_body(&state,NULL)+0x1be5c+(4439u/32u)*4u)==(1u<<(4439u%32u)));
    /* floorLink(1,0) leaves opening floor0x3a0 as runtime previous. The
     * explicit native checkpoint opts to persist that entry information. */
    CHECK(c3_game_state_set_floor(&state,1,0x3a0,0));
    snprintf(path,sizeof path,"%s/retail-initialized.gci",directory);
    CHECK(c3_game_state_save(&state,path,error));
    CHECK(c3_game_state_load(&reload,path,pack_path,error));
    CHECK(c3_game_state_summary(&reload,&summary) && summary.saved_count==1);
    CHECK(summary.floor==1 && summary.previous_floor==0x3a0 && summary.spawn==0);
    CHECK(c3_game_state_get_flag(&reload,0x8d0,&value) && value==1);
    CHECK(!memcmp(c3_game_state_body(&state,NULL),c3_game_state_body(&reload,NULL),C3_SAVE_BODY_SIZE));
    c3_game_state_free(&state);c3_game_state_free(&reload);CHECK(unlink(path)==0);
}

int main(int argc,char **argv)
{
    C3GameState state={0},loaded={0};C3GameSummary summary;
    unsigned char *pack=synthetic_pack(),*altered=malloc(PACK_SIZE+1),*image,*bodycopy;
    char directory[]="/tmp/c3-game-state-XXXXXX",packpath[1024],savepath[1024],extra[1024];
    char error[C3_GAME_ERROR_SIZE];const unsigned char *before;size_t length;unsigned value,i;
    CHECK(altered!=NULL);CHECK(mkdtemp(directory)!=NULL);
    snprintf(packpath,sizeof packpath,"%s/synthetic.c3g",directory);
    snprintf(savepath,sizeof savepath,"%s/synthetic.gci",directory);
    write_file(packpath,pack,PACK_SIZE);
    CHECK(c3_game_state_new(&state,NULL,0x12345678,packpath,error));
    CHECK(c3_game_state_summary(&state,&summary) && !strcmp(summary.name,"WES"));
    CHECK(summary.trainer_id==0x1c465252 && summary.saved_count==0);
    before=c3_game_state_body(&state,&length);CHECK(length==C3_SAVE_BODY_SIZE);
    CHECK(!c3_game_state_new(&state,"TOOLONG8",1,packpath,error));CHECK(c3_game_state_body(&state,NULL)==before);
    CHECK(!c3_game_state_new(&state,"   ",1,packpath,error));
    CHECK(!c3_game_state_new(&state,"A\n",1,packpath,error));
    CHECK(!c3_game_state_new(&state,"WES",1,NULL,error));
    CHECK(!c3_game_state_get_flag(&state,3593,&value));
    CHECK(!c3_game_state_set_flag(&state,3593,1));
    CHECK(c3_game_state_set_flag(&state,31,0x89abcdef));
    CHECK(c3_game_state_get_flag(&state,31,&value) && value==0x89abcdef);
    CHECK(c3_game_state_get_flag(&state,30,&value) && value==0);
    CHECK(c3_game_state_get_flag(&state,32,&value) && value==0);
    CHECK(c3_game_state_set_flag(&state,30,3));
    CHECK(c3_game_state_get_flag(&state,30,&value) && value==1);
    CHECK(c3_game_state_set_options(&state,1,1));
    CHECK(!c3_game_state_set_options(&state,2,1));
    CHECK(c3_game_state_set_floor(&state,1,2,0));
    CHECK(!c3_game_state_set_floor(&state,0,2,0));
    CHECK(!c3_game_state_add_play_time(&state,NAN));
    CHECK(!c3_game_state_add_play_time(&state,-1));
    CHECK(c3_game_state_add_play_time(&state,3661.25));
    CHECK(c3_game_state_summary(&state,&summary) && summary.play_seconds==3661);
    for (i=0;i<15;++i) {
        memcpy(altered,pack,PACK_SIZE);altered[4+i*4+3]^=1;write_file(packpath,altered,PACK_SIZE);
        CHECK(!c3_game_state_new(&state,NULL,1,packpath,error));CHECK(c3_game_state_body(&state,NULL)==before);
    }
    memcpy(altered,pack,PACK_SIZE);altered[BODY+100]^=1;write_file(packpath,altered,PACK_SIZE);
    CHECK(!c3_game_state_new(&state,NULL,1,packpath,error));
    memcpy(altered,pack,PACK_SIZE);altered[NATURES+1]=0;sign_pack(altered);write_file(packpath,altered,PACK_SIZE);
    CHECK(!c3_game_state_new(&state,NULL,1,packpath,error));
    memcpy(altered,pack,PACK_SIZE);w16(altered+FLAGS+4,65535);sign_pack(altered);write_file(packpath,altered,PACK_SIZE);
    CHECK(!c3_game_state_new(&state,NULL,1,packpath,error));
    write_file(packpath,pack,PACK_SIZE-1);CHECK(!c3_game_state_new(&state,NULL,1,packpath,error));
    write_file(packpath,pack,PACK_SIZE+1);CHECK(!c3_game_state_new(&state,NULL,1,packpath,error));
    write_file(packpath,pack,PACK_SIZE);
    CHECK(c3_game_state_save(&state,savepath,error));
    CHECK(c3_game_state_summary(&state,&summary) && summary.saved_count==1);
    CHECK(c3_game_state_load(&loaded,savepath,packpath,error));
    CHECK(c3_game_state_summary(&loaded,&summary) && summary.saved_count==1 && summary.sound_mono==1);
    CHECK(c3_game_state_get_flag(&loaded,31,&value) && value==0x89abcdef);
    CHECK(!memcmp(c3_game_state_body(&state,NULL),c3_game_state_body(&loaded,NULL),C3_SAVE_BODY_SIZE));

    /* Alter an opaque reserved byte in a synthetic valid card, then ensure a
     * typed mutation and disk save retain it exactly. It is not game progress. */
    image=read_file(savepath,&length);bodycopy=malloc(C3_SAVE_BODY_SIZE);CHECK(bodycopy!=NULL);
    memcpy(bodycopy,c3_game_state_body(&loaded,NULL),C3_SAVE_BODY_SIZE);bodycopy[0x1ded0]=0xa5;
    { unsigned char padding[20]={0};CHECK(c3_save_update(image,length,bodycopy,C3_SAVE_BODY_SIZE,padding,NULL)==C3_SAVE_OK); }
    CHECK(c3_game_state_decode(&loaded,image,length,NULL,error));
    CHECK(!c3_game_state_get_flag(&loaded,31,&value));
    CHECK(c3_game_state_set_floor(&loaded,2,1,1));
    CHECK(c3_game_state_save(&loaded,savepath,error));
    CHECK(c3_game_state_body(&loaded,NULL)[0x1ded0]==0xa5);
    CHECK(c3_game_state_load(&state,savepath,NULL,error));
    CHECK(c3_game_state_body(&state,NULL)[0x1ded0]==0xa5);
    before=c3_game_state_body(&state,NULL);
    image[0]^=1;CHECK(!c3_game_state_decode(&state,image,length,NULL,error));CHECK(c3_game_state_body(&state,NULL)==before);
    snprintf(extra,sizeof extra,"%s/synthetic.gci.tmp",directory);write_file(extra,"pending",7);
    CHECK(!c3_game_state_save(&state,savepath,error));
    CHECK(c3_game_state_body(&state,NULL)==before);
    CHECK(unlink(extra)==0);
    CHECK(c3_game_state_new(&state,"CUSTOM7",0,packpath,error));
    CHECK(c3_game_state_summary(&state,&summary) && !strcmp(summary.name,"CUSTOM7") && summary.saved_count==0);
    CHECK(c3_game_state_save(&state,savepath,error));
    { C3SaveInfo info;free(image);image=read_file(savepath,&length);
      CHECK(c3_save_inspect(image,length,&info)==C3_SAVE_OK && info.selected_count==4); }
    c3_game_state_free(&state);c3_game_state_free(&loaded);
    CHECK(!c3_game_state_summary(&state,&summary));
    CHECK(c3_game_state_body(&state,&length)==NULL && length==0);
    free(image);free(bodycopy);free(pack);free(altered);
    CHECK(unlink(savepath)==0);snprintf(extra,sizeof extra,"%s/synthetic.gci.bak",directory);CHECK(unlink(extra)==0);
    CHECK(unlink(packpath)==0);
    if (argc>1) retail_checks(argv[1],directory);
    CHECK(rmdir(directory)==0);
    printf("game-state checks: %u passed%s\n",checks,argc>1?" (including private retail data)":" (synthetic fixtures only)");
    return 0;
}
