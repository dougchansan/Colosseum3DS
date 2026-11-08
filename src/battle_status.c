/* Table-driven pokemonGetStatus / pokemonSetStatus for FightPokemon and
 * FightOutPokemon records (selectors 0xCB..0x123), over a guest-addressed
 * arena. The table is generated from the pinned accessors. */
#include "colosseum3ds/battle_status.h"

enum { C3_FIELD_NONE = 0, C3_FIELD_VALUE, C3_FIELD_ADDRESS, C3_FIELD_COMPUTED };
typedef struct {
    uint16_t selector;
    uint8_t get_kind, set_kind;
    uint16_t offset;
    uint8_t width, is_signed, limit, stride;
    int32_t limit_value;
} FieldDesc;

static const FieldDesc fields[] = {
#include "battle_status_fields.inc"
};
#define FIRST_SELECTOR 0xCBu
#define FIELD_COUNT (sizeof fields / sizeof *fields)

uint8_t *c3_battle_arena_bytes(const C3BattleArena *a,uint32_t address,uint32_t width)
{
    if(!a||!a->bytes||!width||address<C3_ARENA_BASE)return NULL;
    uint32_t off=address-C3_ARENA_BASE;
    if(off>a->size||width>a->size-off)return NULL;
    return a->bytes+off;
}
uint32_t c3_battle_arena_address(const C3BattleArena *a,const uint8_t *p)
{
    if(!a||!a->bytes||!p||p<a->bytes||p>=a->bytes+a->size)return 0;
    return C3_ARENA_BASE+(uint32_t)(p-a->bytes);
}

static const FieldDesc *lookup(uint32_t selector)
{
    selector&=0xFFFFu; /* the original switches on (u16)selector */
    if(selector<FIRST_SELECTOR||selector-FIRST_SELECTOR>=FIELD_COUNT)return NULL;
    const FieldDesc *d=&fields[selector-FIRST_SELECTOR];
    return d->selector==selector?d:NULL;
}

int c3_battle_fight_status_get(const C3BattleArena *a,uint32_t obj,uint32_t selector,uint32_t index,uint32_t *value)
{
    const FieldDesc *d=lookup(selector);
    if(!d||!value||(d->get_kind!=C3_FIELD_VALUE&&d->get_kind!=C3_FIELD_ADDRESS))return 0;
    if(!obj){*value=0;return 1;}                      /* pokemonGetStatus: obj NULL -> 0 */
    uint32_t idx=index&0xFFu,off=d->offset;           /* accessors take a u8 index */
    if(d->limit){
        if(idx>=d->limit){*value=(uint32_t)d->limit_value;return 1;}
        off+=idx*d->stride;
    }
    if(d->get_kind==C3_FIELD_ADDRESS){
        if(!c3_battle_arena_bytes(a,obj,off+1))return 0;  /* the object must reach that far */
        *value=obj+off;return 1;
    }
    const uint8_t *p=c3_battle_arena_bytes(a,obj+off,d->width);
    if(!p)return 0;
    uint32_t v=0;
    for(unsigned i=0;i<d->width;++i)v=v<<8|p[i];
    if(d->is_signed&&d->width<4&&(v>>(8*d->width-1)&1u))v|=~0u<<(8*d->width);
    *value=v;return 1;
}

int c3_battle_fight_status_set(C3BattleArena *a,uint32_t obj,uint32_t selector,uint32_t index,uint32_t value)
{
    const FieldDesc *d=lookup(selector);
    if(!d||d->set_kind!=C3_FIELD_VALUE)return 0;
    if(!obj)return 1;                                 /* pokemonSetStatus: obj NULL -> no-op */
    uint32_t idx=index&0xFFu,off=d->offset;
    if(d->limit){
        if(idx>=d->limit)return 1;                    /* out-of-range index: no-op */
        off+=idx*d->stride;
    }
    uint8_t *p=c3_battle_arena_bytes(a,obj+off,d->width);
    if(!p)return 0;
    for(unsigned i=0;i<d->width;++i)p[i]=(uint8_t)(value>>8*(d->width-1-i));
    return 1;
}
