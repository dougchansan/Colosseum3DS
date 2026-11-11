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

/* ---- entry fn_802342CC --------------------------------------------------- */

static int has(const C3BattleState *s,uint32_t fo,uint16_t id,int *fault)
{return c3_battle_condition_check(s,fo,id,fault)==1;}
/* fightOutPokemonInitFightActionBuff: the battler's action (0xFE, 0x30
 * bytes: everything but the pad, queue index -1), FightWaza (0xD9) and
 * FightItem (0xE5: u16 0, u16 0, s32 -1, u32 0). */
static int init_action_buff(C3BattleState *s,uint32_t fo)
{
    uint32_t action=get(s,fo,0xFE);
    if(!action)return 1;
    uint8_t *a=c3_battle_arena_bytes(&s->arena,action,0x30);
    if(!a)return 0;
    a[0]=a[1]=0;memset(a+4,0,0x18);memset(a+0x20,0,0x10);
    a[0x1C]=a[0x1D]=a[0x1E]=a[0x1F]=0xFF;
    if(!c3_battle_waza_init(s,get(s,fo,0xD9)))return 0;
    uint32_t item=get(s,fo,0xE5);
    uint8_t *it=item?c3_battle_arena_bytes(&s->arena,item,12):NULL;
    if(item&&!it)return 0;
    if(it){memset(it,0,12);it[4]=it[5]=it[6]=it[7]=0xFF;}
    return 1;
}
/* fightTrainerGetNoActionFightOutPokemonPtr: a valid battler whose action
 * is not set (fightActionCheckValid: kind and data pointer). */
static uint32_t no_action_battler(const C3BattleState *s,uint32_t trainer,unsigned i,int *fault)
{
    uint32_t fo=trainer+C3_RUNTIME_FIGHT_OUT_OFFSET+i*C3_FIGHT_OUT_POKEMON_BYTES;
    if(!c3_battle_fight_out_check_valid(s,fo,fault))return 0;
    uint32_t action=get(s,fo,0xFE);
    const uint8_t *a=action?c3_battle_arena_bytes(&s->arena,action,0x30):NULL;
    if(!a)return 0;
    return (be16(a)&&be32(a+8))?0:fo;
}
/* fightOutPokemonCheckCanOutOkWazaBanme(fo, slot, 0, NULL): 0 when the move
 * in the slot can be chosen. Disable (0x29), Torment (0x1B), Taunt (0x30)
 * and a Choice Band are refused (-1); Imprison (0x27 on an opposing battler
 * in battle that knows the move), an empty slot (0 or 0x163) and no PP block
 * it. */
static int move_blocked(C3BattleState *s,uint32_t fo,unsigned slot,int *fault)
{
    uint32_t pokemon=pokemon_of(s,fo);
    const uint8_t *p=pokemon?c3_battle_arena_bytes(&s->arena,pokemon,C3_BATTLE_POKEMON_BYTES):NULL;
    if(!p)return -1;
    uint16_t move=be16(p+0x78+4*slot),effect=0;
    if(has(s,fo,0x29,fault)||has(s,fo,0x1B,fault)||has(s,fo,0x30,fault))return -1;
    if(!c3_battle_state_held_item_effect(s,fo,&effect))return -1;
    int result=0;
    uint32_t list[8];unsigned n=0;
    if(!c3_battle_state_valid_fight_outs(s,list,&n))return -1;
    uint32_t own=c3_battle_state_battler_side(s,fo);
    for(unsigned i=0;i<n&&!result;++i){                            /* fightFloorCheckHuuinWazaFightOutPokemon */
        if(!c3_battle_fight_out_check_fight_out(s,list[i],fault))continue;
        if(c3_battle_state_battler_side(s,list[i])==own||!has(s,list[i],0x27,fault))continue;
        const uint8_t *q=c3_battle_arena_bytes(&s->arena,pokemon_of(s,list[i]),C3_BATTLE_POKEMON_BYTES);
        for(unsigned k=0;q&&k<4;++k)if(be16(q+0x78+4*k)==move)result=4;
    }
    if(!move||move==0x163||!p[0x7A+4*slot])result=6;              /* pokemonWazaCheckValid, PP */
    return result;
}
/* fightOutPokemonCheckFightActionWazaSelect(fo, 1): 0 when the battler
 * chooses its move; a battler with no usable move (Struggle) or under
 * Encore (0x2A) would have its action set here: refused. */
static int must_choose_move(C3BattleState *s,C3BattleScript *sc,uint32_t fo,int *choose)
{
    int fault=0,usable=0;
    *choose=1;
    if(!c3_battle_fight_out_check_fight_out(s,fo,&fault))return !fault;
    uint32_t pokemon=pokemon_of(s,fo);
    const uint8_t *p=pokemon?c3_battle_arena_bytes(&s->arena,pokemon,C3_BATTLE_POKEMON_BYTES):NULL;
    if(!p)return refuse(sc,"AI battler Pokemon");
    for(unsigned k=0;k<4;++k){                                      /* fightOutPokemonGetOutOkWazaBanmeAry */
        uint16_t move=be16(p+0x78+4*k);
        if(!move||move==0x163||move==0x165)continue;
        int b=move_blocked(s,fo,k,&fault);
        if(b<0)return refuse(sc,"move usability (Disable, Torment, Taunt or a held item)");
        if(!b)++usable;
    }
    if(!usable)return refuse(sc,"Struggle action (fightOutPokemonCheckFightActionWazaSelect)");
    if(has(s,fo,0x2A,&fault))return refuse(sc,"Encore action (condition 0x2A)");
    return !fault?1:refuse(sc,"AI battler conditions");
}
/* fightOutPokemonGetNowNimbleness: speed (0x8C), Swift Swim 0x21 in rain /
 * Chlorophyll 0x22 in sun doubled, the speed stage (0xEA, lbl_80375D10),
 * x1.1 with the speed badge (floor 0x1D and the hero's badge 3, +0xA92),
 * paralysis (condition 5) quartered. Held items are refused (-1). */
static int64_t now_speed(C3BattleHandlers *h,uint32_t fo,uint8_t badge,int weather,int *fault)
{
    C3BattleState *s=h->state;
    uint32_t pokemon=pokemon_of(s,fo);
    const uint8_t *p=pokemon?c3_battle_arena_bytes(&s->arena,pokemon,C3_BATTLE_POKEMON_BYTES):NULL;
    uint16_t effect=0;
    if(!p)return 0;
    if(!c3_battle_state_held_item_effect(s,fo,&effect))return -1;
    uint16_t ability=(uint16_t)get(s,fo,0x100);
    uint8_t stage=(uint8_t)get(s,fo,0xEA);
    uint32_t speed=be16(p+0x96);
    if((ability==0x21&&weather==2)||(ability==0x22&&weather==1))speed<<=1;
    const uint8_t *r=h->damage.stage_ratios&&stage<h->damage.stage_count?h->damage.stage_ratios+2u*stage:NULL;
    speed=r&&r[1]?speed*r[0]/r[1]:0;                              /* fightAbicntDoKakeWaru */
    uint32_t original=get(s,fo,0xD5),trainer=0;
    for(unsigned side=0;side<2&&!trainer;++side)for(unsigned t=0;t<2;++t){
        uint32_t tr=s->floor+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES+C3_FIGHT_SIDE_TRAINER_OFFSET+
            t*C3_BATTLE_RUNTIME_TRAINER_BYTES;
        for(unsigned k=0;k<6;++k)if(tr+0xB1Cu+k*0x154u==original)trainer=tr;
    }
    const uint8_t *hero=trainer?c3_battle_arena_bytes(&s->arena,trainer+4u+0xA92u,1):NULL;
    if(badge==1&&hero&&hero[0]==1)speed=speed*110u/100u;
    if(has(s,fo,5,fault))speed>>=2;
    return speed;
}
/* fightFloorSortFightOutPokemonPtrArySub(0, list, 8, 0): by speed, ties by
 * one draw (fightFloorCmpfightOutPokemonNimbleness, priority off). */
static int sort_by_speed(C3BattleHandlers *h,C3BattleScript *sc,uint32_t list[8])
{
    C3BattleState *s=h->state;
    uint32_t badge=0;int weather=0,fault=0;
    if(!c3_battle_floor_value(s,0x1D,&badge))return refuse(sc,"floor field 0x1D");
    if(!c3_battle_state_ability_on_any_battler(s,13)&&!c3_battle_state_ability_on_any_battler(s,0x4D)&&
       !c3_battle_state_weather(s,0,&weather))return refuse(sc,"weather");
    for(unsigned i=0;i<7;++i)for(unsigned j=i+1;j<8;++j){
        if(!list[i]&&!list[j])continue;
        if(!list[i]){list[i]=list[j];list[j]=0;continue;}
        if(!list[j])continue;
        int64_t a=now_speed(h,list[i],(uint8_t)badge,weather,&fault),b=now_speed(h,list[j],(uint8_t)badge,weather,&fault);
        if(a<0||b<0)return refuse(sc,"held item speed effect");
        int first=a>b;
        if(a==b){
            uint16_t tick=0;
            if(!c3_battle_rng_next16(&s->rng,&tick))return refuse(sc,"random stream");
            first=tick&1;
        }
        if(!first){uint32_t t=list[i];list[i]=list[j];list[j]=t;}
    }
    return !fault?1:refuse(sc,"speed records");
}
/* ---- move choice fn_8023A308 ---------------------------------------------- */

/* fn_8023A118 with a trainer and a Pokemon: the debug message variables of
 * the AI's random choices (0x0D / 0x0E the texts, 0x13 the trainer's name,
 * 0x14 the Pokemon's nickname (0x77), 0x2F the value); the message itself
 * needs the debug menu (fn_80008164). */
static void debug_vars(C3BattleState *s,uint16_t text1,uint16_t text2,uint32_t trainer,uint32_t pokemon,uint32_t value)
{
    c3_battle_msgctrl_set(s,0x0D,C3_MSGREF_MESSAGE(text1));
    c3_battle_msgctrl_set(s,0x0E,C3_MSGREF_MESSAGE(text2));
    if(trainer)c3_battle_msgctrl_set(s,0x13,trainer+4u);          /* fightTrainerGetNamePtr */
    if(pokemon)c3_battle_msgctrl_set(s,0x14,pokemon+0x2Eu);
    c3_battle_msgctrl_set(s,0x2F,value);
}
/* fn_802367CC(trainer, fo, moves, slots, 1): the battler's moves in slot
 * order that can be chosen (fightOutPokemonCheckCanOutOkWazaBanme == 0), with
 * their slots; up to 10 entries, the rest 0 / -1. */
static int usable_moves(C3BattleState *s,C3BattleScript *sc,uint32_t fo,uint16_t moves[10],int16_t slots[10],unsigned *count)
{
    int fault=0;
    *count=0;
    for(unsigned i=0;i<10;++i){moves[i]=0;slots[i]=-1;}
    uint32_t pokemon=pokemon_of(s,fo);
    const uint8_t *p=pokemon?c3_battle_arena_bytes(&s->arena,pokemon,C3_BATTLE_POKEMON_BYTES):NULL;
    if(!p)return refuse(sc,"AI battler Pokemon");
    for(unsigned k=0;k<4;++k){
        uint16_t move=be16(p+0x78+4*k);
        if(!move)continue;
        int b=move_blocked(s,fo,k,&fault);
        if(b<0)return refuse(sc,"move usability (Disable, Torment, Taunt or a held item)");
        if(b)continue;
        moves[*count]=move;slots[*count]=(int16_t)k;++*count;
    }
    return !fault?1:refuse(sc,"AI battler conditions");
}
/* fn_8022B2CC(fo, move, fight type, fn_8023C368, 1, 0, -1): the target the
 * move's category (data 5) imposes, or 0 to keep the AI's. Category 0 with
 * at least two other battlers in battle asks fn_8023C368 (always 0); 1, 4, 6
 * and 7 take the first opposing battler in battle (target kind 0xF, else its
 * partner, kind 0xE); 2 and 5 the user. The random targets (category 3, or
 * category 0 against a lone battler) and Curse's category are refused. */
static int target_override(C3BattleState *s,C3BattleScript *sc,uint32_t fo,uint16_t move,uint32_t *out)
{
    uint32_t category=0;uint16_t others=0;int fault=0;
    *out=0;
    if(move==0xAE)return refuse(sc,"Curse target category (fightOutPokemonIsZokuseiDataId)");
    if(!c3_battle_waza_status_get(&s->arena,&s->moves,0,move,5,0,&category))return refuse(sc,"move target category");
    if(!c3_battle_state_valid_fight_out_count(s,0,fo,1,&others))return refuse(sc,"battler count");
    switch((uint8_t)category){
    case 0:
        if(others<2)return refuse(sc,"random target (fightFloorGetFightOutPokemonPtrRandom)");
        return 1;                                                   /* fn_8023C368 */
    case 1:case 4:case 6:case 7:
        if(!c3_battle_state_target_now(s,0xF,fo,out))return refuse(sc,"target kind 0xF");
        if(!c3_battle_fight_out_check_fight_out(s,*out,&fault)&&!c3_battle_state_target_now(s,0xE,*out,out))
            return refuse(sc,"target kind 0xE");
        return !fault?1:refuse(sc,"target records");
    case 3:
        return refuse(sc,"random target (fightFloorGetFightOutPokemonPtrRandom)");
    case 2:case 5:
        *out=fo;
        return 1;
    default:
        return 1;
    }
}
/* fightOutPokemonCreateFightActionAttackWaza(fo, 0, 0x13, 0, lbl_80375CA8,
 * move, target id, slot, 0): the FightWaza (fightWazaCreate), then the
 * action (fightActionCreate: cleared, kind 0x13, buff 0, actor fo, data
 * lbl_80375CA8, whose first entry is kind 0x13 buff 0, so fightActionSetData
 * keeps it; buff data ID the move). */
#define AI_ATTACK_ACTION_DATA 0x80375CA8u
static int create_attack(C3BattleState *s,uint32_t fo,uint16_t move,uint16_t target,int8_t slot)
{
    uint32_t fw=get(s,fo,0xD9),action=get(s,fo,0xFE);
    if(!fw||!action||!c3_battle_waza_create(s,fw,slot,move,target,0))return 0;
    uint8_t *a=c3_battle_arena_bytes(&s->arena,action,0x30);
    if(!a)return 0;
    a[0]=0;a[1]=0x13;memset(a+4,0,0x18);memset(a+0x20,0,0x10);
    a[0x1C]=a[0x1D]=a[0x1E]=a[0x1F]=0xFF;
    const uint32_t words[3][2]={{8,AI_ATTACK_ACTION_DATA},{0x10,move},{0x14,fo}};
    for(unsigned i=0;i<3;++i){
        uint8_t *w=a+words[i][0];uint32_t v=words[i][1];
        w[0]=(uint8_t)(v>>24);w[1]=(uint8_t)(v>>16);w[2]=(uint8_t)(v>>8);w[3]=(uint8_t)v;
    }
    return 1;
}
/* fn_8023A308(trainer, fo, fight type): with AI bytes 0x30 and 0x31 both 1
 * (random target, random move), one draw picks an opposing battler in
 * battle and one draw a usable move, each after its debug variables; the
 * move's category may override the target; the move is set as the
 * battler's attack action. The scored choices (fn_8023B498, fn_8023A740)
 * are refused, as are the paths where the original sets no action. */
static int choose_move(C3BattleState *s,C3BattleScript *sc,uint32_t trainer,uint32_t fo)
{
    uint16_t moves[10];int16_t slots[10];unsigned move_count=0,foe_count=0;
    uint32_t foes[8],random_target=0,random_move=0,fight_type=0,target=0,forced=0;
    uint16_t tick=0,id=0;
    if(!usable_moves(s,sc,fo,moves,slots,&move_count))return 0;
    if(!move_count)return refuse(sc,"AI move choice without a usable move");
    if(!battlers_of(s,trainer,0,foes,&foe_count))return refuse(sc,"AI battler lists");
    if(!foe_count)return refuse(sc,"AI move choice without an opposing battler");
    if(!c3_battle_trainer_ai_value(s,trainer,0x30,&random_target)||!c3_battle_trainer_ai_value(s,trainer,0x31,&random_move)||
       !c3_battle_floor_value(s,0x14,&fight_type))return refuse(sc,"AI data (0x30 / 0x31)");
    if((uint8_t)random_target!=1)return refuse(sc,"AI target choice (fn_8023B498)");
    uint32_t pokemon=pokemon_of(s,fo);
    debug_vars(s,0xEC2C,0xEC2D,trainer,pokemon,0);
    if(!c3_battle_rng_next16(&s->rng,&tick))return refuse(sc,"random stream");
    target=foes[tick%foe_count];
    if((uint8_t)random_move!=1)return refuse(sc,"AI move choice by value (fn_8023A740)");
    debug_vars(s,0xEC2C,0xEC32,trainer,pokemon,0);
    if(!c3_battle_rng_next16(&s->rng,&tick))return refuse(sc,"random stream");
    unsigned k=tick%move_count;
    if(!target_override(s,sc,fo,moves[k],&forced))return 0;
    if(forced)target=forced;
    if(!c3_battle_state_relative_target_id(s,target,(uint16_t)fight_type,&id))return refuse(sc,"relative target ID");
    if(!create_attack(s,fo,moves[k],id,(int8_t)slots[k]))return refuse(sc,"attack action");
    return 1;
}
/* ---- item choice fightTrainerAiSelectFightActionItem --------------------- */

/* fightTrainerAiSelectFightActionItem(trainer, fo, fight type) returns 0
 * without an action when the trainer's usable item list is empty
 * (fightTrainerGetTemotiNormalItemDataIdAry(trainer, items, 20, 1): the
 * normal-item pocket of its hero record, trainer field 0x44 = +4, 20 entries
 * at +0x780, less the items its other battlers already chose this turn).
 * A computer trainer's pocket holds the items of its trainer row (+0x14, 8
 * IDs) added at creation. An empty pocket gives an empty list whatever was
 * chosen, so this returns 1 with no action set; any entry in the pocket is
 * refused, since the scoring of items is not lowered. */
#define AI_HERO_NORMAL_ITEMS 0x780u
static int item_choice(C3BattleState *s,C3BattleScript *sc,uint32_t trainer)
{
    const uint8_t *pocket=c3_battle_arena_bytes(&s->arena,trainer+4u+AI_HERO_NORMAL_ITEMS,20u*4u);
    if(!pocket)return refuse(sc,"trainer item pocket");
    for(unsigned i=0;i<20u*4u;++i)
        if(pocket[i])return refuse(sc,"AI item choice with items in the bag (fightTrainerAiSelectFightActionItem)");
    return 1;
}
/* One percentage roll of fn_802342CC: the debug message variables (0x0D the
 * roll's text, 0x13 the trainer's name, 0x2F the chance), then one draw. */
static int roll(C3BattleState *s,C3BattleScript *sc,uint32_t trainer,uint32_t selector,uint16_t text,int *selected)
{
    uint32_t chance=0;uint16_t tick=0;
    if(!c3_battle_trainer_ai_value(s,trainer,selector,&chance))return refuse(sc,"AI data");
    if(!c3_battle_rng_next16(&s->rng,&tick))return refuse(sc,"random stream");
    c3_battle_msgctrl_set(s,0x0D,C3_MSGREF_MESSAGE(text));
    c3_battle_msgctrl_set(s,0x13,trainer+4u);                     /* fightTrainerGetNamePtr */
    c3_battle_msgctrl_set(s,0x2F,(uint8_t)chance);
    *selected=(int32_t)(uint8_t)chance>(int32_t)(tick%100u);
    return 1;
}
int c3_battle_ai_select(C3BattleHandlers *h,C3BattleScript *sc,uint32_t trainer,uint32_t order[8])
{
    C3BattleState *s=h->state;
    uint32_t count=0,items=0;
    int fault=0,selected=0;
    memset(order,0,8*sizeof *order);
    if(!c3_battle_floor_value(s,0x18,&count)||!c3_battle_floor_value(s,0x20,&items))return refuse(sc,"floor fields");
    if((uint16_t)count>2)return refuse(sc,"more than two active slots");
    if(!c3_battle_ai_prepare(h,sc,trainer))return 0;
    for(unsigned i=0;i<(uint16_t)count;++i){
        uint32_t fo=trainer+C3_RUNTIME_FIGHT_OUT_OFFSET+i*C3_FIGHT_OUT_POKEMON_BYTES;
        if(!c3_battle_fight_out_check_valid(s,fo,&fault))continue;
        /* fightOutPokemonCheckFightActionSelect(fo, 1) */
        if(!c3_battle_fight_out_check_fight_out(s,fo,&fault))continue;
        if(has(s,fo,0x12,&fault)||has(s,fo,0x22,&fault))return refuse(sc,"locked action (conditions 0x12 / 0x22)");
        if(!init_action_buff(s,fo))return refuse(sc,"action buffer");
    }
    if(!roll(s,sc,trainer,0x26,0xEC04,&selected))return 0;
    for(unsigned i=0;selected&&i<(uint16_t)count;++i)
        if(no_action_battler(s,trainer,i,&fault))return refuse(sc,"AI switch choice (fightTrainerAiSelectFightActionIrekae)");
    if((uint8_t)items==1){
        if(!roll(s,sc,trainer,0x27,0xEC46,&selected))return 0;
        for(unsigned i=0;selected&&i<(uint16_t)count;++i)
            if(no_action_battler(s,trainer,i,&fault)&&!item_choice(s,sc,trainer))return 0;
    }
    if(!roll(s,sc,trainer,0x25,0xEC47,&selected))return 0;
    for(unsigned i=0;selected&&i<(uint16_t)count;++i)
        if(no_action_battler(s,trainer,i,&fault))return refuse(sc,"AI action (fn_8024E534)");
    c3_battle_msgctrl_set(s,0x0D,C3_MSGREF_MESSAGE(0xEC2C));
    c3_battle_msgctrl_set(s,0x13,trainer+4u);
    c3_battle_msgctrl_set(s,0x2F,100);
    unsigned n=0;
    for(unsigned i=0;i<(uint16_t)count;++i){
        uint32_t fo=no_action_battler(s,trainer,i,&fault);
        int choose=0;
        if(!fo)continue;
        if(!must_choose_move(s,sc,fo,&choose))return 0;
        if(choose)order[n++]=fo;
    }
    if(fault)return refuse(sc,"AI battler records");
    if(n&&!sort_by_speed(h,sc,order))return 0;
    for(unsigned i=0;i<n;++i)if(order[i]&&!choose_move(s,sc,trainer,order[i]))return 0;
    return 1;
}
