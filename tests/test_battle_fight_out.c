/* fightOutPokemonCreate over the arena. Synthetic records check the source
 * order and lookups; with C3_ENTRY_PACK/C3_ENTRY_NEWGAME the real trainer
 * entry's active FightPokemon become FightOutPokemon. */
#include "colosseum3ds/battle_entry_pack.h"
#include "colosseum3ds/battle_fight_out.h"
#include "colosseum3ds/battle_runtime_trainer.h"
#include "colosseum3ds/game_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: CHECK(%s)\n",__FILE__,__LINE__,#x);exit(1);}}while(0)

static uint32_t get(C3BattleArena *a,uint32_t obj,uint32_t sel,uint32_t idx)
{uint32_t v=0xDEADBEEF;CHECK(c3_battle_fight_status_get(a,obj,sel,idx,&v));return v;}

static void synthetic(void)
{
    static uint8_t bytes[0x2000],rows[3*C3_SPECIES_ROW_BYTES];
    C3BattleArena a={bytes,sizeof bytes};
    /* Species 0: types 17/17, abilities 28/0. Species 1: types 0/9, abilities 53/62. */
    rows[0x30]=17;rows[0x31]=17;rows[0x32]=28;rows[0x33]=0;
    uint8_t *r1=rows+C3_SPECIES_ROW_BYTES;r1[0x30]=0;r1[0x31]=9;r1[0x32]=53;r1[0x33]=62;
    C3BattleSpeciesTable t={rows,3};
    const uint32_t fo=C3_ARENA_BASE+0x100,fp=C3_ARENA_BASE+0x1000;
    static const struct{uint16_t species;uint8_t flag;uint16_t ability;uint8_t t0,t1;}cases[]={
        {0,1,28,17,17},  /* no second ability: flag ignored */
        {1,0,53,0,9},{1,1,62,0,9},{1,2,0,0,9},   /* flag beyond the pair reads 0 */
        {7,0,0,0,0}};    /* species beyond the table: NULL row */
    for(unsigned i=0;i<sizeof cases/sizeof *cases;++i){
        memset(bytes,0xAA,sizeof bytes);
        uint8_t *poke=bytes+0x1000+4;poke[0]=(uint8_t)(cases[i].species>>8);poke[1]=(uint8_t)cases[i].species;
        poke[0xCC]=cases[i].flag;
        CHECK(c3_battle_fight_out_pokemon_create(&a,fo,fp,0x55667788u,&t));
        CHECK(get(&a,fo,0xD5,0)==fp&&get(&a,fo,0xD6,0)==fp&&get(&a,fo,0xEE,0)==0x55667788u);
        CHECK(get(&a,fo,0xFF,0)==cases[i].t0&&get(&a,fo,0xFF,1)==cases[i].t1);
        CHECK(get(&a,fo,0x100,0)==cases[i].ability);
        /* fightOutPokemonInit effects: turn count 2, hit-move type 9, stat
         * stages 6, meet-enemy IDs and irekae target -1, flags 0. */
        CHECK(get(&a,fo,0xED,0)==2&&get(&a,fo,0xF4,0)==9);
        for(uint32_t s=0xE6;s<=0xEC;++s)CHECK(get(&a,fo,s,0)==6);
        for(uint32_t j=0;j<12;++j)CHECK(get(&a,fo,0xFD,j)==0xFFFFFFFFu);
        CHECK(get(&a,fo,0x121,0)==0xFFFFFFFFu&&get(&a,fo,0x106,0)==0&&get(&a,fo,0x11F,0)==0);
        CHECK(bytes[0x1000+4+0xCC]==cases[i].flag);                /* source record untouched */
    }
    /* Sequence 0 leaves 0xEE as the init value (0). */
    memset(bytes,0xAA,sizeof bytes);bytes[0x1004]=0;bytes[0x1005]=1;bytes[0x1004+0xCC]=0;
    CHECK(c3_battle_fight_out_pokemon_create(&a,fo,fp,0,&t)&&get(&a,fo,0xEE,0)==0);
    /* Source no-ops and refusals. */
    memset(bytes,0x5C,sizeof bytes);
    CHECK(c3_battle_fight_out_pokemon_create(&a,0,fp,1,&t)&&c3_battle_fight_out_pokemon_create(&a,fo,0,1,&t));
    CHECK(bytes[0x100]==0x5C);
    CHECK(!c3_battle_fight_out_pokemon_create(&a,C3_ARENA_BASE+sizeof bytes-0x100,fp,1,&t));
    CHECK(!c3_battle_fight_out_pokemon_create(&a,fo,fo+0x100,1,&t));          /* overlap */
    CHECK(bytes[0x100]==0x5C);
}

static int32_t sstate(void *u,uint32_t g){(void)u;(void)g;return 0;}
static int srequest(void *u,uint32_t g){(void)u;(void)g;return 1;}
static uint32_t sfind(void *u,uint32_t g,uint32_t m){(void)u;return g&&m?(g<<8)^m:0;}
static uint32_t salloc(void *u){return ++*(uint32_t *)u;}
static int sfields(void *u,uint32_t q,uint16_t i,uint16_t g,uint8_t f){(void)u;(void)q;(void)i;(void)g;return !f;}
static int sload(void *u,uint32_t q,uint32_t r){(void)u;(void)q;(void)r;return 1;}
static void srelease(void *u,uint32_t q){(void)u;(void)q;}
static int shide(void *u,uint32_t q){(void)u;(void)q;return 1;}
static void sreport(void *u,uint32_t g){(void)u;(void)g;CHECK(0);}
static int sgrid(void *u,uint32_t q,uint8_t x,uint8_t y){(void)u;(void)q;(void)x;(void)y;return 1;}
static int supdate(void *u){(void)u;return 1;}
static int svisible(void *u,uint32_t q,int v){(void)u;(void)q;return v==1;}
static void sabort(void *u,const C3BattleEntryOwnership *o,size_t n){(void)u;(void)o;(void)n;CHECK(0);}

static uint8_t *read_file(const char *path,size_t *n)
{
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long len=ftell(f);CHECK(len>0);rewind(f);
    uint8_t *d=malloc((size_t)len);CHECK(d&&fread(d,1,(size_t)len,f)==(size_t)len);fclose(f);*n=(size_t)len;return d;
}
static void original(const char *pack_path,const char *newgame)
{
    size_t n;uint8_t *bytes=read_file(pack_path,&n);char error[C3_BATTLE_ERROR_SIZE];char gerr[C3_GAME_ERROR_SIZE];
    C3BattleEntryPack *pack=malloc(sizeof *pack);CHECK(pack&&c3_battle_entry_pack_decode(pack,bytes,n,error));
    C3GameState game={0};CHECK(c3_game_state_new(&game,"WES",0x12345678u,newgame,gerr));
    size_t bn;const uint8_t *body=c3_game_state_body(&game,&bn);
    C3BattleEntrySessionConfig config;uint32_t seqs=0;
    CHECK(c3_battle_entry_pack_config(pack,10,body,bn,0x12345678u,&config,error));
    config.sequence=(C3BattleSequenceServices){sstate,srequest,sfind,salloc,sfields,sload,srelease,shide,sreport};
    config.grid_add=sgrid;config.grid_update=supdate;config.set_visible=svisible;config.abort_owned=sabort;config.scene=&seqs;
    C3BattleEntrySession *s=calloc(1,sizeof *s);CHECK(s&&c3_battle_entry_session_begin(s,&config));
    int r;unsigned guard=0;while((r=c3_battle_entry_session_step(s))==1)CHECK(++guard<64);CHECK(r==0);

    /* Arena over the two contiguous runtime trainer records. */
    C3BattleArena arena={&s->runtime[0][0],2u*C3_BATTLE_RUNTIME_TRAINER_BYTES};
    C3BattleSpeciesTable species={pack->sections[C3_PACK_SPC].bytes,pack->sections[C3_PACK_SPC].count};
    CHECK(species.count==413);
    static const struct{uint16_t species;uint8_t t0,t1;uint16_t ability;}want[2][2]={
        {{197,17,17,28},{196,14,14,28}},   /* Umbreon, Espeon: Synchronize */
        {{288,0,0,53},{288,0,0,53}}};      /* Zigzagoon: Pickup */
    for(unsigned side=0;side<2;++side)for(unsigned slot=0;slot<2;++slot){
        uint32_t trainer=C3_ARENA_BASE+side*C3_BATTLE_RUNTIME_TRAINER_BYTES;
        uint32_t fp=trainer+0xB1Cu+slot*0x154u,fo=trainer+C3_RUNTIME_FIGHT_OUT_OFFSET+slot*C3_FIGHT_OUT_POKEMON_BYTES;
        CHECK(c3_battle_fight_out_pokemon_create(&arena,fo,fp,0,&species));
        const uint8_t *poke=c3_battle_arena_bytes(&arena,fp+4,2);
        CHECK(((uint16_t)poke[0]<<8|poke[1])==want[side][slot].species);
        CHECK(get(&arena,fo,0xD5,0)==fp&&get(&arena,fo,0xD6,0)==fp);
        CHECK(get(&arena,fo,0xFF,0)==want[side][slot].t0&&get(&arena,fo,0xFF,1)==want[side][slot].t1);
        CHECK(get(&arena,fo,0x100,0)==want[side][slot].ability);
        printf("fight_out side=%u slot=%u species=%u types=%u/%u ability=%u fp=%08lx\n",side,slot,
            want[side][slot].species,(unsigned)get(&arena,fo,0xFF,0),(unsigned)get(&arena,fo,0xFF,1),
            (unsigned)get(&arena,fo,0x100,0),(unsigned long)fp);
    }
    free(s);c3_game_state_free(&game);free(pack);free(bytes);
}

int main(void)
{
    synthetic();
    const char *pack=getenv("C3_ENTRY_PACK"),*newgame=getenv("C3_ENTRY_NEWGAME");
    if(!pack||!newgame){puts("fight_out synthetic OK; private inputs absent");return 0;}
    original(pack,newgame);
    puts("fight_out OK");
    return 0;
}
