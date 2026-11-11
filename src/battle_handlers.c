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
 *   0x09 fn_80226FD4  move effect (fightWazaDoEffectFunc) or, on a miss, the
 *                     status-menu close and the target's motion
 *   0x0A fn_80226F0C  wait for the move effect's signal (operand byte)
 *   0x0B fn_80226914  HP update: damage records, physical/special counters,
 *                     AI damage memory, Transform copy, HP bar
 *   0x0C fn_802267E8  wait for the target's HP bar, faint flag, Substitute break
 *   0x0D fn_80226730  critical-hit message 0x7631 (floor 0x52, work byte 7)
 *   0x0E fn_802266EC  reads the attacker's FightWaza (0xD9), nothing else
 *   0x0F fn_8022631C  effectiveness message from the FightWaza conditions
 *   0x15 fn_80224820  secondary-effect chance roll (work byte 3)
 *   0x19 fn_80224158  faint check: fainted flag, faint sequence
 *   0x1A fn_80224060  faint begin (status panel; Substitute refused)
 *   0x56 fn_8021EE48  cry
 *   0x10 fn_802262D0  open a message
 *   0x1B fn_80223F1C  faint end: status slot, opponents' records
 *   0x23 fn_802232F4  EXP award (faint mark sequence lbl_80378721)
 *   0x24 fn_80222C44  battle end check (faint script lbl_80378798)
 *   0x66 fn_8021DF80  status animation (battler model only)
 *   0x49 fn_8021FAD4  move end: after-move reactions and bookkeeping
 *   0x12 fn_8022622C  with a message open (work byte 7): wait the operand's
 *                     frames, close the message, clear the flag
 *   0x5C fn_8021E9F4  target damage blink (fightOutPokemonDamageEffect) on a
 *                     hit with hit-marker bit 0x80 and not 0x100
 * Script variables (hit marker 0x8047B618, ...) are the interpreter's
 * windows; battle records are the state's arena. */
#include "colosseum3ds/battle_handlers.h"
#include "colosseum3ds/battle_rules.h"
#include "colosseum3ds/battle_runtime_trainer.h"
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
static int end_of_move(void *u,struct C3BattleScript *sc,C3BattleScriptPass pass,uint8_t arg);

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

/* ---- 0x09 move effect (fn_80226FD4) ------------------------------------ */

#define EFFECT_DONE_COUNT 0x8038FF5Au  /* lbl_80379F58[0x16002] */
#define EFFECT_COUNT 0x8038FFF9u       /* lbl_80379F58[0x160a1] */

static int effect_counters(C3BattleScript *sc)
{
    uint32_t done=0,count=0;
    if(!c3_battle_script_read(sc,EFFECT_DONE_COUNT,1,&done)||!c3_battle_script_read(sc,EFFECT_COUNT,1,&count))return 0;
    c3_battle_script_write(sc,EFFECT_DONE_COUNT,1,(done+1u)&0xFFu);
    c3_battle_script_write(sc,EFFECT_COUNT,1,(count+1u)&0xFFu);
    sc->pc+=1;
    return 1;
}
static int op_move_effect(C3BattleHandlers *h,C3BattleScript *sc,unsigned phase)
{
    C3BattleState *s=h->state;
    if(phase==1)return effect_counters(sc)?C3_SCRIPT_HANDLER_DONE:stop(sc,"effect counters");
    uint32_t attacker=0,target=0,flags=0,count=0;
    int fault=0;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker)||!c3_battle_target(&s->arena,s->floor,0x12,0,&target))
        return stop(sc,"move effect targets");
    uint32_t fw=get(s,attacker,0xD9);
    uint16_t move=fw?(uint16_t)waza(s,fw,0,0x28):0;              /* fightOutPokemonGetUseWazaDataId */
    int hit=waza_hit(s,fw,&fault);
    if(fault)return stop(sc,"move effect hit check");
    if(hit){
        if(!c3_battle_script_read(sc,HIT_MARKER,4,&flags))return stop(sc,"hit marker");
        if((flags&0x80u)&&move!=0x90&&move!=0xA4){
            /* fn_80211B94(lbl_8047B62C, lbl_80378964, 0): the pause sequence. */
            if(!c3_battle_script_call(sc,0x80378964u,sc->action,0))return stop(sc,"move effect sequence");
            return C3_SCRIPT_HANDLER_CALL;
        }
        if(!c3_battle_script_read(sc,EFFECT_COUNT,1,&count))return stop(sc,"effect counter");
        if(!h->present_move)return stop(sc,"move effect presentation (fightWazaDoEffectFunc)");
        if(h->present_move(h->present_user,move,attacker,target,(uint8_t)count)!=1)
            return stop(sc,"move effect presentation failed");
        return effect_counters(sc)?C3_SCRIPT_HANDLER_DONE:stop(sc,"effect counters");
    }
    present(h,C3_PRESENT_STATUS_MENUS_CLOSE,0,0);
    if(get(s,target,0xEE))return stop(sc,"target model motion (fn_801DDD28 0x57)");
    sc->pc+=1;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x0A effect wait (fn_80226F0C) -------------------------------------- */

static int op_effect_wait(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t attacker=0,flags=0,signal=0;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker))return stop(sc,"effect wait attacker");
    uint32_t fw=get(s,attacker,0xD9);
    uint16_t move=fw?(uint16_t)waza(s,fw,0,0x28):0;
    if(!c3_battle_script_read(sc,HIT_MARKER,4,&flags))return stop(sc,"hit marker");
    /* With bit 0x80 the original skips the wait and tests r29, which then
     * still holds the interpreter's own register. */
    if((flags&0x80u)&&move!=0x90&&move!=0xA4)return stop(sc,"effect wait skipped (menu close reads the caller's r29)");
    if(!c3_battle_script_fetch(sc,1,1,&signal))return C3_SCRIPT_HANDLER_FAIL;
    if(!h->effect_signal)return stop(sc,"move effect signal (fn_801DA5C4)");
    int reached=h->effect_signal(h->present_user,(uint8_t)signal);
    if(reached<0)return stop(sc,"move effect signal failed");
    if(reached==0){sc->wait_frames=1;return C3_SCRIPT_HANDLER_WAIT;}   /* _threadSwitch */
    if(signal==2||signal==6)present(h,C3_PRESENT_STATUS_MENUS_CLOSE,0,0);
    sc->pc+=2;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x0E fn_802266EC ----------------------------------------------------- */

/* pokemonGetStatus(attacker, 0xD9) with the result unused (0x802266EC
 * disassembly: no other call or store), then PC + 1. */
static int op_fight_waza_touch(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t attacker=0;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker))return stop(sc,"attacker");
    (void)get(s,attacker,0xD9);
    sc->pc+=1;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x5C fn_8021E9F4 ----------------------------------------------------- */

/* Operand: target kind. fightOutPokemonDamageEffect returns at once when the
 * target has no battler model (0xEE); with one it blinks it eight times
 * (3 + 2 frames), which needs the model and is refused. */
static int op_damage_blink(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t kind=0,target=0,attacker=0,flags=0;
    int fault=0;
    if(!c3_battle_script_fetch(sc,1,1,&kind))return C3_SCRIPT_HANDLER_FAIL;
    if(!c3_battle_target(&s->arena,s->floor,(uint8_t)kind,0,&target))return stop(sc,"damage blink target kind");
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker))return stop(sc,"attacker");
    uint32_t fw=get(s,attacker,0xD9);
    int hit=waza_hit(s,fw,&fault);
    if(fault)return stop(sc,"damage blink hit check");
    if(hit){
        if(!c3_battle_script_read(sc,HIT_MARKER,4,&flags))return stop(sc,"hit marker");
        if(!(flags&0x100u)&&(flags&0x80u)&&get(s,target,0xEE))return stop(sc,"target damage blink (battler model)");
    }
    sc->pc+=2;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x0B HP update (fn_80226914) --------------------------------------- */

static int set_field(C3BattleState *s,uint32_t fo,uint32_t sel,uint32_t value,int *fault)
{if(!c3_battle_fight_status_set(&s->arena,fo,sel,0,value))*fault=1;return 1;}
/* fightOutPokemonAddFightOutPokemonEnemyDamage(self, other, amount): the
 * other battler's enemy entry (0x122, 4 x 12 bytes) whose target is self. */
static void enemy_damage(C3BattleState *s,uint32_t self,uint32_t other,uint16_t amount,int *fault)
{
    if(!self||!other||self==other)return;
    uint8_t *entries=c3_battle_arena_bytes(&s->arena,other+0x6B0u,48);
    if(!entries){*fault=1;return;}
    int created=0;
    for(unsigned i=0;i<4;++i)if(entries[12*i]|entries[12*i+1]|entries[12*i+2]|entries[12*i+3])created=1;
    if(!created){*fault=2;return;}           /* battle-start enemy arrays were never created */
    for(unsigned i=0;i<4;++i){                                   /* fightOutPokemonEnemySearchAry */
        uint8_t *e=entries+12*i;
        uint32_t target=(uint32_t)e[0]<<24|(uint32_t)e[1]<<16|(uint32_t)e[2]<<8|e[3];
        if(!target||target!=self)continue;
        uint16_t damage=(uint16_t)(((uint16_t)e[8]<<8|e[9])+amount);
        e[8]=(uint8_t)(damage>>8);e[9]=(uint8_t)damage;
        return;
    }
}
static uint16_t pokemon_hp(const C3BattleState *s,uint32_t pokemon,uint32_t offset,int *fault)
{
    const uint8_t *p=pokemon?c3_battle_arena_bytes(&s->arena,pokemon+offset,2):NULL;
    if(!p){*fault=1;return 0;}
    return (uint16_t)((uint16_t)p[0]<<8|p[1]);
}
static int op_hp_update(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t type=0,attacker=0,ally=0,kind=0,flags=0,target=0;
    int fault=0;
    if(!c3_battle_floor_value(s,0x14,&type))return stop(sc,"fight type");
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker)||!c3_battle_target(&s->arena,s->floor,0x12,0,&ally))
        return stop(sc,"HP update targets");
    uint32_t fw=get(s,attacker,0xD9);
    uint16_t move=fw?(uint16_t)waza(s,fw,0,0x28):0;
    int32_t damage=(int32_t)waza(s,fw,0,0x2D);
    uint32_t initial=waza(s,fw,0,0x2E);
    uint16_t category=(uint16_t)waza(s,fw,0,0x30);
    if(!c3_battle_script_fetch(sc,1,1,&kind))return C3_SCRIPT_HANDLER_FAIL;
    if(!c3_battle_state_target_now(s,(uint16_t)kind,0,&target))return stop(sc,"HP update target kind");
    int hit=waza_hit(s,fw,&fault);
    if(fault)return stop(sc,"HP update hit check");
    if(!hit){
        if(get(s,target,0x11B)==0)set_field(s,target,0x11B,0xFFFF,&fault);
        if(fault)return stop(sc,"HP update record");
        sc->pc+=2;
        return C3_SCRIPT_HANDLER_DONE;
    }
    uint32_t pokemon=battler_pokemon(s,target);
    int32_t hp=pokemon_hp(s,pokemon,0x8A,&fault),maximum=pokemon_hp(s,pokemon,0x8C,&fault);
    if(fault)return stop(sc,"target Pokemon record");
    if(!c3_battle_script_read(sc,HIT_MARKER,4,&flags))return stop(sc,"hit marker");
    if(c3_battle_condition_check(s,target,0x14,&fault)==1&&!(flags&0x100u))
        return stop(sc,"Substitute damage (fightOutPokemonGetJoutaiMigawariHp)");
    flags&=~0x100u;
    uint32_t effect;int32_t final;
    uint32_t source=kind==0x12?attacker:ally;
    uint16_t relative=0;
    if(damage<0){                                                /* healing */
        effect=initial;final=hp-damage;
        if(final>maximum)final=maximum;
    }else{
        if(hp>damage){effect=(uint32_t)damage;final=hp-damage;}else{effect=(uint32_t)hp;final=0;}
        if(flags&0x20u)flags&=~0x20u;
        else{
            int16_t previous=(int16_t)get(s,target,0xF5);
            previous=(int16_t)(previous+damage);
            set_field(s,target,0xF5,(uint16_t)previous,&fault);
            if(!c3_battle_state_relative_target_id(s,source,(uint16_t)type,&relative))return stop(sc,"relative target ID");
            set_field(s,target,0xF6,relative,&fault);
            enemy_damage(s,target,attacker,(uint16_t)effect,&fault);
            if(fault==2)return stop(sc,"AI damage memory not created (battle start fightFloorCreateFightPokemonEnemyAryEnemySideAll)");
        }
        if(get(s,target,0x11B)==0&&!(flags&0x100000u))set_field(s,target,0x11B,effect,&fault);
        const C3BattleDamageTables *t=&h->damage;
        uint8_t mode=t->types&&category<t->type_count?t->types[(uint32_t)category*44u]:0;   /* fn_8010C4A0 */
        if((mode==1&&!(flags&0x100000u)&&move!=0xDC)||(mode==2&&!(flags&0x100000u))){
            uint32_t value=mode==1?0x102:0x104,mine=mode==1?0x11C:0x11E;
            set_field(s,target,value,effect,&fault);set_field(s,target,mine,effect,&fault);
            if(!c3_battle_state_relative_target_id(s,source,(uint16_t)type,&relative))return stop(sc,"relative target ID");
            set_field(s,target,value+1,relative,&fault);set_field(s,target,mine+1,relative,&fault);
        }
    }
    flags&=~0x100000u;
    c3_battle_script_write(sc,HIT_MARKER,4,flags);
    if(!c3_battle_waza_status_set(&s->arena,fw,0x2E,0,effect))fault=1;
    uint8_t *hp_bytes=c3_battle_arena_bytes(&s->arena,pokemon+0x8A,2);
    if(!hp_bytes)return stop(sc,"target Pokemon record");
    hp_bytes[0]=(uint8_t)((uint32_t)final>>8);hp_bytes[1]=(uint8_t)final;
    if(get(s,target,0xD6)==get(s,target,0xD7)){                   /* fightOutPokemonIsUseHensinBuff */
        uint32_t from=get(s,target,0xD7),to=get(s,target,0xD5);  /* SetHensinPokemonStatusId(0x83, 0, 0) */
        if(from&&to){
            uint32_t src=get(s,from,0xCC),dst=get(s,to,0xCC);
            uint16_t copied=pokemon_hp(s,src,0x8A,&fault);
            uint8_t *d=dst?c3_battle_arena_bytes(&s->arena,dst+0x8A,2):NULL;
            if(!d)fault=1;else{d[0]=(uint8_t)(copied>>8);d[1]=(uint8_t)copied;}
        }
    }
    if(fault)return stop(sc,"HP update record");
    if(present(h,C3_PRESENT_STATUS_MENU_SHOWN,target,type)!=1)present(h,C3_PRESENT_ACTOR_BEGIN,target,type);
    present(h,C3_PRESENT_HP_BAR,target,(uint16_t)final);
    sc->pc+=2;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x0D critical-hit message (fn_80226730) ----------------------------- */

#define CRITICAL_MESSAGE 0x7631u
static int op_critical_message(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t attacker=0;
    int fault=0;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker))return stop(sc,"attacker");
    uint32_t fw=get(s,attacker,0xD9);
    if((uint8_t)waza(s,fw,0,0x2B)==2){
        int hit=waza_hit(s,fw,&fault);
        if(fault)return stop(sc,"critical message hit check");
        if(hit){
            if(!c3_battle_floor_status_set(&s->arena,s->floor,s->floor,0x52,0,CRITICAL_MESSAGE))
                return stop(sc,"floor critical message");
            if(present(h,C3_PRESENT_MESSAGE_OPEN,CRITICAL_MESSAGE,0)==1)c3_battle_script_write(sc,WORK_BASE+7,1,1);
        }
    }
    sc->pc+=1;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x12 timed message close (fn_8022622C) ------------------------------ */

/* fightMainWaitFrame(operand) then fightMenuCloseMsg() when work byte 7 says
 * a message window is open; PC + 3 either way. A zero-frame wait returns at
 * once in the original, so it does not suspend the script here. */
static int op_message_close(C3BattleHandlers *h,C3BattleScript *sc,unsigned phase)
{
    uint32_t open=0,frames=0;
    if(phase==0){
        if(!c3_battle_script_read(sc,WORK_BASE+7,1,&open))return stop(sc,"message flag");
        if(!open){sc->pc+=3;return C3_SCRIPT_HANDLER_DONE;}
        if(!c3_battle_script_fetch(sc,1,2,&frames))return C3_SCRIPT_HANDLER_FAIL;
        if(frames){sc->wait_frames=(uint16_t)frames;return C3_SCRIPT_HANDLER_WAIT;}
    }
    present(h,C3_PRESENT_MESSAGE_CLOSE,0,0);
    c3_battle_script_write(sc,WORK_BASE+7,1,0);
    sc->pc+=3;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x0F effectiveness message (fn_8022631C) ---------------------------- */

/* fightOutPokemonGetSoubiItemDataId (0x802040E8): 0 without a Pokemon or
 * under condition 0x3D, else the held item (PokemonBios +0x88). */
static int held_item_id(const C3BattleState *s,uint32_t fo,uint16_t *item,int *fault)
{
    uint32_t pokemon=battler_pokemon(s,fo);
    *item=0;
    if(!pokemon)return 1;
    if(c3_battle_condition_check(s,fo,0x3D,fault)==1)return 1;
    const uint8_t *p=c3_battle_arena_bytes(&s->arena,pokemon+0x88,2);
    if(!p)return 0;
    *item=(uint16_t)((uint16_t)p[0]<<8|p[1]);
    return 1;
}
/* fightWazaIsMix: two or more of conditions 0x41..0x47 (lbl_80279D08). */
static int waza_mix(const C3BattleState *s,uint32_t fw,int *fault)
{
    unsigned n=0;
    for(uint16_t id=0x41;id<=0x47;++id)n+=(unsigned)waza_is(s,fw,id,fault);
    return n>=2;
}
/* Miss messages by work byte 6 (lbl_80279D18). */
static const uint16_t miss_messages[8]={0x753E,0x753F,0x7542,0x7540,0x7541,0x7645,0x7645,0x7549};

/* The nested sequences are fn_80211B94 calls; the original then returns
 * without advancing, so the opcode runs again from the start: every phase
 * here is a fresh execution. */
static int op_effect_message(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t attacker=0,defender=0,work=0;
    uint16_t item=0,message=0;
    int fault=0;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker)||!c3_battle_target(&s->arena,s->floor,0x12,0,&defender))
        return stop(sc,"effectiveness message targets");
    uint32_t fw=get(s,attacker,0xD9);
    if(!held_item_id(s,defender,&item,&fault))return stop(sc,"defender held item");
    c3_battle_script_write(sc,WORK_BASE+7,1,0);
    if(!c3_battle_script_read(sc,WORK_BASE+6,1,&work))return stop(sc,"work byte 6");
    int missed=waza_is(s,fw,0x40,&fault),immune=waza_is(s,fw,0x43,&fault);
    if((missed&&!immune)||(missed&&work>=3)){
        if(work>=8)return stop(sc,"miss message index");
        message=miss_messages[work];
    }else if(!waza_mix(s,fw,&fault)){
        if(waza_is(s,fw,0x41,&fault))message=0x7637;
        else if(waza_is(s,fw,0x42,&fault))message=0x7636;
        else if(waza_is(s,fw,0x43,&fault))message=0x7543;
        else if(waza_is(s,fw,0x44,&fault))message=0x7632;
        else if(waza_is(s,fw,0x45,&fault))message=0x7647;
        else if(waza_is(s,fw,0x46,&fault))message=0x75C9;
        else if(waza_is(s,fw,0x47,&fault))return stop(sc,"Focus Band message sequence (lbl_80379BE7)");
    }else{
        if(waza_is(s,fw,0x43,&fault))message=0x7543;
        else if(waza_is(s,fw,0x44,&fault)){
            waza_erase(s,fw,0x44,&fault);waza_erase(s,fw,0x41,&fault);waza_erase(s,fw,0x42,&fault);
            if(fault)return stop(sc,"effectiveness condition");
            return c3_battle_script_call(sc,0x803791C7u,sc->action,0)?C3_SCRIPT_HANDLER_CALL:stop(sc,"sequence 0x803791C7");
        }else if(waza_is(s,fw,0x46,&fault)){
            waza_erase(s,fw,0x46,&fault);waza_erase(s,fw,0x47,&fault);
            if(fault)return stop(sc,"effectiveness condition");
            return c3_battle_script_call(sc,0x803791BEu,sc->action,0)?C3_SCRIPT_HANDLER_CALL:stop(sc,"sequence 0x803791BE");
        }else if(waza_is(s,fw,0x47,&fault))return stop(sc,"Focus Band message sequence (lbl_80379BE7)");
        else if(waza_is(s,fw,0x45,&fault))message=0x7647;
    }
    if(fault)return stop(sc,"effectiveness condition");
    if(message){
        if(!c3_battle_floor_status_set(&s->arena,s->floor,s->floor,0x53,0,message))return stop(sc,"floor effect message");
        if(item)return stop(sc,"held item in the effectiveness message");
        if(present(h,C3_PRESENT_EFFECT_MESSAGE,message,defender)==1)c3_battle_script_write(sc,WORK_BASE+7,1,1);
    }
    sc->pc+=1;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x0C HP bar wait (fn_802267E8) ------------------------------------- */

/* Operand: target kind. With the target's status menu shown, the original
 * loops one frame at a time (menuFightStatusWaitAnimeHP) until its HP bar
 * stops; each resumed phase here repeats the step from the start, which
 * re-asks the same questions. fightOutPokemonSetWazaEffectDownFlag after
 * fightOutPokemonIsHinsi only reaches a battler model (0xEE), so without one
 * it has no effect; the Substitute-break sequence (condition 0x14) is
 * refused, as in the HP update. */
static int op_hp_bar_wait(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t type=0,kind=0,target=0;
    int fault=0;
    if(!c3_battle_floor_value(s,0x14,&type))return stop(sc,"fight type");
    if(!c3_battle_script_fetch(sc,1,1,&kind))return C3_SCRIPT_HANDLER_FAIL;
    if(!c3_battle_state_target_now(s,(uint16_t)kind,0,&target))return stop(sc,"HP bar target kind");
    if(present(h,C3_PRESENT_STATUS_MENU_SHOWN,target,type)==1&&present(h,C3_PRESENT_HP_BAR_BUSY,target,0)==1){
        sc->wait_frames=1;return C3_SCRIPT_HANDLER_WAIT;           /* _threadSwitch */
    }
    if(get(s,target,0xEE))return stop(sc,"fainting effect flag (battler model)");
    if(c3_battle_condition_check(s,target,0x14,&fault)==1)return stop(sc,"Substitute break sequence (lbl_80379A22)");
    if(fault)return stop(sc,"HP bar wait condition");
    sc->pc+=2;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x15 secondary-effect chance (fn_80224820) -------------------------- */

#define EFFECT_ID_BYTE (WORK_BASE+3)   /* lbl_80478D78[3]: pending secondary effect */
#define EFFECT_FLAG_BYTE 0x8047B625u   /* lbl_8047B625 */
/* Closes the status menus and the message, then rolls the move's effect
 * chance (move row 0xC, doubled by Serene Grace 0x20). fn_8000817C reads the
 * debug menu's always-apply toggle (lbl_8047A271, .sbss, only written by the
 * debug settings menu fn_80007B30), which is 0 in play. A pending effect
 * that applies runs fn_802249B8, which is not lowered yet: refused. The
 * draw happens whenever the first (bit 0x80) branch is not taken, even with
 * no effect pending. */
static int secondary_effect(C3BattleHandlers *h,C3BattleScript *sc,uint8_t param1,uint8_t param2);
/* After fn_802249B8 (skipIncrement): work byte 3 and 0x8047B625 cleared. */
static int effect_chance_finish(C3BattleScript *sc,int applied)
{
    if(applied==0)return C3_SCRIPT_HANDLER_FAIL;
    if(applied==2)return C3_SCRIPT_HANDLER_CALL;           /* the effect's sequence runs first */
    c3_battle_script_write(sc,EFFECT_ID_BYTE,1,0);
    c3_battle_script_write(sc,EFFECT_FLAG_BYTE,1,0);
    return C3_SCRIPT_HANDLER_DONE;
}
static int op_effect_chance(C3BattleHandlers *h,C3BattleScript *sc,unsigned phase)
{
    C3BattleState *s=h->state;
    uint32_t attacker=0,effect=0;
    int fault=0;
    if(phase==1){sc->pc+=1;return effect_chance_finish(sc,1);}   /* fn_802249B8's PC + 1 after its sequence */
    present(h,C3_PRESENT_TRAINER_MENUS_CLOSE,0,0);
    present(h,C3_PRESENT_STATUS_MENUS_CLOSE,0,0);
    present(h,C3_PRESENT_MESSAGE_CLOSE,0,0);
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker))return stop(sc,"attacker");
    uint32_t fw=get(s,attacker,0xD9);
    uint16_t move=fw?(uint16_t)waza(s,fw,0,0x28):0;
    uint16_t ability=(uint16_t)get(s,attacker,0x100);
    uint16_t chance=(uint16_t)(uint8_t)waza(s,0,move,0xC);
    if(ability==0x20)chance=(uint16_t)(chance*2u);
    if(!c3_battle_script_read(sc,EFFECT_ID_BYTE,1,&effect))return stop(sc,"effect byte");
    if((effect&0x80u)&&waza_hit(s,fw,&fault)){
        c3_battle_script_write(sc,EFFECT_ID_BYTE,1,effect&0x7Fu);
        return effect_chance_finish(sc,secondary_effect(h,sc,0,0x80));
    }
    if(fault)return stop(sc,"effect chance hit check");
    uint16_t random=0;
    if(!c3_battle_rng_next16(&s->rng,&random))return stop(sc,"random stream");
    if(random%100u<=chance&&effect!=0){
        int hit=waza_hit(s,fw,&fault);
        if(fault)return stop(sc,"effect chance hit check");
        if(hit)return effect_chance_finish(sc,secondary_effect(h,sc,0,chance>=100?0x80:0));
    }
    sc->pc+=1;
    c3_battle_script_write(sc,EFFECT_ID_BYTE,1,0);
    c3_battle_script_write(sc,EFFECT_FLAG_BYTE,1,0);
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x19 faint check (fn_80224158) -------------------------------------- */

/* fightFloorSetStatus(0x4B, fo): message variables 0x1E (the battler) and
 * 0x1C (its ability's name), cleared for 0 or an invalid battler, then the
 * appointed-Pokemon field (fightFloorBiosSetAppointPokemonPtr, FightFloor
 * +0xA494; the generic floor setter leaves such target fields out). */
#define APPOINT_OFFSET 0xA494u
static int appointed_get(const C3BattleState *s,uint32_t *fo)
{
    const uint8_t *p=c3_battle_arena_bytes(&s->arena,s->floor+APPOINT_OFFSET,4);
    if(!p)return 0;
    *fo=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
    return 1;
}
static int floor_appoint(C3BattleState *s,uint32_t fo)
{
    int fault=0;
    if(fo&&c3_battle_fight_out_check_valid(s,fo,&fault)){
        c3_battle_msgctrl_set(s,0x1E,fo);
        c3_battle_msgctrl_set(s,0x1C,C3_MSGREF_ABILITY_NAME((uint16_t)get(s,fo,0x100)));
    }else{
        c3_battle_msgctrl_set(s,0x1E,0);
        c3_battle_msgctrl_set(s,0x1C,0);
    }
    uint8_t *p=c3_battle_arena_bytes(&s->arena,s->floor+APPOINT_OFFSET,4);
    if(fault||!p)return 0;
    p[0]=(uint8_t)(fo>>24);p[1]=(uint8_t)(fo>>16);p[2]=(uint8_t)(fo>>8);p[3]=(uint8_t)fo;
    return 1;
}
/* Operands: target kind, mode byte, jump address. Mode 0 with kind 0x11 /
 * 0x12 / 0x15 checks that battler; when it fainted it gets 0x120 = 1 and the
 * faint sequence runs with it appointed (floor 0x4B), after which 0x4B is
 * restored and the script continues. */
static int op_faint_check(C3BattleHandlers *h,C3BattleScript *sc,unsigned phase)
{
    C3BattleState *s=h->state;
    uint32_t kind=0,mode=0,flags=0;
    if(phase==1){
        if(!floor_appoint(s,h->saved_appoint))return stop(sc,"floor appointed Pokemon");
        sc->pc+=7;
        return C3_SCRIPT_HANDLER_DONE;
    }
    present(h,C3_PRESENT_TRAINER_MENUS_CLOSE,0,0);
    present(h,C3_PRESENT_STATUS_MENUS_CLOSE,0,0);
    present(h,C3_PRESENT_MESSAGE_CLOSE,0,0);
    if(!c3_battle_script_fetch(sc,1,1,&kind)||!c3_battle_script_fetch(sc,2,1,&mode))return C3_SCRIPT_HANDLER_FAIL;
    if(mode){
        uint32_t target=0;
        if(!c3_battle_state_target_now(s,(uint16_t)kind,0,&target))return stop(sc,"faint check target kind");
        int fainted=c3_battle_fight_out_is_fainted(s,target);
        if(fainted<0)return stop(sc,"faint check record");
        if(fainted)return stop(sc,"fainted side branch (fightSideSetStatus 8)");
        sc->pc+=7;
        return C3_SCRIPT_HANDLER_DONE;
    }
    uint32_t fainter=0;uint32_t sequence=0x80378703u;            /* lbl_80378703 */
    uint8_t subject=0;
    if(kind==0x11){subject=0x11;sequence=0x803786F4u;}
    else if(kind==0x12)subject=0x12;
    else if(kind==0x15){subject=0x15;sequence=0x80378712u;}
    if(subject&&!c3_battle_target(&s->arena,s->floor,subject,0,&fainter))return stop(sc,"faint check battler");
    int fainted=c3_battle_fight_out_is_fainted(s,fainter);
    if(fainted<0)return stop(sc,"faint check record");
    if(!fainted){sc->pc+=7;return C3_SCRIPT_HANDLER_DONE;}
    if(!fainter)return stop(sc,"faint sequence without a battler");
    if(!c3_battle_fight_status_set(&s->arena,fainter,0x120,0,1))return stop(sc,"fainted flag");
    int hero=c3_battle_fight_out_is_gc_hero(s,fainter);
    if(hero<0)return stop(sc,"player battler check");
    if(!c3_battle_script_read(sc,HIT_MARKER,4,&flags))return stop(sc,"hit marker");
    if(hero){
        uint32_t friendship=0;
        flags|=0x400000u;c3_battle_script_write(sc,HIT_MARKER,4,flags);
        if(!c3_battle_floor_value(s,0x27,&friendship))return stop(sc,"floor field 0x27");
        if((friendship&0xFF)==1)return stop(sc,"faint friendship loss (fn_80224158_faintFriendship)");
    }
    if(!(flags&0x1000000u)){
        uint32_t defender=0;int fault=0;
        if(!c3_battle_target(&s->arena,s->floor,0x12,0,&defender))return stop(sc,"defender");
        if(c3_battle_condition_check(s,defender,0x28,&fault)==1)return stop(sc,"Grudge (lbl_80379167)");
        if(fault)return stop(sc,"Grudge condition");
    }
    if(flags&0x40u)return stop(sc,"Destiny Bond (lbl_80378D54)");
    if(!appointed_get(s,&h->saved_appoint))return stop(sc,"floor appointed Pokemon");
    if(!floor_appoint(s,fainter))return stop(sc,"floor appointed Pokemon");
    return c3_battle_script_call(sc,sequence,sc->action,0)?C3_SCRIPT_HANDLER_CALL:stop(sc,"faint sequence");
}

/* ---- 0x49 move end (fn_8021FAD4) ----------------------------------------- */

#define MOVE_END_STAGE 0x8047B627u   /* lbl_8047B627: next stage, 0x11 when done */
/* fightOutPokemonIsJoutaiKie: the first semi-invulnerable condition held
 * (0x1F, 0x20, 0x21: lbl_8047E510), else 0. */
static uint16_t joutai_kie(const C3BattleState *s,uint32_t fo,int *fault)
{
    static const uint16_t ids[3]={0x1F,0x20,0x21};
    if(!fo)return 0;
    for(unsigned i=0;i<3;++i)if(c3_battle_condition_check(s,fo,ids[i],fault)==1)return ids[i];
    return 0;
}
static int checked_get(const C3BattleState *s,uint32_t obj,uint32_t sel,uint32_t *v,int *fault)
{if(!c3_battle_fight_status_get(&s->arena,obj,sel,0,v)){*fault=1;*v=0;return 0;}return 1;}
static int fight_out(const C3BattleState *s,uint32_t fo,int *fault)
{return fo&&c3_battle_fight_out_check_fight_out(s,fo,fault)==1;}
/* fightFloorSetStatus(0x56, item): message variable 0x29 (the item's name)
 * and the appointed item (FightFloor +0xA4BC). */
static int floor_appoint_item(C3BattleState *s,uint16_t item)
{
    uint8_t *p=c3_battle_arena_bytes(&s->arena,s->floor+0xA4BCu,2);
    if(!p)return 0;
    c3_battle_msgctrl_set(s,0x29,C3_MSGREF_ITEM_NAME(item));
    p[0]=(uint8_t)(item>>8);p[1]=(uint8_t)item;
    return 1;
}
/* fightOutPokemonSetOumuWazaDataId(self, other, move): self's 0xF7, then
 * the mimic move (+4) of self's enemy entry for other. */
static int set_oumu(C3BattleState *s,uint32_t self,uint32_t other,uint16_t move)
{
    if(!self)return 1;
    if(!c3_battle_fight_status_set(&s->arena,self,0xF7,0,move))return 0;
    if(!other)return 0;                                           /* every-entry branch: not reached here */
    uint8_t *entries=c3_battle_arena_bytes(&s->arena,self+0x6B0u,48);
    if(!entries)return 0;
    for(unsigned i=0;i<4;++i){                                    /* fightOutPokemonEnemySearchAry(.., 4, other) */
        uint8_t *e=entries+12*i;
        uint32_t t=(uint32_t)e[0]<<24|(uint32_t)e[1]<<16|(uint32_t)e[2]<<8|e[3];
        if(!t||t!=other)continue;
        e[4]=(uint8_t)(move>>8);e[5]=(uint8_t)move;
        return 1;
    }
    return 1;
}
/* fn_8022D394 status-curing abilities: ability, conditions cured. */
static const struct{uint16_t ability,c1,c2;}curing[]={
    {0x11,3,4},{0x14,9,9},{7,5,5},{0xF,8,8},{0x48,8,8},{0x29,6,6},{0x28,7,7},{0xC,10,10}};
/* fn_8022D6BC after-hit abilities of the defender. */
static const uint16_t after_hit[]={0x10,0x18,0x1B,0x26,0x09,0x31,0x38};

/* Operands: mode byte, stop stage. The original runs stages 0..16 in one
 * call (a do-while that leaves as soon as the stage reaches 0x11), each a
 * reaction that may call a nested sequence and stop. Stage 17, which closes
 * the menus and the message in mode 0, runs only when the command is entered
 * at stage 17 (after a reaction at stage 16). Mode 1 runs one stage; mode 2
 * stops after the given stage. Every reaction that would start a sequence,
 * draw a random number, or reach a battler model is refused by name before
 * it changes anything; the stages that only keep records (11, 14, 15) run as
 * the original does. Held items: their effect IDs come from the item table,
 * which the state does not hold yet, so a held item on a battler a stage
 * inspects is refused. */
static int op_move_end(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t mode=0,stop_stage=0,stage=0,flags=0,type=0,attacker=0,defender=0;
    int fault=0;
    if(!c3_battle_script_fetch(sc,1,1,&mode)||!c3_battle_script_fetch(sc,2,1,&stop_stage))return C3_SCRIPT_HANDLER_FAIL;
    if(!c3_battle_floor_value(s,0x14,&type))return stop(sc,"fight type");
    if(!c3_battle_state_target_now(s,0x11,0,&attacker)||!c3_battle_state_target_now(s,0x12,0,&defender))
        return stop(sc,"move end targets");
    uint32_t fw=attacker?get(s,attacker,0xD9):0;
    int hit=attacker?waza_hit(s,fw,&fault):0;
    uint16_t used=attacker&&fw?(uint16_t)waza(s,fw,0,0x28):0;
    uint16_t original=attacker&&fw?(uint16_t)waza(s,fw,0,0x27):0;
    uint16_t valid_original=original==0xFFFF?0:original;
    uint16_t power=attacker?(uint16_t)waza(s,fw,0,0x30):9;
    uint8_t move_target=attacker?(uint8_t)waza(s,0,used,5):1;
    uint16_t move_joutai=attacker?(uint16_t)waza(s,0,used,7):0;
    int16_t dealt_hp=0,dealt_other=0;
    if(defender){
        uint32_t v1=0,v2=0;
        checked_get(s,defender,0x11C,&v1,&fault);checked_get(s,defender,0x11E,&v2,&fault);
        dealt_hp=(int16_t)v1;dealt_other=(int16_t)v2;
    }
    int dealt=dealt_hp!=0||dealt_other!=0;
    if(fault)return stop(sc,"move end records");
    uint32_t list[8];unsigned n=0;
    if(!c3_battle_state_valid_fight_outs(s,list,&n))return stop(sc,"valid battlers");
    for(;;){
        if(!c3_battle_script_read(sc,MOVE_END_STAGE,1,&stage)||!c3_battle_script_read(sc,HIT_MARKER,4,&flags))
            return stop(sc,"move end stage");
        if(stage>0x11)return stop(sc,"move end stage out of range");
        switch(stage){
        case 0:                                                    /* Rage */
            if(c3_battle_condition_check(s,defender,0x13,&fault)==1&&hit&&dealt&&move_joutai)
                return stop(sc,"Rage (lbl_80378D02)");
            break;
        case 1:                                                    /* thaw by a fire move */
            if(c3_battle_condition_check(s,defender,7,&fault)==1&&hit&&(uint16_t)waza(s,0,used,3)==10)
                return stop(sc,"thaw (lbl_803792C9)");
            break;
        case 2:case 5:{                                            /* Synchronize (fn_8022D2CC / fn_8022D20C) */
            uint32_t who=stage==2?defender:attacker;
            if(fight_out(s,who,&fault)&&(uint16_t)get(s,who,0x100)==0x1C&&(flags&0x4000u))
                return stop(sc,"Synchronize (lbl_80379945)");
            break;
        }
        case 3:{                                                   /* fn_8022D6BC */
            uint16_t ability=(uint16_t)get(s,defender,0x100);
            for(unsigned i=0;i<sizeof after_hit/sizeof *after_hit;++i)
                if(ability==after_hit[i]&&hit&&dealt)return stop(sc,"after-hit ability (fn_8022D6BC)");
            break;
        }
        case 4:                                                    /* fn_8022D394 over the battlers */
            for(unsigned i=0;i<n;++i){
                if(!fight_out(s,list[i],&fault))continue;
                uint16_t ability=(uint16_t)get(s,list[i],0x100);
                for(unsigned k=0;k<sizeof curing/sizeof *curing;++k)
                    if(ability==curing[k].ability&&(c3_battle_condition_check(s,list[i],curing[k].c1,&fault)==1||
                       c3_battle_condition_check(s,list[i],curing[k].c2,&fault)==1))
                        return stop(sc,"status-curing ability (lbl_8037994A)");
            }
            break;
        case 6:{                                                   /* Choice Band lock */
            uint16_t effect=0;
            if((flags&0x2000000u)&&!held_item_effect(s,attacker,&effect))return stop(sc,"held item (Choice Band check)");
            if(c3_battle_condition_check(s,attacker,0x36,&fault)==1)return stop(sc,"Choice lock (condition 0x36)");
            break;
        }
        case 7:                                                    /* fn_802207D4 over the battlers */
            for(unsigned i=0;i<n;++i){
                uint32_t pending=0;
                checked_get(s,list[i],0xFB,&pending,&fault);
                if((uint16_t)pending)return stop(sc,"pending held item (0xFB, fn_802207D4)");
            }
            break;
        case 8:                                                    /* fn_8022B5C8 over the battlers */
            for(unsigned i=0;i<n;++i){
                uint16_t item=0;
                if(!fight_out(s,list[i],&fault))continue;
                if(!held_item_id(s,list[i],&item,&fault))return stop(sc,"held item");
                if(!floor_appoint_item(s,item))return stop(sc,"floor appointed item");
                if(item)return stop(sc,"held item reaction (fn_8022B5C8)");
            }
            break;
        case 9:{                                                   /* fn_8022BB84 */
            uint16_t effect=0;
            if(fw&&waza(s,fw,0,0x2D)&&!held_item_effect(s,attacker,&effect))
                return stop(sc,"held item after damage (fn_8022BB84)");
            break;
        }
        case 10:                                                   /* semi-invulnerable attacker */
            if(fight_out(s,attacker,&fault)&&joutai_kie(s,attacker,&fault))
                return stop(sc,"semi-invulnerable visibility (battler model)");
            break;
        case 11:
            /* Stage 10 refused a semi-invulnerable attacker in battle, so
             * the condition reduces to the attacker being in battle; no
             * semi-invulnerable condition is left for InitJoutaiKie. */
            if(fight_out(s,attacker,&fault)){
                if(!c3_battle_fight_status_set(&s->arena,attacker,0x115,0,1))return stop(sc,"attacker field 0x115");
                if(get(s,attacker,0xEE))return stop(sc,"attacker visibility (battler model)");
            }
            break;
        case 12:
            if(fight_out(s,defender,&fault)){
                uint32_t shown=0;
                checked_get(s,defender,0x115,&shown,&fault);
                if(!shown&&!joutai_kie(s,defender,&fault)&&get(s,defender,0xEE))
                    return stop(sc,"defender visibility (battler model)");
            }
            break;
        case 13:                                                   /* Substitute over the battlers */
            for(unsigned i=0;i<n;++i)
                if(c3_battle_condition_check(s,list[i],0x14,&fault)==1)return stop(sc,"Substitute end (condition 0x14)");
            break;
        case 14:{                                                  /* last-move bookkeeping */
            if(flags&0x1000u)return stop(sc,"attacker/defender swap (fightFloorReplaceFightOutPokemonAttackToDefense)");
            uint16_t effect=(uint16_t)waza(s,0,valid_original,9);
            if(effect!=0x7F||!hit)set_field(s,attacker,0xEF,original,&fault);
            if(fight_out(s,attacker,&fault)&&effect!=0x7F){
                if(flags&0x2000000u){set_field(s,attacker,0xF0,original,&fault);set_field(s,attacker,0xF1,used,&fault);}
                else{set_field(s,attacker,0xF0,0xFFFF,&fault);set_field(s,attacker,0xF1,0xFFFF,&fault);}
                if(fight_out(s,defender,&fault)){
                    uint16_t id=0;
                    if(!c3_battle_state_relative_target_id(s,attacker,(uint16_t)type,&id))return stop(sc,"relative target ID");
                    set_field(s,defender,0xF2,id,&fault);
                }
                if((flags&0x2000000u)&&hit){
                    if(original==0xFFFF)set_field(s,defender,0xF3,original,&fault);
                    else{set_field(s,defender,0xF3,used,&fault);set_field(s,defender,0xF4,power,&fault);}
                }else set_field(s,defender,0xF3,0xFFFF,&fault);
            }
            break;
        }
        case 15:{                                                  /* Mirror Move record */
            int av=attacker&&c3_battle_fight_out_check_valid(s,attacker,&fault)==1;
            int dv=defender&&c3_battle_fight_out_check_valid(s,defender,&fault)==1;
            if(av&&(uint8_t)waza(s,0,valid_original,0x11)==1&&(flags&0x2000000u)&&attacker!=defender&&dv&&hit&&
               !set_oumu(s,defender,attacker,original))return stop(sc,"Mirror Move record");
            break;
        }
        case 16:{                                                  /* spread move: next target */
            uint32_t active=0,locked=0;
            if(!c3_battle_floor_value(s,0x19,&active))return stop(sc,"floor field 0x19");
            if(attacker)checked_get(s,attacker,0x109,&locked,&fault);
            if(!(flags&0x80000u)&&(uint16_t)active>=2&&!locked&&move_target==4&&!(flags&0x200u)){
                uint32_t other=0;
                if(!c3_battle_state_target_now(s,0xE,defender,&other))return stop(sc,"spread move partner");
                if(fight_out(s,other,&fault))return stop(sc,"spread move second target (lbl_80379BF4)");
                c3_battle_script_write(sc,HIT_MARKER,4,flags|0x200u);
            }
            break;
        }
        case 17:
            if(mode==0){
                present(h,C3_PRESENT_TRAINER_MENUS_CLOSE,0,0);
                present(h,C3_PRESENT_STATUS_MENUS_CLOSE,0,0);
                present(h,C3_PRESENT_MESSAGE_CLOSE,0,0);
            }
            break;
        }
        if(fault)return stop(sc,"move end records");
        if(stage<0x11)++stage;
        if(mode==1)stage=0x11;
        if(mode==2&&(uint8_t)stop_stage==stage)stage=0x11;
        if(!c3_battle_script_write(sc,MOVE_END_STAGE,1,stage))return stop(sc,"move end stage");
        if(stage==0x11)break;
    }
    sc->pc+=3;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- end-of-move passes (fn_80211B94) ------------------------------------ */

/* fightFloorSetStatus(0x36, fo): message variables 0x0F, 0x1A (its ability's
 * name), 0x1F, 0x21, 0x20, cleared for 0 or an invalid battler, then the
 * attacker field (FightFloor +0xA474). */
static int floor_set_attacker(C3BattleState *s,uint32_t fo)
{
    int fault=0;
    uint8_t *p=c3_battle_arena_bytes(&s->arena,s->floor+0xA474u,4);
    if(!p)return 0;
    int valid=fo&&c3_battle_fight_out_check_valid(s,fo,&fault)==1;
    c3_battle_msgctrl_set(s,0x0F,valid?fo:0);
    c3_battle_msgctrl_set(s,0x1A,valid?C3_MSGREF_ABILITY_NAME((uint16_t)get(s,fo,0x100)):0);
    c3_battle_msgctrl_set(s,0x1F,valid?fo:0);
    c3_battle_msgctrl_set(s,0x21,valid?fo:0);
    c3_battle_msgctrl_set(s,0x20,valid?fo:0);
    p[0]=(uint8_t)(fo>>24);p[1]=(uint8_t)(fo>>16);p[2]=(uint8_t)(fo>>8);p[3]=(uint8_t)fo;
    return !fault;
}
static int fail_pass(C3BattleScript *sc,const char *what){(void)stop(sc,what);return 0;}
/* A floor target field without message variables (e.g. 0x46, +0xA484). */
static int floor_set_target(C3BattleState *s,uint32_t offset,uint32_t fo)
{
    uint8_t *p=c3_battle_arena_bytes(&s->arena,s->floor+offset,4);
    if(!p)return 0;
    p[0]=(uint8_t)(fo>>24);p[1]=(uint8_t)(fo>>16);p[2]=(uint8_t)(fo>>8);p[3]=(uint8_t)fo;
    return 1;
}
/* fightFloorSetStatus(0x42, fo): message variables 0x10, 0x1B (its ability's
 * name), 0x42, 0x44, 0x43, cleared for 0 or an invalid battler, then the
 * defender field (+0xA478). */
static int floor_set_defender(C3BattleState *s,uint32_t fo)
{
    int fault=0;
    int valid=fo&&c3_battle_fight_out_check_valid(s,fo,&fault)==1;
    c3_battle_msgctrl_set(s,0x10,valid?fo:0);
    c3_battle_msgctrl_set(s,0x1B,valid?C3_MSGREF_ABILITY_NAME((uint16_t)get(s,fo,0x100)):0);
    c3_battle_msgctrl_set(s,0x42,valid?fo:0);
    c3_battle_msgctrl_set(s,0x44,valid?fo:0);
    c3_battle_msgctrl_set(s,0x43,valid?fo:0);
    return !fault&&floor_set_target(s,0xA478u,fo);
}
static int appointed_attacker(const C3BattleState *s,uint32_t *fo)              /* floor 0x36 */
{
    const uint8_t *p=c3_battle_arena_bytes(&s->arena,s->floor+0xA474u,4);
    if(!p)return 0;
    *fo=(uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
    return 1;
}

/* Each pass is fightFloorLoopValidFightOutPokemon(0, callback, &arg, 0).
 * The callbacks that would run a nested sequence (fn_80211B94 inside the
 * loop) are refused by name; the rest run as the original does. */
static int end_of_move(void *u,struct C3BattleScript *sc,C3BattleScriptPass pass,uint8_t arg)
{
    C3BattleHandlers *h=u;
    C3BattleState *s=h->state;
    uint32_t list[8];unsigned n=0;
    int fault=0;
    (void)arg;
    if(!c3_battle_state_valid_fight_outs(s,list,&n))return fail_pass(sc,"valid battlers");
    switch(pass){
    case C3_SCRIPT_PASS_FAINT_MARK:                                   /* fn_8023011C */
        /* A fainted battler (0x120 == 1) becomes the fainted (floor 0x46)
         * and defending (0x42) Pokemon and its sequence lbl_80378721 (0x23
         * EXP award, return) runs; the loop then continues after it. */
        if(!arg)h->pass_cursor=0;
        for(unsigned i=h->pass_cursor;i<n;++i){
            uint32_t fainted=0;
            if(!checked_get(s,list[i],0x120,&fainted,&fault))return fail_pass(sc,"fainted flag");
            if((uint8_t)fainted!=1)continue;
            if(!floor_set_target(s,0xA484u,list[i])||!floor_set_defender(s,list[i]))return fail_pass(sc,"floor fainted Pokemon");
            h->pass_cursor=i+1u;
            return c3_battle_script_call(sc,0x80378721u,sc->action,0)?2:fail_pass(sc,"faint mark sequence");
        }
        return 1;
    case C3_SCRIPT_PASS_POST_FAINT_A:                                 /* fn_8022E34C: Intimidate */
        for(unsigned i=0;i<n;++i)
            if(fight_out(s,list[i],&fault)&&(uint16_t)get(s,list[i],0x100)==0x16&&
               c3_battle_condition_check(s,list[i],0x3B,&fault)==1)return fail_pass(sc,"Intimidate (lbl_803795F5)");
        break;
    case C3_SCRIPT_PASS_POST_FAINT_B:                                 /* fn_8022E1F8: Trace */
        for(unsigned i=0;i<n;++i)
            if(fight_out(s,list[i],&fault)&&(uint16_t)get(s,list[i],0x100)==0x24&&
               c3_battle_condition_check(s,list[i],0x3C,&fault)==1)return fail_pass(sc,"Trace (lbl_80379667)");
        break;
    case C3_SCRIPT_PASS_POST_FAINT_C:{                                /* fn_80230088: held items */
        uint32_t saved=0;
        if(!appointed_attacker(s,&saved))return fail_pass(sc,"floor attacker");
        for(unsigned i=0;i<n;++i){
            uint16_t item=0;
            if(!fight_out(s,list[i],&fault))continue;
            if(!floor_set_attacker(s,list[i]))return fail_pass(sc,"floor attacker");
            /* fn_8022BE2C(fo, 1): appoints the held item, then its effect. */
            if(!held_item_id(s,list[i],&item,&fault))return fail_pass(sc,"held item");
            if(!floor_appoint_item(s,item))return fail_pass(sc,"floor appointed item");
            if(item)return fail_pass(sc,"held item effect (fn_8022BE2C)");
            if(!floor_set_attacker(s,saved))return fail_pass(sc,"floor attacker");
        }
        break;
    }
    case C3_SCRIPT_PASS_POST_FAINT_D:                                 /* fn_8022EB9C: Forecast */
        /* fn_8022EC40 needs species 0x181 and ability 0x3B: refused on the
         * ability alone. */
        for(unsigned i=0;i<n;++i)
            if(fight_out(s,list[i],&fault)&&(uint16_t)get(s,list[i],0x100)==0x3B)
                return fail_pass(sc,"Forecast (lbl_803796D7)");
        break;
    case C3_SCRIPT_PASS_MOVE_CLEANUP:                                 /* fn_802136A4 */
        for(unsigned i=0;i<n;++i)                                     /* fightOutPokemonInitOneSelfTurn */
            for(uint32_t sel=0x113;sel<=0x11F;++sel)set_field(s,list[i],sel,0,&fault);
        break;
    default:return fail_pass(sc,"unknown end-of-move pass");
    }
    if(fault)return fail_pass(sc,"end-of-move pass records");
    return 1;
}

/* ---- 0x24 battle end check (fn_80222C44) --------------------------------- */

/* fn_801F8A18: does the trainer have a party Pokemon it can send in
 * (fightTrainerCheckCanIrekaeInline 0: in battle-ready health, not the
 * FightPokemon of one of its battlers, not reserved by one of them)? -1 when
 * the reserve check would read a battler's switch action (0xFE), which is
 * not modelled. */
static int can_get_exp(const C3BattleState *s,uint32_t trainer);   /* trainer type 1: the player */
static int can_send_in(const C3BattleState *s,uint32_t trainer,int *fault)
{
    for(unsigned j=0;j<6;++j){
        uint32_t fp=trainer+0xB1Cu+j*0x154u;
        if(!c3_battle_fight_pokemon_check_fight_out(s,fp,fault))continue;          /* 1 */
        int taken=0;
        for(unsigned i=0;i<2&&!taken;++i){
            uint32_t fo=trainer+C3_RUNTIME_FIGHT_OUT_OFFSET+i*C3_FIGHT_OUT_POKEMON_BYTES;
            if(c3_battle_fight_out_check_valid(s,fo,fault)&&get(s,fo,0xD5)==fp)taken=1; /* 2 */
        }
        if(taken)continue;
        int16_t entry=(int16_t)(uint16_t)get(s,fp,0xCE);
        for(unsigned i=0;i<2&&!taken&&entry>=0;++i){                                     /* 3 */
            uint32_t fo=trainer+C3_RUNTIME_FIGHT_OUT_OFFSET+i*C3_FIGHT_OUT_POKEMON_BYTES;
            if(!c3_battle_fight_out_check_valid(s,fo,fault))continue;
            if((int16_t)(uint16_t)get(s,fo,0x121)==entry)taken=1;
            else if(get(s,fo,0xFE))return -1;
        }
        if(!taken)return 1;
    }
    return 0;
}

/* Operand: u32 whose low byte selects the replacement check. Each side's
 * remaining HP (fightSideGetFightPokemonNokoriHp over kinds 5 and 4, side 1
 * then side 0); a side at 0 requests fight result 2 / 3 through the
 * original setter (fightFloorSetFightResultId, c3_battle_rules_combine_result,
 * floor 0x23 its draw flag): the original computes the result here, nothing
 * is assumed. A decided battle then only reviews the self-destruct draw
 * rule (result 7 with floor 0x34 and a move of effect 7 reads the rules
 * option fn_80077AF4, which is refused) and the script goes on; otherwise,
 * with the low byte 1, the replacement check and fn_80222EF0 run. The state
 * result is the battle's own variable (lbl_8047B5D8): no save or story
 * record is written here. */
static int op_battle_end_check(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t operand=0,trainers=0,pokemon=0,active=0;
    int fault=0;
    if(!c3_battle_script_fetch(sc,1,4,&operand))return C3_SCRIPT_HANDLER_FAIL;
    if(!c3_battle_floor_value(s,0x16,&trainers)||!c3_battle_floor_value(s,0x17,&pokemon)||
       !c3_battle_floor_value(s,0x18,&active))return stop(sc,"floor counts");
    static const uint16_t kinds[2]={5,4};
    for(unsigned i=0;i<2;++i){
        uint32_t side=0,hp=0;
        if(!c3_battle_state_target_now(s,kinds[i],0,&side))return stop(sc,"side target");
        if(!c3_battle_side_remaining_hp(s,side,(uint16_t)trainers,(uint16_t)pokemon,&hp))return stop(sc,"side remaining HP");
        if(!hp){
            uint32_t draw_flag=0;
            C3BattleRuleResult set={0};
            if(!c3_battle_floor_value(s,0x23,&draw_flag))return stop(sc,"floor field 0x23");
            if((uint8_t)draw_flag>1||s->result>7)return stop(sc,"fight result range");
            if(!c3_battle_rules_combine_result(s->result,kinds[i]==5?2u:3u,(uint8_t)draw_flag,&set))
                return stop(sc,"fightFloorSetFightResultId");
            s->result=set.result;
        }
    }
    if(s->result){
        uint32_t rule=0,attacker=0;
        if(!c3_battle_floor_value(s,0x34,&rule))return stop(sc,"floor field 0x34");
        if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker))return stop(sc,"attacker");
        uint32_t fw=attacker?get(s,attacker,0xD9):0;
        uint16_t move=fw?(uint16_t)waza(s,fw,0,0x28):0;
        if((uint8_t)rule==1&&(uint16_t)waza(s,0,move,9)==7&&s->result==7)
            return stop(sc,"self-destruct draw rule (rules option fn_80077AF4)");
        sc->pc+=5;
        return C3_SCRIPT_HANDLER_DONE;
    }
    if((operand&0xFFu)==1){
        /* fightFloorIsIrekaeFightOutPokemon: over the valid trainers
         * (fightFloorLoopValidFightTrainer, no threads), a trainer with a
         * party Pokemon it can send in (fn_801F8A18) and an active slot out
         * of battle needs a replacement: 2 for the player, else 1. */
        uint8_t needed=0;
        for(unsigned side=0;side<2;++side){
            uint32_t sd=s->floor+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES;
            if(!c3_battle_side_check_valid(s,sd,&fault))continue;
            for(uint32_t t=0;t<(uint16_t)trainers;++t){
                if(t>=2)return stop(sc,"more than two trainers");
                uint32_t tr=sd+C3_FIGHT_SIDE_TRAINER_OFFSET+t*C3_BATTLE_RUNTIME_TRAINER_BYTES;
                if(!c3_battle_trainer_check_valid(s,tr,&fault))continue;
                int can=can_send_in(s,tr,&fault);
                if(can<0)return stop(sc,"switch action reserve check (fightOutPokemonCheckIrekaeReserveFightPokemon)");
                if(!can)continue;
                for(uint32_t k=0;k<(uint16_t)active;++k){
                    if(k>=2)return stop(sc,"more than two active slots");
                    uint32_t fo=tr+C3_RUNTIME_FIGHT_OUT_OFFSET+k*C3_FIGHT_OUT_POKEMON_BYTES;
                    if(!fight_out(s,fo,&fault)&&needed!=2)needed=can_get_exp(s,tr)?2:1;   /* fightTrainerIsGcHero */
                }
            }
        }
        if(needed)return stop(sc,"replacement choice (fn_802230BC: fightTrainerSelectIrekaeFightPokemon, AI or party menu)");
        /* fn_80222EF0 over the battlers: one out of battle with no
         * replacement entry (0x121 < 0) is cleared when it fainted (0x120:
         * fightOutPokemonInit, then the battle grid); a replacement entry
         * would run the send-out sequence lbl_8037879E. */
        uint32_t list[8];unsigned n=0;
        if(!c3_battle_state_valid_fight_outs(s,list,&n))return stop(sc,"valid battlers");
        for(unsigned i=0;i<n;++i){
            uint32_t fo=list[i],entry=0,fainted=0,reset=0;
            if(fight_out(s,fo,&fault))continue;
            checked_get(s,fo,0x121,&entry,&fault);checked_get(s,fo,0x120,&fainted,&fault);
            uint32_t fp=get(s,fo,0xD6);
            if(fp)checked_get(s,fp,0xD2,&reset,&fault);
            if(fault)return stop(sc,"battle end records");
            if((int32_t)entry>=0)return stop(sc,"send-out sequence (lbl_8037879E)");
            if((uint8_t)reset==1)return stop(sc,"fainted Pokemon re-initialization (FightPokemon 0xD2)");
            if((uint8_t)fainted==1){
                uint8_t *record=c3_battle_arena_bytes(&s->arena,fo,C3_BATTLE_FIGHT_OUT_POKEMON_RECORD);
                if(!record||!c3_battle_fight_out_pokemon_init(record))return stop(sc,"fightOutPokemonInit");
                present(h,C3_PRESENT_GRID_UPDATE,0,0);
            }
        }
    }
    if(fault)return stop(sc,"battle end records");
    sc->pc+=5;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- faint sequence: 0x1A, 0x56, 0x10, 0x1B ------------------------------ */

/* fightOutPokemonWriteJoutaiDataId(fo, id): erase condition `id` from the
 * record its category names (0x7C / 0xC8 the Pokemon, 0xCD the
 * FightPokemon, 0xD8 the battler; fn_80121B4C / fn_8011B788). Its model
 * calls (ids 0, 7, 8) are the caller's to refuse. */
static void battler_erase(C3BattleState *s,uint32_t fo,uint16_t id,int *fault)
{
    uint16_t category=condition_category(s,id);
    uint32_t fp=fo?get(s,fo,0xD6):0;
    if(category==0x7C||category==0xC8)c3_battle_condition_erase(s,fp?get(s,fp,0xCC):0,id,fault);
    else if(category==0xCD)c3_battle_condition_erase(s,fp,id,fault);
    else if(category==0xD8)c3_battle_condition_erase(s,fo,id,fault);
}
/* fightOutPokemonIsUseHensinBuff (0xD6 == 0xD7), then
 * fightOutPokemonSetHensinPokemonStatusId(fo, field, 0, 0): copies the
 * field from 0xD7's Pokemon to 0xD5's. With 0xD5 == 0xD7 (not transformed)
 * that copies the field onto itself; a transformed battler is refused. */
static int hensin_sync(const C3BattleState *s,uint32_t fo)
{
    uint32_t d5=get(s,fo,0xD5),d6=get(s,fo,0xD6),d7=get(s,fo,0xD7);
    return d6!=d7||d5==d7;
}
/* 0x1A fn_80224060 (operand: target kind): the battler starts fainting.
 * A Substitute with HP left would be broken first (fightWazaWzxTypeFuncMigawari
 * and a wait on the effect): refused. fightOutPokemonFreeAllSequenceWaza and
 * fightOutPokemonKizetuEffect 0 / 1 only reach a battler model: refused with
 * one. Then fn_80265598 opens the battler's status panel. */
static int op_faint_begin(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t kind=0,type=0,fo=0;
    int fault=0;
    if(!c3_battle_floor_value(s,0x14,&type))return stop(sc,"fight type");
    if(!c3_battle_script_fetch(sc,1,1,&kind))return C3_SCRIPT_HANDLER_FAIL;
    if(!c3_battle_state_target_now(s,(uint16_t)kind,0,&fo))return stop(sc,"fainting battler");
    if(c3_battle_condition_check(s,fo,0x14,&fault)==1)return stop(sc,"Substitute break on fainting (fightWazaWzxTypeFuncMigawari)");
    if(fault)return stop(sc,"fainting battler condition");
    if(get(s,fo,0xEE))return stop(sc,"fainting effect (battler model)");
    present(h,C3_PRESENT_ACTOR_BEGIN,fo,type);
    sc->pc+=2;
    return C3_SCRIPT_HANDLER_DONE;
}
/* 0x56 fn_8021EE48 (operand: target kind): the battler's cry. The original
 * plays fightOutPokemonGetVoiceSndId (species data 0x61 of the Pokemon's
 * species, selector 0x6E); the request carries the species, whose voice the
 * presenter looks up. */
static int op_cry(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t kind=0,fo=0;
    if(!c3_battle_script_fetch(sc,1,1,&kind))return C3_SCRIPT_HANDLER_FAIL;
    if(!c3_battle_state_target_now(s,(uint16_t)kind,0,&fo))return stop(sc,"cry target");
    uint32_t pokemon=battler_pokemon(s,fo);
    const uint8_t *p=pokemon?c3_battle_arena_bytes(&s->arena,pokemon,2):NULL;
    present(h,C3_PRESENT_CRY,fo,p?(uint32_t)((uint16_t)p[0]<<8|p[1]):0);
    sc->pc+=2;
    return C3_SCRIPT_HANDLER_DONE;
}
/* 0x10 fn_802262D0 (operand: u32 message): fightMenuOpenMsg; work byte 7
 * records an opened window. */
static int op_message_open(C3BattleHandlers *h,C3BattleScript *sc)
{
    uint32_t message=0;
    if(!c3_battle_script_fetch(sc,1,4,&message))return C3_SCRIPT_HANDLER_FAIL;
    if(present(h,C3_PRESENT_MESSAGE_OPEN,message,0)==1)c3_battle_script_write(sc,WORK_BASE+7,1,1);
    sc->pc+=5;
    return C3_SCRIPT_HANDLER_DONE;
}
/* 0x1B fn_80223F1C (operand: target kind): the battler has fainted.
 * fightOutPokemonKizetuEffect 2 / 3 and fightOutPokemonResetSeqStatus only
 * reach a battler model (refused with one); fn_8026532C closes its status
 * panel. Over the battlers (fn_80232FE4): a condition aimed at it (0x16,
 * 0x0A, 0x0E) is refused; each opponent forgets it as a Mimic source
 * (fightOutPokemonSetOumuWazaDataId(.., 0): 0xF7 and the entry's move) and
 * drops its enemy entry (fn_80203198: the entry's move is now 0, so only
 * fightOutPokemonEnemyInit). Then its Pokemon's status slot is cleared
 * (pokemonInitJoutai), condition 0x17 and 0x3E erased, with the Transform
 * copies (0x7C, 0xC8) refused for a transformed battler. */
static int op_faint_end(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t kind=0,type=0,fo=0;
    int fault=0;
    if(!c3_battle_floor_value(s,0x14,&type))return stop(sc,"fight type");
    if(!c3_battle_script_fetch(sc,1,1,&kind))return C3_SCRIPT_HANDLER_FAIL;
    if(!c3_battle_state_target_now(s,(uint16_t)kind,0,&fo)||!fo)return stop(sc,"fainted battler");
    if(get(s,fo,0xEE))return stop(sc,"fainted effect (battler model)");
    if(!hensin_sync(s,fo))return stop(sc,"Transform status copy (fightOutPokemonSetHensinPokemonStatusId)");
    present(h,C3_PRESENT_ACTOR_END,fo,type);
    uint32_t list[8];unsigned n=0;
    if(!c3_battle_state_valid_fight_outs(s,list,&n))return stop(sc,"valid battlers");
    uint32_t side=battler_side(s,fo);
    for(unsigned i=0;i<n;++i){
        uint32_t p=list[i];
        if(!fight_out(s,p,&fault)||p==fo)continue;
        if(c3_battle_condition_check(s,p,0x16,&fault)==1||c3_battle_condition_check(s,p,0x0A,&fault)==1||
           c3_battle_condition_check(s,p,0x0E,&fault)==1)return stop(sc,"condition aimed at the fainted battler (fn_80232FE4)");
        if(side&&battler_side(s,p)==side)continue;                      /* fightOutPokemonIsAlly */
        if(!set_oumu(s,p,fo,0))return stop(sc,"Mimic record");
        uint8_t *entries=c3_battle_arena_bytes(&s->arena,p+0x6B0u,48);
        if(!entries)return stop(sc,"enemy entries");
        for(unsigned k=0;k<4;++k){                                       /* fightOutPokemonEnemySearchAry */
            uint8_t *e=entries+12*k;
            uint32_t t=(uint32_t)e[0]<<24|(uint32_t)e[1]<<16|(uint32_t)e[2]<<8|e[3];
            if(t&&t==fo){memset(e,0,10);break;}                         /* fightOutPokemonEnemyInit */
        }
    }
    uint32_t pokemon=battler_pokemon(s,fo);
    if(!pokemon||!c3_battle_pokemon_init_joutai(s,pokemon))return stop(sc,"status slot");
    battler_erase(s,fo,0x17,&fault);
    if(c3_battle_condition_check(s,fo,0x3E,&fault)==1)battler_erase(s,fo,0x3E,&fault);
    if(fault)return stop(sc,"fainted battler records");
    sc->pc+=2;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- 0x23 EXP award (fn_802232F4) ---------------------------------------- */

static uint32_t rd32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint16_t rd16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static const uint8_t *species_row(const C3BattleState *s,uint16_t species)
{return s->species.rows&&species<s->species.count?s->species.rows+(uint32_t)species*C3_SPECIES_ROW_BYTES:NULL;}
/* pokemonGetLevelToExp: the growth row of the species (data 0x11), u32 per
 * level; 0 without a row. */
static int level_to_exp(const C3BattleState *s,uint32_t pokemon,uint32_t level,uint32_t *exp)
{
    const uint8_t *p=c3_battle_arena_bytes(&s->arena,pokemon,2),*sp=p?species_row(s,rd16(p)):NULL;
    *exp=0;
    if(!p)return 0;
    if(!sp)return 1;
    const C3BattleDataRows *g=&s->growth;
    if(!g->rows||sp[0]>=g->count||level>100)return 0;
    *exp=rd32(g->rows+(uint32_t)sp[0]*g->stride+4u*level);
    return 1;
}
/* fightTrainerCheckCanGetExp: trainer data field 4 (the trainer's type) is 1. */
static int can_get_exp(const C3BattleState *s,uint32_t trainer)
{
    const uint8_t *id=c3_battle_arena_bytes(&s->arena,trainer,2);
    const C3BattleDataRows *t=&s->trainers;
    if(!id||!t->rows||rd16(id)>=t->count)return 0;
    return rd16(t->rows+(uint32_t)rd16(id)*t->stride+2)==1;
}
/* fightTrainerCheckTemotiPokemonFightEntry(trainer, party slot): the valid
 * FightPokemon whose original Pokemon (0xCB) is that party slot. */
static uint32_t party_fight_pokemon(const C3BattleState *s,uint32_t trainer,unsigned slot,int *fault)
{
    uint32_t party=trainer+C3_BATTLE_HERO_PARTY_OFFSET+slot*C3_BATTLE_POKEMON_BYTES;   /* heroGetStatus(.., 3, i) */
    for(unsigned j=0;j<6;++j){
        uint32_t fp=trainer+0xB1Cu+j*0x154u;
        if(c3_battle_fight_pokemon_check_valid(s,fp,fault)&&get(s,fp,0xCB)==party)return fp;
    }
    return 0;
}
/* fightTrainerCheckFightPokemonFightOut: the trainer's battler in battle
 * whose FightPokemon (0xD5) is fp, else 0. */
static uint32_t trainer_battler(const C3BattleState *s,uint32_t trainer,uint32_t fp,int *fault)
{
    for(unsigned i=0;i<2;++i){
        uint32_t fo=trainer+C3_RUNTIME_FIGHT_OUT_OFFSET+i*C3_FIGHT_OUT_POKEMON_BYTES;
        if(c3_battle_fight_out_check_valid(s,fo,fault)&&get(s,fo,0xD5)==fp)
            return c3_battle_fight_out_check_fight_out(s,fo,fault)?fo:0;
    }
    return 0;
}
/* fightTrainerIsMineFightPokemon: the Pokemon's trainer ID (0x75, +0x14)
 * and name (0x76, +0x18) match the hero's (heroBiosGetRnd +0x2C, name +0). */
static int is_mine(const C3BattleState *s,uint32_t trainer,uint32_t pokemon)
{
    const uint8_t *hero=c3_battle_arena_bytes(&s->arena,trainer+4,0x30),*p=c3_battle_arena_bytes(&s->arena,pokemon,0x2E);
    if(!hero||!p)return 0;
    if(rd32(hero+0x2C)!=rd32(p+0x14))return 0;
    for(unsigned i=0;i<11;++i){                                       /* GScharCmp */
        uint16_t a=rd16(hero+2*i),b=rd16(p+0x18+2*i);
        if(a!=b)return 0;
        if(!a)return 1;
    }
    return 1;
}
/* pokemonGetEffortFromPokemon(pokemon, item 0, single, defeated species). */
static int effort_gain(C3BattleState *s,uint32_t pokemon,uint16_t defeated)
{
    uint8_t *p=c3_battle_arena_bytes(&s->arena,pokemon,C3_BATTLE_POKEMON_BYTES);
    const uint8_t *dr=species_row(s,defeated);
    if(!p)return 0;
    uint16_t species=rd16(p);
    const uint8_t *sr=species_row(s,species);
    if(!species||!sr||!rd32(sr+0x18))return 1;                        /* data 1, species count */
    if(!p[8]||!p[9]||!p[10]||!p[11])return 1;                         /* gamedataAttestCheckValid */
    if(p[0xCD]==1||species==0x19C||p[0xCB]==1||!rd16(p+0x8A))return 1; /* 0xB8, 0xB6, fainted (0x7B) */
    uint16_t total=0;
    for(unsigned i=0;i<6;++i)total=(uint16_t)(total+rd16(p+0x98+2*i));
    for(unsigned j=0;j<6;++j){
        if(total>=510)return 1;
        uint16_t gain=dr?rd16(dr+0x90+2*j):0,now=rd16(p+0x98+2*j);
        if(total+gain>510)gain=(uint16_t)(gain-(total+gain-510));
        if(gain+now>255)gain=(uint16_t)(gain-(gain+now-255));
        now=(uint16_t)(now+gain);total=(uint16_t)(total+gain);
        if(now>255)now=255;                                           /* pokemonSetStatus 0x8D..0x92 */
        p[0x98+2*j]=(uint8_t)(now>>8);p[0x99+2*j]=(uint8_t)now;
    }
    return 1;
}

/* Operand: target kind (the defeated battler). When it is not the player's
 * and floor 0x24 grants EXP, its base EXP (species data 0x10) x level / 7 is
 * shared between the party Pokemon of the other side's trainers who can
 * gain EXP (trainer type 1) and met it (fightOutPokemonCheckMeetEnemyFightPokemon),
 * with Exp. Share holders taking half. x1.5 for floor 0x2D and for another
 * trainer's Pokemon (message 0x7534 instead of 0x7533). Each recipient in
 * turn: the camera and grid once (C3_PRESENT_EXP_CAMERA), its status panel
 * when in battle, message 0x7532 (variables 0x0D nickname, 0x0E the
 * 0x7533/0x7534 text, 0x2F the amount) opened and closed, its effort values,
 * the EXP added, and in battle the EXP bar (C3_PRESENT_EXP_BAR), a 0x40-frame
 * wait, one-frame polls until the bar stops (C3_PRESENT_EXP_BAR_BUSY) and the
 * panel renewed (C3_PRESENT_STATUS_RENEW); then the panel closes. The level
 * up menu buffer (lbl_80478278, lbl_8047B64C) is only read by the level-up
 * sequence. Refused by name: a level up (lbl_80378724, stat growth), a
 * Shadow Pokemon (its EXP is stored instead), a held item (Exp. Share /
 * Lucky Egg / Macho Brace need the item table). */
static int op_exp_award(C3BattleHandlers *h,C3BattleScript *sc,unsigned phase)
{
    C3BattleState *s=h->state;
    C3BattleExpAward *a=NULL;
    uint32_t type=0;
    int fault=0;
    if(!c3_battle_floor_value(s,0x14,&type))return stop(sc,"fight type");
    if(phase==0){
        uint32_t kind=0,target=0,exp_on=0,bonus=0,trainers=0;
        h->exp_count=h->exp_index=h->exp_step=h->exp_camera=0;
        if(!c3_battle_script_fetch(sc,1,1,&kind))return C3_SCRIPT_HANDLER_FAIL;
        if(!c3_battle_state_target_now(s,(uint16_t)kind,0,&target))return stop(sc,"defeated battler");
        uint32_t defeated_pokemon=battler_pokemon(s,target);
        const uint8_t *dp=defeated_pokemon?c3_battle_arena_bytes(&s->arena,defeated_pokemon,0x61):NULL;
        uint16_t species=dp?rd16(dp):0;
        const uint8_t *dr=species_row(s,species);
        uint16_t base=dr?rd16(dr+6):0;
        uint8_t level=dp?dp[0x60]:0;
        h->exp_species=species;
        int hero=c3_battle_fight_out_is_gc_hero(s,target);
        if(hero<0)return stop(sc,"player battler check");
        if(!c3_battle_floor_value(s,0x24,&exp_on)||!c3_battle_floor_value(s,0x2D,&bonus)||
           !c3_battle_floor_value(s,0x16,&trainers))return stop(sc,"floor EXP fields");
        if(hero!=1&&(uint8_t)exp_on){
            uint32_t side=0;
            if(!c3_battle_state_target_now(s,3,target,&side))return stop(sc,"winning side");
            if(side&&c3_battle_side_check_valid(s,side,&fault)){
                int met=0,share=0;             /* share: Exp. Share holders (held items refused) */
                for(uint32_t t=0;t<(uint16_t)trainers;++t){
                    if(t>=2)return stop(sc,"more than two trainers");
                    uint32_t tr=side+C3_FIGHT_SIDE_TRAINER_OFFSET+t*C3_BATTLE_RUNTIME_TRAINER_BYTES;
                    if(!c3_battle_trainer_check_valid(s,tr,&fault)||!can_get_exp(s,tr))continue;
                    for(unsigned k=0;k<6;++k){
                        uint32_t fp=party_fight_pokemon(s,tr,k,&fault);
                        if(!fp||!c3_battle_fight_pokemon_check_fight_out(s,fp,&fault))continue;
                        int m=c3_battle_fight_out_met(s,target,fp);
                        if(m<0)return stop(sc,"met check");
                        met+=m==1;
                        uint16_t item=0;
                        const uint8_t *ip=c3_battle_arena_bytes(&s->arena,get(s,fp,0xCC)+0x88,2);
                        if(ip)item=rd16(ip);
                        if(item)return stop(sc,"held item (Exp. Share check, item table)");
                    }
                }
                if(met||share){
                    uint16_t each_met=(uint16_t)((uint32_t)base*level/7u),each_share;
                    each_share=(uint16_t)(each_met/(unsigned)met);                /* no holder: met != 0 */
                    if(!each_share)each_share=1;
                    each_met=0;
                    uint32_t share_exp=each_share,met_exp=each_met;
                    for(uint32_t t=0;t<(uint16_t)trainers;++t){
                        uint32_t tr=side+C3_FIGHT_SIDE_TRAINER_OFFSET+t*C3_BATTLE_RUNTIME_TRAINER_BYTES;
                        if(!can_get_exp(s,tr))continue;
                        for(unsigned k=0;k<6;++k){
                            uint32_t fp=party_fight_pokemon(s,tr,k,&fault),gain=0;
                            if(!fp||!c3_battle_fight_pokemon_check_fight_out(s,fp,&fault))continue;
                            uint32_t pokemon=get(s,fp,0xCC);
                            const uint8_t *pp=pokemon?c3_battle_arena_bytes(&s->arena,pokemon,C3_BATTLE_POKEMON_BYTES):NULL;
                            if(!pp)return stop(sc,"recipient Pokemon");
                            if(pp[0x60]>=100)continue;
                            if(c3_battle_fight_out_met(s,target,fp)==1)gain=share_exp;
                            (void)met_exp;                                           /* no Exp. Share holder */
                            if(!gain)continue;
                            if((uint8_t)bonus==1)gain=gain*150u/100u;
                            uint16_t msg=0x7533;
                            if(!is_mine(s,tr,pokemon)){gain=gain*150u/100u;msg=0x7534;}
                            if(rd16(pp+0xD8)&&(int32_t)rd32(pp+0xDC)>=0)return stop(sc,"Shadow Pokemon EXP storage (0xC6)");
                            if(h->exp_count>=12)return stop(sc,"EXP recipients");
                            h->exp[h->exp_count++]=(C3BattleExpAward){tr,fp,gain,msg};
                        }
                    }
                }
            }
        }
        if(fault)return stop(sc,"EXP records");
    }
    while(h->exp_index<h->exp_count){
        a=&h->exp[h->exp_index];
        uint32_t pokemon=get(s,a->fight_pokemon,0xCC);
        uint8_t *pp=pokemon?c3_battle_arena_bytes(&s->arena,pokemon,C3_BATTLE_POKEMON_BYTES):NULL;
        if(!pp)return stop(sc,"recipient Pokemon");
        if(h->exp_step==0){
            uint32_t now=0,next=0,base_exp=0,gain=a->gain;
            if(!h->exp_camera){present(h,C3_PRESENT_EXP_CAMERA,0,0);h->exp_camera=1;}
            uint32_t fo=trainer_battler(s,a->trainer,a->fight_pokemon,&fault);
            if(fo)present(h,C3_PRESENT_ACTOR_BEGIN,fo,type);
            c3_battle_msgctrl_set(s,0x0D,pokemon+0x2Eu);                     /* nickname (0x77) */
            c3_battle_msgctrl_set(s,0x0E,C3_MSGREF_MESSAGE(a->message));
            c3_battle_msgctrl_set(s,0x2F,a->gain);
            h->exp_opened=present(h,C3_PRESENT_MESSAGE_OPEN,0x7532,0)==1;
            present(h,C3_PRESENT_MESSAGE_CLOSE,0,0);
            if(!effort_gain(s,pokemon,h->exp_species))return stop(sc,"effort values");
            h->exp_step=2;                                                   /* to the close */
            if(gain&&c3_battle_fight_pokemon_check_fight_out(s,a->fight_pokemon,&fault)&&pp[0x60]<100){
                uint8_t level=pp[0x60];
                now=rd32(pp+0x5C);
                if(!level_to_exp(s,pokemon,level+1u,&next)||!level_to_exp(s,pokemon,level,&base_exp))return stop(sc,"growth table");
                if(now+gain>=next)return stop(sc,"level up (lbl_80378724, fightPokemonGrowBasisStatus)");
                pp[0x5C]=(uint8_t)((now+gain)>>24);pp[0x5D]=(uint8_t)((now+gain)>>16);
                pp[0x5E]=(uint8_t)((now+gain)>>8);pp[0x5F]=(uint8_t)(now+gain);   /* figthPokemonSetExp */
                fo=trainer_battler(s,a->trainer,a->fight_pokemon,&fault);
                if(fo){
                    present(h,C3_PRESENT_EXP_BAR,fo,now+gain-base_exp);
                    h->exp_battler=fo;h->exp_step=1;
                    sc->wait_frames=0x40;return C3_SCRIPT_HANDLER_WAIT;          /* fightMainWaitFrame(0x40) */
                }
            }
        }
        if(h->exp_step==1){
            if(present(h,C3_PRESENT_EXP_BAR_BUSY,h->exp_battler,0)==1){sc->wait_frames=1;return C3_SCRIPT_HANDLER_WAIT;}
            present(h,C3_PRESENT_STATUS_RENEW,h->exp_battler,type);
            h->exp_step=2;
        }
        if(h->exp_opened)present(h,C3_PRESENT_MESSAGE_CLOSE,0,0);
        uint32_t fo=trainer_battler(s,a->trainer,a->fight_pokemon,&fault);
        if(fo)present(h,C3_PRESENT_ACTOR_END,fo,type);
        if(fault)return stop(sc,"EXP records");
        ++h->exp_index;h->exp_step=0;
    }
    sc->pc+=2;
    return C3_SCRIPT_HANDLER_DONE;
}

/* ---- secondary effects (fn_802249B8) ------------------------------------- */

/* fn_802025B8 / fn_8020248C on a battler: the record by category (0xCD its
 * FightPokemon, 0xD8 itself); the Pokemon-level writer (0x7C / 0xC8,
 * fn_801219F4) is not modelled: -1. */
static int battler_condition_object(C3BattleState *s,uint32_t fo,uint16_t id,uint32_t *object)
{
    uint16_t category=condition_category(s,id);
    if(category==0xD8){*object=fo;return 1;}
    if(category==0xCD){*object=fo?get(s,fo,0xD6):0;return 1;}
    if(category==0x7C||category==0xC8)return -1;
    *object=0;return 0;
}
static int battler_write(C3BattleState *s,uint32_t fo,uint16_t id,int *fault)
{
    uint32_t object=0;
    int r=battler_condition_object(s,fo,id,&object);
    if(r<=0)return r;
    if(c3_battle_condition_check_write(s,object,id,fault)==2&&!c3_battle_condition_write(s,object,id,0,fault))*fault=1;
    return 1;
}
/* fightFloorSetStatus(0x47, fo): message variables 0x12 and 0x1D (its
 * ability's name), cleared for 0 or an invalid battler, then +0xA488. */
static int floor_set_effect_target(C3BattleState *s,uint32_t fo)
{
    int fault=0;
    int valid=fo&&c3_battle_fight_out_check_valid(s,fo,&fault)==1;
    c3_battle_msgctrl_set(s,0x12,valid?fo:0);
    c3_battle_msgctrl_set(s,0x1D,valid?C3_MSGREF_ABILITY_NAME((uint16_t)get(s,fo,0x100)):0);
    uint8_t *p=c3_battle_arena_bytes(&s->arena,s->floor+0xA488u,4);
    if(fault||!p)return 0;
    p[0]=(uint8_t)(fo>>24);p[1]=(uint8_t)(fo>>16);p[2]=(uint8_t)(fo>>8);p[3]=(uint8_t)fo;
    return 1;
}
/* lbl_80279EF4: the condition each effect writes or is blocked by. */
static const uint16_t effect_conditions[16]={0,8,3,6,7,5,4,9,0x11,0,0x0B,0,0x22,0x0E,0,0};
#define CONFUSION_SEQUENCE 0x8037951Bu   /* lbl_80399F58[7] */
/* fn_802249B8(param1, param2) for work byte 3. Bit 0x40 aims the effect at
 * the attacker instead of the defender (floor 0x47 the target, 0x4B the
 * other). Shield Dust (ability 0x13) and Safeguard (side condition 0x4B)
 * block weaker effects, as do a target out of battle and a Substitute.
 * Implemented: 7 confusion (condition 9 unless Own Tempo 0x14; the
 * confusion sequence) and 8 flinch (condition 0x11 unless Inner Focus 0x27
 * or 0x112). Returns 1 with the PC advanced, 2 with the sequence pushed
 * (the caller advances after it), 0 refused. */
static int secondary_effect(C3BattleHandlers *h,C3BattleScript *sc,uint8_t param1,uint8_t param2)
{
    C3BattleState *s=h->state;
    uint32_t attacker=0,defender=0,effect=0,flags=0,deck=0;
    int fault=0;
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&attacker)||!c3_battle_target(&s->arena,s->floor,0x12,0,&defender))
        return stop(sc,"secondary effect targets"),0;
    if(!c3_battle_script_read(sc,EFFECT_ID_BYTE,1,&effect)||!c3_battle_script_read(sc,HIT_MARKER,4,&flags))
        return stop(sc,"effect byte"),0;
    uint32_t ctx=defender,other=attacker;
    uint8_t self=0;
    if(effect&0x40u){ctx=attacker;other=defender;effect&=0xBFu;self=0x40;c3_battle_script_write(sc,EFFECT_ID_BYTE,1,effect);}
    if(!floor_set_effect_target(s,ctx)||!floor_appoint(s,other))return stop(sc,"floor effect target"),0;
    uint16_t ability=(uint16_t)get(s,ctx,0x100);
    if(!c3_battle_state_target_now(s,2,ctx,&deck))return stop(sc,"effect target side"),0;
    if(ability==0x13&&!(flags&0x2000u)&&param1==0&&effect<0xA){sc->pc+=1;return 1;}             /* Shield Dust */
    if(deck&&c3_battle_condition_present(s,deck,0x4B,&fault)==1&&!(flags&0x2000u)&&param1==0&&effect<8){sc->pc+=1;return 1;}
    if(!fight_out(s,ctx,&fault)&&effect!=0xB&&effect!=0x1F){sc->pc+=1;return 1;}
    if(c3_battle_condition_check(s,ctx,0x14,&fault)==1&&self!=0x40){sc->pc+=1;return 1;}     /* Substitute */
    if(fault)return stop(sc,"secondary effect records"),0;
    if(effect<7)return stop(sc,"status condition effect (fn_802249B8 status block)"),0;
    uint16_t blocking=effect<16?effect_conditions[effect]:0;
    if(effect>=16)return stop(sc,"secondary effect kind (fn_802249B8)"),0;
    if(blocking&&c3_battle_condition_check(s,ctx,blocking,&fault)==1){sc->pc+=1;return 1;}
    switch(effect){
    case 7:
        if(ability!=0x14&&c3_battle_condition_check(s,ctx,9,&fault)!=1){
            int w=battler_write(s,ctx,9,&fault);
            if(w<0||fault)return stop(sc,"confusion condition"),0;
            return c3_battle_script_call(sc,CONFUSION_SEQUENCE,sc->action,0)?2:(stop(sc,"confusion sequence"),0);
        }
        sc->pc+=1;return 1;
    case 8:
        if(ability==0x27&&(param1==1||param2==0x80))return stop(sc,"Inner Focus message (lbl_80379868)"),0;
        if(ability!=0x27&&get(s,ctx,0x112)==0){
            int w=battler_write(s,ctx,0x11,&fault);
            if(w<0||fault)return stop(sc,"flinch condition"),0;
        }
        sc->pc+=1;return 1;
    default:
        return stop(sc,"secondary effect kind (fn_802249B8)"),0;
    }
}

/* 0x66 fn_8021DF80 (operands: target kind, u32 kind at +3): for kind 7 and
 * hit marker bit 0x80 clear, the confusion animation on the target's battler
 * model and its status panel; nothing without a model. */
static int op_status_animation(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t kind=0,which=0,flags=0,fo=0;
    if(!c3_battle_script_fetch(sc,1,1,&kind)||!c3_battle_script_fetch(sc,3,4,&which))return C3_SCRIPT_HANDLER_FAIL;
    if(!c3_battle_state_target_now(s,(uint16_t)kind,0,&fo))return stop(sc,"status animation target");
    if(!c3_battle_script_read(sc,HIT_MARKER,4,&flags))return stop(sc,"hit marker");
    if(!(flags&0x80u)&&which==7&&get(s,fo,0xEE))return stop(sc,"status animation (battler model)");
    sc->pc+=7;
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
    case 0x09:return op_move_effect(h,sc,phase);
    case 0x0A:return op_effect_wait(h,sc);
    case 0x0B:return op_hp_update(h,sc);
    case 0x0C:return op_hp_bar_wait(h,sc);
    case 0x0D:return op_critical_message(h,sc);
    case 0x0E:return op_fight_waza_touch(h,sc);
    case 0x0F:return op_effect_message(h,sc);
    case 0x10:return op_message_open(h,sc);
    case 0x12:return op_message_close(h,sc,phase);
    case 0x15:return op_effect_chance(h,sc,phase);
    case 0x19:return op_faint_check(h,sc,phase);
    case 0x1A:return op_faint_begin(h,sc);
    case 0x1B:return op_faint_end(h,sc);
    case 0x23:return op_exp_award(h,sc,phase);
    case 0x24:return op_battle_end_check(h,sc);
    case 0x49:return op_move_end(h,sc);
    case 0x56:return op_cry(h,sc);
    case 0x66:return op_status_animation(h,sc);
    case 0x5C:return op_damage_blink(h,sc);
    default:return C3_SCRIPT_HANDLER_NONE;
    }
}
