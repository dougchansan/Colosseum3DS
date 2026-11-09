/* Attack-canceller pieces over the real trainer entry. Needs C3_ENTRY_PACK,
 * C3_ENTRY_NEWGAME and C3_SCRIPT_PACK; otherwise only argument refusals run. */
#include "colosseum3ds/battle_canceler.h"
#include "colosseum3ds/battle_script.h"
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
static void put16(uint8_t *p,uint16_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static uint16_t row_category(const C3BattleState *s,uint16_t id)
{const uint8_t *r=s->conditions.rows+id*C3_CONDITION_ROW_BYTES;return (uint16_t)((uint16_t)r[8]<<8|r[9]);}
/* Slot of a condition on the attacker: FightOutPokemon (0xD8) or its Pokemon (0x7C/0xC8). */
static uint8_t *slot_of(C3BattleState *s,uint32_t fo,uint32_t fp,uint16_t id)
{
    const uint8_t *row=s->conditions.rows+id*C3_CONDITION_ROW_BYTES;
    uint16_t cat=row_category(s,id);
    CHECK(row[0]==5);
    if(cat==0xD8)return c3_battle_arena_bytes(&s->arena,fo+0x15C+row[1]*16u,16);
    if(cat==0x7C)return c3_battle_arena_bytes(&s->arena,fp+4+0x64,16);
    if(cat==0xC8)return c3_battle_arena_bytes(&s->arena,fp+4+0xE8,16);
    CHECK(cat==0xCD);
    return c3_battle_arena_bytes(&s->arena,fp+0x13C,16);
}

static void original(const char *pack_path,const char *newgame,const char *script_pack)
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
    int r;unsigned guard=0;while((r=c3_battle_entry_session_step(e))==1)CHECK(++guard<64);CHECK(r==0);
    C3BattleState *s=calloc(1,sizeof *s);CHECK(s&&c3_battle_state_from_entry(s,e,pack,error));
    size_t sn;uint8_t *sb=read_file(script_pack,&sn);C3BattleScriptPack sp;char se[C3_SCRIPT_ERROR_SIZE];
    CHECK(c3_battle_script_pack_decode(&sp,sb,sn,se));
    s->conditions=(C3BattleDataRows){sp.conditions,sp.condition_count,C3_CONDITION_ROW_BYTES};
    for(unsigned side=0;side<2;++side)for(unsigned k=0;k<2;++k)
        CHECK(c3_battle_fight_out_pokemon_create(&s->arena,c3_battle_state_fight_out(s,side,k),
            c3_battle_state_fight_pokemon(s,side,k),0,&s->species));

    /* Computed floor fields from the encounter tables (encounter 10). */
    uint32_t v,kind,flag;
    CHECK(c3_battle_floor_value(s,0x14,&v)&&v==1);          /* fight type 1 */
    CHECK(c3_battle_floor_value(s,0x16,&v)&&v==1);          /* one trainer per side */
    CHECK(c3_battle_floor_value(s,0x18,&v)&&v==2);          /* two active */
    CHECK(c3_battle_floor_value(s,0x1A,&kind)&&kind==2&&c3_battle_floor_value(s,0x31,&flag));
    printf("encounter 10: type 1, kind %u, hyper-mode flag %u\n",(unsigned)kind,(unsigned)flag);

    /* Zigzagoon A attacks with Tackle. */
    uint32_t fo=c3_battle_state_fight_out(s,1,0),fp=c3_battle_state_fight_pokemon(s,1,0);
    CHECK(c3_battle_fight_out_trainer(s,fo)==s->trainer[1]);
    CHECK(c3_battle_fight_out_trainer(s,c3_battle_state_fight_out(s,0,1))==s->trainer[0]);
    put32(s->memory+0xA474,fo);
    CHECK(c3_battle_waza_status_set(&s->arena,fo+0x49C,0x28,0,33));
    char reason[C3_CANCEL_REASON_SIZE]="";
    uint32_t step=0;
    /* Conditions 0x15 / 0x28 are cleared at step 0 (padding byte +7 kept). */
    uint8_t *c15=slot_of(s,fo,fp,0x15),*c28=slot_of(s,fo,fp,0x28);
    put16(c15,0x15);c15[6]=3;c15[7]=0xAB;put16(c28,0x28);
    CHECK(c3_battle_status_canceller(s,&step,reason)==0&&step==15);
    CHECK(c15[0]==0&&c15[1]==0&&c15[6]==0&&c15[7]==0xAB&&c28[0]==0&&c28[1]==0);
    CHECK(c3_battle_hyper_canceller(s,fo,reason)==0);
    CHECK(c3_battle_shadow_check(s,fo,0,reason)==0);

    /* A condition that holds stops at its step, named, with the step kept. */
    static const struct{uint16_t id;uint32_t step;const char *name;}held[]={{8,1,"sleep"},{0x11,5,"flinch"},
        {9,9,"confusion"},{5,10,"paralysis"}};
    for(unsigned i=0;i<4;++i){
        uint8_t *slot=slot_of(s,fo,fp,held[i].id);put16(slot,held[i].id);
        step=0;reason[0]=0;
        CHECK(c3_battle_status_canceller(s,&step,reason)==C3_CANCEL_UNPORTED&&step==held[i].step&&strstr(reason,held[i].name));
        printf("condition %u (category %#x) stops at step %u: %s\n",held[i].id,row_category(s,held[i].id),
            (unsigned)step,reason);
        put16(slot,0);
    }
    step=16;CHECK(c3_battle_status_canceller(s,&step,reason)==C3_CANCEL_UNPORTED);

    /* Shadow: a dark Pokemon reaches the not-yet-lowered paths only when the
     * fight kind allows Hyper Mode. */
    uint8_t *poke=c3_battle_arena_bytes(&s->arena,fp+4,0x138);
    put16(poke+0xD8,1);put32(poke+0xDC,0);
    CHECK(c3_battle_pokemon_is_dark(s,fp+4));
    put32(poke+0xDC,0xFFFFFFFFu);CHECK(!c3_battle_pokemon_is_dark(s,fp+4));
    put32(poke+0xDC,0);
    int expect=flag?C3_CANCEL_UNPORTED:0;
    step=0;
    CHECK(c3_battle_shadow_check(s,fo,0,reason)==expect);
    CHECK(c3_battle_status_canceller(s,&step,reason)==expect);
    put16(slot_of(s,fo,fp,0x3E),0x3E);
    CHECK(c3_battle_hyper_canceller(s,fo,reason)==expect);
    put16(poke+0xD8,0);
    CHECK(c3_battle_hyper_canceller(s,fo,reason)==0);
    free(sb);free(s);free(e);c3_game_state_free(&game);free(pack);free(bytes);
}

int main(void)
{
    uint32_t step=0;char reason[C3_CANCEL_REASON_SIZE];
    CHECK(c3_battle_status_canceller(NULL,&step,reason)==C3_CANCEL_UNPORTED);
    const char *pack=getenv("C3_ENTRY_PACK"),*newgame=getenv("C3_ENTRY_NEWGAME"),*script=getenv("C3_SCRIPT_PACK");
    if(!pack||!newgame||!script){puts("battle_canceler private inputs absent; refusal checks only");return 0;}
    original(pack,newgame,script);
    puts("battle_canceler OK");
    return 0;
}
