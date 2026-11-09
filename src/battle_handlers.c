/* Move-script battle-logic opcodes over the battle state (guarded lowering).
 *   0x00 WS_HITCHECK  fight_range_80211A00.c (attack canceler)
 *   0x01 fn_802284B0  accuracy check (with fn_80228DAC, fn_80229934,
 *                     fn_802284B0_hit, fightWazaIsHit, fn_8022DCB8,
 *                     fightFloorGetNowTenkouDataId)
 *   0x02 fn_802282D8  attack string
 *   0x03 fn_8022808C  PP reduce
 * Script variables (hit marker 0x8047B618, ...) are the interpreter's
 * windows; battle records are the state's arena. */
#include "colosseum3ds/battle_handlers.h"
#include <stdio.h>
#include <string.h>

#define HIT_MARKER 0x8047B618u

static uint32_t get(const C3BattleState *s,uint32_t obj,uint32_t sel)
{uint32_t v=0;(void)c3_battle_fight_status_get(&s->arena,obj,sel,0,&v);return v;}
static uint32_t waza(const C3BattleState *s,uint32_t fw,uint16_t move,uint32_t status)
{uint32_t v=0;(void)c3_battle_waza_status_get(&s->arena,&s->moves,fw,move,status,0,&v);return v;}
static int has(const C3BattleState *s,uint32_t fo,uint16_t id){return c3_battle_condition_check(s,fo,id,NULL)==1;}
static int stop(C3BattleScript *sc,const char *what)
{c3_battle_script_fail(sc,"Not lowered yet: %s",what);return C3_SCRIPT_HANDLER_FAIL;}

/* ---- services ------------------------------------------------------------ */

static int status_get(void *u,uint8_t kind,uint32_t a1,uint32_t a2,uint32_t field,uint32_t a4,uint32_t *value)
{
    C3BattleState *s=((C3BattleHandlers *)u)->state;(void)a4;
    switch(kind){
    case 0:*value=0;return 1;
    case 5:return c3_battle_fight_status_get(&s->arena,a1,field,a4,value);
    case 6:return c3_battle_waza_status_get(&s->arena,&s->moves,a1,(uint16_t)a2,field,a4,value);
    case 7:
        if(a1)return c3_battle_floor_status_get(&s->arena,&s->floors,s->floor,a1,a2,field,a4,value);
        return c3_battle_floor_value(s,field,value);
    default:return 0;
    }
}
static int status_set(void *u,uint8_t kind,uint32_t a1,uint32_t a2,uint32_t field,uint32_t a4,uint32_t value)
{
    C3BattleState *s=((C3BattleHandlers *)u)->state;(void)a2;
    switch(kind){
    case 0:return 1;
    case 5:return c3_battle_fight_status_set(&s->arena,a1,field,a4,value);
    case 6:return c3_battle_waza_status_set(&s->arena,a1,field,a4,value);
    case 7:return c3_battle_floor_status_set(&s->arena,s->floor,a1,field,a4,value);
    default:return 0;
    }
}
static int target(void *u,uint8_t kind,uint32_t arg,uint32_t *handle)
{C3BattleState *s=((C3BattleHandlers *)u)->state;return c3_battle_target(&s->arena,s->floor,kind,arg,handle);}
static int end_of_move(void *u,C3BattleScriptPass pass,uint8_t arg){(void)u;(void)pass;(void)arg;return 0;}

C3BattleScriptServices c3_battle_handler_services(C3BattleHandlers *h)
{
    C3BattleScriptServices sv={h,status_get,status_set,target,end_of_move,c3_battle_handler_dispatch};
    return sv;
}

/* ---- 0x00 WS_HITCHECK ---------------------------------------------------- */

/* Any battler slot holding condition `id` (superset of the valid battlers a
 * fightFloorLoopValidFightOutPokemon pass would visit). */
static int any_battler_has(const C3BattleState *s,uint16_t id)
{
    for(unsigned side=0;side<2;++side)for(unsigned t=0;t<2;++t)for(unsigned k=0;k<3;++k){
        uint32_t trainer=s->floor+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES+C3_FIGHT_SIDE_TRAINER_OFFSET+
            t*C3_BATTLE_RUNTIME_TRAINER_BYTES;
        uint32_t fo=trainer+(k<2?C3_RUNTIME_FIGHT_OUT_OFFSET+k*C3_FIGHT_OUT_POKEMON_BYTES:0x20D4u);
        if(c3_battle_condition_present(s,fo,id,NULL))return 1;
    }
    return 0;
}
/* pokemonBiosGetPokemonWazaPp: PP byte of move slot (0..3), else 0. */
static uint8_t move_pp(const C3BattleState *s,uint32_t pokemon,int32_t slot)
{
    const uint8_t *p=pokemon&&slot>=0&&slot<4?c3_battle_arena_bytes(&s->arena,pokemon+0x78u+(uint32_t)slot*4u,4):NULL;
    return p?p[2]:0;
}

static int ws_hitcheck(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t attacker=0,defender=0,flags=0;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker)||!c3_battle_target(&s->arena,s->floor,0x12,0,&defender))
        return stop(sc,"attack canceler targets");
    uint32_t fp=get(s,attacker,0xD6),pokemon=fp?get(s,fp,0xCC):0;  /* fightOutPokemonGetPokemonPtr */
    if(s->result!=0){sc->state=2;return C3_SCRIPT_HANDLER_DONE;}
    if(!c3_battle_script_read(sc,HIT_MARKER,4,&flags))return stop(sc,"hit marker");
    if(!c3_battle_fight_out_check_fight_out(s,attacker,NULL)&&!(flags&0x200)){
        c3_battle_script_write(sc,HIT_MARKER,4,flags|0x80000u);
        return c3_battle_script_jump(sc,0x80375FBFu)?C3_SCRIPT_HANDLER_DONE:C3_SCRIPT_HANDLER_FAIL;  /* lbl_80375FBF */
    }
    int canceled=c3_battle_status_canceller(s,&h->canceller_step,h->reason);
    if(canceled<0)return stop(sc,h->reason);
    if(canceled!=0)return stop(sc,"status canceller result");  /* its bodies set the PC: none lowered */
    uint16_t move=(uint16_t)get(s,attacker,0xDA);
    if((uint16_t)get(s,defender,0x100)==0x2B){                  /* Soundproof */
        if(c3_battle_fight_out_check_fight_out(s,defender,NULL)&&(waza(s,0,move,0x17)&0xFF)==1){
            if(has(s,attacker,0x22))flags|=0x800;
            c3_battle_script_write(sc,HIT_MARKER,4,flags);
            return c3_battle_script_jump(sc,0x8037989Eu)?C3_SCRIPT_HANDLER_DONE:C3_SCRIPT_HANDLER_FAIL; /* lbl_8037989E */
        }
    }
    uint32_t fw=get(s,attacker,0xD9);
    uint8_t protectable=(uint8_t)waza(s,0,move,0x0E),reflectable=(uint8_t)waza(s,0,move,0x0F);
    int8_t slot=(int8_t)waza(s,fw,0,0x26);
    uint8_t pp=(slot<0||move==0xA5||move==0x164)?0:move_pp(s,pokemon,slot);
    if(!(pp||move==0xA5||move==0x164)&&!(has(s,attacker,0x22)||(flags&0x800200u)))
        return stop(sc,"no PP left (WS_HITCHECK)");
    uint32_t old=flags;flags&=0xFF7FFFFFu;c3_battle_script_write(sc,HIT_MARKER,4,flags);
    if(!(old&0x2000000u)&&!has(s,attacker,0x22)){
        int r=c3_battle_hyper_canceller(s,attacker,h->reason);
        if(r<0)return stop(sc,h->reason);
        if(r!=0)return stop(sc,"Hyper Mode canceller result");
    }
    flags|=0x2000000u;c3_battle_script_write(sc,HIT_MARKER,4,flags);
    if(has(s,defender,0x37)&&reflectable==1)return stop(sc,"Magic Coat (WS_HITCHECK)");
    if((waza(s,0,move,0x10)&0xFF)==1&&any_battler_has(s,0x33))return stop(sc,"Snatch (WS_HITCHECK)");
    if((int32_t)get(s,defender,0x114)==1)return stop(sc,"Lightning Rod redirect (WS_HITCHECK)");
    if(has(s,defender,0x2B)&&protectable==1)return stop(sc,"Protect (WS_HITCHECK)");
    sc->pc+=1;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- shared helpers ------------------------------------------------------ */

#define WORK_BASE 0x80478D78u /* lbl_80478D78 script work bytes */

static int ability_on_any_battler(const C3BattleState *s,uint16_t ability)
{
    for(unsigned side=0;side<2;++side)for(unsigned t=0;t<2;++t)for(unsigned k=0;k<3;++k){
        uint32_t trainer=s->floor+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES+C3_FIGHT_SIDE_TRAINER_OFFSET+
            t*C3_BATTLE_RUNTIME_TRAINER_BYTES;
        uint32_t fo=trainer+(k<2?C3_RUNTIME_FIGHT_OUT_OFFSET+k*C3_FIGHT_OUT_POKEMON_BYTES:0x20D4u);
        if((uint16_t)get(s,fo,0x100)==ability)return 1;
    }
    return 0;
}
/* fightFloorGetNowTenkouDataId(floor, check_abilities): Cloud Nine (13) /
 * Air Lock (0x4D) on any valid battler -> 0 (decided conservatively: any
 * battler slot holding them is refused), then floor weather conditions. */
static int weather(const C3BattleState *s,int check_abilities,int *out)
{
    static const struct{uint16_t id;uint8_t w;}table[]={{0x4E,0},{0x4F,1},{0x50,2},{0x51,3},{0x52,4},{0x53,1},{0x54,2},{0x55,3}};
    if(check_abilities&&(ability_on_any_battler(s,13)||ability_on_any_battler(s,0x4D)))return 0;
    *out=0;
    for(unsigned i=0;i<sizeof table/sizeof *table;++i){
        const uint8_t *row=s->conditions.rows&&table[i].id<s->conditions.count?
            s->conditions.rows+table[i].id*s->conditions.stride:NULL;
        if(row&&((uint16_t)row[8]<<8|row[9])==9&&c3_battle_condition_present(s,s->floor,table[i].id,NULL)){
            *out=table[i].w;return 1;
        }
    }
    return 1;
}
/* fightOutPokemonGetSoubiItemSoubiDataId / ...Buff: 0 under condition 0x3D
 * or with no held item (PokemonBios 0x82 at +0x88); a held item's effect is
 * read from the item table, which is not modelled yet. */
static int held_item_effect(const C3BattleState *s,uint32_t fo,uint16_t *effect)
{
    uint32_t fp=get(s,fo,0xD6),poke=fp?get(s,fp,0xCC):0;
    *effect=0;
    if(!poke||c3_battle_condition_check(s,fo,0x3D,NULL))return 1;
    const uint8_t *p=c3_battle_arena_bytes(&s->arena,poke+0x88,2);
    return p&&!p[0]&&!p[1];
}
/* fightOutPokemonGetPokemonPtr. */
static uint32_t battler_pokemon(const C3BattleState *s,uint32_t fo)
{uint32_t fp=fo?get(s,fo,0xD6):0;return fp?get(s,fp,0xCC):0;}
/* Side record holding a battler's trainer (fightTargetGetPtrAsNowFightType(2, fo)). */
static uint32_t battler_side(const C3BattleState *s,uint32_t fo)
{
    uint32_t trainer=c3_battle_fight_out_trainer(s,fo);
    if(!trainer)return 0;
    uint32_t off=trainer-s->floor-C3_FIGHT_SIDE_OFFSET;
    return s->floor+C3_FIGHT_SIDE_OFFSET+(off/C3_FIGHT_SIDE_BYTES)*C3_FIGHT_SIDE_BYTES;
}
static int present(C3BattleHandlers *h,C3BattlePresent r,uint32_t a,uint32_t b)
{return h->present?h->present(h->present_user,r,a,b):0;}

/* ---- 0x01 accuracy check (fn_802284B0) ----------------------------------- */

/* fn_802284B0_hit: the move hit unless a FightWaza miss condition is set;
 * then the follow-me flag and the absorbing-ability check (fn_8022DCB8). */
static int accuracy_hit(C3BattleHandlers *h,C3BattleScript *sc,uint16_t move,uint32_t attacker,uint32_t defender)
{
    C3BattleState *s=h->state;
    uint32_t fw=get(s,attacker,0xD9),flags=0;
    int missed=c3_battle_condition_present(s,fw,0x40,NULL)||c3_battle_condition_present(s,fw,0x43,NULL)||
        c3_battle_condition_present(s,fw,0x45,NULL);                              /* fightWazaIsHit */
    if(missed)return stop(sc,"miss result (fn_802284B0_hit)");
    if(has(s,defender,0x15)&&battler_side(s,attacker)!=battler_side(s,defender)){
        if(!c3_battle_script_read(sc,HIT_MARKER,4,&flags))return stop(sc,"hit marker");
        if(!(flags&0x1000000u))c3_battle_script_write(sc,HIT_MARKER,4,flags|0x40u);
    }
    uint16_t def_ability=(uint16_t)get(s,defender,0x100);
    (void)move;
    if(c3_battle_fight_out_check_fight_out(s,defender,NULL)&&(def_ability==0x0A||def_ability==0x0B||def_ability==0x12))
        return stop(sc,"absorbing ability (fn_8022DCB8)");
    sc->pc+=7;
    return C3_SCRIPT_HANDLER_DONE;
}

static int op_accuracy(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t operand=0,attacker=0,defender=0,flags=0,kind_flag=0;
    if(!c3_battle_script_fetch(sc,5,2,&operand))return C3_SCRIPT_HANDLER_FAIL;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker)||!c3_battle_target(&s->arena,s->floor,0x12,0,&defender))
        return stop(sc,"accuracy targets");
    uint16_t move=(uint16_t)operand,att_ability=(uint16_t)get(s,attacker,0x100),def_ability=(uint16_t)get(s,defender,0x100);
    uint16_t item=0;int w=0;
    if(!held_item_effect(s,defender,&item))return stop(sc,"defender held-item effect");
    if(!weather(s,1,&w))return stop(sc,"Cloud Nine / Air Lock");
    if(move==0xFFFF||move==0xFFFE)return stop(sc,"special accuracy operand");
    if(!move)move=(uint16_t)get(s,attacker,0xDA);
    uint16_t effect=(uint16_t)waza(s,0,move,9);
    /* fn_80229934: only moves 0x11D, 0xC2/0xC3, 0x31/0x52 and sleep-effect
     * moves can be blocked, when floor field 0x34 is 1. */
    if(!c3_battle_floor_value(s,0x34,&kind_flag))return stop(sc,"floor field 0x34");
    if((kind_flag&0xFF)==1&&(move==0x11D||move==0xC3||move==0xC2||move==0x52||move==0x31||effect==1))
        return stop(sc,"fn_80229934 move block");
    /* fn_802284B0_reflect: Protect on the defender. */
    if(has(s,defender,0x2B)&&(waza(s,0,(uint16_t)get(s,attacker,0xDA),0x0E)&0xFF)==1)
        return stop(sc,"Protect (fn_802284B0_reflect)");
    /* fn_80228DAC: lock-on / never-miss conditions and sure-hit effects. */
    if(has(s,defender,0x1D))return stop(sc,"Lock-On (fn_80228DAC)");
    if(!c3_battle_script_read(sc,HIT_MARKER,4,&flags))return stop(sc,"hit marker");
    if(!(flags&0x10000u)&&has(s,defender,0x1F))return stop(sc,"condition 0x1F (fn_80228DAC)");
    uint32_t old=flags;flags&=0xFFFDFFFFu;c3_battle_script_write(sc,HIT_MARKER,4,flags);
    if(!(old&0x40000u)&&has(s,defender,0x21))return stop(sc,"condition 0x21 (fn_80228DAC)");
    flags&=0xFFFBFFFFu;c3_battle_script_write(sc,HIT_MARKER,4,flags);
    int w2=0;
    if(!weather(s,1,&w2))return stop(sc,"Cloud Nine / Air Lock");
    if((w2==2&&effect==0x98)||effect==0x11||effect==0x4E)return stop(sc,"sure-hit path (fn_80228DAC)");
    /* Accuracy. */
    int att_acc=(int)(get(s,attacker,0xEB)&0xFF),def_eva=(int)(get(s,defender,0xEC)&0xFF);
    int stage=(int)(int8_t)(att_acc+(12-def_eva)-6);
    if(has(s,defender,0x19))stage=(int)(int8_t)att_acc;
    if(stage<0)stage=0;
    if(stage>12)stage=12;
    uint32_t base=waza(s,0,move,6)&0xFF;
    if(w==1&&effect==0x98)base=0x32;
    const uint8_t *row=s->accuracy.rows&&(uint32_t)stage<s->accuracy.count?s->accuracy.rows+stage*2:NULL;
    uint16_t product=(uint16_t)(base*(row?row[0]:0u));
    uint8_t divisor=row?row[1]:0;
    if(!divisor)return stop(sc,"accuracy stage table");
    uint16_t accuracy=(uint16_t)((int32_t)product/(int32_t)divisor);
    if(att_ability==0x0E)accuracy=(uint16_t)(accuracy*0x82/100);                  /* Compound Eyes */
    if(w==3&&def_ability==8)accuracy=(uint16_t)(accuracy*0x50/100);              /* Sand Veil */
    if(att_ability==0x37)return stop(sc,"Hustle accuracy");
    if(item==0x16)return stop(sc,"held-item accuracy");
    uint16_t random=0;
    if(!c3_battle_rng_next16(&s->rng,&random))return stop(sc,"random stream");
    int roll=(int)(random%100u)+1;
    if(roll>(int)accuracy)return stop(sc,"miss (type check fn_802274F0)");
    return accuracy_hit(h,sc,move,attacker,defender);
}

/* ---- 0x02 attack string (fn_802282D8) ------------------------------------ */

static int op_attack_string(C3BattleHandlers *h,C3BattleScript *sc,unsigned phase)
{
    C3BattleState *s=h->state;
    uint32_t flags=0,attacker=0,type=0;
    if(!c3_battle_script_read(sc,HIT_MARKER,4,&flags))return stop(sc,"hit marker");
    if(phase==0&&!(flags&0x600u)){
        if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker))return stop(sc,"attacker");
        uint16_t move=(uint16_t)get(s,attacker,0xDA);
        if(get(s,attacker,0xEE))return stop(sc,"attack-string model sequence");
        (void)c3_battle_floor_value(s,0x14,&type);
        present(h,C3_PRESENT_ACTOR_BEGIN,attacker,type);
        h->message_open=present(h,C3_PRESENT_ATTACK_STRING,attacker,move)==1;
        /* No model sequence: the original runs lbl_80378964 (pause 0x20). */
        if(!c3_battle_script_call(sc,0x80378964u,sc->action,0))return stop(sc,"attack-string sequence");
        return C3_SCRIPT_HANDLER_CALL;
    }
    if(phase==1){
        if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker))return stop(sc,"attacker");
        (void)c3_battle_floor_value(s,0x14,&type);
        if(h->message_open)present(h,C3_PRESENT_MESSAGE_CLOSE,0,0);
        h->message_open=0;
        present(h,C3_PRESENT_ACTOR_END,attacker,type);
        c3_battle_script_write(sc,HIT_MARKER,4,flags|0x400u);
    }
    sc->pc+=1;
    c3_battle_script_write(sc,WORK_BASE+7,1,0);
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x03 PP reduce (fn_8022808C) ---------------------------------------- */

static int op_pp_reduce(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t attacker=0,defender=0,flags=0;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker)||!c3_battle_target(&s->arena,s->floor,0x12,0,&defender))
        return stop(sc,"PP-reduce targets");
    uint32_t fw=get(s,attacker,0xD9),poke=battler_pokemon(s,attacker);
    uint16_t def_ability=(uint16_t)get(s,defender,0x100);
    if(!c3_battle_script_read(sc,HIT_MARKER,4,&flags))return stop(sc,"hit marker");
    if(!(flags&0xA00u)){
        uint16_t move=(uint16_t)get(s,attacker,0xDA);
        uint8_t range=(uint8_t)waza(s,0,move,5);
        int8_t slot=(int8_t)waza(s,fw,0,0x26);
        uint8_t *entry=poke&&slot>=0&&slot<4?c3_battle_arena_bytes(&s->arena,poke+0x78u+(uint32_t)slot*4u,4):NULL;
        uint8_t pp=entry?entry[2]:0,deduction=1;
        if((int32_t)get(s,attacker,0x118)==0){
            if(range==6||range==4||range==7){
                if(ability_on_any_battler(s,0x2E))return stop(sc,"Pressure count (fn_8022808C)");
            }else if(attacker!=defender&&def_ability==0x2E)deduction=2;
        }
        if(pp){
            (void)c3_battle_fight_status_set(&s->arena,attacker,0x111,0,1);
            pp=deduction<pp?(uint8_t)(pp-deduction):0;
            /* pokemonBiosSetPokemonWazaPp clamps to the move's maximum PP:
             * with no PP Ups that is the move's base PP; never raises PP. */
            if(entry[3])return stop(sc,"PP Ups maximum (pokemonWazaGetMaxPP)");
            if(pp>(uint8_t)waza(s,0,move,2))return stop(sc,"stored PP above maximum");
            entry[2]=pp;
            if(!has(s,attacker,0x10)&&has(s,attacker,0x31))return stop(sc,"Transform PP mirror (fn_8022808C)");
        }
    }
    c3_battle_script_write(sc,HIT_MARKER,4,flags&0xFFFFF7FFu);
    sc->pc+=1;
    return C3_SCRIPT_HANDLER_DONE;
}

int c3_battle_handler_dispatch(void *user,C3BattleScript *sc,uint8_t op,unsigned phase)
{
    C3BattleHandlers *h=user;(void)phase;
    if(!h||!h->state||!sc)return C3_SCRIPT_HANDLER_NONE;
    switch(op){
    case 0x00:return ws_hitcheck(h,sc);
    case 0x01:return op_accuracy(h,sc);
    case 0x02:return op_attack_string(h,sc,phase);
    case 0x03:return op_pp_reduce(h,sc);
    default:return C3_SCRIPT_HANDLER_NONE;
    }
}
