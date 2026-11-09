/* Battle memory after trainer entry, and the original validity checks:
 * pokemon_range_8011F5FC.c pokemonCheckValid / pokemonCheckFightOut,
 * fight_out_pokemon.c fightPokemonCheckValidInline / ...CheckFightOutInline,
 * fightOutPokemonCheckValid / fightOutPokemonCheckFightOut. */
#include "colosseum3ds/battle_state.h"
#include <stdio.h>
#include <string.h>

static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static int fail(char *e,const char *m){if(e)snprintf(e,C3_BATTLE_ERROR_SIZE,"%s",m);return 0;}

uint32_t c3_battle_state_fight_pokemon(const C3BattleState *s,unsigned side,unsigned slot)
{return s&&side<2&&slot<6?s->trainer[side]+0xB1Cu+slot*0x154u:0;}
uint32_t c3_battle_state_fight_out(const C3BattleState *s,unsigned side,unsigned slot)
{return s&&side<2&&slot<2?s->trainer[side]+C3_RUNTIME_FIGHT_OUT_OFFSET+slot*C3_FIGHT_OUT_POKEMON_BYTES:0;}

/* ---- msgctrlSetValue (msgctrl_exact_80132A38.c) -------------------------- */

void c3_battle_msgctrl_set(C3BattleState *s,uint32_t id,uint32_t value)
{
    static const uint8_t ids[]={0x2D,0x2E,0x2F,0x30,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x51,0x39,0x4D,0x57,
        0x4E,0x4B,0x4C,0x50,0x55,0x56,0x58,0x59,0x5D,0x0D,0x0E,0x41,0x0F,0x10,0x11,0x12,0x13,0x14,0x15,0x16,
        0x17,0x18,0x19,0x1A,0x1B,0x1C,0x1D,0x1E,0x1F,0x20,0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,
        0x42,0x43,0x44};
    static const uint8_t half[]={0x2D,0x2E,0x39,0x4E,0x58,0x59,0x5D};
    if(!s||id>=C3_MSGCTRL_IDS)return;
    int known=0;
    for(unsigned i=0;i<sizeof ids;++i)known|=ids[i]==id;
    if(!known)return;
    for(unsigned i=0;i<sizeof half;++i)if(half[i]==id)value&=0xFFFFu;
    s->msgctrl[id]=value;
}

/* ---- fightFloorInit / fightSideInitAry ----------------------------------- */

/* fn_8011B950: reset `count` 16-byte condition slots, preserving the two
 * padding bytes (+7, +0xE..0xF stay as they were). */
static void condition_init(uint8_t *p,unsigned count)
{
    for(unsigned i=0;i<count;++i,p+=16){memset(p,0,7);memset(p+8,0,6);}
}
static void floor_field(C3BattleState *s,uint32_t field,uint32_t value)
{
    (void)c3_battle_floor_status_set(&s->arena,s->floor,s->floor,field,0,value);
}
/* fightFloorSetStatus with value 0 for the target fields whose setter also
 * drives message variables: the zero branch clears them, then stores 0. */
static void floor_target_zero(C3BattleState *s,uint32_t field)
{
    static const uint8_t clear36[]={0x0F,0x1A,0x1F,0x21,0x20},clear42[]={0x10,0x1B,0x42,0x44,0x43},
        clear47[]={0x12,0x1D},clear4b[]={0x1E,0x1C};
    const uint8_t *ids=NULL;unsigned n=0;
    if(field==0x36){ids=clear36;n=sizeof clear36;}
    else if(field==0x42){ids=clear42;n=sizeof clear42;}
    else if(field==0x47){ids=clear47;n=sizeof clear47;}
    else if(field==0x4B){ids=clear4b;n=sizeof clear4b;}
    for(unsigned i=0;i<n;++i)c3_battle_msgctrl_set(s,ids[i],0);
    /* 0x49 with 0: no item lookup. Every target then stores 0 at its
     * FightFloor offset (generated floor table: 0x36 +0xA474 ... 0x4F +0xA4A4). */
    static const struct{uint8_t field;uint16_t offset;}store[13]={
        {0x36,0xA474},{0x42,0xA478},{0x44,0xA47C},{0x45,0xA480},{0x46,0xA484},{0x47,0xA488},{0x48,0xA48C},
        {0x49,0xA490},{0x4B,0xA494},{0x4C,0xA498},{0x4D,0xA49C},{0x4E,0xA4A0},{0x4F,0xA4A4}};
    for(unsigned i=0;i<13;++i)if(store[i].field==field)memset(s->memory+store[i].offset,0,4);
}

int c3_battle_floor_init(C3BattleState *s,uint16_t encounter)
{
    if(!s)return 0;
    uint8_t *f=s->memory;
    condition_init(f+0x00,1);                                   /* field 9: floor condition */
    floor_field(s,0x0C,0);floor_field(s,0x0D,0);
    for(unsigned side=0;side<2;++side){                         /* fightSideInitAry(field 0x35, 2) */
        uint8_t *sd=f+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES;
        sd[0]=sd[1]=0;                                          /* side data ID */
        condition_init(sd+4,6);
        for(unsigned t=0;t<2;++t)                               /* fightTrainerInit(trainer, 2) */
            if(!c3_battle_runtime_trainer_init(sd+C3_FIGHT_SIDE_TRAINER_OFFSET+t*C3_BATTLE_RUNTIME_TRAINER_BYTES))return 0;
        sd[0x522C]=0;                                           /* spikes check flag */
    }
    static const uint8_t targets[13]={0x36,0x42,0x44,0x45,0x46,0x47,0x48,0x49,0x4B,0x4C,0x4D,0x4E,0x4F}; /* lbl_80279C28 */
    for(unsigned i=0;i<13;++i)floor_target_zero(s,targets[i]);
    for(uint32_t field=0x50;field<=0x58;++field){
        if(field==0x56){                                        /* item 0's name, then the item ID */
            c3_battle_msgctrl_set(s,0x29,C3_MSGREF_ITEM_NAME(0));f[0xA4BC]=f[0xA4BD]=0;
        }else if(field==0x57){                                  /* ability 0's name, then the ID */
            c3_battle_msgctrl_set(s,0x1C,C3_MSGREF_ABILITY_NAME(0));f[0xA4BE]=f[0xA4BF]=0;
        }else floor_field(s,field,0);
    }
    memset(f+0xA4C4,0,8*4);                                     /* field 0x5A: active array */
    floor_field(s,0x5B,0);
    floor_field(s,0x0D,encounter);
    floor_field(s,0x0C,1);
    return 1;
}

int c3_battle_state_from_entry(C3BattleState *s,const C3BattleEntrySession *e,const C3BattleEntryPack *p,char *err)
{
    if(!s||!e||!p)return fail(err,"Missing battle state inputs.");
    if(e->state!=C3_ENTRY_SESSION_DONE||e->source_result[0]!=1||e->source_result[1]!=1)
        return fail(err,"Trainer entry did not finish.");
    memset(s,0,sizeof *s);
    s->arena.bytes=s->memory;s->arena.size=sizeof s->memory;
    s->floor=C3_ARENA_BASE;
    if(!c3_battle_floor_init(s,e->config.encounter_id))return fail(err,"FightFloor initialization failed.");
    /* fightActionFlowKaijou: fightSideCreate(side target 4/5, floor data
     * field 3 [side]) after fightFloorInit. Its condition, trainer and spikes
     * resets repeat fightFloorInit's; the side data ID is the new state. */
    for(unsigned side=0;side<2;++side){
        uint8_t *sd=s->memory+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES;
        sd[0]=(uint8_t)(e->config.side_data_id[side]>>8);sd[1]=(uint8_t)e->config.side_data_id[side];
    }
    for(unsigned side=0;side<2;++side){
        uint32_t off=C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES+C3_FIGHT_SIDE_TRAINER_OFFSET;
        s->trainer[side]=C3_ARENA_BASE+off;
        memcpy(s->memory+off,e->runtime[side],C3_BATTLE_RUNTIME_TRAINER_BYTES);
    }
    /* Relocate FightPokemon source links (native party identity -> address). */
    for(unsigned side=0;side<2;++side)for(unsigned j=0;j<6;++j){
        uint8_t *fp=c3_battle_arena_bytes(&s->arena,c3_battle_state_fight_pokemon(s,side,j),0x154);
        uint32_t link=be32(fp);
        if(!link)continue;
        int found=0;
        for(unsigned k=0;k<6&&!found;++k)
            if(link==e->slots[side].party[k]){
                put32(fp,s->trainer[side]+C3_BATTLE_HERO_PARTY_OFFSET+k*C3_BATTLE_POKEMON_BYTES);found=1;}
        if(!found)return fail(err,"FightPokemon source link is not a known party slot.");
    }
    const C3BattleEntryPackView *spc=&p->sections[C3_PACK_SPC],*mov=&p->sections[C3_PACK_MOV],*flr=&p->sections[C3_PACK_FLR];
    if(!spc->bytes||!mov->bytes||!flr->bytes)return fail(err,"Battle tables are missing from the entry pack.");
    s->species=(C3BattleSpeciesTable){spc->bytes,spc->count};
    s->moves=(C3BattleDataRows){mov->bytes,mov->count,56};
    s->floors=(C3BattleDataRows){flr->bytes,flr->count,24};
    const C3BattleEntryPackView *enc=&p->sections[C3_PACK_ENC],*typ=&p->sections[C3_PACK_TYP],*knd=&p->sections[C3_PACK_KND];
    if(!enc->bytes||!typ->bytes||!knd->bytes)return fail(err,"Encounter tables are missing from the entry pack.");
    s->encounters=(C3BattleDataRows){enc->bytes,enc->count,56};
    /* fightTypeDataBiosGetPtr / fightKindDataBiosGetPtr admit index == count;
     * the pack carries that boundary row. */
    s->types=(C3BattleDataRows){typ->bytes,typ->count+1u,8};
    s->kinds=(C3BattleDataRows){knd->bytes,knd->count+1u,32};
    s->rng=e->rng;                                              /* shared stream continues */
    if(err)err[0]=0;
    return 1;
}

static const uint8_t *row_at(const C3BattleDataRows *r,uint32_t id)
{return r->rows&&id<r->count?r->rows+id*r->stride:NULL;}
int c3_battle_floor_value(const C3BattleState *s,uint32_t field,uint32_t *value)
{
    if(!s||!value)return 0;
    field&=0xFFFFu;
    if(field==0x14||field==0x16||field==0x18||field==0x19||field==0x1A||(field>=0x1C&&field<=0x34)){
        uint32_t encounter=0;
        if(!c3_battle_floor_status_get(&s->arena,&s->floors,s->floor,0,0,0x0D,0,&encounter))return 0;
        const uint8_t *er=row_at(&s->encounters,encounter&0xFFFFu);
        if(field==0x14||field==0x16||field==0x18||field==0x19){
            uint8_t type=er?er[1]:0;
            if(field==0x14){*value=type;return 1;}
            const uint8_t *tr=row_at(&s->types,type);
            if(field==0x19){*value=tr?(uint32_t)tr[0]*tr[2]:0;return 1;}   /* fightTypeGetFightSideFightOutPokemonMax */
            *value=tr?tr[field==0x16?0:2]:0;return 1;
        }
        uint8_t kind=er?er[0]:0;
        if(field==0x1A){*value=kind;return 1;}
        if(field==0x1B)return 0;                                     /* kind name: not a flag */
        const uint8_t *kr=row_at(&s->kinds,kind);
        *value=kr?kr[field-0x1C]:0;return 1;
    }
    return c3_battle_floor_status_get(&s->arena,&s->floors,s->floor,0,0,field,0,value);
}

static const uint8_t *bytes(const C3BattleState *s,uint32_t a,uint32_t n,int *fault)
{
    const uint8_t *p=c3_battle_arena_bytes(&s->arena,a,n);
    if(!p&&fault)*fault=1;
    return p;
}
static uint32_t status(const C3BattleState *s,uint32_t obj,uint32_t sel,int *fault)
{
    uint32_t v=0;
    if(!c3_battle_fight_status_get(&s->arena,obj,sel,0,&v)&&fault)*fault=1;
    return v;
}
/* Species validity used by both Pokemon checks: id nonzero, species name
 * (data selector 0x01, row +0x18) nonzero, id below the table count. */
static int species_valid(const C3BattleState *s,uint16_t id)
{
    if(!id||id>=s->species.count||!s->species.rows)return 0;
    return be32(s->species.rows+(uint32_t)id*C3_SPECIES_ROW_BYTES+0x18)!=0;
}
/* gamedataAttestCheckValid(&pokemon.attest): all four attest bytes nonzero. */
static int attest_valid(const uint8_t *p){return p[8]&&p[9]&&p[10]&&p[11];}

int c3_battle_pokemon_check_valid(const C3BattleState *s,uint32_t pokemon,int *fault)
{
    if(!s||!pokemon)return 0;
    const uint8_t *p=bytes(s,pokemon,C3_BATTLE_POKEMON_BYTES,fault);
    if(!p||!species_valid(s,be16(p))||!attest_valid(p))return 0;
    return p[0xCD]!=1;                                          /* bad-egg flag 0xB8 */
}
int c3_battle_pokemon_check_fight_out(const C3BattleState *s,uint32_t pokemon,int *fault)
{
    if(!c3_battle_pokemon_check_valid(s,pokemon,fault))return 0;
    const uint8_t *p=bytes(s,pokemon,C3_BATTLE_POKEMON_BYTES,fault);
    if(be16(p)==0x19C)return 0;                                 /* species 412 */
    if(p[0xCB]==1)return 0;                                     /* egg flag 0xB6 */
    return be16(p+0x8A)!=0;                                     /* 0x7B fainted: HP 0x83 == 0 */
}
int c3_battle_fight_pokemon_check_valid(const C3BattleState *s,uint32_t fp,int *fault)
{
    if(!s||!fp||s->result==1)return 0;
    uint32_t moto=status(s,fp,0xCB,fault);
    if(!moto||!c3_battle_pokemon_check_valid(s,moto,fault))return 0;
    uint32_t poke=status(s,fp,0xCC,fault);                      /* fightPokemonGetPokemonPtrInline */
    if(!poke||!c3_battle_pokemon_check_valid(s,poke,fault))return 0;
    return (int32_t)status(s,fp,0xCE,fault)>=0;                 /* entry ID */
}
int c3_battle_fight_pokemon_check_fight_out(const C3BattleState *s,uint32_t fp,int *fault)
{
    if(!fp||!c3_battle_fight_pokemon_check_valid(s,fp,fault))return 0;
    if(status(s,fp,0xD2,fault)==1)return 0;                     /* hokaku (caught) flag */
    uint32_t poke=status(s,fp,0xCC,fault);
    return poke&&c3_battle_pokemon_check_fight_out(s,poke,fault);
}
int c3_battle_fight_out_check_valid(const C3BattleState *s,uint32_t fo,int *fault)
{
    if(!s||!fo||s->result==1)return 0;
    uint32_t fp=status(s,fo,0xD6,fault);
    return fp&&c3_battle_fight_pokemon_check_valid(s,fp,fault);
}
int c3_battle_fight_out_check_fight_out(const C3BattleState *s,uint32_t fo,int *fault)
{
    if(!fo||!c3_battle_fight_out_check_valid(s,fo,fault))return 0;
    if((int32_t)status(s,fo,0x120,fault)==1)return 0;
    return c3_battle_fight_pokemon_check_fight_out(s,status(s,fo,0xD6,fault),fault);
}

int c3_battle_side_check_valid(const C3BattleState *s,uint32_t side,int *fault)
{
    if(!s||!side||s->result==1)return 0;
    const uint8_t *p=bytes(s,side,2,fault);
    return p&&be16(p)!=0;                                        /* fightSideBiosGetFightSideDataId */
}
int c3_battle_hero_check_valid(const C3BattleState *s,uint32_t hero,int *fault)
{
    /* GScharCmp(name, "") == 0 exactly when the name's first code unit is
     * the terminator (msgGetLength 0). */
    if(!s||!hero)return 0;
    const uint8_t *name=bytes(s,hero,2,fault),*flag=bytes(s,hero+0xA80,1,fault);
    if(!name||!flag||!be16(name))return 0;
    return flag[0]!=2;
}
int c3_battle_trainer_check_valid(const C3BattleState *s,uint32_t trainer,int *fault)
{
    if(!s||!trainer||s->result==1)return 0;
    const uint8_t *id=bytes(s,trainer,2,fault);                 /* field 0x43 */
    if(!id||!be16(id))return 0;
    return c3_battle_hero_check_valid(s,trainer+4,fault);       /* field 0x44 */
}

/* ---- conditions (field_range_80117E58.c, fight_range_80201764.c) -------- */

static const uint8_t *condition_row(const C3BattleState *s,uint16_t id)
{
    const C3BattleDataRows *r=&s->conditions;
    return r->rows&&id<r->count?r->rows+(uint32_t)id*r->stride:NULL;
}
static uint16_t condition_category(const C3BattleState *s,uint16_t id)  /* fn_80119ED0 */
{const uint8_t *row=condition_row(s,id);return row?be16(row+8):0;}

/* statusGetStatus(kind, object, 0, selector, 0) for the condition arrays. */
static uint32_t condition_base(const C3BattleState *s,uint8_t kind,uint32_t object,uint16_t selector,int *fault)
{
    uint32_t v=0;
    if(!object||kind==0||kind>9)return 0;                        /* statusGetStatus: 0 */
    if(kind==7&&selector==9)return object;                       /* fightFloorBiosGetJoutaiPtr(floor, 0) */
    if(kind==8&&selector==6)return object+4;                     /* fightSideBiosGetJoutaiPtr(side, 0) */
    if(kind==5){
        if(selector==0x7C)return object+0x64;                    /* fn_8011F474: condition[16] */
        if(selector==0xC8)return object+0xE8;                    /* fn_8011EDC4: attest2[16] */
        if(c3_battle_fight_status_get(&s->arena,object,selector,0,&v))return v;
    }else if(kind==6){
        if(c3_battle_waza_status_get(&s->arena,&s->moves,object,0,selector,0,&v))return v;
    }
    if(fault)*fault=1;                                           /* other kinds/selectors: not modelled */
    return 0;
}
int c3_battle_condition_present(const C3BattleState *s,uint32_t object,uint16_t id,int *fault)
{
    if(!s||!id)return 0;
    const uint8_t *row=condition_row(s,id);
    uint8_t kind=row?row[0]:0,index=row?row[1]:0;              /* fn_80119F10, fn_80119E90 */
    uint32_t base=condition_base(s,kind,object,condition_category(s,id),fault);
    if(!base)return 0;
    const uint8_t *slot=bytes(s,base+(uint32_t)index*16u,2,fault);
    if(!slot)return 0;
    uint16_t stored=be16(slot);                                  /* fn_8011A090 */
    return stored!=0&&stored==id;
}
static uint8_t *condition_slot(C3BattleState *s,uint32_t object,uint16_t id,const uint8_t **row,int *fault)
{
    *row=condition_row(s,id);
    uint8_t kind=*row?(*row)[0]:0,index=*row?(*row)[1]:0;     /* fn_80119F10, fn_80119E90 */
    uint32_t base=condition_base(s,kind,object,condition_category(s,id),fault);
    if(!base)return NULL;
    uint8_t *slot=c3_battle_arena_bytes(&s->arena,base+(uint32_t)index*16u,16);
    if(!slot&&fault)*fault=1;
    return slot;
}
static void slot_zero(uint8_t *slot){memset(slot,0,7);memset(slot+8,0,6);}  /* fn_80119FF0 ... fn_80119F90 */
int c3_battle_condition_write(C3BattleState *s,uint32_t object,uint16_t id,uint16_t argument,int *fault)
{
    const uint8_t *row=NULL;
    if(!s||!id)return 1;
    uint8_t *slot=condition_slot(s,object,id,&row,fault);
    if(!slot)return 1;
    uint8_t type=row?row[2]:0,limit=row?row[3]:0,kaisuu=row?row[4]:0;
    if(kaisuu!=0){if(fault)*fault=1;return 0;}                   /* kaisuuGetKaisuu: rows 1+ not packed */
    uint8_t old=type==4?slot[6]:0;                               /* fn_8011A030 */
    slot_zero(slot);
    slot[0]=(uint8_t)(id>>8);slot[1]=(uint8_t)id;
    slot[2]=(uint8_t)(argument>>8);slot[3]=(uint8_t)argument;
    slot[4]=0xFF;                                                /* kaisuu row 0: -1 */
    if(type==4){uint8_t count=(uint8_t)(old+1u);slot[6]=limit<count?limit:count;}
    return 1;
}
int c3_battle_condition_check_write(const C3BattleState *s,uint32_t object,uint16_t id,int *fault)
{
    const uint8_t *row=NULL;
    if(!s)return 0;
    if(!id)return 1;
    uint8_t *slot=condition_slot((C3BattleState *)s,object,id,&row,fault);
    if(!slot)return 0;
    uint16_t stored=be16(slot);
    uint8_t type=row?row[2]:0;
    const uint8_t *stored_row=condition_row(s,stored);
    uint16_t link=stored_row?be16(stored_row+0xA):0;            /* fn_80119E10 */
    switch(type){
    case 0:case 1:return 2;
    case 2:return stored?1:2;
    case 3:if(!stored)return 2;return stored==id||link==id?1:2;
    case 4:if(!stored||stored==id)return 2;return link!=id?1:2;
    default:return 0;
    }
}
void c3_battle_condition_erase(C3BattleState *s,uint32_t object,uint16_t id,int *fault)
{
    const uint8_t *row=NULL;
    if(!s||!id||!c3_battle_condition_present(s,object,id,fault))return;
    uint8_t *slot=condition_slot(s,object,id,&row,fault);
    if(slot)slot_zero(slot);
}
int c3_battle_condition_check(const C3BattleState *s,uint32_t fo,uint16_t id,int *fault)
{
    if(!s)return 0;
    uint16_t category=condition_category(s,id);
    if(category==0x7C||category==0xC8||category==0xCD){
        uint32_t fp=fo?status(s,fo,0xD6,fault):0;
        if(category==0xCD)return c3_battle_condition_present(s,fp,id,fault);
        uint32_t poke=fp?status(s,fp,0xCC,fault):0;
        return c3_battle_condition_present(s,poke,id,fault);      /* fn_80121ADC */
    }
    if(category!=0xD8)return 0;
    return c3_battle_condition_present(s,fo,id,fault);
}

/* ---- shared battle queries ------------------------------------------------ */

int c3_battle_state_ability_on_any_battler(const C3BattleState *s,uint16_t ability)
{
    for(unsigned side=0;side<2;++side)for(unsigned tr=0;tr<2;++tr)for(unsigned k=0;k<3;++k){
        uint32_t trainer=s->floor+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES+C3_FIGHT_SIDE_TRAINER_OFFSET+
            tr*C3_BATTLE_RUNTIME_TRAINER_BYTES;
        uint32_t fo=trainer+(k<2?C3_RUNTIME_FIGHT_OUT_OFFSET+k*C3_FIGHT_OUT_POKEMON_BYTES:0x20D4u);
        uint32_t v=0;
        if(c3_battle_fight_status_get(&s->arena,fo,0x100,0,&v)&&(uint16_t)v==ability)return 1;
    }
    return 0;
}
int c3_battle_state_weather(const C3BattleState *s,int check,int *out)
{
    static const struct{uint16_t id;uint8_t w;}table[]={{0x4E,0},{0x4F,1},{0x50,2},{0x51,3},{0x52,4},{0x53,1},{0x54,2},{0x55,3}};
    if(!s||!out)return 0;
    if(check&&(c3_battle_state_ability_on_any_battler(s,13)||c3_battle_state_ability_on_any_battler(s,0x4D)))return 0;
    *out=0;
    for(unsigned i=0;i<sizeof table/sizeof *table;++i){
        const uint8_t *row=s->conditions.rows&&table[i].id<s->conditions.count?
            s->conditions.rows+table[i].id*s->conditions.stride:NULL;
        if(row&&be16(row+8)==9&&c3_battle_condition_present(s,s->floor,table[i].id,NULL)){*out=table[i].w;return 1;}
    }
    return 1;
}
int c3_battle_state_held_item_effect(const C3BattleState *s,uint32_t fo,uint16_t *effect)
{
    uint32_t fp=0,poke=0;
    if(!s||!effect)return 0;
    *effect=0;
    if(fo)(void)c3_battle_fight_status_get(&s->arena,fo,0xD6,0,&fp);
    if(fp)(void)c3_battle_fight_status_get(&s->arena,fp,0xCC,0,&poke);
    if(!poke||c3_battle_condition_check(s,fo,0x3D,NULL))return 1;
    const uint8_t *p=c3_battle_arena_bytes(&s->arena,poke+0x88,2);
    return p&&!p[0]&&!p[1];
}
uint32_t c3_battle_state_battler_side(const C3BattleState *s,uint32_t fo)
{
    /* fightFloorGetFightOutPokemonPtrToFightTrainerPtr, then its FightSide. */
    uint32_t original=0;
    if(!s||!fo||!c3_battle_fight_status_get(&s->arena,fo,0xD5,0,&original)||!original)return 0;
    for(unsigned side=0;side<2;++side)for(unsigned tr=0;tr<2;++tr){
        uint32_t trainer=s->floor+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES+C3_FIGHT_SIDE_TRAINER_OFFSET+
            tr*C3_BATTLE_RUNTIME_TRAINER_BYTES;
        for(unsigned k=0;k<6;++k)if(trainer+0xB1Cu+k*0x154u==original)
            return s->floor+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES;
    }
    return 0;
}
int c3_battle_waza_set_use(C3BattleState *s,uint32_t fw,uint16_t move)
{
    uint32_t power=0,type=0;
    if(!s)return 0;
    if(!c3_battle_waza_status_set(&s->arena,fw,0x28,0,move))return 0;
    if(!c3_battle_waza_status_get(&s->arena,&s->moves,0,move,7,0,&power)||
       !c3_battle_waza_status_get(&s->arena,&s->moves,0,move,3,0,&type))return 0;
    return c3_battle_waza_status_set(&s->arena,fw,0x2F,0,(uint16_t)power)&&
           c3_battle_waza_status_set(&s->arena,fw,0x30,0,(uint16_t)type);
}
int c3_battle_state_valid_fight_out_count(const C3BattleState *s,uint32_t mode,uint32_t target,int check,uint16_t *count)
{
    uint32_t trainers=0,pokemons=0;
    int fault=0;
    if(!s||!count||mode>1)return 0;
    if(!c3_battle_floor_value(s,0x16,&trainers)||!c3_battle_floor_value(s,0x18,&pokemons))return 0;
    uint16_t n=0;
    uint32_t target_side=target&&mode==1?c3_battle_state_battler_side(s,target):0;
    for(uint16_t pokemon=0;pokemon<(uint16_t)pokemons;++pokemon)
        for(uint16_t trainer=0;trainer<(uint16_t)trainers;++trainer)
            for(uint16_t side=0;side<2;++side){
                uint32_t sd=s->floor+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES;   /* floor field 0x35 */
                if(!c3_battle_side_check_valid(s,sd,&fault))continue;
                if(trainer>=2)return 0;                          /* beyond the side's two trainers */
                uint32_t tr=sd+C3_FIGHT_SIDE_TRAINER_OFFSET+trainer*C3_BATTLE_RUNTIME_TRAINER_BYTES;
                if(!c3_battle_trainer_check_valid(s,tr,&fault))continue;
                if(pokemon>=2)return 0;                          /* trainer field 0x46 holds two */
                uint32_t fo=tr+C3_RUNTIME_FIGHT_OUT_OFFSET+pokemon*C3_FIGHT_OUT_POKEMON_BYTES;
                if(!c3_battle_fight_out_check_valid(s,fo,&fault))continue;
                /* _fightFloorGetValidFightOutPokemonCountSub */
                if(check&&!c3_battle_fight_out_check_fight_out(s,fo,&fault))continue;
                if(mode==1){
                    if(!target_side||target_side!=c3_battle_state_battler_side(s,fo))continue;
                }else if(target&&target==fo)continue;
                ++n;
            }
    if(fault)return 0;
    *count=n;
    return 1;
}
