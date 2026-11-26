#ifndef C3_PDA_H
#define C3_PDA_H
#include "colosseum3ds/game_state.h"

typedef struct { const uint8_t *data; size_t size; uint32_t count; } C3PdaAssets;
typedef struct {
    uint16_t id,dex,height,weight;
    uint8_t types[2],abilities[2],growth,color;
    uint32_t name,category,description,seen_flag,caught_flag;
    uint16_t stats[6];
} C3PdaSpecies;
typedef struct {
    uint16_t id,species,level,slot,notified_flag,snag_flag,purified_flag,seen_flag,hint_flag,unlock_flag;
    uint8_t seen,snagged,purified,notified,hint;
    uint32_t hint_message,location;
} C3PdaSnag;
typedef struct { C3GameMailRead save; uint32_t script; } C3PdaMailAction;
int c3_pda_assets_open(C3PdaAssets *assets,const void *data,size_t size);
const uint8_t *c3_pda_entry(const C3PdaAssets *assets,unsigned kind,unsigned key,size_t *size);
int c3_pda_text(const C3PdaAssets *assets,uint32_t id,const char *hero,const char *partner,char *text,size_t capacity);
/* As c3_pda_text, with e-acute and gender signs kept as UTF-8 (story text box). */
int c3_pda_text_utf8(const C3PdaAssets *assets,uint32_t id,const char *hero,const char *partner,char *text,size_t capacity);
int c3_pda_species(const C3PdaAssets *assets,unsigned id,C3PdaSpecies *species);
int c3_pda_snag(const C3PdaAssets *assets,const C3GameState *state,unsigned index,C3PdaSnag *snag);
int c3_pda_mail_action(const C3PdaAssets *assets,const C3GameState *state,unsigned id,C3PdaMailAction *action);
/* Read-only helpers for actual memo/party records. */
int c3_pda_memo_caught(const C3PdaAssets *assets,const C3GameState *state,unsigned species,int *caught);
int c3_pda_experience(const C3PdaAssets *assets,const uint8_t pokemon[312],uint32_t *progress,uint32_t *range);
/* A Shadow Pokemon's heart gauge as the summary bar draws it
 * (pokemonToMenuPokemonStatusSubBar / pokemonToMenuPokemonStatus): range is
 * the full gauge (fn_801EEEB8: the save's Shadow runtime table, row
 * id*0x18 +0x4A6, in gauge units) and progress the gauge left (pokemonGetDp,
 * +0xDC / 100), both x100 here; state is darkState (1 Shadow, 2 in Hyper
 * Mode: the record's status entry holds 0x3E); level is the dark level
 * (shadow_moves.h). Returns 0 for a Pokemon that is not Shadow. */
typedef struct C3PdaHeart { uint32_t progress, range; uint8_t state, level; } C3PdaHeart;
int c3_pda_heart(const C3GameState *state,const uint8_t pokemon[312],C3PdaHeart *heart);
/* The move a party record's slot shows (Shadow Rush, "????" or its own). */
uint16_t c3_pda_shown_move(const C3GameState *state,const uint8_t pokemon[312],unsigned slot);
int c3_pda_snag_text(const C3PdaAssets *assets,const C3GameState *state,const C3PdaSnag *snag,char *text,size_t capacity);
uint16_t c3_pda_be16(const uint8_t *p);
uint32_t c3_pda_be32(const uint8_t *p);
#endif
