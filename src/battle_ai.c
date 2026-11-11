/* Computer trainer AI, lowered for Willie's battle (battle_ai.h). */
#include "colosseum3ds/battle_ai.h"
#include "colosseum3ds/battle_fight_out.h"
#include "colosseum3ds/battle_runtime_trainer.h"
#include <stdio.h>
#include <string.h>

static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int refuse(C3BattleScript *sc,const char *what)
{if(sc->status!=C3_SCRIPT_FAILED)c3_battle_script_fail(sc,"Not lowered yet: %s",what);return 0;}
static uint32_t get(const C3BattleState *s,uint32_t obj,uint32_t sel)
{uint32_t v=0;(void)c3_battle_fight_status_get(&s->arena,obj,sel,0,&v);return v;}
static uint32_t pokemon_of(const C3BattleState *s,uint32_t fo)                 /* fightOutPokemonGetPokemonPtr */
{uint32_t fp=fo?get(s,fo,0xD6):0;return fp?get(s,fp,0xCC):0;}
static const uint8_t *move_row(const C3BattleState *s,uint16_t move)
{return s->moves.rows&&move<s->moves.count?s->moves.rows+(uint32_t)move*s->moves.stride:NULL;}
static const uint8_t *species_row(const C3BattleState *s,uint16_t species)
{return s->species.rows&&species<s->species.count?s->species.rows+(uint32_t)species*C3_SPECIES_ROW_BYTES:NULL;}

/* ---- damage routines (fight_trainer_ai_waza_damage.c) ------------------- */

/* Routine addresses in lbl_8035C430 (symbols.txt). */
#define AI_DAMAGE_000 0x802538C0u
#define AI_DAMAGE_031 0x80253400u
#define AI_DAMAGE_076 0x80252B44u
#define AI_DAMAGE_197 0x80250E00u
#define AI_DAMAGE_121 0x8025228Cu

/* fn_80236BFC(trainer, target, id): the target's condition when the AI data
 * lets the trainer see conditions (byte 0x24). */
static int ai_sees_condition(C3BattleState *s,uint32_t trainer,uint32_t target,uint16_t id)
{
    uint32_t seen=0;
    if(!c3_battle_trainer_ai_value(s,trainer,0x24,&seen))return -1;
    return (uint8_t)seen==1?c3_battle_condition_check(s,target,id,NULL)==1:0;
}
int c3_battle_ai_damage_estimate(C3BattleHandlers *h,C3BattleScript *sc,uint32_t trainer,uint32_t attacker,
                                 uint16_t move,uint32_t target,uint32_t *damage)
{
    C3BattleState *s=h->state;
    uint32_t enabled=0;
    *damage=0;
    if(!c3_battle_trainer_ai_value(s,trainer,0x2D,&enabled))return refuse(sc,"AI data (0x2D)");
    if((uint8_t)enabled!=1)return 1;
    const uint8_t *row=move_row(s,move);
    if(!row)return refuse(sc,"AI damage move row");
    uint16_t index=be16(row+0x1C);                                 /* wazaDataBiosGet...FuncPtr */
    uint32_t routine=0;
    if(index<h->ai_move_count&&h->ai_moves)routine=be32(h->ai_moves+(uint32_t)index*0x14u+8u);
    /* fightTrainerAiWazaDamageNull when the slot is empty: not lowered. */
    switch(routine){
    case AI_DAMAGE_000:case AI_DAMAGE_031:case AI_DAMAGE_076:case AI_DAMAGE_197:{
        if(!c3_battle_handlers_normal_damage(h,sc,move,attacker,target,0,damage))return 0;
        int seen=ai_sees_condition(s,trainer,target,0x21);
        if(seen<0)return refuse(sc,"AI data (0x24)");
        if(seen&&move==0x39)*damage<<=1;                           /* _helper1: doubled */
        return 1;
    }
    case AI_DAMAGE_121:
        return c3_battle_handlers_normal_damage(h,sc,move,attacker,target,1,damage);
    default:{
        char what[64];
        snprintf(what,sizeof what,"AI damage routine %08lx (move %u)",(unsigned long)routine,(unsigned)move);
        return refuse(sc,what);
    }
    }
}

/* ---- preparation (fn_80234A0C) ------------------------------------------- */

/* fightFloorGetFightTrainerFightOutPokemonPtrAry(0, trainer, out, allies, 1):
 * the battlers in battle (valid-loop order) on the trainer's side (allies)
 * or not (fightTrainerIsAllyFightTargetPtr: fightTargetGetPtr kind 2). */
static int battlers_of(C3BattleState *s,uint32_t trainer,int allies,uint32_t out[8],unsigned *count)
{
    uint32_t list[8],own=0;unsigned n=0;
    int fault=0;
    *count=0;
    if(!c3_battle_state_valid_fight_outs(s,list,&n)||!c3_battle_state_target_now(s,2,trainer,&own))return 0;
    for(unsigned i=0;i<n;++i){
        uint32_t side=0;
        if(!c3_battle_fight_out_check_fight_out(s,list[i],&fault))continue;
        if(!c3_battle_state_target_now(s,2,list[i],&side))return 0;
        if((side==own)!=(allies!=0))continue;
        out[(*count)++]=list[i];
    }
    return !fault;
}
/* fn_80234A0C_wazaType with AI byte 0x2A: type 9 (none) for the
 * fixed-damage moves 0xA5, 0x164, 0xF8, 0x161, else the move's type; 9
 * without the type check. */
static int ai_move_type(C3BattleState *s,C3BattleScript *sc,uint32_t trainer,uint16_t move,uint16_t *type)
{
    uint32_t check=0;
    if(!c3_battle_trainer_ai_value(s,trainer,0x2A,&check))return refuse(sc,"AI data (0x2A)");
    *type=9;
    if((uint8_t)check!=1)return 1;
    if(move==0xA5||move==0x164||move==0xF8||move==0x161)return 1;
    if(move==0xED)return refuse(sc,"Hidden Power type (pokemonGetMezamerupower)");
    if(move==0x137)return refuse(sc,"Weather Ball type (AI weather view)");
    const uint8_t *row=move_row(s,move);
    if(!row)return refuse(sc,"AI move row");
    uint32_t t=0;
    if(!c3_battle_waza_status_get(&s->arena,&s->moves,0,move,3,0,&t))return refuse(sc,"move type");
    *type=(uint16_t)t;
    return 1;
}
static int type_id(const C3BattleState *s,uint16_t move,uint8_t id)     /* wazaIsWazaTypeId */
{
    const uint8_t *row=move_row(s,move);
    if(!id||!row)return 0;
    for(unsigned i=0;i<3;++i)if(row[0x34+i]==id)return 1;
    return 0;
}

int c3_battle_ai_prepare(C3BattleHandlers *h,C3BattleScript *sc,uint32_t trainer)
{
    C3BattleState *s=h->state;
    uint32_t party[8],opponents[8],move_score[8]={0},stat_score[8]={0};
    unsigned party_n=0,opp_n=0;
    if(!h||!sc||!trainer)return 0;
    /* fightTrainerInitEnemyPokemonFightOutStatus(trainer, 1) */
    for(unsigned i=0;i<12;++i){
        uint8_t *e=c3_battle_arena_bytes(&s->arena,trainer+0x27F4u+i*0x14u,0x14);
        if(!e)return refuse(sc,"enemy registry");
        if((int16_t)be16(e)>=0)memset(e+0x0E,0,5);
    }
    if(!battlers_of(s,trainer,0,party,&party_n)||!battlers_of(s,trainer,1,opponents,&opp_n))
        return refuse(sc,"AI battler lists");
    uint16_t min_hp=0xFFFF,min_def=0xFFFF;uint8_t min_level=0xFF;
    int32_t max_move=-0xFFFF,max_stat=-0xFFFF;
    for(unsigned i=0;i<party_n;++i){
        uint32_t fo=party[i],fp=get(s,fo,0xD5);
        if((int16_t)(uint16_t)get(s,fp,0xCE)<0)continue;           /* fightOutPokemonGetFightEntryId */
        uint32_t pokemon=pokemon_of(s,fo);
        const uint8_t *p=pokemon?c3_battle_arena_bytes(&s->arena,pokemon,C3_BATTLE_POKEMON_BYTES):NULL;
        if(!p)return refuse(sc,"AI battler Pokemon");
        const uint8_t *sp=species_row(s,be16(p));
        uint16_t hp=be16(p+0x8C)?(uint16_t)((uint32_t)be16(p+0x8A)*100u/be16(p+0x8C)):0;   /* pokemonGetNowHpPercentage */
        if(min_hp>hp)min_hp=hp;
        if(min_level>p[0x60])min_level=p[0x60];
        uint16_t def=sp?(uint16_t)(be16(sp+0x88)+be16(sp+0x8C)):0;  /* data 5 + 7 */
        if(min_def>def)min_def=def;
        for(unsigned k=0;k<6;++k)stat_score[i]+=sp?be16(sp+0x84+2*k):0;   /* data 3..8 */
        stat_score[i]*=p[0x60];
        if(max_stat<(int32_t)stat_score[i])max_stat=(int32_t)stat_score[i];
        uint16_t moves[4];unsigned move_n=0;                         /* fn_80234A0C_wazaList */
        for(unsigned k=0;k<4;++k){uint16_t m=be16(p+0x78+4*k);if(m)moves[move_n++]=m;}
        for(unsigned o=0;o<opp_n;++o)
            for(unsigned k=0;k<move_n;++k){
                uint16_t move=moves[k],type=9;
                if(move==0||move==0x165)continue;
                if(!ai_move_type(s,sc,trainer,move,&type))return 0;
                if(type==9||!type_id(s,move,1))continue;
                uint32_t damage=0;
                if(!c3_battle_ai_damage_estimate(h,sc,trainer,fo,move,opponents[o],&damage))return 0;
                move_score[i]+=damage;
            }
        if(max_move<(int32_t)move_score[i])max_move=(int32_t)move_score[i];
    }
    for(unsigned i=0;i<party_n;++i){
        uint32_t fo=party[i],fp=get(s,fo,0xD5);
        int16_t entry=(int16_t)(uint16_t)get(s,fp,0xCE);
        if(entry<0)continue;
        uint32_t pokemon=pokemon_of(s,fo);
        const uint8_t *p=c3_battle_arena_bytes(&s->arena,pokemon,C3_BATTLE_POKEMON_BYTES);
        const uint8_t *sp=species_row(s,be16(p));
        uint16_t hp=be16(p+0x8C)?(uint16_t)((uint32_t)be16(p+0x8A)*100u/be16(p+0x8C)):0;
        uint16_t def=sp?(uint16_t)(be16(sp+0x88)+be16(sp+0x8C)):0;
        if(min_hp>=hp)c3_battle_trainer_enemy_flag(s,trainer,entry,0x52,1);
        if(min_level>=p[0x60])c3_battle_trainer_enemy_flag(s,trainer,entry,0x53,1);
        if(min_def>=def)c3_battle_trainer_enemy_flag(s,trainer,entry,0x54,1);
        if(max_move<=(int32_t)move_score[i])c3_battle_trainer_enemy_flag(s,trainer,entry,0x55,1);
        if(max_stat<=(int32_t)stat_score[i])c3_battle_trainer_enemy_flag(s,trainer,entry,0x56,1);
    }
    return 1;
}
