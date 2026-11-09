/* Guarded lowering of the attack-canceller pieces:
 *   fight_out_pokemon_candidate_80202810.c fightOutPokemonWriteJoutaiDataId
 *   field_range_80117E58.c fn_8011B788 (slot clear)
 *   pokemon_range_8011F5FC.c pokemonIsDarkPokemon / pokemonBiosGetDarkFlag
 *   fight_floor.c fightFloorGetFightOutPokemonPtrToFightTrainerPtr,
 *                 fightFloorCheckHuuinWazaFightOutPokemon
 *   fight_range_exact_8022DCB8.c fn_8022EDEC
 *   fight_range_8022A504.c fn_8022A6C8
 *   fight_range_80211A00.c fn_8022F2F8 */
#include "colosseum3ds/battle_canceler.h"
#include <stdio.h>
#include <string.h>

static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int unported(char *reason,const char *what)
{
    if(reason)snprintf(reason,C3_CANCEL_REASON_SIZE,"%s",what);
    return C3_CANCEL_UNPORTED;
}
static uint32_t get(const C3BattleState *s,uint32_t obj,uint32_t sel)
{uint32_t v=0;(void)c3_battle_fight_status_get(&s->arena,obj,sel,0,&v);return v;}
static int has(const C3BattleState *s,uint32_t fo,uint16_t id){return c3_battle_condition_check(s,fo,id,NULL)==1;}

/* ---- conditions ---------------------------------------------------------- */

static uint16_t category(const C3BattleState *s,uint16_t id)                 /* fn_80119ED0 */
{
    const C3BattleDataRows *r=&s->conditions;
    return r->rows&&id<r->count?be16(r->rows+(uint32_t)id*r->stride+8):0;
}
/* fn_8011B788: if the slot holds `id`, zero its seven fields. */
static void slot_clear(C3BattleState *s,uint32_t object,uint16_t id)
{
    if(!id||!c3_battle_condition_present(s,object,id,NULL))return;
    const uint8_t *row=s->conditions.rows+(uint32_t)id*s->conditions.stride;
    uint8_t kind=row[0],index=row[1];uint16_t selector=be16(row+8);uint32_t base=0;
    if(kind==5){
        if(selector==0x7C)base=object+0x64;else if(selector==0xC8)base=object+0xE8;
        else (void)c3_battle_fight_status_get(&s->arena,object,selector,0,&base);
    }else if(kind==6)(void)c3_battle_waza_status_get(&s->arena,&s->moves,object,0,selector,0,&base);
    uint8_t *slot=base?c3_battle_arena_bytes(&s->arena,base+(uint32_t)index*16u,16):NULL;
    if(slot){memset(slot,0,7);memset(slot+8,0,6);}      /* id, sub, byte4/5, count, value, half_c */
}
int c3_battle_condition_clear(C3BattleState *s,uint32_t fo,uint16_t id,char *reason)
{
    if(!s)return 0;
    if((id==0||id==7||id==8)&&get(s,fo,0xEE))
        return unported(reason,"model-sequence update for sleep/freeze (fn_801DA36C)");
    uint16_t c=category(s,id);
    if(c==0x7C||c==0xC8||c==0xCD){
        uint32_t fp=get(s,fo,0xD6);
        if(c==0xCD){slot_clear(s,fp,id);return 1;}
        slot_clear(s,fp?get(s,fp,0xCC):0,id);           /* fn_80121B4C */
    }else if(c==0xD8)slot_clear(s,fo,id);
    return 1;
}

/* ---- Shadow checks ------------------------------------------------------- */

int c3_battle_pokemon_is_dark(const C3BattleState *s,uint32_t pokemon)
{
    const uint8_t *p=pokemon?c3_battle_arena_bytes(&s->arena,pokemon,0xE0):NULL;
    if(!p||!be16(p+0xD8))return 0;                       /* darkpokemonDataId */
    return (int32_t)be32(p+0xDC)>=0;                     /* dp */
}
static uint32_t fight_out_pokemon(const C3BattleState *s,uint32_t fo)   /* fightOutPokemonGetPokemonPtr */
{uint32_t fp=fo?get(s,fo,0xD6):0;return fp?get(s,fp,0xCC):0;}

uint32_t c3_battle_fight_out_trainer(const C3BattleState *s,uint32_t fo)
{
    uint32_t original=fo?get(s,fo,0xD5):0;
    if(!original)return 0;
    for(unsigned side=0;side<2;++side)for(unsigned t=0;t<2;++t){
        uint32_t trainer=s->floor+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES+C3_FIGHT_SIDE_TRAINER_OFFSET+
            t*C3_BATTLE_RUNTIME_TRAINER_BYTES;
        for(unsigned k=0;k<6;++k)if(trainer+0xB1Cu+k*0x154u==original)return trainer;
    }
    return 0;
}

int c3_battle_shadow_check(C3BattleState *s,uint32_t fo,unsigned mode,char *reason)
{
    /* fightOutPokemonCheckFightOut, fightOutPokemonIsGcHeroFightOutPokemon,
     * floor 0x31, pokemonIsDarkPokemon must all hold before any action; the
     * remaining three are pure, so a non-Shadow result is decided without
     * the trainer-kind query. */
    uint32_t flag=0;
    if(!s||!fo||!c3_battle_fight_out_check_fight_out(s,fo,NULL))return 0;
    if(!c3_battle_floor_value(s,0x31,&flag))return unported(reason,"floor field 0x31 unavailable");
    if(!flag||!c3_battle_pokemon_is_dark(s,fight_out_pokemon(s,fo)))return 0;
    (void)mode;
    return unported(reason,"Shadow Pokemon reverse/hyper path (fn_8022EDEC)");
}

int c3_battle_hyper_canceller(C3BattleState *s,uint32_t fo,char *reason)
{
    if(!s||!c3_battle_fight_out_trainer(s,fo))return 0;  /* trainer lookup fails: 0 */
    uint32_t fw=get(s,fo,0xD9),automake=0,flag=0;
    if(!c3_battle_waza_status_get(&s->arena,&s->moves,fw,0,0x32,0,&automake))return unported(reason,"FightWaza 0x32 unavailable");
    if((automake&0xFF)==1)return 0;
    if(!c3_battle_floor_value(s,0x31,&flag))return unported(reason,"floor field 0x31 unavailable");
    if(!(flag&0xFF))return 0;
    if(!c3_battle_pokemon_is_dark(s,fight_out_pokemon(s,fo)))return 0;
    uint32_t move=get(s,fo,0xDA);
    if(!has(s,fo,0x3E)||(move&0xFFFF)==0x164)return 0;
    return unported(reason,"Shadow Hyper Mode canceller body (fn_8022A6C8)");
}

/* ---- fn_8022F2F8 --------------------------------------------------------- */

/* fightFloorCheckHuuinWazaFightOutPokemon(0, attacker, move): the result can
 * only be 1 if some battler holds Imprison (0x27). Every FightOutPokemon slot
 * of every trainer is checked (a superset of the valid ones); none holding it
 * decides 0, otherwise the rest of the original test is not lowered yet. */
static int imprison_possible(const C3BattleState *s)
{
    for(unsigned side=0;side<2;++side)for(unsigned t=0;t<2;++t)for(unsigned k=0;k<3;++k){
        uint32_t trainer=s->floor+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES+C3_FIGHT_SIDE_TRAINER_OFFSET+
            t*C3_BATTLE_RUNTIME_TRAINER_BYTES;
        uint32_t fo=trainer+(k<2?C3_RUNTIME_FIGHT_OUT_OFFSET+k*C3_FIGHT_OUT_POKEMON_BYTES:0x20D4u);
        if(c3_battle_condition_present(s,fo,0x27,NULL))return 1;
    }
    return 0;
}

int c3_battle_status_canceller(C3BattleState *s,uint32_t *step,char *reason)
{
    uint32_t ctx=0;
    if(!s||!step)return unported(reason,"missing state");
    if(*step>0xF)return unported(reason,"status-canceller step out of range");
    if(!c3_battle_target(&s->arena,s->floor,0x11,0,&ctx))return unported(reason,"attacker target unavailable");
    /* Prologue reads are pure; the ones used only by unported branches
     * (side-relative target 2, floor 0x14) are not needed here. */
    uint16_t ability=(uint16_t)get(s,ctx,0x100),move=(uint16_t)get(s,ctx,0xDA);
    uint32_t power=0;
    (void)c3_battle_waza_status_get(&s->arena,&s->moves,0,move,7,0,&power);
    int result=0;
    do{
        switch(*step){
        case 0:
            if(c3_battle_condition_clear(s,ctx,0x15,reason)<0||c3_battle_condition_clear(s,ctx,0x28,reason)<0)
                return C3_CANCEL_UNPORTED;
            ++*step;break;
        case 1:if(has(s,ctx,8))return unported(reason,"sleep (fn_8022F2F8 step 1)");++*step;break;
        case 2:if(has(s,ctx,7))return unported(reason,"freeze (fn_8022F2F8 step 2)");++*step;break;
        case 3:if(ability==0x36&&(get(s,ctx,0xF9)&0xFF))return unported(reason,"Truant (fn_8022F2F8 step 3)");++*step;break;
        case 4:if(has(s,ctx,0x12))return unported(reason,"recharge (fn_8022F2F8 step 4)");++*step;break;
        case 5:if(has(s,ctx,0x11))return unported(reason,"flinch (fn_8022F2F8 step 5)");++*step;break;
        case 6:if(has(s,ctx,0x29))return unported(reason,"Disable (fn_8022F2F8 step 6)");++*step;break;
        case 7:if(has(s,ctx,0x30)&&(power&0xFFFF)==0)return unported(reason,"Taunt (fn_8022F2F8 step 7)");++*step;break;
        case 8:if(imprison_possible(s))return unported(reason,"Imprison (fn_8022F2F8 step 8)");++*step;break;
        case 9:if(has(s,ctx,9))return unported(reason,"confusion (fn_8022F2F8 step 9)");++*step;break;
        case 10:if(has(s,ctx,5))return unported(reason,"paralysis (fn_8022F2F8 step 10)");++*step;break;
        case 11:if(has(s,ctx,0xA))return unported(reason,"attraction (fn_8022F2F8 step 11)");++*step;break;
        case 12:if(has(s,ctx,0xC))return unported(reason,"Bide (fn_8022F2F8 step 12)");++*step;break;
        case 13:if(has(s,ctx,7))return unported(reason,"freeze thaw (fn_8022F2F8 step 13)");++*step;break;
        case 14:{
            int r=c3_battle_shadow_check(s,ctx,has(s,ctx,0x3E)?2u:0u,reason);
            if(r<0)return r;
            ++*step;break;
        }
        default:break;                                     /* case 15 */
        }
    }while(*step!=0xF&&result==0);
    return result;
}
