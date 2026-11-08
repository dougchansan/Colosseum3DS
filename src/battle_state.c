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

int c3_battle_state_from_entry(C3BattleState *s,const C3BattleEntrySession *e,const C3BattleEntryPack *p,char *err)
{
    if(!s||!e||!p)return fail(err,"Missing battle state inputs.");
    if(e->state!=C3_ENTRY_SESSION_DONE||e->source_result[0]!=1||e->source_result[1]!=1)
        return fail(err,"Trainer entry did not finish.");
    memset(s,0,sizeof *s);
    s->arena.bytes=s->memory;s->arena.size=sizeof s->memory;
    for(unsigned side=0;side<2;++side){
        s->trainer[side]=C3_ARENA_BASE+side*C3_BATTLE_STATE_TRAINER_STRIDE;
        memcpy(s->memory+side*C3_BATTLE_STATE_TRAINER_STRIDE,e->runtime[side],C3_BATTLE_RUNTIME_TRAINER_BYTES);
    }
    s->floor=C3_ARENA_BASE+C3_BATTLE_STATE_FLOOR_OFFSET;
    /* Relocate FightPokemon source links (native party identity -> address). */
    for(unsigned side=0;side<2;++side)for(unsigned j=0;j<6;++j){
        uint8_t *fp=s->memory+side*C3_BATTLE_STATE_TRAINER_STRIDE+0xB1Cu+j*0x154u;
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
    if(err)err[0]=0;
    return 1;
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
    if(kind==5){
        if(selector==0x7C)return object+0x64;                    /* fn_8011F474: condition[16] */
        if(selector==0xC8)return object+0xE8;                    /* fn_8011EDC4: attest2[16] */
        if(c3_battle_fight_status_get(&s->arena,object,selector,0,&v))return v;
    }else if(kind==6){
        if(c3_battle_waza_status_get(&s->arena,&s->moves,object,0,selector,0,&v))return v;
    }
    if(fault)*fault=1;                                           /* kinds 1-4, 7-9: not modelled */
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
