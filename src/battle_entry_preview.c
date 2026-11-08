/* Hand a finished original trainer entry to the nonpersistent preview. */
#include "colosseum3ds/battle_entry_preview.h"
#include <stdio.h>

static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

int c3_battle_entry_preview_begin(C3BattlePreview *out,const C3BattleRequest *r,
    const C3BattleEntrySession *s,const C3BattlePreviewTables *t,char *e)
{
    if(!s||s->state!=C3_ENTRY_SESSION_DONE||s->source_result[0]!=1||s->source_result[1]!=1){
        if(e)snprintf(e,C3_BATTLE_ERROR_SIZE,"Trainer entry did not finish; no entry records to battle with.");
        return 0;
    }
    const uint8_t *records[4];
    for(unsigned i=0;i<4;++i){
        const uint8_t *f=c3_battle_entry_session_fighter(s,i/2u,i%2u);
        /* A constructed FightPokemon stores its nonzero source handle at +0;
         * the PokemonBios copy follows at +4. */
        if(!f||!be32(f)){
            if(e)snprintf(e,C3_BATTLE_ERROR_SIZE,"Trainer entry built no active Pokemon for side %u slot %u.",i/2u,i%2u);
            return 0;
        }
        records[i]=f+4;
    }
    return c3_battle_preview_begin_records(out,r,records,&s->rng,t,e);
}
