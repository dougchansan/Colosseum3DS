/* Original move scripts running battle-logic opcodes over the real trainer
 * entry. Needs C3_ENTRY_PACK, C3_ENTRY_NEWGAME and C3_SCRIPT_PACK. */
#include "colosseum3ds/battle_handlers.h"
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

typedef struct {
    uint8_t *entry_bytes,*script_bytes;C3BattleEntryPack *pack;C3GameState game;C3BattleEntrySession *entry;
    C3BattleState *state;C3BattleScriptPack script_pack;
} World;
static void world_init(World *w,const char *pack_path,const char *newgame,const char *script_path)
{
    size_t n;char error[C3_BATTLE_ERROR_SIZE];char gerr[C3_GAME_ERROR_SIZE];char se[C3_SCRIPT_ERROR_SIZE];
    memset(w,0,sizeof *w);
    w->entry_bytes=read_file(pack_path,&n);
    w->pack=malloc(sizeof *w->pack);CHECK(w->pack&&c3_battle_entry_pack_decode(w->pack,w->entry_bytes,n,error));
    CHECK(c3_game_state_new(&w->game,"WES",0x12345678u,newgame,gerr));
    size_t bn;const uint8_t *body=c3_game_state_body(&w->game,&bn);
    static uint32_t seqs;C3BattleEntrySessionConfig config;
    CHECK(c3_battle_entry_pack_config(w->pack,10,body,bn,0x12345678u,&config,error));
    config.sequence=(C3BattleSequenceServices){sstate,srequest,sfind,salloc,sfields,sload,srelease,shide,sreport};
    config.grid_add=sgrid;config.grid_update=supdate;config.set_visible=svisible;config.abort_owned=sabort;config.scene=&seqs;
    w->entry=calloc(1,sizeof *w->entry);CHECK(w->entry&&c3_battle_entry_session_begin(w->entry,&config));
    int r;unsigned guard=0;while((r=c3_battle_entry_session_step(w->entry))==1)CHECK(++guard<64);CHECK(r==0);
    w->script_bytes=read_file(script_path,&n);CHECK(c3_battle_script_pack_decode(&w->script_pack,w->script_bytes,n,se));
    w->state=malloc(sizeof *w->state);CHECK(w->state);
}
/* Fresh battle state with all four battlers sent out; attacker and defender set. */
static void battle_reset(World *w,unsigned att_side,unsigned att_slot,unsigned def_side,unsigned def_slot,
    uint16_t move,int8_t move_slot)
{
    char error[C3_BATTLE_ERROR_SIZE];C3BattleState *s=w->state;
    CHECK(c3_battle_state_from_entry(s,w->entry,w->pack,error));
    s->conditions=(C3BattleDataRows){w->script_pack.conditions,w->script_pack.condition_count,C3_CONDITION_ROW_BYTES};
    for(unsigned side=0;side<2;++side)for(unsigned k=0;k<2;++k)
        CHECK(c3_battle_fight_out_pokemon_create(&s->arena,c3_battle_state_fight_out(s,side,k),
            c3_battle_state_fight_pokemon(s,side,k),0,&s->species));
    uint32_t att=c3_battle_state_fight_out(s,att_side,att_slot),def=c3_battle_state_fight_out(s,def_side,def_slot);
    put32(s->memory+0xA474,att);put32(s->memory+0xA478,def);
    uint32_t fw=att+0x49C;
    CHECK(c3_battle_waza_status_set(&s->arena,fw,0x27,0,move)&&c3_battle_waza_status_set(&s->arena,fw,0x28,0,move));
    CHECK(c3_battle_waza_status_set(&s->arena,fw,0x26,0,(uint8_t)move_slot));
}
typedef struct { C3BattleHandlers handlers;C3BattleScript script; } Run;
static C3BattleScriptStatus run_effect(World *w,Run *r,uint16_t effect)
{
    memset(r,0,sizeof *r);r->handlers.state=w->state;
    C3BattleScriptServices sv=c3_battle_handler_services(&r->handlers);
    CHECK(c3_battle_script_init(&r->script,&w->script_pack,&sv));
    CHECK(c3_battle_script_call(&r->script,w->script_pack.effects[effect],1,1));
    for(unsigned i=0;i<64;++i){
        C3BattleScriptStatus st=c3_battle_script_step(&r->script);
        if(st==C3_SCRIPT_WAITING){CHECK(c3_battle_script_resume(&r->script));continue;}
        if(st!=C3_SCRIPT_RUNNING)return st;
    }
    CHECK(0);return C3_SCRIPT_FAILED;
}

int main(void)
{
    const char *pack=getenv("C3_ENTRY_PACK"),*newgame=getenv("C3_ENTRY_NEWGAME"),*script=getenv("C3_SCRIPT_PACK");
    if(!pack||!newgame||!script){puts("battle_handlers private inputs absent");return 0;}
    static World w;world_init(&w,pack,newgame,script);
    static Run r;uint32_t v;

    /* Zigzagoon A (opponent slot 0) uses Tackle (slot 0) on Umbreon. The hit
     * script's Surf guard reads the attacker's move, jumps to the attack
     * canceler, which passes, and the script stops at the accuracy check. */
    battle_reset(&w,1,0,0,0,33,0);
    CHECK(run_effect(&w,&r,0)==C3_SCRIPT_FAILED);
    printf("tackle: stops at %08lx: %s\n",(unsigned long)r.script.failure_pc,r.script.failure);
    CHECK(r.script.failure_pc==0x80375F8Fu&&strstr(r.script.failure,"fn_802284B0"));
    CHECK(c3_battle_script_read(&r.script,0x8047B618u,4,&v)&&v==0x02000000u);   /* hit marker */
    CHECK(c3_battle_script_read(&r.script,0x8047B63Cu,2,&v)&&v==33);            /* Surf guard read */
    CHECK(r.handlers.canceller_step==15&&r.script.opcodes==3);

    /* Espeon (player slot 1) uses Confusion (slot 0): same path. */
    battle_reset(&w,0,1,1,0,93,0);
    CHECK(run_effect(&w,&r,76)==C3_SCRIPT_FAILED);
    printf("confusion: stops at %08lx: %s\n",(unsigned long)r.script.failure_pc,r.script.failure);
    CHECK(r.handlers.canceller_step==15&&!strstr(r.script.failure,"WS_HITCHECK"));

    /* An asleep attacker: the canceller stops at its sleep step, named. */
    battle_reset(&w,1,0,0,0,33,0);
    uint8_t *poke=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,1,0)+4,0x138);
    put16(poke+0x64,8);
    CHECK(run_effect(&w,&r,0)==C3_SCRIPT_FAILED);
    CHECK(r.script.failure_pc==0x80375F8Eu&&strstr(r.script.failure,"sleep"));

    /* A fainted attacker: hit marker 0x80000 and the jump to lbl_80375FBF. */
    battle_reset(&w,1,0,0,0,33,0);
    poke=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,1,0)+4,0x138);
    put16(poke+0x8A,0);
    CHECK(run_effect(&w,&r,0)==C3_SCRIPT_FAILED);
    CHECK(c3_battle_script_read(&r.script,0x8047B618u,4,&v)&&(v&0x80000u));
    printf("fainted attacker: continues at %08lx: %s\n",(unsigned long)r.script.failure_pc,r.script.failure);
    CHECK(r.script.failure_pc>=0x80375FBFu);

    /* Battle already decided: the canceler ends the script (state 2). */
    battle_reset(&w,1,0,0,0,33,0);w.state->result=2;
    CHECK(run_effect(&w,&r,0)==C3_SCRIPT_FAILED&&strstr(r.script.failure,"End-of-move pass"));

    /* No PP left in the move's slot. */
    battle_reset(&w,1,0,0,0,33,0);
    poke=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,1,0)+4,0x138);
    poke[0x78+2]=0;
    CHECK(run_effect(&w,&r,0)==C3_SCRIPT_FAILED&&strstr(r.script.failure,"no PP"));

    puts("battle_handlers OK");
    return 0;
}
