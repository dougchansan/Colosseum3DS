/* Source lowering: fight_out_pokemon.c fightOutPokemonCreate (0x802068C8) and
 * pokemon_range_exact_801248C4.c pokemonGetTokuseiDataId, over the arena. */
#include "colosseum3ds/battle_fight_out.h"
#include "colosseum3ds/battle_runtime_trainer.h"

static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}

/* pokemonDataBiosGetPtr(species): NULL at or beyond the table count. */
static const uint8_t *species_row(const C3BattleSpeciesTable *t,uint16_t species)
{
    return t&&t->rows&&species<t->count?t->rows+(uint32_t)species*C3_SPECIES_ROW_BYTES:NULL;
}
/* pokemonGetStatus(NULL, species, 0x16 / 0x17, idx): row byte, 0 if absent
 * or idx >= 2 (pokemonDataBiosGetZokusei/TokuseiDataId). */
static uint8_t species_byte(const C3BattleSpeciesTable *t,uint16_t species,unsigned base,uint32_t idx)
{
    const uint8_t *row=species_row(t,species);
    return row&&(uint16_t)idx<2?row[base+(uint16_t)idx]:0;
}

int c3_battle_pokemon_ability(const C3BattleArena *a,uint32_t pokemon,const C3BattleSpeciesTable *t,uint16_t *ability)
{
    if(!ability)return 0;
    if(!pokemon){*ability=0;return 1;}
    const uint8_t *p=c3_battle_arena_bytes(a,pokemon,0xCD);
    if(!p)return 0;
    uint16_t species=be16(p);                                   /* selector 0x6E, +0x00 */
    if(species_byte(t,species,0x32,1)==0){*ability=species_byte(t,species,0x32,0);return 1;}
    /* selector 0xB7, pokemonBiosGetTokuseiFlag: 0 when the species has no
     * second ability (already excluded above), else PokemonBios +0xCC. */
    uint8_t flag=p[0xCC];
    *ability=species_byte(t,species,0x32,flag);
    return 1;
}

int c3_battle_fight_out_pokemon_create(C3BattleArena *a,uint32_t fo,uint32_t fp,uint32_t sequence,const C3BattleSpeciesTable *t)
{
    if(!fo||!fp)return 1;                                       /* if ((r3 != 0) && (r4 != 0)) */
    uint8_t *record=c3_battle_arena_bytes(a,fo,C3_FIGHT_OUT_POKEMON_BYTES);
    uint32_t poke=fp+4;                                         /* fightPokemonGetPokemonPtrInline: 0xCC */
    uint16_t ability=0;
    if(!record||!c3_battle_arena_bytes(a,fp,0x154))return 0;
    /* The FightPokemon must not overlap the record being initialized, so
     * reading it first is equivalent to the source order. */
    if(fp<fo+C3_FIGHT_OUT_POKEMON_BYTES&&fo<fp+0x154u)return 0;
    if(!c3_battle_pokemon_ability(a,poke,t,&ability))return 0; /* read before init: no aliasing */
    const uint8_t *pb=c3_battle_arena_bytes(a,poke,2);
    uint16_t species=be16(pb);
    if(!c3_battle_fight_out_pokemon_init(record))return 0;
    if(!c3_battle_fight_status_set(a,fo,0xD5,0,fp)||!c3_battle_fight_status_set(a,fo,0xD6,0,fp))return 0;
    if(sequence&&!c3_battle_fight_status_set(a,fo,0xEE,0,sequence))return 0;
    for(uint32_t i=0;i<2;++i)
        if(!c3_battle_fight_status_set(a,fo,0xFF,i,species_byte(t,species,0x30,i)))return 0;
    return c3_battle_fight_status_set(a,fo,0x100,0,ability);
}
