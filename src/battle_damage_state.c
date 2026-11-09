/* The pinned original damage arithmetic fn_80232110 (generated as
 * build/battle_damage_state.inc with the c3s_src_ prefix) bound to the
 * battle state. Each engine service it calls is an adapter over the arena,
 * the condition table and the original tables; services whose original
 * behaviour is not modelled yet raise accessor_error, which rejects the
 * whole calculation (guarded lowering). */
#include "colosseum3ds/battle_damage_state.h"
#include "colosseum3ds/battle_canceler.h"
#include "dolphin/types.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static atomic_flag in_use = ATOMIC_FLAG_INIT;
static const C3BattleState *ctx;
static const C3BattleDamageTables *tbl;
static int accessor_error;
static const char *error_name;
static u8 c3s_src_lbl_80279ED0[0x24];

static void unmodelled(const char *what){if(!accessor_error)error_name=what;accessor_error=1;}
static u32 field(u32 obj,u32 sel)
{
    u32 v=0;
    if(!c3_battle_fight_status_get(&ctx->arena,obj,sel,0,&v))unmodelled("battler field");
    return v;
}
static u32 pokemon_of(u32 fo){u32 fp=fo?field(fo,0xD6):0;return fp?field(fp,0xCC):0;}
static const u8 *bios(u32 pokemon,u32 n){const u8 *p=pokemon?c3_battle_arena_bytes(&ctx->arena,pokemon,n):NULL;if(pokemon&&!p)unmodelled("pokemon record");return p;}
static u16 rd16(const u8 *p){return (u16)((u16)p[0]<<8|p[1]);}

static u32 c3s_src_pokemonGetStatus(u32 obj,u32 id,u32 sel,u32 index)
{
    if(id||index){unmodelled("pokemonGetStatus arguments");return 0;}
    if(!obj)return 0;
    if(sel>=0x87&&sel<=0x8C){const u8 *p=bios(obj,0x98);return p?rd16(p+0x8C+(sel-0x87)*2):0;}  /* stats */
    if(sel>=0xCB)return field(obj,sel);
    unmodelled("pokemonGetStatus selector");return 0;
}
static s32 c3s_src_wazaGetStatus(u32 handle,u32 id,u32 sel,u32 index)
{
    u32 v=0;
    if(!c3_battle_waza_status_get(&ctx->arena,&ctx->moves,handle,(u16)id,sel,index,&v))unmodelled("wazaGetStatus");
    return (s32)v;
}
static u32 c3s_src_fightFloorGetNowTenkouDataId(u32 floor,u32 check)
{
    (void)floor;int w=0;
    if(!c3_battle_state_weather(ctx,(int)(check&0xFF),&w))unmodelled("weather (Cloud Nine / Air Lock)");
    return (u32)w;
}
static u32 c3s_src_fightFloorGetStatus(u32 a,u32 b,u32 sel,u32 index)
{
    u32 v=0;
    if(a||b||index){unmodelled("fightFloorGetStatus arguments");return 0;}
    if(!c3_battle_floor_value(ctx,sel,&v))unmodelled("floor field");
    return v;
}
static u16 c3s_src_fightOutPokemonGetTokuseiDataId(u32 fo){return (u16)field(fo,0x100);}
static u16 held(u32 fo)
{
    u16 e=0;
    if(!c3_battle_state_held_item_effect(ctx,fo,&e))unmodelled("held item");
    return e;
}
static u32 c3s_src_fightOutPokemonGetSoubiItemSoubiDataId(u32 fo){return held(fo);}
static u32 c3s_src_figthOutPokemonGetSoubiItemBuff(u32 fo){(void)held(fo);return 0;}  /* no item: 0 */
static u32 c3s_src_figthOutPokemonGetPokemonDataId(u32 fo){const u8 *p=bios(pokemon_of(fo),2);return p?rd16(p):0;}
static u32 c3s_src_fightOutPokemonGetPokemonPtr(u32 fo){return pokemon_of(fo);}
static u32 c3s_src_fightFloorGetFightOutPokemonPtrToHeroPtr(u32 floor,u32 fo)
{(void)floor;u32 t=c3_battle_fight_out_trainer(ctx,fo);return t?t+4:0;}            /* trainer field 0x44 */
static u8 c3s_src_figthOutPokemonGetLevel(u32 fo){const u8 *p=bios(pokemon_of(fo),0x61);return p?p[0x60]:0;}
static u32 c3s_src_fightFloorGetValidFightOutPokemonCount(u32 floor,u32 mode,u32 fo,u32 flag)
{
    uint16_t n=0;
    if(floor||!c3_battle_state_valid_fight_out_count(ctx,mode&0xFF,fo,(flag&0xFF)==1,&n))
        unmodelled("valid battler count");
    return n;
}
static u32 c3s_src_heroGetStatus(u32 a,u32 b,u32 c){(void)a;(void)b;(void)c;unmodelled("badge boosts (heroGetStatus)");return 0;}
static u32 c3s_src_fn_8010C4A0(u32 type)                                            /* physical/special */
{
    if(!tbl->types||(u16)type>=tbl->type_count)return 0;
    return tbl->types[(u16)type*44u];
}
static u32 c3s_src_fightFloorCheckFightOutPokemonPtrAryPokemonTokuseiDataId(u32 a,u32 ability,u32 b,u32 c)
{
    (void)a;(void)b;(void)c;
    if(c3_battle_state_ability_on_any_battler(ctx,(u16)ability))unmodelled("ability count");
    return 0;
}
static u8 c3s_src_fightOutPokemonIsJoutaiNormal(u32 fo)
{
    u32 p=pokemon_of(fo);
    if(!p)return 0;
    for(u16 id=3;id<=8;++id)if(c3_battle_condition_present(ctx,p,id,NULL))return 0;   /* 0x7C/0xC8 */
    return 1;
}
static u32 c3s_src_fightFloorCheckFightOutPokemonPtrAryJoutaiDataId(u32 a,u32 b)
{(void)a;(void)b;unmodelled("battler condition count");return 0;}
static u32 c3s_src_fightOutPokemonIsNokoriHpFollowing(u32 fo,u32 divisor)
{
    const u8 *p=bios(pokemon_of(fo),0x98);
    if(!(u16)divisor||!p)return 0;
    return (s32)rd16(p+0x8C)/(s32)(u16)divisor>=(s32)rd16(p+0x8A);
}
static u32 c3s_src_fightAbicntDoKakeWaru(u32 stage,u32 value)
{
    u8 num=0,den=1;
    if((u16)stage<tbl->stage_count){num=tbl->stage_ratios[(u16)stage*2];den=tbl->stage_ratios[(u16)stage*2+1];}
    return value*num/den;
}
static u8 c3s_src_fightSideIsJoutaiDataId(u32 side,u32 id)
{
    const C3BattleDataRows *r=&ctx->conditions;
    if(!r->rows||(u16)id>=r->count)return 0;
    const u8 *row=r->rows+(u16)id*r->stride;
    if(rd16(row+8)!=6)return 0;
    return (u8)c3_battle_condition_present(ctx,side,(u16)id,NULL);
}
static u32 c3s_src_tenkouDataBiosGetSolarFlag(u32 weather)
{if(weather)unmodelled("solar flag of weather");return 0;}
static u8 c3s_src_fn_802026E4(u32 fo,u32 id){return (u8)c3_battle_condition_check(ctx,fo,(u16)id,NULL);}

#include "battle_damage_state.inc"

int c3_battle_state_damage(const C3BattleState *s,const C3BattleDamageTables *t,uint32_t attacker,
    uint32_t defender,uint32_t defender_side,uint16_t move,uint16_t power,uint16_t type,int32_t *out,
    char *reason,size_t reason_size)
{
    if(!s||!t||!out||!t->stage_ratios||t->stage_count!=13||!t->item_types)return 0;
    if(atomic_flag_test_and_set_explicit(&in_use,memory_order_acquire)){
        if(reason)snprintf(reason,reason_size,"damage calculation busy");
        return 0;
    }
    ctx=s;tbl=t;accessor_error=0;error_name=NULL;
    memcpy(c3s_src_lbl_80279ED0,t->item_types,sizeof c3s_src_lbl_80279ED0);
    s32 value=c3s_src_fn_80232110(attacker,defender,defender_side,move,power,(s32)type);
    int ok=!accessor_error;
    if(!ok&&reason)snprintf(reason,reason_size,"damage formula: %s",error_name?error_name:"?");
    ctx=NULL;tbl=NULL;
    atomic_flag_clear_explicit(&in_use,memory_order_release);
    if(ok)*out=value;
    return ok;
}
