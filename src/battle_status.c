/* Table-driven status accessors over a guest-addressed arena, generated from
 * the pinned *Bios accessors (tools/prepare_battle_status_fields.py):
 *   pokemonGetStatus/SetStatus  selectors 0xCB..0x123 (FightPokemon/FightOutPokemon)
 *   wazaGetStatus/SetStatus     1..0x24 move data rows, 0x25..0x34 FightWaza
 *   fightFloorGetStatus/Set     1..7 floor data rows, 8..0x5F FightFloor
 * plus the computed selectors lowered by hand below. */
#include "colosseum3ds/battle_status.h"

enum { C3_FIELD_NONE = 0, C3_FIELD_VALUE, C3_FIELD_ADDRESS, C3_FIELD_COMPUTED };
typedef struct {
    uint16_t selector;
    uint8_t get_kind, set_kind;
    uint16_t offset;
    uint8_t width, is_signed, limit, stride;
    int32_t limit_value;
    uint8_t data_row;
} FieldDesc;

static const FieldDesc pokemon_fields[] = {
#include "battle_status_fields.inc"
};
static const FieldDesc waza_fields[] = {
#include "battle_waza_fields.inc"
};
static const FieldDesc floor_fields[] = {
#include "battle_floor_fields.inc"
};
typedef struct { const FieldDesc *rows; uint32_t first, count, index_mask; } FieldTable;
static const FieldTable POKEMON = {pokemon_fields, 0xCBu, sizeof pokemon_fields / sizeof *pokemon_fields, 0xFFu};
static const FieldTable WAZA = {waza_fields, 0x01u, sizeof waza_fields / sizeof *waza_fields, 0xFFFFu};
static const FieldTable FLOOR = {floor_fields, 0x01u, sizeof floor_fields / sizeof *floor_fields, 0xFFFFu};

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

static const FieldDesc *lookup(const FieldTable *t,uint32_t selector)
{
    selector&=0xFFFFu; /* the original dispatchers switch on a u16 selector */
    if(selector<t->first||selector-t->first>=t->count)return NULL;
    const FieldDesc *d=&t->rows[selector-t->first];
    return d->selector==selector?d:NULL;
}
static uint32_t decode(const uint8_t *p,const FieldDesc *d)
{
    uint32_t v=0;
    for(unsigned i=0;i<d->width;++i)v=v<<8|p[i];
    if(d->is_signed&&d->width<4&&(v>>(8*d->width-1)&1u))v|=~0u<<(8*d->width);
    return v;
}
/* Field offset after index handling; returns 0 and sets *value when the index
 * is out of range (the accessor's early return). */
static int field_offset(const FieldTable *t,const FieldDesc *d,uint32_t index,uint32_t *off,uint32_t *value)
{
    uint32_t idx=index&t->index_mask;*off=d->offset;
    if(d->limit){
        if(idx>=d->limit){if(value)*value=(uint32_t)d->limit_value;return 0;}
        *off+=idx*d->stride;
    }
    return 1;
}
/* Generic record getter: obj is an arena address (nonzero). */
static int record_get(const FieldTable *t,const FieldDesc *d,const C3BattleArena *a,uint32_t obj,uint32_t index,uint32_t *value)
{
    uint32_t off;
    if(!field_offset(t,d,index,&off,value))return 1;
    if(d->get_kind==C3_FIELD_ADDRESS){
        if(!c3_battle_arena_bytes(a,obj,off+1))return 0;
        *value=obj+off;return 1;
    }
    const uint8_t *p=c3_battle_arena_bytes(a,obj+off,d->width);
    if(!p)return 0;
    *value=decode(p,d);return 1;
}
static int record_set(const FieldTable *t,const FieldDesc *d,C3BattleArena *a,uint32_t obj,uint32_t index,uint32_t value)
{
    uint32_t off;
    if(!field_offset(t,d,index,&off,NULL))return 1;          /* out-of-range: no-op */
    uint8_t *p=c3_battle_arena_bytes(a,obj+off,d->width);
    if(!p)return 0;
    for(unsigned i=0;i<d->width;++i)p[i]=(uint8_t)(value>>8*(d->width-1-i));
    return 1;
}
/* Data-row getter (static original tables, outside the arena). */
static int row_get(const FieldTable *t,const FieldDesc *d,const C3BattleDataRows *rows,uint32_t id,uint32_t index,uint32_t *value)
{
    if(!rows||!rows->rows||id>=rows->count){*value=0;return 1;}  /* *DataBiosGetPtr: NULL -> 0 */
    uint32_t off;
    if(!field_offset(t,d,index,&off,value))return 1;
    if(d->get_kind!=C3_FIELD_VALUE||off+d->width>rows->stride)return 0;
    *value=decode(rows->rows+id*rows->stride+off,d);return 1;
}

/* ---- pokemonGetStatus / SetStatus ---------------------------------------- */

int c3_battle_fight_status_get(const C3BattleArena *a,uint32_t obj,uint32_t selector,uint32_t index,uint32_t *value)
{
    const FieldDesc *d=lookup(&POKEMON,selector);
    if(!d||!value)return 0;
    if(d->get_kind==C3_FIELD_COMPUTED){
        /* 0xDA / 0xDB: fightOutPokemonGetUse/MotoWazaDataId = FightWaza
         * (selector 0xD9) wazaGetStatus 0x28 / 0x27. */
        if((selector&0xFFFFu)==0xDA||(selector&0xFFFFu)==0xDB){
            uint32_t fw;
            if(!obj){*value=0;return 1;}
            if(!c3_battle_fight_status_get(a,obj,0xD9,0,&fw))return 0;
            return c3_battle_waza_status_get(a,NULL,fw,0,(selector&0xFFFFu)==0xDA?0x28u:0x27u,0,value);
        }
        return 0;
    }
    if(d->get_kind!=C3_FIELD_VALUE&&d->get_kind!=C3_FIELD_ADDRESS)return 0;
    if(!obj){*value=0;return 1;}                               /* obj NULL -> 0 */
    return record_get(&POKEMON,d,a,obj,index,value);
}
int c3_battle_fight_status_set(C3BattleArena *a,uint32_t obj,uint32_t selector,uint32_t index,uint32_t value)
{
    const FieldDesc *d=lookup(&POKEMON,selector);
    if(!d||d->set_kind!=C3_FIELD_VALUE)return 0;
    if(!obj)return 1;                                          /* obj NULL -> no-op */
    return record_set(&POKEMON,d,a,obj,index,value);
}

/* ---- wazaGetStatus / SetStatus ------------------------------------------- */

int c3_battle_waza_status_get(const C3BattleArena *a,const C3BattleDataRows *moves,uint32_t fight_waza,
    uint16_t data_id,uint32_t status,uint32_t index,uint32_t *value)
{
    status&=0xFFFFu;
    if(!value)return 0;
    if(!status||status>=0x35){*value=0;return 1;}
    if(status==0x33||status==0x34){
        /* Type of the original (0x27) / used (0x28) move: data status 3. */
        uint32_t move;
        if(!c3_battle_waza_status_get(a,moves,fight_waza,0,status==0x33?0x27u:0x28u,0,&move))return 0;
        return c3_battle_waza_status_get(a,moves,0,(uint16_t)move,3,0,value);
    }
    const FieldDesc *d=lookup(&WAZA,status);
    if(!d||(d->get_kind!=C3_FIELD_VALUE&&d->get_kind!=C3_FIELD_ADDRESS))return 0;
    if(d->data_row)return row_get(&WAZA,d,moves,data_id,index,value);
    if(!fight_waza){*value=0;return 1;}
    return record_get(&WAZA,d,a,fight_waza,index,value);
}
int c3_battle_waza_status_set(C3BattleArena *a,uint32_t fight_waza,uint32_t status,uint32_t index,uint32_t value)
{
    const FieldDesc *d=lookup(&WAZA,status);
    /* Move data rows are static original tables: never written here. */
    if(!d||d->data_row||d->set_kind!=C3_FIELD_VALUE)return 0;
    if(!fight_waza)return 1;
    return record_set(&WAZA,d,a,fight_waza,index,value);
}

/* ---- fightFloorGetStatus / SetStatus ------------------------------------- */

int c3_battle_floor_status_get(const C3BattleArena *a,const C3BattleDataRows *floors,uint32_t floor,
    uint32_t object,uint32_t slot,uint32_t field,uint32_t arg,uint32_t *value)
{
    uint32_t f=field&0xFFFFu;
    if(!value)return 0;
    if(f>=0x60){*value=0;return 1;}
    if(f==0){*value=floor;return 1;}                           /* fightFloorBiosGetFightFloorPtr */
    if(f>=8&&f<0x5F&&!object){if(!floor){*value=0;return 1;}object=floor;}
    if(f==0x37||f==0x38){
        /* pokemonGetStatus(attacker, 0, 0xDA / 0xDB). */
        uint32_t attacker;
        if(!c3_battle_floor_status_get(a,floors,floor,object,0,0x36,0,&attacker))return 0;
        return c3_battle_fight_status_get(a,attacker,f==0x37?0xDAu:0xDBu,0,value);
    }
    const FieldDesc *d=lookup(&FLOOR,f);
    if(!d||(d->get_kind!=C3_FIELD_VALUE&&d->get_kind!=C3_FIELD_ADDRESS))return 0;
    if(d->data_row)return row_get(&FLOOR,d,floors,slot&0xFFFFu,arg,value);
    if(!object){*value=0;return 1;}
    return record_get(&FLOOR,d,a,object,arg,value);
}
int c3_battle_floor_status_set(C3BattleArena *a,uint32_t floor,uint32_t object,uint32_t field,uint32_t arg,uint32_t value)
{
    const FieldDesc *d=lookup(&FLOOR,field);
    if(!d||d->data_row||d->set_kind!=C3_FIELD_VALUE)return 0;
    if(!object)object=floor;
    if(!object)return 1;
    return record_set(&FLOOR,d,a,object,arg,value);
}

/* ---- fightTargetGetPtr (fight_target.c), fixed floor slots --------------- */

int c3_battle_target(const C3BattleArena *a,uint32_t floor,uint32_t kind,uint32_t index,uint32_t *target)
{
    static const uint8_t floor_field[13]={0x36,0x42,0x44,0x45,0x46,0x47,0x48,0x49,0x4B,0x4C,0x4D,0x4E,0x4F};
    uint32_t k=kind&0xFFFFu;(void)index;
    if(!target)return 0;
    if(k==0){*target=0;return 1;}
    if(k==1){*target=floor;return 1;}
    if(k>=0x11&&k<=0x1D)return c3_battle_floor_status_get(a,NULL,floor,floor,0,floor_field[k-0x11],0,target);
    return 0; /* side/trainer-relative selections: not lowered yet */
}
