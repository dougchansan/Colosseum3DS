/* Battle state from a real trainer entry, and the original validity checks.
 * Needs C3_ENTRY_PACK and C3_ENTRY_NEWGAME for the entry; without them only
 * argument refusals run. */
#include "colosseum3ds/battle_state.h"
#include "colosseum3ds/game_state.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: CHECK(%s)\n",__FILE__,__LINE__,#x);exit(1);}}while(0)

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
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

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
    C3BattleEntrySession *e=calloc(1,sizeof *e);CHECK(e&&c3_battle_entry_session_begin(e,&config));
    C3BattleState *s=calloc(1,sizeof *s);CHECK(s);
    CHECK(!c3_battle_state_from_entry(s,e,pack,error)&&strstr(error,"did not finish"));
    int r;unsigned guard=0;while((r=c3_battle_entry_session_step(e))==1)CHECK(++guard<64);CHECK(r==0);
    CHECK(c3_battle_state_from_entry(s,e,pack,error));

    for(unsigned side=0;side<2;++side){
        unsigned linked=0;
        for(unsigned j=0;j<6;++j){
            uint32_t fp=c3_battle_state_fight_pokemon(s,side,j);
            const uint8_t *f=c3_battle_arena_bytes(&s->arena,fp,0x154);CHECK(f);
            uint32_t link=be32(f);
            if(!link)continue;
            ++linked;
            /* Relocated to a party slot of this side's embedded Hero, whose
             * bytes the FightPokemon copied (PokemonBios at +4). */
            CHECK(link>=s->trainer[side]+C3_BATTLE_HERO_PARTY_OFFSET);
            uint32_t k=(link-s->trainer[side]-C3_BATTLE_HERO_PARTY_OFFSET)/C3_BATTLE_POKEMON_BYTES;
            CHECK(k<6&&link==s->trainer[side]+C3_BATTLE_HERO_PARTY_OFFSET+k*C3_BATTLE_POKEMON_BYTES);
            const uint8_t *party=c3_battle_arena_bytes(&s->arena,link,C3_BATTLE_POKEMON_BYTES);
            CHECK(party&&!memcmp(party,f+4,C3_BATTLE_POKEMON_BYTES));
            int fault=0;
            CHECK(c3_battle_pokemon_check_valid(s,link,&fault)&&c3_battle_pokemon_check_fight_out(s,link,&fault));
            CHECK(c3_battle_fight_pokemon_check_valid(s,fp,&fault)&&c3_battle_fight_pokemon_check_fight_out(s,fp,&fault));
            CHECK(!fault);
        }
        CHECK(linked==2);   /* two Pokemon per side in encounter 10 */
        for(unsigned slot=0;slot<2;++slot){
            uint32_t fo=c3_battle_state_fight_out(s,side,slot),fp=c3_battle_state_fight_pokemon(s,side,slot);
            int fault=0;
            CHECK(!c3_battle_fight_out_check_fight_out(s,fo,&fault));   /* before send-out: no FightPokemon link */
            CHECK(c3_battle_fight_out_pokemon_create(&s->arena,fo,fp,0,&s->species));
            CHECK(c3_battle_fight_out_check_valid(s,fo,&fault)&&c3_battle_fight_out_check_fight_out(s,fo,&fault)&&!fault);
        }
    }
    printf("battle_state: 4 active FightOutPokemon valid and in battle\n");

    /* Each rule on a real record (restored after each probe). */
    uint32_t fo=c3_battle_state_fight_out(s,1,0),fp=c3_battle_state_fight_pokemon(s,1,0);
    uint8_t *poke=c3_battle_arena_bytes(&s->arena,fp+4,C3_BATTLE_POKEMON_BYTES),saved[C3_BATTLE_POKEMON_BYTES];
    memcpy(saved,poke,sizeof saved);
    poke[0x8A]=poke[0x8B]=0;                                      /* fainted */
    CHECK(c3_battle_fight_out_check_valid(s,fo,NULL)&&!c3_battle_fight_out_check_fight_out(s,fo,NULL));
    memcpy(poke,saved,sizeof saved);poke[0xCB]=1;                 /* egg */
    CHECK(c3_battle_fight_out_check_valid(s,fo,NULL)&&!c3_battle_fight_out_check_fight_out(s,fo,NULL));
    memcpy(poke,saved,sizeof saved);poke[0xCD]=1;                 /* bad egg: invalid */
    CHECK(!c3_battle_fight_out_check_valid(s,fo,NULL));
    memcpy(poke,saved,sizeof saved);poke[10]=0;                   /* attest area ID */
    CHECK(!c3_battle_fight_out_check_valid(s,fo,NULL));
    memcpy(poke,saved,sizeof saved);poke[0]=0x01;poke[1]=0x9D;    /* species 413: beyond table */
    CHECK(!c3_battle_fight_out_check_valid(s,fo,NULL));
    memcpy(poke,saved,sizeof saved);
    uint8_t *fo_bytes=c3_battle_arena_bytes(&s->arena,fo,C3_FIGHT_OUT_POKEMON_BYTES);
    fo_bytes[0x6AC]=1;                                            /* 0x120 set */
    CHECK(c3_battle_fight_out_check_valid(s,fo,NULL)&&!c3_battle_fight_out_check_fight_out(s,fo,NULL));
    fo_bytes[0x6AC]=0;
    s->result=1;                                                  /* battle result 1 */
    CHECK(!c3_battle_fight_out_check_valid(s,fo,NULL));
    s->result=0;
    CHECK(c3_battle_fight_out_check_fight_out(s,fo,NULL));
    int fault=0;
    CHECK(!c3_battle_pokemon_check_valid(s,0x80001000u,&fault)&&fault); /* outside the arena */
    free(s);free(e);c3_game_state_free(&game);free(pack);free(bytes);
}

int main(void)
{
    char error[C3_BATTLE_ERROR_SIZE];
    CHECK(!c3_battle_state_from_entry(NULL,NULL,NULL,error));
    const char *pack=getenv("C3_ENTRY_PACK"),*newgame=getenv("C3_ENTRY_NEWGAME");
    if(!pack||!newgame){puts("battle_state private inputs absent; refusal checks only");return 0;}
    original(pack,newgame);
    puts("battle_state OK");
    return 0;
}
