/* Move-script battle-logic opcodes over the battle state (guarded lowering).
 *   0x00 WS_HITCHECK  fight_range_80211A00.c (attack canceler)
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

int c3_battle_handler_dispatch(void *user,C3BattleScript *sc,uint8_t op,unsigned phase)
{
    C3BattleHandlers *h=user;(void)phase;
    if(!h||!h->state||!sc)return C3_SCRIPT_HANDLER_NONE;
    switch(op){
    case 0x00:return ws_hitcheck(h,sc);
    default:return C3_SCRIPT_HANDLER_NONE;
    }
}
