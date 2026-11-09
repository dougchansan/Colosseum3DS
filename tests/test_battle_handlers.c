/* Original move scripts running battle-logic opcodes over the real trainer
 * entry. Needs C3_ENTRY_PACK, C3_ENTRY_NEWGAME and C3_SCRIPT_PACK. */
#include "colosseum3ds/battle_handlers.h"
#include "colosseum3ds/battle_preview.h"
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
    s->accuracy=(C3BattleDataRows){w->script_pack.accuracy,w->script_pack.accuracy_count,2};
    for(unsigned side=0;side<2;++side)for(unsigned k=0;k<2;++k)
        CHECK(c3_battle_fight_out_pokemon_create(&s->arena,c3_battle_state_fight_out(s,side,k),
            c3_battle_state_fight_pokemon(s,side,k),0,&s->species));
    uint32_t att=c3_battle_state_fight_out(s,att_side,att_slot),def=c3_battle_state_fight_out(s,def_side,def_slot);
    put32(s->memory+0xA474,att);put32(s->memory+0xA478,def);
    uint32_t fw=att+0x49C;
    CHECK(c3_battle_waza_status_set(&s->arena,fw,0x27,0,move)&&c3_battle_waza_set_use(s,fw,move));
    CHECK(c3_battle_waza_status_set(&s->arena,fw,0x26,0,(uint8_t)move_slot));
}
typedef struct { char log[256];size_t used; } Presentation;
static int present(void *u,C3BattlePresent req,uint32_t a,uint32_t b)
{
    Presentation *p=u;char t[48];
    snprintf(t,sizeof t,"%d:%lx:%lx;",(int)req,(unsigned long)(a&0xFFFF),(unsigned long)b);
    size_t n=strlen(t);CHECK(p->used+n<sizeof p->log);memcpy(p->log+p->used,t,n+1);p->used+=n;
    return req==C3_PRESENT_ATTACK_STRING;          /* a message window opened */
}
typedef struct { C3BattleHandlers handlers;C3BattleScript script;Presentation presentation;unsigned waits; } Run;
static C3BattleScriptStatus run_effect(World *w,Run *r,uint16_t effect)
{
    memset(r,0,sizeof *r);r->handlers.state=w->state;
    r->handlers.present=present;r->handlers.present_user=&r->presentation;
    r->handlers.damage=(C3BattleDamageTables){w->script_pack.stage_ratios,w->script_pack.stage_ratio_count,
        w->script_pack.item_types,w->pack->sections[C3_PACK_T44].bytes,w->pack->sections[C3_PACK_T44].count};
    r->handlers.critical=w->script_pack.critical;r->handlers.critical_count=w->script_pack.critical_count;
    C3BattleScriptServices sv=c3_battle_handler_services(&r->handlers);
    CHECK(c3_battle_script_init(&r->script,&w->script_pack,&sv));
    CHECK(c3_battle_script_call(&r->script,w->script_pack.effects[effect],1,1));
    for(unsigned i=0;i<64;++i){
        C3BattleScriptStatus st=c3_battle_script_step(&r->script);
        if(st==C3_SCRIPT_WAITING){++r->waits;CHECK(r->script.wait_frames==0x20);CHECK(c3_battle_script_resume(&r->script));continue;}
        if(st!=C3_SCRIPT_RUNNING)return st;
    }
    CHECK(0);return C3_SCRIPT_FAILED;
}

/* Damage loss (fn_802271E0): the third draw of a hit (accuracy, critical,
 * loss) gives 100 - draw % 16 percent, at least 1 of nonzero damage. */
static uint32_t after_loss(C3BattleRng rng,uint32_t damage)
{
    uint16_t draw=0;
    for(int i=0;i<3;++i)CHECK(c3_battle_rng_next16(&rng,&draw));
    uint32_t lost=damage*(100u-draw%16u)/100u;
    return damage&&!lost?1u:lost;
}

/* Base damage from the preview binding of the same original arithmetic
 * (src/battle_damage.c), with preview mons built from the same records. */
static uint32_t preview_base_damage(World *w,const char *dir,unsigned att_side,unsigned att_slot,
    unsigned def_side,unsigned def_slot,uint16_t move,uint16_t power,unsigned type)
{
    char path[512],error[C3_BATTLE_ERROR_SIZE];
    static C3BattlePreviewTables t;static C3BattlePreview b;
    snprintf(path,sizeof path,"%s/willie.c3p",dir);CHECK(c3_battle_preview_load(&t,path,error));
    memset(&b,0,sizeof b);
    const unsigned sides[4]={0,0,1,1},slots[4]={0,1,0,1};
    for(unsigned i=0;i<4;++i){
        C3BattleState *s=w->state;uint32_t fp=c3_battle_state_fight_pokemon(s,sides[i],slots[i]);
        const uint8_t *pk=c3_battle_arena_bytes(&s->arena,fp+4,0x138);uint32_t ab=0;
        CHECK(c3_battle_fight_status_get(&s->arena,c3_battle_state_fight_out(s,sides[i],slots[i]),0x100,0,&ab));
        C3BattlePreviewMon *m=&b.mons[i];
        m->species=(uint16_t)(pk[0]<<8|pk[1]);m->level=pk[0x60];m->ability=(uint8_t)ab;
        m->hp=(uint16_t)(pk[0x8A]<<8|pk[0x8B]);m->maximum_hp=(uint16_t)(pk[0x8C]<<8|pk[0x8D]);
        static const unsigned order[]={C3_STAT_HP,C3_STAT_ATTACK,C3_STAT_DEFENSE,C3_STAT_SP_ATTACK,C3_STAT_SP_DEFENSE,C3_STAT_SPEED};
        for(unsigned j=0;j<6;++j)m->stats[order[j]]=(uint16_t)(pk[0x8C+2*j]<<8|pk[0x8D+2*j]);
    }
    uint32_t out=0;
    CHECK(c3_battle_preview_base_damage(&b,&t,att_side*2+att_slot,def_side*2+def_slot,move,power,type,1,&out));
    return out;
}

int main(void)
{
    const char *pack=getenv("C3_ENTRY_PACK"),*newgame=getenv("C3_ENTRY_NEWGAME"),*script=getenv("C3_SCRIPT_PACK");
    const char *preview_dir=getenv("C3_PREVIEW_DIR");
    if(!pack||!newgame||!script||!preview_dir){puts("battle_handlers private inputs absent");return 0;}
    static World w;world_init(&w,pack,newgame,script);
    static Run r;uint32_t v;

    /* Zigzagoon A (opponent slot 0) uses Tackle (slot 0) on Umbreon: Surf
     * guard, attack canceler, accuracy (one draw from the shared stream),
     * attack string (nested pause 0x20, then return), PP reduce; the script
     * stops at the critical-hit check. A draw above the accuracy is a miss,
     * which needs the type check and stops there instead. */
    battle_reset(&w,1,0,0,0,33,0);
    C3BattleRng predicted=w.state->rng;uint16_t draw=0;CHECK(c3_battle_rng_next16(&predicted,&draw));
    int hit=(int)(draw%100u)+1<=95;                       /* Tackle 95%, stage 6 (1/1) */
    uint32_t fp=c3_battle_state_fight_pokemon(w.state,1,0);
    uint8_t *tackle=c3_battle_arena_bytes(&w.state->arena,fp+4+0x78,4);
    CHECK(tackle[0]==0&&tackle[1]==33);uint8_t pp_before=tackle[2];
    CHECK(run_effect(&w,&r,0)==C3_SCRIPT_FAILED);
    printf("tackle: draw %u -> %s; stops at %08lx: %s\n",draw,hit?"hit":"miss",
        (unsigned long)r.script.failure_pc,r.script.failure);
    if(!hit)CHECK(w.state->rng.draws==predicted.draws&&w.state->rng.seed==predicted.seed);
    {   /* fightSideCreate data IDs (floor data field 3) and the valid count. */
        const uint8_t *sd=c3_battle_arena_bytes(&w.state->arena,w.state->floor+C3_FIGHT_SIDE_OFFSET,C3_FIGHT_SIDE_BYTES+2);
        CHECK(sd[0]==0&&sd[1]==2&&sd[C3_FIGHT_SIDE_BYTES]==0&&sd[C3_FIGHT_SIDE_BYTES+1]==1);
        uint32_t own=c3_battle_state_fight_out(w.state,0,0);uint16_t n=0;
        CHECK(c3_battle_state_valid_fight_out_count(w.state,1,own,0,&n)&&n==2);   /* own side */
        CHECK(c3_battle_state_valid_fight_out_count(w.state,0,own,0,&n)&&n==3);   /* all but own */
        CHECK(c3_battle_state_valid_fight_out_count(w.state,0,0,1,&n)&&n==4);     /* all out */
        CHECK(!c3_battle_state_valid_fight_out_count(w.state,2,own,0,&n));        /* not modelled */
    }
    if(hit){
        /* Critical check (one draw when critical hits are possible), damage
         * calculation, type check (same-type bonus; Normal on Dark is
         * neutral), damage loss (one draw: 85-100%), the set-byte step, then
         * fn_80226FD4 (not lowered yet). */
        uint16_t crit_draw=0,loss_draw=0;C3BattleRng after=predicted;CHECK(c3_battle_rng_next16(&after,&crit_draw));
        uint32_t crit=crit_draw%16u==0?2u:1u,flag=0;
        CHECK(c3_battle_floor_value(w.state,0x29,&flag));
        if(!(flag&0xFF)){crit=1;after=predicted;}
        CHECK(c3_battle_rng_next16(&after,&loss_draw));
        CHECK(w.state->rng.draws==after.draws&&w.state->rng.seed==after.seed);
        CHECK(r.script.failure_pc==0x80375FA2u&&strstr(r.script.failure,"fn_80226FD4"));
        CHECK(c3_battle_script_read(&r.script,0x8047B600u,1,&v)&&v==0);           /* 0x2E set byte */
        CHECK(c3_battle_script_read(&r.script,0x8047B618u,4,&v)&&v==0x02000400u);   /* hit marker */
        CHECK(tackle[2]==pp_before-1);                                                  /* PP 35 -> 34 */
        CHECK(c3_battle_fight_status_get(&w.state->arena,c3_battle_state_fight_out(w.state,1,0),0x111,0,&v)&&v==1);
        CHECK(r.waits==1&&r.script.depth==1);
        uint32_t att=c3_battle_state_fight_out(w.state,1,0),fw=att+0x49C;
        char expect[160];
        snprintf(expect,sizeof expect,"1:%lx:1;2:%lx:21;3:0:0;4:%lx:1;",(unsigned long)(att&0xFFFF),
            (unsigned long)(att&0xFFFF),(unsigned long)(att&0xFFFF));
        CHECK(!strcmp(r.presentation.log,expect));
        CHECK(c3_battle_script_read(&r.script,0x80478D7Fu,1,&v)&&v==0);
        uint32_t stored_crit=0,damage=0;
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2B,0,&stored_crit)&&stored_crit==crit);
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2D,0,&damage));
        uint32_t base=preview_base_damage(&w,preview_dir,1,0,0,0,33,35,0);
        uint32_t typed=base*crit*15/10,percent=100u-loss_draw%16u,lost=typed?typed*percent/100u:0;
        if(typed&&!lost)lost=1;
        printf("tackle damage: base %u x crit %u x 1.5 = %u, x %u%% = %u (crit draw %u, loss draw %u)\n",base,crit,
            typed,percent,damage,crit_draw,loss_draw);
        CHECK(damage==lost);
        CHECK(c3_battle_condition_present(w.state,fw,0x3F,NULL));                    /* fightWazaInit default */
        for(uint16_t id=0x40;id<=0x45;++id)CHECK(!c3_battle_condition_present(w.state,fw,id,NULL));
        CHECK(c3_battle_script_read(&r.script,0x80478D7Eu,1,&v)&&v==0);           /* work byte 6 */
    }else{
        CHECK(!strstr(r.script.failure,"miss"));
        CHECK(tackle[2]==pp_before);
    }
    CHECK(r.handlers.canceller_step==15);

    /* Espeon (player slot 1) uses Confusion (slot 0) on Zigzagoon A. */
    battle_reset(&w,0,1,1,0,93,0);
    C3BattleRng start=w.state->rng;
    CHECK(run_effect(&w,&r,76)==C3_SCRIPT_FAILED);
    printf("confusion: stops at %08lx: %s\n",(unsigned long)r.script.failure_pc,r.script.failure);
    CHECK(r.handlers.canceller_step==15&&!strstr(r.script.failure,"WS_HITCHECK"));
    {   /* Espeon's Confusion: special, Psychic, power 50, cross-checked too. */
        uint32_t fw=c3_battle_state_fight_out(w.state,0,1)+0x49C,damage=0,crit=0;
        if(strstr(r.script.failure,"fn_80226FD4")){
            CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2D,0,&damage));
            CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2B,0,&crit));
            uint32_t base=preview_base_damage(&w,preview_dir,0,1,1,0,93,50,14);
            printf("confusion damage: base %u x crit %u x 1.5, loss = %u\n",base,crit,damage);
            CHECK(damage==after_loss(start,base*crit*15/10));     /* Psychic on Psychic Espeon */
        }
    }

    /* Accuracy stages: maximum evasion on the defender (stage 0: 33/100). */
    battle_reset(&w,1,0,0,0,33,0);
    uint8_t *def_fo=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_out(w.state,0,0),0x6E0);
    def_fo[0x60A]=12;                                     /* 0xEC evasion stage +6 */
    predicted=w.state->rng;CHECK(c3_battle_rng_next16(&predicted,&draw));
    hit=(int)(draw%100u)+1<=95*33/100;
    CHECK(run_effect(&w,&r,0)==C3_SCRIPT_FAILED);
    printf("evasion +6: draw %u -> %s: %s\n",draw,hit?"hit":"miss",r.script.failure);
    if(hit)CHECK(strstr(r.script.failure,"fn_80226FD4")!=NULL);
    else{   /* fightWazaIsHit fails: miss condition 0x40, the defender's hit
             * move cleared (0xF3 0, 0xF4 9) and the miss branch taken. */
        uint32_t fw=c3_battle_state_fight_out(w.state,1,0)+0x49C,att_type=0,hit_move=1,hit_type=0;
        uint32_t def=c3_battle_state_fight_out(w.state,0,0);
        CHECK(c3_battle_condition_present(w.state,fw,0x40,NULL));
        CHECK(c3_battle_fight_status_get(&w.state->arena,def,0xF3,0,&hit_move)&&hit_move==0);
        CHECK(c3_battle_fight_status_get(&w.state->arena,def,0xF4,0,&hit_type)&&hit_type==9);
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x30,0,&att_type)&&att_type==0);
        CHECK(!strstr(r.script.failure,"miss"));
        printf("evasion +6 miss continues at %08lx\n",(unsigned long)r.script.failure_pc);
    }

    /* Type chart (fn_802279F8): Tackle retyped Fighting (1) on Dark Umbreon
     * is super effective: x2, condition 0x41, no same-type bonus. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        uint32_t att=c3_battle_state_fight_out(w.state,1,0),fw=att+0x49C,damage=0,crit=0;
        CHECK(c3_battle_waza_status_set(&w.state->arena,fw,0x30,0,1));
        start=w.state->rng;
        CHECK(run_effect(&w,&r,0)==C3_SCRIPT_FAILED);
        CHECK(strstr(r.script.failure,"fn_80226FD4")!=NULL);
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2D,0,&damage));
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2B,0,&crit));
        /* The preview admits types 0/14/17 only; Fighting is physical like
         * Normal and no type-specific modifier applies to these battlers. */
        uint32_t base=preview_base_damage(&w,preview_dir,1,0,0,0,33,35,0);
        printf("fighting tackle: base %u x crit %u x 2, loss = %u\n",base,crit,damage);
        CHECK(damage==after_loss(start,base*crit*2));
        CHECK(c3_battle_condition_present(w.state,fw,0x41,NULL)&&!c3_battle_condition_present(w.state,fw,0x42,NULL));
    }
    /* Endure (condition 0x2C) on Umbreon at 3 HP: damage capped at HP - 1 and
     * FightWaza condition 0x46 recorded. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        uint32_t att=c3_battle_state_fight_out(w.state,1,0),fw=att+0x49C,def=c3_battle_state_fight_out(w.state,0,0);
        uint32_t damage=0;
        uint8_t *hp=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,0,0)+4+0x8A,2);
        hp[0]=0;hp[1]=3;
        uint8_t *slot=c3_battle_arena_bytes(&w.state->arena,def+0x15C+0x23*16,16);   /* row 0x2C: slot 0x23 */
        slot[0]=0;slot[1]=0x2C;
        CHECK(c3_battle_condition_check(w.state,def,0x2C,NULL)==1);
        CHECK(run_effect(&w,&r,0)==C3_SCRIPT_FAILED);
        printf("endure: %s\n",r.script.failure);
        CHECK(strstr(r.script.failure,"fn_80226FD4")!=NULL);
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2D,0,&damage)&&damage==2);
        CHECK(c3_battle_condition_present(w.state,fw,0x46,NULL));
    }
    /* Umbreon retyped Ghost (7/7): Normal has no effect: damage 0, condition
     * 0x43 and the attacker's no-effect flag 0x108. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        uint32_t att=c3_battle_state_fight_out(w.state,1,0),fw=att+0x49C,def=c3_battle_state_fight_out(w.state,0,0);
        uint32_t damage=1,flag=0;
        CHECK(c3_battle_fight_status_set(&w.state->arena,def,0xFF,0,7)&&c3_battle_fight_status_set(&w.state->arena,def,0xFF,1,7));
        CHECK(run_effect(&w,&r,0)==C3_SCRIPT_FAILED);
        printf("ghost umbreon: %s\n",r.script.failure);
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2D,0,&damage)&&damage==0);
        CHECK(c3_battle_condition_present(w.state,fw,0x43,NULL));
        CHECK(c3_battle_fight_status_get(&w.state->arena,att,0x108,0,&flag)&&flag==1);
    }

    /* Protect on the defender stops at the accuracy check's reflect test. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        const uint8_t *row=w.state->conditions.rows+0x2B*C3_CONDITION_ROW_BYTES;
        CHECK(((uint16_t)row[8]<<8|row[9])==0xD8);
        uint8_t *slot=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_out(w.state,0,0)+0x15C+row[1]*16u,2);
        put16(slot,0x2B);
        CHECK(run_effect(&w,&r,0)==C3_SCRIPT_FAILED&&strstr(r.script.failure,"Protect"));
    }

    /* An asleep attacker: the canceller stops at its sleep step, named. */
    battle_reset(&w,1,0,0,0,33,0);
    uint8_t *sleeper=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,1,0)+4,0x138);
    put16(sleeper+0x64,8);
    CHECK(run_effect(&w,&r,0)==C3_SCRIPT_FAILED);
    CHECK(r.script.failure_pc==0x80375F8Eu&&strstr(r.script.failure,"sleep"));
    uint8_t *poke;

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
