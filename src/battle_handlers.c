/* Move-script battle-logic opcodes over the battle state (guarded lowering).
 *   0x00 WS_HITCHECK  fight_range_80211A00.c (attack canceler)
 *   0x01 fn_802284B0  accuracy check (with fn_80228DAC, fn_80229934,
 *                     fn_802284B0_hit, fightWazaIsHit, fn_8022DCB8,
 *                     fightFloorGetNowTenkouDataId)
 *   0x02 fn_802282D8  attack string
 *   0x03 fn_8022808C  PP reduce
 *   0x04 WS_CRITICAL_CHECK
 *   0x05 fn_80227C40  damage calculation (fn_80232110 via battle_damage_state)
 *   0x06 WS_TYPE_CHECK fn_802274F0(1, 1, 1, 0): STAB, Levitate, type chart
 *                     (fn_802279F8), Wonder Guard
 *   0x07 WS_DAMAGE_LOSS / 0x08 WS_DAMAGE_LOSS_ONLY  fn_802271E0(1|0, 1):
 *                     random 85-100%, Focus Band, False Swipe / Endure
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

/* ---- FightWaza conditions (fight_out_pokemon_exact_802096E8.c) ---------- */

#define WAZA_CATEGORY 0x2A
static uint16_t condition_category(const C3BattleState *s,uint16_t id)   /* fn_80119ED0 */
{
    const C3BattleDataRows *r=&s->conditions;
    return r->rows&&id<r->count?(uint16_t)((uint16_t)r->rows[(uint32_t)id*r->stride+8]<<8|r->rows[(uint32_t)id*r->stride+9]):0;
}
static int waza_is(const C3BattleState *s,uint32_t fw,uint16_t id,int *fault)        /* fightWazaIsJoutaiDataId */
{return condition_category(s,id)==WAZA_CATEGORY&&c3_battle_condition_present(s,fw,id,fault)==1;}
static int waza_check_write(const C3BattleState *s,uint32_t fw,uint16_t id,int *fault) /* fightWazaCheckWriteJoutaiDataId */
{return condition_category(s,id)==WAZA_CATEGORY?c3_battle_condition_check_write(s,fw,id,fault):0;}
static void waza_write(C3BattleState *s,uint32_t fw,uint16_t id,int *fault)          /* fightWazaWriteJoutaiDataId(.., 0) */
{if(condition_category(s,id)==WAZA_CATEGORY&&!c3_battle_condition_write(s,fw,id,0,fault)&&fault)*fault=1;}
static void waza_erase(C3BattleState *s,uint32_t fw,uint16_t id,int *fault)          /* fightWazaInitJoutaiDataId */
{if(condition_category(s,id)==WAZA_CATEGORY)c3_battle_condition_erase(s,fw,id,fault);}
static int waza_hit(const C3BattleState *s,uint32_t fw,int *fault)                  /* fightWazaIsHit */
{return !waza_is(s,fw,0x40,fault)&&!waza_is(s,fw,0x43,fault)&&!waza_is(s,fw,0x45,fault);}
static int waza_sousai(const C3BattleState *s,uint32_t fw,int *fault)               /* fightWazaIsJoutaiSousai */
{return waza_is(s,fw,0x41,fault)&&waza_is(s,fw,0x42,fault);}
/* pokemonSetStatus(fo, 0xDE, id): write the condition when writable. */
static void waza_set_condition(C3BattleState *s,uint32_t fo,uint16_t id,int *fault)
{
    uint32_t fw=0;
    if(!c3_battle_fight_status_get(&s->arena,fo,0xD9,0,&fw)){if(fault)*fault=1;return;}
    if(waza_check_write(s,fw,id,fault)==2)waza_write(s,fw,id,fault);
}
/* fightFloorSetStatus(0, 0, 0x3B, 0, id): the floor attacker's 0xDE. */
static void floor_attacker_condition(C3BattleState *s,uint16_t id,int *fault)
{
    uint32_t attacker=0;
    if(!c3_battle_floor_value(s,0x36,&attacker)&&
       !c3_battle_floor_status_get(&s->arena,&s->floors,s->floor,0,0,0x36,0,&attacker)){if(fault)*fault=1;return;}
    waza_set_condition(s,attacker,id,fault);
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

#define ability_on_any_battler c3_battle_state_ability_on_any_battler
static int weather(const C3BattleState *s,int check,int *out){return c3_battle_state_weather(s,check,out);}
static int held_item_effect(const C3BattleState *s,uint32_t fo,uint16_t *e){return c3_battle_state_held_item_effect(s,fo,e);}
/* fightOutPokemonGetPokemonPtr. */
static uint32_t battler_pokemon(const C3BattleState *s,uint32_t fo)
{uint32_t fp=fo?get(s,fo,0xD6):0;return fp?get(s,fp,0xCC):0;}
#define battler_side c3_battle_state_battler_side
static int present(C3BattleHandlers *h,C3BattlePresent r,uint32_t a,uint32_t b)
{return h->present?h->present(h->present_user,r,a,b):0;}

/* ---- 0x01 accuracy check (fn_802284B0) ----------------------------------- */

/* fn_802284B0_hit: the move hit unless a FightWaza miss condition is set;
 * then the follow-me flag and the absorbing-ability check (fn_8022DCB8). */
static int type_check(C3BattleHandlers *h,C3BattleScript *sc,uint8_t r3,uint8_t r4,uint8_t r5,uint8_t r6);
static int accuracy_hit(C3BattleHandlers *h,C3BattleScript *sc,uint16_t move,uint32_t attacker,uint32_t defender)
{
    C3BattleState *s=h->state;
    uint32_t fw=get(s,attacker,0xD9),flags=0;
    int missed=c3_battle_condition_present(s,fw,0x40,NULL)||c3_battle_condition_present(s,fw,0x43,NULL)||
        c3_battle_condition_present(s,fw,0x45,NULL);                              /* fightWazaIsHit */
    if(missed){
        uint32_t jump=0;
        if(!c3_battle_fight_status_set(&s->arena,defender,0xF3,0,0)||!c3_battle_fight_status_set(&s->arena,defender,0xF4,0,9))
            return stop(sc,"defender hit move");
        if(!c3_battle_script_fetch(sc,1,4,&jump))return C3_SCRIPT_HANDLER_FAIL;
        return c3_battle_script_jump(sc,jump)?C3_SCRIPT_HANDLER_DONE:C3_SCRIPT_HANDLER_FAIL;
    }
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
    if(roll>(int)accuracy){
        int fault=0;uint32_t fw=get(s,attacker,0xD9),maximum=0,old=0,now=0;
        if(waza_check_write(s,fw,0x40,&fault)==2)waza_write(s,fw,0x40,&fault);
        if(fault)return stop(sc,"miss condition");
        if(!c3_battle_floor_value(s,0x19,&maximum))return stop(sc,"floor field 0x19");
        uint8_t category=(uint8_t)waza(s,0,move,5);
        c3_battle_script_write(sc,WORK_BASE+6,1,((uint16_t)maximum>=2&&(category==4||category==6))?2u:0u);
        (void)c3_battle_script_read(sc,WORK_BASE+6,1,&old);
        if(type_check(h,sc,0,0,0,1)!=C3_SCRIPT_HANDLER_DONE)return C3_SCRIPT_HANDLER_FAIL;
        (void)c3_battle_script_read(sc,WORK_BASE+6,1,&now);
        if(now==3&&(old==2||old==0))c3_battle_script_write(sc,WORK_BASE+6,1,old);
    }
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

/* ---- 0x04 WS_CRITICAL_CHECK --------------------------------------------- */

static int op_critical(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t attacker=0,defender=0,flag=0;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker)||!c3_battle_target(&s->arena,s->floor,0x12,0,&defender))
        return stop(sc,"critical targets");
    uint16_t item=0;
    if(!held_item_effect(s,attacker,&item))return stop(sc,"attacker held-item effect");
    uint32_t fw=get(s,attacker,0xD9),poke=battler_pokemon(s,attacker);
    uint16_t move=(uint16_t)get(s,attacker,0xDA);
    const uint8_t *pb=poke?c3_battle_arena_bytes(&s->arena,poke,2):NULL;
    uint16_t species=pb?(uint16_t)((uint16_t)pb[0]<<8|pb[1]):0;
    uint16_t effect=(uint16_t)waza(s,0,move,9);
    int lucky=item==0x3F&&species==0x71,stick=item==0x42&&species==0x53;
    uint16_t crit=(uint16_t)((has(s,attacker,0x0F)?2:0)+(effect==0x2B)+(effect==0x4B)+(effect==200)+(effect==0xD1)+
        (item==0x29)+lucky*2+stick*2);
    if(!h->critical||!h->critical_count)return stop(sc,"critical-rate table");
    if(crit>h->critical_count-1u)crit=(uint16_t)(h->critical_count-1u);
    uint16_t def_ability=(uint16_t)get(s,defender,0x100);
    uint32_t value=1;
    if(!c3_battle_floor_value(s,0x29,&flag))return stop(sc,"floor field 0x29");
    if(def_ability!=4&&def_ability!=0x4B&&(flag&0xFF)==1){
        uint8_t denominator=crit<h->critical_count?h->critical[crit]:0;
        uint16_t r1=0,r2=0;
        if(!denominator)return stop(sc,"critical denominator");
        if(!c3_battle_rng_next16(&s->rng,&r1))return stop(sc,"random stream");
        if(r1%denominator==0)value=2;
        else if(has(s,attacker,0x3E)&&move==0x164){
            if(!c3_battle_rng_next16(&s->rng,&r2))return stop(sc,"random stream");
            if(r2%100<90)value=2;
        }
    }
    if(!c3_battle_waza_status_set(&s->arena,fw,0x2B,0,value))return stop(sc,"FightWaza critical");
    sc->pc+=1;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x05 damage calculation (fn_80227C40) ------------------------------- */

static int op_damage_calc(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t attacker=0,defender=0;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker)||!c3_battle_target(&s->arena,s->floor,0x12,0,&defender))
        return stop(sc,"damage targets");
    uint32_t side=battler_side(s,defender);
    uint16_t move=(uint16_t)get(s,attacker,0xDA);
    uint32_t fw=get(s,attacker,0xD9);
    uint16_t power=(uint16_t)waza(s,fw,0,0x2F),type=(uint16_t)waza(s,fw,0,0x30);
    int32_t damage=0;
    if(!c3_battle_state_damage(s,&h->damage,attacker,defender,side,move,power,type,&damage,h->reason,sizeof h->reason))
        return stop(sc,h->reason);
    damage*=(int32_t)(uint8_t)waza(s,fw,0,0x2B);
    damage*=(int32_t)(uint8_t)waza(s,fw,0,0x2C);
    if(has(s,attacker,0x24)&&type==0x0D)damage*=2;            /* Charge */
    if(has(s,attacker,0x32))damage=damage*15/10;              /* Helping Hand */
    if(!c3_battle_waza_status_set(&s->arena,fw,0x2D,0,(uint32_t)damage))return stop(sc,"FightWaza damage");
    sc->pc+=1;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x06 WS_TYPE_CHECK (fn_802274F0, fn_802279F8) ----------------------- */

/* fn_802279F8(fight_waza, effectiveness, move, apply): 0x43 no effect, 0x42
 * not very effective, 0x41 super effective; 0x3F and others leave it. */
static void type_effect(C3BattleState *s,uint32_t fw,uint16_t effect,uint16_t move,uint8_t apply,int *fault)
{
    int32_t damage=(int32_t)waza(s,fw,0,0x2D);
    uint16_t power=(uint16_t)waza(s,0,move,7);
    uint8_t mult;
    if(waza_is(s,fw,0x43,fault)&&apply==0)return;
    if(effect==0x3F)return;
    if(effect==0x43)mult=0;else if(effect==0x42)mult=5;else if(effect==0x41)mult=0x14;else return;
    if(apply==1){
        damage=damage*(int32_t)mult/10;
        if(damage==0&&mult!=0)damage=1;
        if(!c3_battle_waza_status_set(&s->arena,fw,0x2D,0,(uint32_t)damage)&&fault)*fault=1;
    }
    if(mult==0){
        if(waza_check_write(s,fw,effect,fault)==2)waza_write(s,fw,effect,fault);
        waza_erase(s,fw,0x42,fault);waza_erase(s,fw,0x41,fault);
    }else if(power!=0&&waza_hit(s,fw,fault)){
        uint16_t opposite=mult==5?0x41:0x42;
        if(waza_is(s,fw,opposite,fault))waza_erase(s,fw,opposite,fault);
        else if(waza_check_write(s,fw,effect,fault)==2)waza_write(s,fw,effect,fault);
    }
}
/* zokuseiGetWazaJoutai (fn_8010C508): type row +8 + defender type * 2. */
static uint16_t type_chart(const C3BattleHandlers *h,uint16_t move_type,uint16_t defender_type)
{
    const C3BattleDamageTables *t=&h->damage;
    if(!t->types||move_type>=t->type_count||defender_type>=0x12)return 0;
    const uint8_t *p=t->types+(uint32_t)move_type*0x2Cu+8u+(uint32_t)defender_type*2u;
    return (uint16_t)((uint16_t)p[0]<<8|p[1]);
}
static uint16_t battler_type(const C3BattleState *s,uint32_t fo,uint32_t slot)   /* fightOutPokemonGetZokuseiDataId */
{uint32_t v=0;(void)c3_battle_fight_status_get(&s->arena,fo,0xFF,slot,&v);return (uint16_t)v;}

static int type_check(C3BattleHandlers *h,C3BattleScript *sc,uint8_t r3,uint8_t r4,uint8_t r5,uint8_t r6)
{
    C3BattleState *s=h->state;
    uint32_t attacker=0,defender=0,flags=0;
    int fault=0;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker)||!c3_battle_target(&s->arena,s->floor,0x12,0,&defender))
        return stop(sc,"type check targets");
    uint16_t att1=battler_type(s,attacker,0),att2=battler_type(s,attacker,1);
    uint32_t fw=get(s,attacker,0xD9);
    uint16_t move=fw?(uint16_t)waza(s,fw,0,0x28):0;              /* fightOutPokemonGetUseWazaDataId */
    uint16_t power=(uint16_t)waza(s,0,move,7),move_type=(uint16_t)waza(s,fw,0,0x30);
    uint16_t def1=battler_type(s,defender,0),def2=battler_type(s,defender,1);
    uint16_t def_ability=(uint16_t)get(s,defender,0x100);
    uint8_t backup[0xAC];
    uint8_t *record=fw?c3_battle_arena_bytes(&s->arena,fw,sizeof backup):NULL;
    if(fw&&!record)return stop(sc,"FightWaza record");
    if(record)memcpy(backup,record,sizeof backup);
    if(move!=0xA5&&move!=0x164&&(r6!=1||power!=0)){
        if(r3==1&&(att1==move_type||att2==move_type)){             /* same-type bonus */
            int32_t damage=(int32_t)waza(s,fw,0,0x2D);
            if(!c3_battle_waza_status_set(&s->arena,fw,0x2D,0,(uint32_t)(damage*0xF/10)))return stop(sc,"FightWaza damage");
        }
        if(def_ability==0x1A&&move_type==4){                       /* Levitate vs Ground */
            if(r5==1){
                floor_attacker_condition(s,0x43,&fault);floor_attacker_condition(s,0x40,&fault);
                if(!c3_battle_fight_status_set(&s->arena,defender,0xF3,0,0)||!c3_battle_fight_status_set(&s->arena,defender,0xF4,0,9))
                    fault=1;
            }
            c3_battle_script_write(sc,WORK_BASE+6,1,4);
        }else{
            int identified=c3_battle_condition_check(s,defender,0x19,&fault)==1;
            if(!identified||(def1!=7&&def2!=7)||move_type>1){
                type_effect(s,fw,type_chart(h,move_type,def1),move,r3,&fault);
                if(def1!=def2)type_effect(s,fw,type_chart(h,move_type,def2),move,r3,&fault);
            }
        }
        if(def_ability==0x19){                                     /* Wonder Guard */
            uint16_t effect=(uint16_t)waza(s,0,move,9);
            int guard=2,w=0;
            if(effect==0x97&&(c3_battle_state_weather(s,0,&w)?w==1:(fault=1,0)))guard=2;
            else if(effect==0x91||effect==0x27||effect==0x4B||effect==0x97||effect==0x9B||effect==0x1A){
                if(!c3_battle_script_read(sc,HIT_MARKER,4,&flags))return stop(sc,"hit marker");
                guard=(flags&0x8000000u)?1:2;
            }
            if(guard==2&&(!waza_is(s,fw,0x41,&fault)||waza_sousai(s,fw,&fault))&&power!=0){
                if(r5==1){
                    floor_attacker_condition(s,0x40,&fault);
                    if(!c3_battle_fight_status_set(&s->arena,defender,0xF3,0,0)||!c3_battle_fight_status_set(&s->arena,defender,0xF4,0,9))
                        fault=1;
                }
                c3_battle_script_write(sc,WORK_BASE+6,1,3);
            }
        }
        if(waza_is(s,fw,0x43,&fault)&&!c3_battle_fight_status_set(&s->arena,attacker,0x108,0,1))fault=1;
    }
    if(r3==0){
        int f40=waza_is(s,fw,0x40,&fault),f43=waza_is(s,fw,0x43,&fault);
        if(record)memcpy(record,backup,sizeof backup);
        if(f40)floor_attacker_condition(s,0x40,&fault);
        if(f43)floor_attacker_condition(s,0x43,&fault);
    }
    if(fault)return stop(sc,"type check condition or record");
    if(r4==1)sc->pc+=1;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x07 WS_DAMAGE_LOSS, 0x08 WS_DAMAGE_LOSS_ONLY (fn_802271E0) --------- */

static int damage_loss(C3BattleHandlers *h,C3BattleScript *sc,uint8_t r3,uint8_t r4)
{
    C3BattleState *s=h->state;
    uint32_t attacker=0,defender=0;
    int fault=0;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker)||!c3_battle_target(&s->arena,s->floor,0x12,0,&defender))
        return stop(sc,"damage loss targets");
    uint32_t fw=get(s,attacker,0xD9);
    uint16_t move=fw?(uint16_t)waza(s,fw,0,0x28):0;              /* fightOutPokemonGetUseWazaDataId */
    int32_t damage=(int32_t)waza(s,fw,0,0x2D);
    uint16_t effect=0;
    if(!held_item_effect(s,defender,&effect))return stop(sc,"defender held item (Focus Band)");
    uint32_t pokemon=battler_pokemon(s,defender);                 /* fightOutPokemonGetPokemonPtr */
    const uint8_t *hp_bytes=pokemon?c3_battle_arena_bytes(&s->arena,pokemon+0x8A,2):NULL;
    uint16_t hp=hp_bytes?(uint16_t)((uint16_t)hp_bytes[0]<<8|hp_bytes[1]):0;   /* pokemonGetStatus 0x83 */
    if(pokemon&&!hp_bytes)return stop(sc,"defender Pokemon record");
    if(r4==1){
        uint16_t random=0;
        if(!c3_battle_rng_next16(&s->rng,&random))return stop(sc,"random stream");
        uint16_t percent=(uint16_t)(100u-random%16u);
        if(damage!=0){
            damage=damage*(int32_t)percent/100;
            if(damage==0)damage=1;
            if(!c3_battle_waza_status_set(&s->arena,fw,0x2D,0,(uint32_t)damage))return stop(sc,"FightWaza damage");
        }
    }
    /* effect 0x27 (Focus Band) needs a held item, refused above. */
    if(c3_battle_condition_check(s,defender,0x14,&fault)==0){
        uint16_t move_effect=(uint16_t)waza(s,0,move,9);
        int endure=c3_battle_condition_check(s,defender,0x2C,&fault)==1;
        uint32_t focus=get(s,defender,0x11A);
        if((move_effect==0x65&&r3==1)||endure||focus){              /* False Swipe, Endure, Focus Band */
            if((int32_t)hp<=damage){
                if(!c3_battle_waza_status_set(&s->arena,fw,0x2D,0,(uint32_t)(hp-1u)))return stop(sc,"FightWaza damage");
                if(endure){
                    if(waza_check_write(s,fw,0x46,&fault)==2)waza_write(s,fw,0x46,&fault);
                }else if(focus)return stop(sc,"Focus Band survival (floor 0x56 item)");
            }
        }
    }
    if(fault)return stop(sc,"damage loss condition");
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
    case 0x04:return op_critical(h,sc);
    case 0x05:return op_damage_calc(h,sc);
    case 0x06:return type_check(h,sc,1,1,1,0);
    case 0x07:return damage_loss(h,sc,1,1);
    case 0x08:return damage_loss(h,sc,0,1);
    default:return C3_SCRIPT_HANDLER_NONE;
    }
}
