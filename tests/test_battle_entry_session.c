/* Trainer-entry session over the private C3E1 pack and a fresh game state.
 * Without C3_ENTRY_PACK/C3_ENTRY_NEWGAME only pack rejection is exercised.
 * Scene services here are test doubles that record calls; they are not the
 * console scene adapter. */
#include "colosseum3ds/battle_entry_pack.h"
#include "colosseum3ds/game_state.h"
#include "colosseum3ds/save.h"
#include "save_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: CHECK(%s)\n",__FILE__,__LINE__,#x);exit(1);}}while(0)

typedef struct {
    unsigned requests, finds, allocs, loads, hides, releases, grids, updates, shown, aborted, fields;
    uint32_t next_sequence, loaded_group[4], loaded_resource[4], grid_xy[4];
} Scene;
static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int32_t state(void *u,uint32_t g){(void)u;(void)g;return 0;}
static int request(void *u,uint32_t g){(void)g;++((Scene *)u)->requests;return 1;}
static uint32_t find(void *u,uint32_t g,uint32_t m){++((Scene *)u)->finds;return g&&m?(g<<8)^m:0;}
static uint32_t allocate(void *u){Scene *s=u;++s->allocs;return ++s->next_sequence;}
static int fields(void *u,uint32_t q,uint16_t i,uint16_t g,uint8_t f){(void)q;(void)i;(void)g;CHECK(!f);++((Scene *)u)->fields;return 1;}
static int load(void *u,uint32_t q,uint32_t r){Scene *s=u;(void)q;CHECK(s->loads<4);s->loaded_resource[s->loads++]=r;return 1;}
static void release(void *u,uint32_t q){(void)q;++((Scene *)u)->releases;}
static int hide(void *u,uint32_t q){(void)q;++((Scene *)u)->hides;return 1;}
static void report(void *u,uint32_t g){(void)u;(void)g;CHECK(0);}
static int grid_add(void *u,uint32_t q,uint8_t x,uint8_t y){Scene *s=u;(void)q;CHECK(s->grids<4);s->grid_xy[s->grids++]=(uint32_t)x<<8|y;return 1;}
static int grid_update(void *u){++((Scene *)u)->updates;return 1;}
static int visible(void *u,uint32_t q,int v){(void)q;CHECK(v==1);++((Scene *)u)->shown;return 1;}
static void abort_owned(void *u,const C3BattleEntryOwnership *o,size_t n){(void)o;((Scene *)u)->aborted+=(unsigned)n;}

static void rejection(void)
{
    C3BattleEntryPack pack;char error[C3_BATTLE_ERROR_SIZE]={0};uint8_t bytes[512]={0};
    CHECK(!c3_battle_entry_pack_decode(&pack,bytes,sizeof bytes,error)&&error[0]);
    memcpy(bytes,"C3E1",4);bytes[7]=1;bytes[10]=2;bytes[15]=C3_PACK_SECTION_COUNT;
    CHECK(!c3_battle_entry_pack_decode(&pack,bytes,sizeof bytes,error));
    CHECK(!c3_battle_entry_pack_decode(NULL,bytes,sizeof bytes,error));
}
static uint8_t *read_file(const char *path,size_t *size)
{
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
    uint8_t *data=malloc((size_t)n);CHECK(data);CHECK(fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return data;
}
static void print_fighter(const C3BattleEntrySession *s,unsigned side,unsigned slot)
{
    const uint8_t *f=c3_battle_entry_session_fighter(s,side,slot);
    const uint8_t *p=f+4; /* FightPokemon: source party ref, then a PokemonBios copy */
    printf("entry_fighter side=%u slot=%u source=%08lx species=%u level=%u hp=%u/%u personality=%08lx entry_id=%d\n",
        side,slot,(unsigned long)be32(f),be16(p),p[0x60],be16(p+0x8a),be16(p+0x8c),(unsigned long)be32(p+4),
        (int)(int16_t)be16(f+0x14c));
}
static void real_entry(const char *pack_path,const char *newgame)
{
    size_t pack_size;uint8_t *bytes=read_file(pack_path,&pack_size);
    C3BattleEntryPack pack;char error[C3_BATTLE_ERROR_SIZE]={0};
    CHECK(c3_battle_entry_pack_decode(&pack,bytes,pack_size,error));
    C3GameState game={0};char game_error[C3_GAME_ERROR_SIZE]={0};
    if(!c3_game_state_new(&game,"WES",0x12345678u,newgame,game_error)){fprintf(stderr,"%s\n",game_error);exit(1);}
    size_t body_size=0;const uint8_t *body=c3_game_state_body(&game,&body_size);CHECK(body);
    uint8_t before[20],after[20];c3_save_codec_sha1(before,body,(uint32_t)body_size);

    C3BattleEntrySessionConfig config;Scene scene={0};
    CHECK(c3_battle_entry_pack_config(&pack,10,body,body_size,0x12345678u,&config,error));
    CHECK(config.requests[0].trainer_id==1&&config.requests[0].input_device==1);
    CHECK(config.requests[1].trainer_id==10&&config.requests[1].input_device==0);
    CHECK(config.side_data_id[0]==2&&config.side_data_id[1]==1&&config.preset_mode==0);
    config.sequence=(C3BattleSequenceServices){state,request,find,allocate,fields,load,release,hide,report};
    config.grid_add=grid_add;config.grid_update=grid_update;config.set_visible=visible;
    config.abort_owned=abort_owned;config.scene=&scene;
    C3BattleEntrySession *s=calloc(1,sizeof *s);CHECK(s);
    CHECK(c3_battle_entry_session_begin(s,&config));
    int r;unsigned guard=0;
    while((r=c3_battle_entry_session_step(s))==1)CHECK(++guard<64);
    if(r<0){fprintf(stderr,"entry failed at %s\n",s->failure?s->failure:"?");exit(1);}
    printf("entry_session result=%lu,%lu steps=%lu waits=%lu rng_seed=%08lx rng_draws=%llu entry_counter=%u\n",
        (unsigned long)s->source_result[0],(unsigned long)s->source_result[1],(unsigned long)s->steps,
        (unsigned long)s->waits,(unsigned long)s->rng.seed,(unsigned long long)s->rng.draws,s->entry_counter);
    printf("entry_scene loads=%u resources=%08lx,%08lx grids=%04lx,%04lx shown=%u hides=%u\n",scene.loads,
        (unsigned long)scene.loaded_resource[0],(unsigned long)scene.loaded_resource[1],
        (unsigned long)scene.grid_xy[0],(unsigned long)scene.grid_xy[1],scene.shown,scene.hides);
    for(unsigned side=0;side<2;++side){const uint8_t *rt=c3_battle_entry_session_runtime(s,side);
        printf("entry_trainer side=%u id=%u device=%u sequence=%08lx\n",side,be16(rt),be16(rt+2),(unsigned long)be32(rt+0x27c0));
        for(unsigned j=0;j<6;++j)if(be32(c3_battle_entry_session_fighter(s,side,j)))print_fighter(s,side,j);}
    CHECK(s->source_result[0]==1&&s->source_result[1]==1);
    CHECK(be16(c3_battle_entry_session_runtime(s,0))==1&&be16(c3_battle_entry_session_runtime(s,1))==10);
    /* Wes = loader row 1, Willie = loader row 38 (opponent flows first). */
    CHECK(scene.loads==2&&scene.shown==2&&scene.hides==2&&scene.grids==2&&!scene.aborted);
    CHECK(scene.loaded_resource[0]==((0x2dbu<<8)^0x058a1e00u));
    CHECK(scene.loaded_resource[1]==((0x9du<<8)^0x030c1e00u));
    for(unsigned j=0;j<2;++j){const uint8_t *f=c3_battle_entry_session_fighter(s,1,j);CHECK(be16(f+4)==288);}
    c3_save_codec_sha1(after,c3_game_state_body(&game,&body_size),(uint32_t)body_size);
    CHECK(!memcmp(before,after,20)); /* the live state is never written */
    free(s);c3_game_state_free(&game);free(bytes);
}
int main(void)
{
    rejection();
    const char *pack=getenv("C3_ENTRY_PACK"),*newgame=getenv("C3_ENTRY_NEWGAME");
    if(!pack||!newgame){puts("entry_session private inputs absent; rejection checks only");return 0;}
    real_entry(pack,newgame);
    puts("entry_session OK");
    return 0;
}
