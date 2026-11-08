#include "colosseum3ds/battle_preview.h"
#include "dolphin/types.h"
#include <stdatomic.h>
#include <string.h>

/* Scalar handles 1..4 identify the admitted preview actors. No host address
 * is narrowed to a GameCube pointer. This TU executes the original arithmetic
 * while exposing only the item-free, weather-free first-encounter profile. */
static atomic_flag in_use = ATOMIC_FLAG_INIT;
static const C3BattlePreview *context;
static const C3BattlePreviewTables *tables;
static unsigned crit;
static int accessor_error;
static u8 c3p_src_lbl_80279ED0[34];
static const C3BattlePreviewMon rejected_mon;

static const C3BattlePreviewMon *mon(u32 handle)
{
    if (!context || handle < 1 || handle > 4) {
        accessor_error = 1;
        /* Keep failed-accessor reads defined even if no context is active.
         * The outer transaction observes accessor_error and rejects output. */
        return &rejected_mon;
    }
    return &context->mons[handle-1];
}
static u32 c3p_src_pokemonGetStatus(u32 handle, u32 id, u32 field, u32 index)
{
    const C3BattlePreviewMon *m = mon(handle);
    if (id || index) accessor_error = 1;
    switch (field) {
    case 0xd9: return handle + 16; /* A scalar handle for current move state. */
    case 0x88: return m->stats[C3_STAT_ATTACK];
    case 0x89: return m->stats[C3_STAT_DEFENSE];
    case 0x8a: return m->stats[C3_STAT_SP_ATTACK];
    case 0x8b: return m->stats[C3_STAT_SP_DEFENSE];
    case 0xe6: return (u32)(6 + m->stages[0]);
    case 0xe7: return (u32)(6 + m->stages[1]);
    case 0xe8: return (u32)(6 + m->stages[2]);
    case 0xe9: return (u32)(6 + m->stages[3]);
    default: accessor_error = 1; return 0;
    }
}
static s32 c3p_src_wazaGetStatus(u32 handle, u32 id, u32 field, u32 index)
{
    C3BattlePreviewMove move = {0};
    if (index) accessor_error = 1;
    if (handle) {
        if (handle < 17 || handle > 20 || id || field != 0x2b) accessor_error = 1;
        return (s32)crit;
    }
    if (id && !c3_battle_preview_move(tables, (uint16_t)id, &move)) accessor_error = 1;
    switch (field) {
    case 3: return move.type;
    case 5: return move.range;
    case 7: return move.power;
    case 9: return move.effect;
    default: accessor_error = 1; return 0;
    }
}
static u32 c3p_src_fightFloorGetNowTenkouDataId(u32 a, u32 b)
{ (void)a; (void)b; return 0; } /* Profile admits no weather. */
static u32 c3p_src_fightFloorGetStatus(u32 a, u32 b, u32 field, u32 index)
{
    if (a || b || index) accessor_error = 1;
    if (field == 0x1d) return context->original_request.data.kind[1];
    if (field == 0x2c) return context->original_request.data.kind[16];
    if (field == 0x19) return 2;
    accessor_error = 1; return 0;
}
static u16 c3p_src_fightOutPokemonGetTokuseiDataId(u32 h) { return mon(h)->ability; }
static u32 c3p_src_fightOutPokemonGetSoubiItemSoubiDataId(u32 h)
{ (void)mon(h); return 0; } /* Nonzero held items are rejected at admission. */
static u32 c3p_src_figthOutPokemonGetSoubiItemBuff(u32 h) { (void)mon(h); return 0; }
static u32 c3p_src_figthOutPokemonGetPokemonDataId(u32 h) { return mon(h)->species; }
static u32 c3p_src_fightOutPokemonGetPokemonPtr(u32 h) { (void)mon(h); return h; }
static u32 c3p_src_fightFloorGetFightOutPokemonPtrToHeroPtr(u32 floor, u32 h)
{ (void)floor; (void)mon(h); return (h-1)/2 + 32; }
static u8 c3p_src_figthOutPokemonGetLevel(u32 h) { return mon(h)->level; }
static u32 c3p_src_fightFloorGetValidFightOutPokemonCount(u32 floor, u32 mode, u32 h, u32 flags)
{
    (void)floor; (void)mode; (void)flags;
    if (!context || h < 1 || h > 4) { accessor_error = 1; return 0; }
    unsigned first = ((h-1)/2)*2;
    return !!context->mons[first].hp + !!context->mons[first+1].hp;
}
static u32 c3p_src_heroGetStatus(u32 a, u32 b, u32 c)
{ (void)a; (void)b; (void)c; accessor_error = 1; return 0; }
static u32 c3p_src_fn_8010C4A0(u32 type)
{
    if (type == 0) return 1;
    if (type == 14 || type == 17) return 2;
    accessor_error = 1; return 0;
}
static u32 c3p_src_fightFloorCheckFightOutPokemonPtrAryPokemonTokuseiDataId(u32 a, u32 ability, u32 b, u32 c)
{
    (void)a; (void)b; (void)c; u32 count = 0;
    for (unsigned i=0;i<4;++i) count += context->mons[i].hp && context->mons[i].ability == ability;
    return count;
}
static u8 c3p_src_fightOutPokemonIsJoutaiNormal(u32 h)
{ (void)mon(h); return 1; } /* Persistent ailments are rejected on admission. */
static u32 c3p_src_fightFloorCheckFightOutPokemonPtrAryJoutaiDataId(u32 a, u32 b)
{ (void)a; (void)b; accessor_error=1; return 0; }
static u32 c3p_src_fightOutPokemonIsNokoriHpFollowing(u32 h, u32 divisor)
{ const C3BattlePreviewMon *m=mon(h); return divisor && m->hp <= m->maximum_hp/divisor; }
static u32 c3p_src_fightAbicntDoKakeWaru(u32 stage, u32 value)
{
    if (stage > 12 || !tables->bytes[885+stage*2]) { accessor_error=1; return 1; }
    return value * tables->bytes[884+stage*2] / tables->bytes[885+stage*2];
}
static u8 c3p_src_fightSideIsJoutaiDataId(u32 side, u32 status)
{
    if (side > 1) { accessor_error=1; return 0; }
    if (status == 0x48) return context->reflect[side] != 0;
    if (status == 0x49) return 0; /* Light Screen cannot be introduced here. */
    accessor_error=1; return 0;
}
static u32 c3p_src_tenkouDataBiosGetSolarFlag(u32 weather)
{ if (weather) accessor_error=1; return 0; }
static u8 c3p_src_fn_802026E4(u32 h, u32 status)
{
    (void)mon(h);
    /* Original damage body queries burn and Flash Fire. Neither can occur
     * with this admitted initial party and the available move set. */
    if (status == 6 || status == 0x3a) return 0;
    accessor_error=1; return 0;
}
#include "battle_damage_original.inc"

int c3_battle_preview_base_damage(const C3BattlePreview *b,
    const C3BattlePreviewTables *t, unsigned a, unsigned d,
    uint16_t move, uint16_t power, unsigned type, unsigned critical, uint32_t *out)
{
    if (!b || !t || !out || a>=4 || d>=4 || !power || power>250 ||
        (type!=0 && type!=14 && type!=17) || (critical!=1 && critical!=2) ||
        b->original_request.data.kind[1]) return 0;
    for (unsigned i=0;i<4;++i) {
        const C3BattlePreviewMon *m=&b->mons[i];
        if (m->ability!=28 && m->ability!=53) return 0;
        if (!m->level || m->level>100) return 0;
        for (unsigned j=0;j<6;++j) if (!m->stats[j] || m->stats[j]>999) return 0;
        for (unsigned j=0;j<7;++j) if (m->stages[j]<-6 || m->stages[j]>6) return 0;
    }
    for (unsigned i=0;i<13;++i)
        if (!t->bytes[884+2*i] || t->bytes[884+2*i]>40 ||
            t->bytes[885+2*i]<10 || t->bytes[885+2*i]>40) return 0;
    if (atomic_flag_test_and_set_explicit(&in_use,memory_order_acquire)) return 0;
    context=b; tables=t; crit=critical; accessor_error=0;
    memcpy(c3p_src_lbl_80279ED0,t->bytes+940,34);
    s32 value=c3p_src_fn_80232110(a+1,d+1,d/2,move,power,(s32)type);
    int ok=!accessor_error && value>=0;
    context=NULL; tables=NULL;
    atomic_flag_clear_explicit(&in_use,memory_order_release);
    if (ok) *out=(uint32_t)value;
    return ok;
}
