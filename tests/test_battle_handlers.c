/* Original move scripts running battle-logic opcodes over the real trainer
 * entry. Needs C3_ENTRY_PACK, C3_ENTRY_NEWGAME and C3_SCRIPT_PACK. */
#include "colosseum3ds/battle_handlers.h"
#include "colosseum3ds/battle_preview.h"
#include "colosseum3ds/game_state.h"
#include <stddef.h>
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
    s->target_kinds=(C3BattleDataRows){w->script_pack.target_kinds,w->script_pack.target_kind_count,8};
    for(unsigned side=0;side<2;++side)for(unsigned k=0;k<2;++k)
        CHECK(c3_battle_fight_out_pokemon_create(&s->arena,c3_battle_state_fight_out(s,side,k),
            c3_battle_state_fight_pokemon(s,side,k),0,&s->species));
    CHECK(c3_battle_state_set_meet_enemies(s));        /* battle-start steps, in their order */
    CHECK(c3_battle_state_create_enemy_arrays(s));    /* battle-start step the move scripts read */
    uint32_t att=c3_battle_state_fight_out(s,att_side,att_slot),def=c3_battle_state_fight_out(s,def_side,def_slot);
    put32(s->memory+0xA474,att);put32(s->memory+0xA478,def);
    uint32_t fw=att+0x49C;
    CHECK(c3_battle_waza_status_set(&s->arena,fw,0x27,0,move)&&c3_battle_waza_set_use(s,fw,move));
    CHECK(c3_battle_waza_status_set(&s->arena,fw,0x26,0,(uint8_t)move_slot));
}
typedef struct { char log[1024];size_t used;unsigned menu_shown,hp_busy; } Presentation;
static int present(void *u,C3BattlePresent req,uint32_t a,uint32_t b)
{
    Presentation *p=u;char t[48];
    snprintf(t,sizeof t,"%d:%lx:%lx;",(int)req,(unsigned long)(a&0xFFFF),(unsigned long)b);
    size_t n=strlen(t);CHECK(p->used+n<sizeof p->log);memcpy(p->log+p->used,t,n+1);p->used+=n;
    if(req==C3_PRESENT_STATUS_MENU_SHOWN)return (int)p->menu_shown;
    if(req==C3_PRESENT_HP_BAR_BUSY){if(p->hp_busy){--p->hp_busy;return 1;}return 0;}
    return req==C3_PRESENT_ATTACK_STRING||req==C3_PRESENT_MESSAGE_OPEN||req==C3_PRESENT_EFFECT_MESSAGE;
}
typedef struct { C3BattleHandlers handlers;C3BattleScript script;Presentation presentation;unsigned waits;
    unsigned effect_waits,signal_delay,message_waits;
    int full_turn,reached_end;uint32_t end_pc; } Run;
/* The failure, or "reached move end" when run_effect stopped where the move
 * script ended (before the faint script, the passes and the cleanup reset
 * the turn's records). */
/* Where the move script ended: the WAZAEND PC saved for the faint script,
 * or the PC itself when the script returned (state 2) to the cleanup. */
static uint32_t end_pc(const Run *r)
{return r->script.frames[0].phase==C3_SCRIPT_PHASE_FAINT_SCRIPT?r->script.frames[0].faint_pc:r->script.pc;}
static uint32_t stop_pc(const Run *r){return r->reached_end?r->end_pc:r->script.failure_pc;}
static const char *why(const Run *r){return r->reached_end?"reached move end":r->script.failure;}
static int stopped(C3BattleScriptStatus st){return st==C3_SCRIPT_FAILED||st==C3_SCRIPT_RUNNING;}
/* For hand-driven loops: marks and reports the point run_effect stops at. */
static int at_end(Run *r)
{
    if(r->script.status!=C3_SCRIPT_RUNNING||r->script.depth!=1||r->script.frames[0].phase==C3_SCRIPT_PHASE_RUN)return 0;
    r->reached_end=1;r->end_pc=end_pc(r);return 1;
}
/* Effect signal: reached after signal_delay polls; logs each signal asked. */
static int effect_signal(void *u,uint8_t signal)
{
    Run *r=(Run *)((char *)u-offsetof(Run,presentation));
    char t[16];snprintf(t,sizeof t,"S:%u;",(unsigned)signal);
    Presentation *p=&r->presentation;size_t n=strlen(t);
    if(r->signal_delay){--r->signal_delay;return 0;}
    CHECK(p->used+n<sizeof p->log);memcpy(p->log+p->used,t,n+1);p->used+=n;
    return 1;
}
static int present_move(void *u,uint16_t move,uint32_t attacker,uint32_t target,uint8_t count)
{
    Presentation *p=u;char t[64];
    snprintf(t,sizeof t,"M:%u:%lx:%lx:%u;",(unsigned)move,(unsigned long)(attacker&0xFFFF),
        (unsigned long)(target&0xFFFF),(unsigned)count);
    size_t n=strlen(t);CHECK(p->used+n<sizeof p->log);memcpy(p->log+p->used,t,n+1);p->used+=n;
    return 1;
}
static C3BattleScriptStatus run_effect_mode(World *w,Run *r,uint16_t effect,int full_turn);
static C3BattleScriptStatus run_effect(World *w,Run *r,uint16_t effect){return run_effect_mode(w,r,effect,0);}
/* Stops where the top-level move script ended (returns RUNNING with
 * reached_end) unless full_turn, which runs on to the end of the call. */
static C3BattleScriptStatus run_effect_mode(World *w,Run *r,uint16_t effect,int full_turn)
{
    memset(r,0,sizeof *r);r->handlers.state=w->state;r->full_turn=full_turn;
    r->handlers.present=present;r->handlers.present_user=&r->presentation;r->handlers.present_move=present_move;
    r->handlers.effect_signal=effect_signal;
    r->handlers.damage=(C3BattleDamageTables){w->script_pack.stage_ratios,w->script_pack.stage_ratio_count,
        w->script_pack.item_types,w->pack->sections[C3_PACK_T44].bytes,w->pack->sections[C3_PACK_T44].count};
    r->handlers.critical=w->script_pack.critical;r->handlers.critical_count=w->script_pack.critical_count;
    C3BattleScriptServices sv=c3_battle_handler_services(&r->handlers);
    CHECK(c3_battle_script_init(&r->script,&w->script_pack,&sv));
    CHECK(c3_battle_script_call(&r->script,w->script_pack.effects[effect],1,1));
    for(unsigned i=0;i<64;++i){
        C3BattleScriptStatus st=c3_battle_script_step(&r->script);
        if(st==C3_SCRIPT_WAITING){
            if(r->script.wait_frames==1)++r->effect_waits;
            else if(r->script.wait_frames==0x40)++r->message_waits;     /* 0x12 timed close */
            else{++r->waits;CHECK(r->script.wait_frames==0x20);}
            CHECK(c3_battle_script_resume(&r->script));continue;
        }
        if(st!=C3_SCRIPT_RUNNING)return st;
        if(!r->full_turn&&r->script.depth==1&&r->script.frames[0].phase!=C3_SCRIPT_PHASE_RUN){
            r->reached_end=1;r->end_pc=end_pc(r);return st;
        }
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
    const uint8_t *umbreon_hp=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,0,0)+4+0x8A,2);
    uint16_t hp_before=(uint16_t)(umbreon_hp[0]<<8|umbreon_hp[1]);
    CHECK(stopped(run_effect(&w,&r,0)));
    printf("tackle: draw %u -> %s; stops at %08lx: %s\n",draw,hit?"hit":"miss",
        (unsigned long)stop_pc(&r),why(&r));
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
         * neutral), damage loss (one draw: 85-100%), the set-byte step, the
         * move effect (fightWazaDoEffectFunc, effect counters 0 -> 1), the
         * effect wait for signal 5, 0x0E, 0x5C (no battler model: no blink), the
         * skip, the HP update (0x0B), the critical-hit message (0x0D: none
         * without a critical hit), the timed message close (0x12: no window
         * open), the effectiveness message (0x0F: neutral, none), the timed
         * close again, the HP bar wait (0x0C: status menu not shown, no wait),
         * the effect wait for signal 6 (closes the status menus), the
         * secondary-effect chance (0x15: one draw, Tackle has no effect
         * pending), the faint check (0x19: Umbreon still standing), the
         * set-byte step, move end (0x49: no reaction applies; stages 11, 14
         * and 15 keep their records), then WAZAEND, whose first end-of-move
         * pass (the faint mark) is not lowered yet. */
        uint16_t crit_draw=0,loss_draw=0;C3BattleRng after=predicted;CHECK(c3_battle_rng_next16(&after,&crit_draw));
        uint32_t crit=crit_draw%16u==0?2u:1u,flag=0;
        CHECK(c3_battle_floor_value(w.state,0x29,&flag));
        if(!(flag&0xFF)){crit=1;after=predicted;}
        CHECK(c3_battle_rng_next16(&after,&loss_draw));
        uint16_t chance_draw=0;C3BattleRng after_loss_rng=after;CHECK(c3_battle_rng_next16(&after,&chance_draw));
        (void)after_loss_rng;
        CHECK(w.state->rng.draws==after.draws&&w.state->rng.seed==after.seed);
        CHECK(r.end_pc==0x80375FC8u&&strstr(why(&r),"reached move end"));
        {
            uint32_t a=c3_battle_state_fight_out(w.state,1,0),d=c3_battle_state_fight_out(w.state,0,0);
            uint32_t ef=0,f0=0,f1=0,shown=0,f2=0,f3=0,f4=0,f7=0,power=0;uint16_t rel=0;
            CHECK(c3_battle_script_read(&r.script,0x8047B627u,1,&v)&&v==0x11);     /* move end stage */
            CHECK(c3_battle_fight_status_get(&w.state->arena,a,0xEF,0,&ef)&&c3_battle_fight_status_get(&w.state->arena,a,0xF0,0,&f0)&&
                  c3_battle_fight_status_get(&w.state->arena,a,0xF1,0,&f1)&&c3_battle_fight_status_get(&w.state->arena,a,0x115,0,&shown));
            CHECK(c3_battle_fight_status_get(&w.state->arena,d,0xF2,0,&f2)&&c3_battle_fight_status_get(&w.state->arena,d,0xF3,0,&f3)&&
                  c3_battle_fight_status_get(&w.state->arena,d,0xF4,0,&f4)&&c3_battle_fight_status_get(&w.state->arena,d,0xF7,0,&f7));
            CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,a+0x49C,0,0x30,0,&power));
            uint32_t ft=0;
            CHECK(c3_battle_floor_value(w.state,0x14,&ft)&&c3_battle_state_relative_target_id(w.state,a,(uint16_t)ft,&rel));
            printf("move end: attacker EF %lx F0 %lx F1 %lx 115 %lx; defender F2 %lx F3 %lx F4 %lx F7 %lx (power field %lx)\n",
                (unsigned long)ef,(unsigned long)f0,(unsigned long)f1,(unsigned long)shown,(unsigned long)f2,(unsigned long)f3,
                (unsigned long)f4,(unsigned long)f7,(unsigned long)power);
            CHECK(ef==0x21&&f0==0x21&&f1==0x21&&shown==1&&f2==rel&&f3==0x21&&f4==power);
        }
        CHECK(c3_battle_script_read(&r.script,0x80478D7Bu,1,&v)&&v==0);           /* effect byte cleared */
        CHECK(c3_battle_script_read(&r.script,0x8047B625u,1,&v)&&v==0);
        CHECK(r.effect_waits==0);
        CHECK(c3_battle_script_read(&r.script,0x8038FF5Au,1,&v)&&v==1);           /* effect done count */
        CHECK(c3_battle_script_read(&r.script,0x8038FFF9u,1,&v)&&v==1);           /* effect counter */
        CHECK(c3_battle_script_read(&r.script,0x8047B600u,1,&v)&&v==0);           /* 0x2E set byte */
        CHECK(c3_battle_script_read(&r.script,0x8047B618u,4,&v)&&v==0x02000400u);   /* hit marker */
        CHECK(tackle[2]==pp_before-1);                                                  /* PP 35 -> 34 */
        CHECK(c3_battle_fight_status_get(&w.state->arena,c3_battle_state_fight_out(w.state,1,0),0x111,0,&v)&&v==1);
        CHECK(r.waits==1&&r.script.depth==1);
        uint32_t att=c3_battle_state_fight_out(w.state,1,0),fw=att+0x49C;
        char expect[200];
        uint32_t def=c3_battle_state_fight_out(w.state,0,0);
        uint16_t hp_after=(uint16_t)(umbreon_hp[0]<<8|umbreon_hp[1]);
        snprintf(expect,sizeof expect,
            "1:%lx:1;2:%lx:21;3:0:0;4:%lx:1;M:33:%lx:%lx:0;S:5;6:%lx:1;1:%lx:1;7:%lx:%x;6:%lx:1;S:6;5:0:0;11:0:0;5:0:0;3:0:0;11:0:0;5:0:0;3:0:0;",
            (unsigned long)(att&0xFFFF),(unsigned long)(att&0xFFFF),(unsigned long)(att&0xFFFF),(unsigned long)(att&0xFFFF),
            (unsigned long)(def&0xFFFF),(unsigned long)(def&0xFFFF),(unsigned long)(def&0xFFFF),(unsigned long)(def&0xFFFF),
            (unsigned)hp_after,(unsigned long)(def&0xFFFF));
        printf("presentation: %s\n",r.presentation.log);
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
        /* HP update: Umbreon loses the damage; records name Zigzagoon A by its
         * relative target ID (other side, first active: kind 15). */
        printf("umbreon hp %u -> %u\n",hp_before,hp_after);
        CHECK(hp_after==(hp_before>lost?hp_before-lost:0));
        uint32_t f=0;
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2E,0,&f)&&f==lost);
        static const uint16_t sel[7]={0xF5,0x11B,0x102,0x11C,0xF6,0x103,0x11D};
        for(unsigned i=0;i<7;++i){
            CHECK(c3_battle_fight_status_get(&w.state->arena,def,sel[i],0,&f));
            CHECK(f==(i<4?lost:15u));
        }
        CHECK(c3_battle_fight_status_get(&w.state->arena,def,0x104,0,&f)&&f==0);  /* special: untouched */
        {
            uint16_t id=0;
            static const uint16_t ids[4]={12,13,15,16};
            for(unsigned i=0;i<4;++i){
                CHECK(c3_battle_state_relative_target_id(w.state,c3_battle_state_fight_out(w.state,i/2,i%2),1,&id));
                CHECK(id==ids[i]);
            }
            uint32_t p=0;
            CHECK(c3_battle_state_target(w.state,2,att,1,&p)&&p==w.state->floor+C3_FIGHT_SIDE_OFFSET+C3_FIGHT_SIDE_BYTES);
            CHECK(c3_battle_state_target(w.state,3,att,1,&p)&&p==w.state->floor+C3_FIGHT_SIDE_OFFSET);
            CHECK(c3_battle_state_target(w.state,0xE,att,1,&p)&&p==c3_battle_state_fight_out(w.state,1,1));
            CHECK(c3_battle_state_target(w.state,0x10,att,1,&p)&&p==c3_battle_state_fight_out(w.state,0,1));
            CHECK(!c3_battle_state_target(w.state,0xE,0,1,&p));                         /* unset local */
            /* Zigzagoon A's enemy array (battle start): Umbreon then Espeon;
             * the hit adds its damage to Umbreon's entry. */
            const uint8_t *e=c3_battle_arena_bytes(&w.state->arena,att+0x6B0,48);
            uint32_t espeon=c3_battle_state_fight_out(w.state,0,1);
            CHECK(e[0]==(uint8_t)(def>>24)&&e[1]==(uint8_t)(def>>16)&&e[2]==(uint8_t)(def>>8)&&e[3]==(uint8_t)def);
            CHECK((uint16_t)(e[6]<<8|e[7])==hp_before&&(uint16_t)(e[8]<<8|e[9])==lost);
            CHECK(e[12]==(uint8_t)(espeon>>24)&&e[15]==(uint8_t)espeon&&e[20]==0&&e[21]==0);
            CHECK(!e[24]&&!e[25]&&!e[26]&&!e[27]&&!e[36]&&!e[37]&&!e[38]&&!e[39]);
        }
        CHECK(c3_battle_condition_present(w.state,fw,0x3F,NULL));                    /* fightWazaInit default */
        for(uint16_t id=0x40;id<=0x45;++id)CHECK(!c3_battle_condition_present(w.state,fw,id,NULL));
        CHECK(c3_battle_script_read(&r.script,0x80478D7Eu,1,&v)&&v==0);           /* work byte 6 */
    }else{
        CHECK(!strstr(why(&r),"miss"));
        CHECK(tackle[2]==pp_before);
    }
    CHECK(r.handlers.canceller_step==15);

    /* Espeon (player slot 1) uses Confusion (slot 0) on Zigzagoon A. */
    battle_reset(&w,0,1,1,0,93,0);
    C3BattleRng start=w.state->rng;
    CHECK(stopped(run_effect(&w,&r,76)));
    printf("confusion: stops at %08lx: %s\n",(unsigned long)stop_pc(&r),why(&r));
    CHECK(r.handlers.canceller_step==15&&!strstr(why(&r),"WS_HITCHECK"));
    {   /* Espeon's Confusion: special, Psychic, power 50, cross-checked too. */
        uint32_t fw=c3_battle_state_fight_out(w.state,0,1)+0x49C,damage=0,crit=0;
        if(strstr(why(&r),"reached move end")){
            CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2D,0,&damage));
            CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2B,0,&crit));
            uint32_t base=preview_base_damage(&w,preview_dir,0,1,1,0,93,50,14);
            printf("confusion damage: base %u x crit %u x 1.5, loss = %u\n",base,crit,damage);
            CHECK(damage==after_loss(start,base*crit*15/10));     /* Psychic on Psychic Espeon */
            /* Special damage records on Zigzagoon A name Espeon (relative 13). */
            uint32_t zig=c3_battle_state_fight_out(w.state,1,0),f=0;
            CHECK(c3_battle_fight_status_get(&w.state->arena,zig,0x104,0,&f)&&f==damage);
            CHECK(c3_battle_fight_status_get(&w.state->arena,zig,0x105,0,&f)&&f==13);
            CHECK(c3_battle_fight_status_get(&w.state->arena,zig,0x11E,0,&f)&&f==damage);
            CHECK(c3_battle_fight_status_get(&w.state->arena,zig,0x102,0,&f)&&f==0);
        }
    }

    /* Accuracy stages: maximum evasion on the defender (stage 0: 33/100). */
    battle_reset(&w,1,0,0,0,33,0);
    uint8_t *def_fo=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_out(w.state,0,0),0x6E0);
    def_fo[0x60A]=12;                                     /* 0xEC evasion stage +6 */
    predicted=w.state->rng;CHECK(c3_battle_rng_next16(&predicted,&draw));
    hit=(int)(draw%100u)+1<=95*33/100;
    CHECK(stopped(run_effect(&w,&r,0)));
    printf("evasion +6: draw %u -> %s: %s\n",draw,hit?"hit":"miss",why(&r));
    if(hit)CHECK(strstr(why(&r),"reached move end")!=NULL);
    else{   /* fightWazaIsHit fails: miss condition 0x40, the defender's hit
             * move cleared (0xF3 0, 0xF4 9) and the miss branch taken. */
        uint32_t fw=c3_battle_state_fight_out(w.state,1,0)+0x49C,att_type=0,hit_move=1,hit_type=0;
        uint32_t def=c3_battle_state_fight_out(w.state,0,0);
        CHECK(c3_battle_condition_present(w.state,fw,0x40,NULL));
        CHECK(c3_battle_fight_status_get(&w.state->arena,def,0xF3,0,&hit_move)&&hit_move==0);
        CHECK(c3_battle_fight_status_get(&w.state->arena,def,0xF4,0,&hit_type)&&hit_type==9);
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x30,0,&att_type)&&att_type==0);
        CHECK(!strstr(why(&r),"miss"));
        printf("evasion +6 miss continues at %08lx\n",(unsigned long)r.script.failure_pc);
    }

    /* Type chart (fn_802279F8): Tackle retyped Fighting (1) on Dark Umbreon
     * is super effective: x2, condition 0x41, no same-type bonus. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        uint32_t att=c3_battle_state_fight_out(w.state,1,0),fw=att+0x49C,damage=0,crit=0;
        CHECK(c3_battle_waza_status_set(&w.state->arena,fw,0x30,0,1));
        start=w.state->rng;
        CHECK(stopped(run_effect(&w,&r,0)));
        CHECK(strstr(why(&r),"reached move end")!=NULL);
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2D,0,&damage));
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2B,0,&crit));
        /* The preview admits types 0/14/17 only; Fighting is physical like
         * Normal and no type-specific modifier applies to these battlers. */
        uint32_t base=preview_base_damage(&w,preview_dir,1,0,0,0,33,35,0);
        printf("fighting tackle: base %u x crit %u x 2, loss = %u\n",base,crit,damage);
        CHECK(damage==after_loss(start,base*crit*2));
        CHECK(c3_battle_condition_present(w.state,fw,0x41,NULL)&&!c3_battle_condition_present(w.state,fw,0x42,NULL));
        /* 0x0F: "super effective" (0x7637) on Umbreon, closed by 0x12 after 0x40 frames. */
        char want[32];uint32_t m=0,open=1;
        snprintf(want,sizeof want,"9:7637:%lx;3:0:0;",(unsigned long)c3_battle_state_fight_out(w.state,0,0));
        CHECK(strstr(r.presentation.log,want)!=NULL&&r.message_waits==1);
        CHECK(c3_battle_floor_status_get(&w.state->arena,&w.state->floors,w.state->floor,0,0,0x53,0,&m)&&m==0x7637);
        CHECK(c3_battle_script_read(&r.script,0x80478D7Fu,1,&open)&&open==0);
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
        CHECK(stopped(run_effect(&w,&r,0)));
        printf("endure: %s\n",why(&r));
        CHECK(strstr(why(&r),"reached move end")!=NULL);
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2D,0,&damage)&&damage==2);
        CHECK(c3_battle_condition_present(w.state,fw,0x46,NULL));
        char want[32];
        snprintf(want,sizeof want,"9:75c9:%lx;3:0:0;",(unsigned long)def);
        CHECK(strstr(r.presentation.log,want)!=NULL);                    /* "endured the hit" */
    }
    /* Super effective into Endure (0x41 + 0x46, "mix"): 0x0F erases 0x46/0x47
     * and calls the Endure sequence 0x803791BE (0x10 opens "endured the hit"
     * 0x75C9, 0x12 closes it, return); 0x0F then runs again, now without
     * the mix, and shows "super effective" (0x7637), and the hit script runs
     * on to its end. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        uint32_t fw=c3_battle_state_fight_out(w.state,1,0)+0x49C,def=c3_battle_state_fight_out(w.state,0,0);
        uint8_t *hp=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,0,0)+4+0x8A,2);
        hp[0]=0;hp[1]=3;
        uint8_t *slot=c3_battle_arena_bytes(&w.state->arena,def+0x15C+0x23*16,16);slot[0]=0;slot[1]=0x2C;
        CHECK(c3_battle_waza_status_set(&w.state->arena,fw,0x30,0,1));
        CHECK(stopped(run_effect(&w,&r,0)));
        printf("endure + super effective: stops at %08lx: %s (depth %u)\n",(unsigned long)r.script.failure_pc,
            why(&r),(unsigned)r.script.depth);
        CHECK(r.reached_end&&r.end_pc==0x80375FC8u);
        CHECK(strstr(r.presentation.log,"8:75c9:0;3:0:0;")!=NULL);
        char want[32];snprintf(want,sizeof want,"9:7637:%lx;",(unsigned long)def);
        CHECK(strstr(r.presentation.log,want)!=NULL);
        CHECK(c3_battle_condition_present(w.state,fw,0x41,NULL)&&!c3_battle_condition_present(w.state,fw,0x46,NULL));
    }
    /* Umbreon retyped Ghost (7/7): Normal has no effect: damage 0, condition
     * 0x43 and the attacker's no-effect flag 0x108. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        uint32_t att=c3_battle_state_fight_out(w.state,1,0),fw=att+0x49C,def=c3_battle_state_fight_out(w.state,0,0);
        uint32_t damage=1,flag=0;
        CHECK(c3_battle_fight_status_set(&w.state->arena,def,0xFF,0,7)&&c3_battle_fight_status_set(&w.state->arena,def,0xFF,1,7));
        CHECK(stopped(run_effect(&w,&r,0)));
        printf("ghost umbreon: %s\n",why(&r));
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,fw,0,0x2D,0,&damage)&&damage==0);
        CHECK(c3_battle_condition_present(w.state,fw,0x43,NULL));
        CHECK(c3_battle_fight_status_get(&w.state->arena,att,0x108,0,&flag)&&flag==1);
        char want[32];
        snprintf(want,sizeof want,"9:7543:%lx;3:0:0;",(unsigned long)def);
        CHECK(strstr(r.presentation.log,want)!=NULL);                    /* "doesn't affect" */
        /* No effect counts as a miss in the move effect step: status menus
         * close, no move effect, the counters stay, the script goes on. */
        uint32_t v2=1;
        CHECK(strstr(r.presentation.log,"5:0:0;")!=NULL&&!strstr(r.presentation.log,"M:"));
        CHECK(c3_battle_script_read(&r.script,0x8038FFF9u,1,&v2)&&v2==0);
        CHECK(strstr(why(&r),"reached move end")!=NULL);
    }
    /* Hit-marker bit 0x80: the effect is replaced by the pause sequence
     * (lbl_80378964, a second 0x20-frame wait); the counters still advance. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        memset(&r,0,sizeof r);r.handlers.state=w.state;
        r.handlers.present=present;r.handlers.present_user=&r.presentation;r.handlers.present_move=present_move;
        r.handlers.damage=(C3BattleDamageTables){w.script_pack.stage_ratios,w.script_pack.stage_ratio_count,
            w.script_pack.item_types,w.pack->sections[C3_PACK_T44].bytes,w.pack->sections[C3_PACK_T44].count};
        r.handlers.critical=w.script_pack.critical;r.handlers.critical_count=w.script_pack.critical_count;
        C3BattleScriptServices sv=c3_battle_handler_services(&r.handlers);
        CHECK(c3_battle_script_init(&r.script,&w.script_pack,&sv));
        CHECK(c3_battle_script_call(&r.script,w.script_pack.effects[0],1,1));
        C3BattleScriptStatus st=C3_SCRIPT_RUNNING;
        for(unsigned i=0;i<64&&st!=C3_SCRIPT_FAILED;++i){
            if(r.script.pc==0x80375FA2u){                    /* set bit 0x80 just before 0x09 */
                uint32_t m=0;CHECK(c3_battle_script_read(&r.script,0x8047B618u,4,&m));
                c3_battle_script_write(&r.script,0x8047B618u,4,m|0x80u);
            }
            st=c3_battle_script_step(&r.script);
            if(at_end(&r))break;
            if(st==C3_SCRIPT_WAITING){++r.waits;CHECK(r.script.wait_frames==0x20);CHECK(c3_battle_script_resume(&r.script));}
        }
        uint32_t v2=0;
        printf("hit marker 0x80: %s (waits %u)\n",why(&r),r.waits);
        CHECK(strstr(why(&r),"effect wait skipped")!=NULL&&r.waits==2&&!strstr(r.presentation.log,"M:"));
        CHECK(c3_battle_script_read(&r.script,0x8038FFF9u,1,&v2)&&v2==1);
    }

    /* The effect signal arrives after two polls: two one-frame waits. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        memset(&r,0,sizeof r);r.handlers.state=w.state;r.signal_delay=2;
        r.handlers.present=present;r.handlers.present_user=&r.presentation;r.handlers.present_move=present_move;
        r.handlers.effect_signal=effect_signal;
        r.handlers.damage=(C3BattleDamageTables){w.script_pack.stage_ratios,w.script_pack.stage_ratio_count,
            w.script_pack.item_types,w.pack->sections[C3_PACK_T44].bytes,w.pack->sections[C3_PACK_T44].count};
        r.handlers.critical=w.script_pack.critical;r.handlers.critical_count=w.script_pack.critical_count;
        C3BattleScriptServices sv=c3_battle_handler_services(&r.handlers);
        CHECK(c3_battle_script_init(&r.script,&w.script_pack,&sv));
        CHECK(c3_battle_script_call(&r.script,w.script_pack.effects[0],1,1));
        C3BattleScriptStatus st=C3_SCRIPT_RUNNING;
        for(unsigned i=0;i<64&&st!=C3_SCRIPT_FAILED;++i){
            st=c3_battle_script_step(&r.script);
            if(at_end(&r))break;
            if(st==C3_SCRIPT_WAITING){
                if(r.script.wait_frames==1)++r.effect_waits;else ++r.waits;
                CHECK(c3_battle_script_resume(&r.script));
            }
        }
        printf("delayed signal: %s (one-frame waits %u)\n",why(&r),r.effect_waits);
        CHECK(strstr(why(&r),"reached move end")!=NULL&&r.effect_waits==2&&strstr(r.presentation.log,"S:5;"));
    }
    /* 0x5C with hit-marker bit 0x80 (set just before it): no battler model
     * means no blink and the script goes on; a target model is refused. */
    for(int model=0;model<2;++model){
        battle_reset(&w,1,0,0,0,33,0);
        if(model)CHECK(c3_battle_fight_status_set(&w.state->arena,c3_battle_state_fight_out(w.state,0,0),0xEE,0,1));
        memset(&r,0,sizeof r);r.handlers.state=w.state;
        r.handlers.present=present;r.handlers.present_user=&r.presentation;r.handlers.present_move=present_move;
        r.handlers.effect_signal=effect_signal;
        r.handlers.damage=(C3BattleDamageTables){w.script_pack.stage_ratios,w.script_pack.stage_ratio_count,
            w.script_pack.item_types,w.pack->sections[C3_PACK_T44].bytes,w.pack->sections[C3_PACK_T44].count};
        r.handlers.critical=w.script_pack.critical;r.handlers.critical_count=w.script_pack.critical_count;
        C3BattleScriptServices sv=c3_battle_handler_services(&r.handlers);
        CHECK(c3_battle_script_init(&r.script,&w.script_pack,&sv));
        CHECK(c3_battle_script_call(&r.script,w.script_pack.effects[0],1,1));
        C3BattleScriptStatus st=C3_SCRIPT_RUNNING;
        for(unsigned i=0;i<64&&st!=C3_SCRIPT_FAILED;++i){
            if(r.script.pc==0x80375FA6u){
                uint32_t m=0;CHECK(c3_battle_script_read(&r.script,0x8047B618u,4,&m));
                c3_battle_script_write(&r.script,0x8047B618u,4,m|0x80u);
            }
            st=c3_battle_script_step(&r.script);
            if(at_end(&r))break;
            if(st==C3_SCRIPT_WAITING)CHECK(c3_battle_script_resume(&r.script));
        }
        printf("0x5C bit 0x80, model %d: %s\n",model,why(&r));
        CHECK(strstr(why(&r),model?"battler model":"effect wait skipped")!=NULL);   /* 0x0A at 80375FB5 sees bit 0x80 */
    }

    /* A critical hit (FightWaza 0x2B = 2, set just before 0x0D) stores and
     * opens message 0x7631 and marks the open window in work byte 7. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        memset(&r,0,sizeof r);r.handlers.state=w.state;
        r.handlers.present=present;r.handlers.present_user=&r.presentation;r.handlers.present_move=present_move;
        r.handlers.effect_signal=effect_signal;
        r.handlers.damage=(C3BattleDamageTables){w.script_pack.stage_ratios,w.script_pack.stage_ratio_count,
            w.script_pack.item_types,w.pack->sections[C3_PACK_T44].bytes,w.pack->sections[C3_PACK_T44].count};
        r.handlers.critical=w.script_pack.critical;r.handlers.critical_count=w.script_pack.critical_count;
        C3BattleScriptServices sv=c3_battle_handler_services(&r.handlers);
        CHECK(c3_battle_script_init(&r.script,&w.script_pack,&sv));
        CHECK(c3_battle_script_call(&r.script,w.script_pack.effects[0],1,1));
        uint32_t fw=c3_battle_state_fight_out(w.state,1,0)+0x49C,msg=0,open=1,opened=0;
        C3BattleScriptStatus st=C3_SCRIPT_RUNNING;
        unsigned long_waits=0;
        for(unsigned i=0;i<64&&st!=C3_SCRIPT_FAILED;++i){
            if(r.script.pc==0x80375FABu)CHECK(c3_battle_waza_status_set(&w.state->arena,fw,0x2B,0,2));
            st=c3_battle_script_step(&r.script);
            if(at_end(&r))break;
            if(r.script.pc==0x80375FACu&&!opened)CHECK(c3_battle_script_read(&r.script,0x80478D7Fu,1,&opened));
            if(st==C3_SCRIPT_WAITING){if(r.script.wait_frames==0x40)++long_waits;CHECK(c3_battle_script_resume(&r.script));}
        }
        CHECK(c3_battle_floor_value(w.state,0x52,&msg)||
              c3_battle_floor_status_get(&w.state->arena,&w.state->floors,w.state->floor,0,0,0x52,0,&msg));
        CHECK(c3_battle_script_read(&r.script,0x80478D7Fu,1,&open));
        printf("critical message: %s (floor 0x52 %lx, window opened %lu, now %lu, 0x40-frame waits %u)\n",
            why(&r),(unsigned long)msg,(unsigned long)opened,(unsigned long)open,long_waits);
        CHECK(msg==0x7631&&opened==1&&open==0&&long_waits==1&&strstr(r.presentation.log,"8:7631:0;3:0:0;"));
        CHECK(strstr(why(&r),"reached move end")!=NULL);
    }

    /* Status menu shown and the HP bar busy for two polls: 0x0C waits two
     * frames before going on (and 0x0B brings in no actor). */
    battle_reset(&w,1,0,0,0,33,0);
    {
        memset(&r,0,sizeof r);r.handlers.state=w.state;r.presentation.menu_shown=1;r.presentation.hp_busy=2;
        r.handlers.present=present;r.handlers.present_user=&r.presentation;r.handlers.present_move=present_move;
        r.handlers.effect_signal=effect_signal;
        r.handlers.damage=(C3BattleDamageTables){w.script_pack.stage_ratios,w.script_pack.stage_ratio_count,
            w.script_pack.item_types,w.pack->sections[C3_PACK_T44].bytes,w.pack->sections[C3_PACK_T44].count};
        r.handlers.critical=w.script_pack.critical;r.handlers.critical_count=w.script_pack.critical_count;
        C3BattleScriptServices sv=c3_battle_handler_services(&r.handlers);
        CHECK(c3_battle_script_init(&r.script,&w.script_pack,&sv));
        CHECK(c3_battle_script_call(&r.script,w.script_pack.effects[0],1,1));
        C3BattleScriptStatus st=C3_SCRIPT_RUNNING;
        unsigned frame_waits=0;
        for(unsigned i=0;i<64&&st!=C3_SCRIPT_FAILED;++i){
            st=c3_battle_script_step(&r.script);
            if(at_end(&r))break;
            if(st==C3_SCRIPT_WAITING){if(r.script.wait_frames==1)++frame_waits;CHECK(c3_battle_script_resume(&r.script));}
        }
        printf("HP bar busy: %s (one-frame waits %u)\n",why(&r),frame_waits);
        char want[96],one[24];uint32_t def=c3_battle_state_fight_out(w.state,0,0);
        snprintf(one,sizeof one,"6:%lx:1;10:%lx:0;",(unsigned long)(def&0xFFFF),(unsigned long)(def&0xFFFF));
        snprintf(want,sizeof want,"%s%s%s",one,one,one);                /* three polls */
        CHECK(strstr(why(&r),"reached move end")!=NULL&&frame_waits==2&&strstr(r.presentation.log,want)!=NULL);
    }

    /* The player's Umbreon fainting needs the friendship loss (floor 0x27 is 1
     * here): refused by name after 0x120 and hit marker 0x400000. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        uint32_t def=c3_battle_state_fight_out(w.state,0,0),f=0;
        uint8_t *hp=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,0,0)+4+0x8A,2);
        hp[0]=0;hp[1]=1;
        CHECK(c3_battle_fight_out_is_gc_hero(w.state,def)==1);
        CHECK(c3_battle_fight_out_is_gc_hero(w.state,c3_battle_state_fight_out(w.state,1,0))==0);
        CHECK(stopped(run_effect(&w,&r,0)));
        printf("umbreon knocked out: %s\n",why(&r));
        CHECK(strstr(why(&r),"faint friendship loss")!=NULL);
        CHECK(c3_battle_fight_status_get(&w.state->arena,def,0x120,0,&f)&&f==1);
        CHECK(c3_battle_script_read(&r.script,0x8047B618u,4,&f)&&(f&0x400000u));
    }
    /* Umbreon's Tackle knocks out Zigzagoon A at 1 HP: 0x19 marks it fainted
     * (0x120), appoints it (floor 0x4B) and calls the defender faint sequence
     * 0x80378703: 0x1A opens its status panel, 0x56 its cry, 0x10 "fainted"
     * (0x7545), 0x12 closes the message after 0x40 frames, 0x1B closes the
     * panel, clears its status slot and makes both opponents forget it (0xF7
     * and their enemy entry for it); then 0x4B is restored and move end
     * runs. WAZAEND's faint mark pass runs Zigzagoon's sequence
     * lbl_80378721: the EXP award (0x23). Both of the player's battlers met
     * it, so each gets base EXP x 24 / 7, halved, x1.5 (floor 0x2D): the EXP
     * camera, the panel, message 0x7532, its effort values (Zigzagoon gives
     * 1 Speed), the EXP and the bar, and the panel renewed and closed. The
     * faint script's battle end check then stops at the replacement check:
     * Zigzagoon A is out of battle. */
    battle_reset(&w,0,0,1,0,33,0);
    {
        uint32_t def=c3_battle_state_fight_out(w.state,1,0),f=0,type=0;
        uint32_t umbreon=c3_battle_state_fight_out(w.state,0,0),espeon=c3_battle_state_fight_out(w.state,0,1);
        uint32_t zig_poke=c3_battle_state_fight_pokemon(w.state,1,0)+4;
        uint8_t *hp=c3_battle_arena_bytes(&w.state->arena,zig_poke+0x8A,2);
        uint8_t *status=c3_battle_arena_bytes(&w.state->arena,zig_poke+0x64,16);
        const uint8_t *species=c3_battle_arena_bytes(&w.state->arena,zig_poke,2);
        hp[0]=0;hp[1]=1;
        status[0]=0;status[1]=3;                                         /* a status condition to clear */
        CHECK(c3_battle_fight_out_is_fainted(w.state,def)==0);
        CHECK(c3_battle_floor_value(w.state,0x14,&type));
        int entry_before=0;
        {
            const uint8_t *e=c3_battle_arena_bytes(&w.state->arena,umbreon+0x6B0,48);
            for(unsigned k=0;k<4;++k)
                if(((uint32_t)e[12*k]<<24|(uint32_t)e[12*k+1]<<16|(uint32_t)e[12*k+2]<<8|e[12*k+3])==def)entry_before=1;
        }
        CHECK(entry_before);
        uint32_t umb_poke=c3_battle_state_fight_pokemon(w.state,0,0)+4,esp_poke=c3_battle_state_fight_pokemon(w.state,0,1)+4;
        uint8_t *umb=c3_battle_arena_bytes(&w.state->arena,umb_poke,C3_BATTLE_POKEMON_BYTES);
        uint8_t *esp=c3_battle_arena_bytes(&w.state->arena,esp_poke,C3_BATTLE_POKEMON_BYTES);
        uint32_t umb_exp=(uint32_t)umb[0x5c]<<24|(uint32_t)umb[0x5d]<<16|(uint32_t)umb[0x5e]<<8|umb[0x5f];
        uint32_t esp_exp=(uint32_t)esp[0x5c]<<24|(uint32_t)esp[0x5d]<<16|(uint32_t)esp[0x5e]<<8|esp[0x5f];
        uint16_t umb_spe=(uint16_t)(umb[0xA2]<<8|umb[0xA3]),esp_spe=(uint16_t)(esp[0xA2]<<8|esp[0xA3]);
        const uint8_t *zrow=w.state->species.rows+0x120u*C3_SPECIES_ROW_BYTES;
        uint16_t base=(uint16_t)(zrow[6]<<8|zrow[7]);
        uint32_t gain=(uint16_t)((uint32_t)base*24u/7u)/2u;gain=gain*150u/100u;
        printf("zigzagoon EXP: base %u -> %lu each; speed effort yield %u\n",(unsigned)base,(unsigned long)gain,
            (unsigned)(zrow[0x9A]<<8|zrow[0x9B]));
        CHECK(stopped(run_effect_mode(&w,&r,0,1)));
        printf("zigzagoon knocked out: stops at %08lx: %s (depth %u)\n",(unsigned long)r.script.failure_pc,
            why(&r),(unsigned)r.script.depth);
        printf("zigzagoon faint presentation: %s\n",r.presentation.log);
        CHECK(strstr(why(&r),"replacement Pokemon")!=NULL&&r.script.depth==1);
        CHECK(((uint32_t)umb[0x5c]<<24|(uint32_t)umb[0x5d]<<16|(uint32_t)umb[0x5e]<<8|umb[0x5f])==umb_exp+gain);
        CHECK(((uint32_t)esp[0x5c]<<24|(uint32_t)esp[0x5d]<<16|(uint32_t)esp[0x5e]<<8|esp[0x5f])==esp_exp+gain);
        CHECK((uint16_t)(umb[0xA2]<<8|umb[0xA3])==umb_spe+(zrow[0x9A]<<8|zrow[0x9B]));
        CHECK((uint16_t)(esp[0xA2]<<8|esp[0xA3])==esp_spe+(zrow[0x9A]<<8|zrow[0x9B]));
        CHECK(w.state->msgctrl[0x2F]==gain&&w.state->msgctrl[0x0E]==C3_MSGREF_MESSAGE(0x7533));
        {
            char exp_want[200];uint32_t lv=0;
            const uint8_t *g=w.state->growth.rows+(uint32_t)w.state->species.rows[197u*C3_SPECIES_ROW_BYTES]*0x194u;
            lv=(uint32_t)g[26*4]<<24|(uint32_t)g[26*4+1]<<16|(uint32_t)g[26*4+2]<<8|g[26*4+3];
            snprintf(exp_want,sizeof exp_want,"13:0:0;1:%lx:%lx;8:7532:0;3:0:0;14:%lx:%lx;15:%lx:0;16:%lx:%lx;3:0:0;4:%lx:%lx;",
                (unsigned long)(umbreon&0xFFFF),(unsigned long)type,(unsigned long)(umbreon&0xFFFF),
                (unsigned long)(umb_exp+gain-lv),(unsigned long)(umbreon&0xFFFF),(unsigned long)(umbreon&0xFFFF),
                (unsigned long)type,(unsigned long)(umbreon&0xFFFF),(unsigned long)type);
            CHECK(strstr(r.presentation.log,exp_want)!=NULL);
        }
        CHECK(hp[0]==0&&hp[1]==0&&c3_battle_fight_out_is_fainted(w.state,def)==1);
        CHECK(c3_battle_fight_status_get(&w.state->arena,def,0x120,0,&f)&&f==1);
        CHECK(status[0]==0&&status[1]==0);                               /* pokemonInitJoutai */
        char want[160];
        snprintf(want,sizeof want,"1:%lx:%lx;12:%lx:%x;8:7545:0;3:0:0;4:%lx:%lx;",
            (unsigned long)(def&0xFFFF),(unsigned long)type,(unsigned long)(def&0xFFFF),
            (unsigned)(species[0]<<8|species[1]),(unsigned long)(def&0xFFFF),(unsigned long)type);
        CHECK(strstr(r.presentation.log,want)!=NULL);
        CHECK(r.message_waits>=1);
        const uint8_t *ap=c3_battle_arena_bytes(&w.state->arena,w.state->floor+0xA494,4);       /* floor 0x4B restored */
        CHECK(ap&&((uint32_t)ap[0]<<24|(uint32_t)ap[1]<<16|(uint32_t)ap[2]<<8|ap[3])!=def);
        for(int o=0;o<2;++o){
            uint32_t opp=o?espeon:umbreon;
            CHECK(c3_battle_fight_status_get(&w.state->arena,opp,0xF7,0,&f)&&f==0);
            const uint8_t *e=c3_battle_arena_bytes(&w.state->arena,opp+0x6B0,48);
            for(unsigned k=0;k<4;++k)
                CHECK(((uint32_t)e[12*k]<<24|(uint32_t)e[12*k+1]<<16|(uint32_t)e[12*k+2]<<8|e[12*k+3])!=def);
        }
        CHECK(c3_battle_script_read(&r.script,0x8047B618u,4,&f)&&!(f&0x400000u));
    }

    /* The whole turn: Zigzagoon's Tackle on Umbreon runs past WAZAEND. The
     * faint mark pass finds no fainted battler, the faint script's battle
     * end check (0x24) finds both sides with HP and every battler in battle,
     * the four post-faint passes find no Intimidate, Trace, held item or
     * Forecast, and the cleanup resets the turn: each battler's 0x113..0x11F,
     * the attacker's 0xF3 / 0xF4 (0 / 9), the damage (0x2D), the hit marker
     * and the effect bytes. The HP loss and move end's records stay; no
     * number is drawn and nothing is presented after the move script. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        C3BattleScriptStatus st=run_effect(&w,&r,0);
        CHECK(st==C3_SCRIPT_RUNNING&&r.reached_end);
        C3BattleRng at_end_rng=w.state->rng;
        char log_at_end[sizeof r.presentation.log];memcpy(log_at_end,r.presentation.log,sizeof log_at_end);
        battle_reset(&w,1,0,0,0,33,0);
        st=run_effect_mode(&w,&r,0,1);
        printf("full turn: status %d, depth %u: %s\n",(int)st,(unsigned)r.script.depth,why(&r));
        CHECK(st==C3_SCRIPT_DONE&&r.script.depth==0);
        CHECK(w.state->rng.draws==at_end_rng.draws&&w.state->rng.seed==at_end_rng.seed);
        CHECK(strcmp(r.presentation.log,log_at_end)==0);
        uint32_t a=c3_battle_state_fight_out(w.state,1,0),d=c3_battle_state_fight_out(w.state,0,0),v=0;
        uint32_t list[8];unsigned n=0;
        CHECK(c3_battle_state_valid_fight_outs(w.state,list,&n)&&n==4);
        for(unsigned i=0;i<n;++i)for(uint32_t sel=0x113;sel<=0x11F;++sel)
            CHECK(c3_battle_fight_status_get(&w.state->arena,list[i],sel,0,&v)&&v==0);
        CHECK(c3_battle_fight_status_get(&w.state->arena,a,0xF3,0,&v)&&v==0);
        CHECK(c3_battle_fight_status_get(&w.state->arena,a,0xF4,0,&v)&&v==9);
        CHECK(c3_battle_fight_status_get(&w.state->arena,a,0xEF,0,&v)&&v==0x21);       /* move end record */
        CHECK(c3_battle_fight_status_get(&w.state->arena,d,0xF3,0,&v)&&v==0x21);
        CHECK(c3_battle_waza_status_get(&w.state->arena,&w.state->moves,a+0x49C,0,0x2D,0,&v)&&v==0);
        CHECK(c3_battle_script_read(&r.script,0x8047B618u,4,&v)&&v==0);                /* hit marker */
        CHECK(c3_battle_script_read(&r.script,0x8038FF5Au,1,&v)&&v==0);
        CHECK(c3_battle_script_read(&r.script,0x8038FFF9u,1,&v)&&v==0);
        const uint8_t *hp=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,0,0)+4+0x8A,2);
        printf("full turn: umbreon hp %u\n",(unsigned)(hp[0]<<8|hp[1]));
        CHECK((hp[0]<<8|hp[1])==81);
        const uint8_t *att=c3_battle_arena_bytes(&w.state->arena,w.state->floor+0xA474,4);  /* floor 0x36 restored */
        CHECK(att&&((uint32_t)att[0]<<24|(uint32_t)att[1]<<16|(uint32_t)att[2]<<8|att[3])==a&&w.state->msgctrl[0x0F]==a);
        CHECK(w.state->msgctrl[0x29]==C3_MSGREF_ITEM_NAME(0));
        CHECK(w.state->result==0);
    }
    /* A Forecast battler in battle: the fourth post-faint pass is refused. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        uint32_t partner=c3_battle_state_fight_out(w.state,0,1);
        CHECK(c3_battle_fight_status_set(&w.state->arena,partner,0x100,0,0x3B));
        CHECK(run_effect_mode(&w,&r,0,1)==C3_SCRIPT_FAILED);
        printf("forecast partner: %s\n",why(&r));
        CHECK(strstr(why(&r),"Forecast")!=NULL);
    }

    /* Umbreon one EXP short of level 27 when Zigzagoon A faints: its award
     * would level it up (lbl_80378724, stat growth): refused by name, its
     * EXP unchanged. */
    battle_reset(&w,0,0,1,0,33,0);
    {
        uint8_t *hp=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,1,0)+4+0x8A,2);
        uint8_t *umb=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,0,0)+4,C3_BATTLE_POKEMON_BYTES);
        const uint8_t *g=w.state->growth.rows+(uint32_t)w.state->species.rows[197u*C3_SPECIES_ROW_BYTES]*0x194u;
        uint32_t next=((uint32_t)g[27*4]<<24|(uint32_t)g[27*4+1]<<16|(uint32_t)g[27*4+2]<<8|g[27*4+3])-1u;
        hp[0]=0;hp[1]=1;
        umb[0x5c]=(uint8_t)(next>>24);umb[0x5d]=(uint8_t)(next>>16);umb[0x5e]=(uint8_t)(next>>8);umb[0x5f]=(uint8_t)next;
        CHECK(run_effect_mode(&w,&r,0,1)==C3_SCRIPT_FAILED);
        printf("level up: %s\n",why(&r));
        CHECK(strstr(why(&r),"level up")!=NULL);
        CHECK(((uint32_t)umb[0x5c]<<24|(uint32_t)umb[0x5d]<<16|(uint32_t)umb[0x5e]<<8|umb[0x5f])==next);
    }

    /* A held item on Espeon (Umbreon's partner, outside this exchange): move
     * end's held-item sweep over the battlers (stage 8, fn_8022B5C8) appoints
     * the item (floor 0x56: message variable
     * 0x29, FightFloor +0xA4BC), then needs its effect ID, which the state
     * does not hold yet: refused by name, the stage left at 8. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        uint8_t *item=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,0,1)+4+0x88,2);
        CHECK(item!=NULL);
        item[0]=0;item[1]=1;
        CHECK(stopped(run_effect(&w,&r,0)));
        printf("partner held item: stops at %08lx: %s\n",(unsigned long)stop_pc(&r),why(&r));
        uint32_t v=0;
        const uint8_t *ap=c3_battle_arena_bytes(&w.state->arena,w.state->floor+0xA4BC,2);
        CHECK(r.script.failure_pc==0x80375FC5u&&strstr(why(&r),"fn_8022B5C8"));
        CHECK(c3_battle_script_read(&r.script,0x8047B627u,1,&v)&&v==8);
        CHECK(ap&&ap[0]==0&&ap[1]==1&&w.state->msgctrl[0x29]==C3_MSGREF_ITEM_NAME(1));
    }

    /* A guaranteed secondary effect pending (work byte 3 bit 0x80, set just
     * before 0x15) on a hit needs fn_802249B8: refused, with no draw. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        memset(&r,0,sizeof r);r.handlers.state=w.state;
        r.handlers.present=present;r.handlers.present_user=&r.presentation;r.handlers.present_move=present_move;
        r.handlers.effect_signal=effect_signal;
        r.handlers.damage=(C3BattleDamageTables){w.script_pack.stage_ratios,w.script_pack.stage_ratio_count,
            w.script_pack.item_types,w.pack->sections[C3_PACK_T44].bytes,w.pack->sections[C3_PACK_T44].count};
        r.handlers.critical=w.script_pack.critical;r.handlers.critical_count=w.script_pack.critical_count;
        C3BattleScriptServices sv=c3_battle_handler_services(&r.handlers);
        CHECK(c3_battle_script_init(&r.script,&w.script_pack,&sv));
        CHECK(c3_battle_script_call(&r.script,w.script_pack.effects[0],1,1));
        C3BattleScriptStatus st=C3_SCRIPT_RUNNING;uint64_t draws=0;
        for(unsigned i=0;i<64&&st!=C3_SCRIPT_FAILED;++i){
            if(r.script.pc==0x80375FB7u){c3_battle_script_write(&r.script,0x80478D7Bu,1,0x85);draws=w.state->rng.draws;}
            st=c3_battle_script_step(&r.script);
            if(at_end(&r))break;
            if(st==C3_SCRIPT_WAITING)CHECK(c3_battle_script_resume(&r.script));
        }
        printf("guaranteed effect: %s\n",why(&r));
        CHECK(strstr(why(&r),"guaranteed secondary effect")!=NULL&&w.state->rng.draws==draws);
    }

    /* Without the battle-start enemy arrays the HP update refuses. */
    battle_reset(&w,1,0,0,0,33,0);
    memset(c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_out(w.state,1,0)+0x6B0,48),0,48);
    CHECK(stopped(run_effect(&w,&r,0)));
    printf("no enemy arrays: %s\n",why(&r));
    CHECK(strstr(why(&r),"AI damage memory not created")!=NULL);

    /* Protect on the defender stops at the accuracy check's reflect test. */
    battle_reset(&w,1,0,0,0,33,0);
    {
        const uint8_t *row=w.state->conditions.rows+0x2B*C3_CONDITION_ROW_BYTES;
        CHECK(((uint16_t)row[8]<<8|row[9])==0xD8);
        uint8_t *slot=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_out(w.state,0,0)+0x15C+row[1]*16u,2);
        put16(slot,0x2B);
        CHECK(stopped(run_effect(&w,&r,0))&&strstr(why(&r),"Protect"));
    }

    /* An asleep attacker: the canceller stops at its sleep step, named. */
    battle_reset(&w,1,0,0,0,33,0);
    uint8_t *sleeper=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,1,0)+4,0x138);
    put16(sleeper+0x64,8);
    CHECK(stopped(run_effect(&w,&r,0)));
    CHECK(r.script.failure_pc==0x80375F8Eu&&strstr(why(&r),"sleep"));
    uint8_t *poke;

    /* A fainted attacker: hit marker 0x80000 and the jump to lbl_80375FBF. */
    battle_reset(&w,1,0,0,0,33,0);
    poke=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,1,0)+4,0x138);
    put16(poke+0x8A,0);
    CHECK(stopped(run_effect(&w,&r,0)));
    CHECK(c3_battle_script_read(&r.script,0x8047B618u,4,&v)&&(v&0x80000u));
    printf("fainted attacker: continues at %08lx: %s\n",(unsigned long)stop_pc(&r),why(&r));
    CHECK(stop_pc(&r)>=0x80375FBFu);

    /* Battle already decided: the canceler ends the script (state 2). */
    battle_reset(&w,1,0,0,0,33,0);w.state->result=2;
    CHECK(stopped(run_effect(&w,&r,0))&&strstr(why(&r),"reached move end")&&r.script.frames[0].phase==C3_SCRIPT_PHASE_CLEANUP);

    /* No PP left in the move's slot. */
    battle_reset(&w,1,0,0,0,33,0);
    poke=c3_battle_arena_bytes(&w.state->arena,c3_battle_state_fight_pokemon(w.state,1,0)+4,0x138);
    poke[0x78+2]=0;
    CHECK(stopped(run_effect(&w,&r,0))&&strstr(why(&r),"no PP"));

    puts("battle_handlers OK");
    return 0;
}
