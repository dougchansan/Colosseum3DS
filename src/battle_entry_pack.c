/* Decoder and config builder for the private C3E1 trainer-entry table pack. */
#include "colosseum3ds/battle_entry_pack.h"
#include "colosseum3ds/battle.h"
#include "colosseum3ds/save.h"
#include "save_codec.h"
#include <stdio.h>
#include <string.h>

#define HEADER_BYTES 128u
#define SAVE_ATTEST 0x0u      /* game-data section 1: attest is its first field */
#define SAVE_HERO 0x70u       /* savedataGetStatus(0,2), 0xB18 bytes */
#define SAVE_PRESET 0xE404u   /* section 14 (body+0xE3E8) + 0x1C, fn_8006B57C */

static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int fail(char *error,const char *message)
{if(error)snprintf(error,C3_BATTLE_ERROR_SIZE,"%s",message);return 0;}

static const struct { char tag[5]; uint32_t stride, count, extra; } layout[C3_PACK_SECTION_COUNT]={
    {"ENC ",56,566,0},{"FLR ",24,50,0},{"TRN ",52,820,0},{"SID ",20,5,0},
    {"TYP ",8,7,1},{"KND ",32,19,1},{"SPC ",284,413,0},{"RST ",80,5510,0},
    {"AI  ",40,261,0},{"MOV ",56,358,0},{"NAT ",40,25,0},{"RAT ",2,8,0},
    {"MSG ",0,2018,0},{"ITX ",2,548,0},{"ITM ",40,397,0},{"GRW ",404,8,0},
    {"T44 ",44,18,0},{"SEQ ",12,76,0},{"AKD ",12,20,0},{"ACT ",8,0,0},{"VAL ",0,4,0}};

int c3_battle_entry_pack_decode(C3BattleEntryPack *out,const uint8_t *d,size_t size,char *error)
{
    C3BattleEntryPack p;memset(&p,0,sizeof p);
    if(!out||!d||size<HEADER_BYTES||memcmp(d,"C3E1",4)||be32(d+4)!=1||be32(d+8)!=size||
       be32(d+12)!=C3_PACK_SECTION_COUNT)return fail(error,"Entry pack header is invalid");
    size_t directory=HEADER_BYTES+16u*C3_PACK_SECTION_COUNT;
    if(size<directory)return fail(error,"Entry pack directory is truncated");
    uint8_t digest[20];c3_save_codec_sha1(digest,d+HEADER_BYTES,(uint32_t)(size-HEADER_BYTES));
    if(memcmp(digest,d+76,20))return fail(error,"Entry pack checksum failed");
    for(unsigned i=0;i<C3_PACK_SECTION_COUNT;++i){
        const uint8_t *e=d+HEADER_BYTES+16u*i;
        uint32_t offset=be32(e+4),bytes=be32(e+8),count=be32(e+12);
        if(memcmp(e,layout[i].tag,4)||offset<directory||offset>size||bytes>size-offset||(offset&3))
            return fail(error,"Entry pack section extent is invalid");
        if(layout[i].count&&count!=layout[i].count)return fail(error,"Entry pack section count is invalid");
        if(layout[i].stride&&bytes!=(count+layout[i].extra)*layout[i].stride)
            return fail(error,"Entry pack section size is invalid");
        p.sections[i]=(C3BattleEntryPackView){d+offset,bytes,count};
    }
    const C3BattleEntryPackView *msg=&p.sections[C3_PACK_MSG],*val=&p.sections[C3_PACK_VAL];
    if(msg->size<16u+8u*msg->count||be16(msg->bytes+4)!=msg->count||val->size!=16)
        return fail(error,"Entry pack message/scalar section is invalid");
    p.item_count=be32(val->bytes);p.special_trainer_id=be32(val->bytes+4);
    uint32_t action_base=be32(val->bytes+8),action_stride=be32(val->bytes+12);
    if(p.item_count!=p.sections[C3_PACK_ITX].count||action_stride!=8)
        return fail(error,"Entry pack scalars are inconsistent");
    const C3BattleEntryPackView *act=&p.sections[C3_PACK_ACT];
    if(!act->count||act->count>C3_ENTRY_PACK_MAX_ACTIONS||act->size!=act->count*8u||
       be16(act->bytes+(act->count-1u)*8u))return fail(error,"Entry pack action data is invalid");
    for(uint32_t i=0;i<act->count;++i){const uint8_t *r=act->bytes+8u*i;
        /* Stable identity: the original FightActionData row address. */
        p.actions[i]=(C3BattleActionData){action_base+8u*i,be16(r),be32(r+4)};}
    p.action_count=act->count;
    const C3BattleEntryPackView *akd=&p.sections[C3_PACK_AKD];
    for(uint32_t i=0;i<akd->count;++i){const uint8_t *r=akd->bytes+12u*i;
        p.kinds[i]=(C3BattleActionKind){(uint16_t)i,be32(r+4),be32(r+8)};}
    p.kind_count=akd->count;
    const C3BattleEntryPackView *seq=&p.sections[C3_PACK_SEQ];
    if(seq->count>C3_ENTRY_PACK_MAX_SEQUENCES)return fail(error,"Entry pack sequence table is too large");
    for(uint32_t i=0;i<seq->count;++i){const uint8_t *r=seq->bytes+12u*i;
        p.sequences[i]=(C3BattleSequenceResource){be32(r),be32(r+4)};}
    p.sequence_count=seq->count;
    *out=p;return 1;
}
int c3_battle_entry_pack_message(const C3BattleEntryPack *p,uint32_t key,const uint8_t **bytes,size_t *size)
{
    if(!p||!bytes||!size)return 0;
    const C3BattleEntryPackView *m=&p->sections[C3_PACK_MSG];
    uint32_t start=0,end=m->size;int found=0;
    for(uint32_t i=0;i<m->count;++i){const uint8_t *e=m->bytes+16u+8u*i;
        if(be32(e)==key){if(found)return 0;found=1;start=be32(e+4);}}
    if(!found)return 0;
    for(uint32_t i=0;i<m->count;++i){uint32_t o=be32(m->bytes+16u+8u*i+4u);if(o>start&&o<end)end=o;}
    if(start<16u+8u*m->count||end<=start||end>m->size)return 0;
    for(uint32_t i=start;i+1u<end;i+=2u){
        if(!be16(m->bytes+i)){*bytes=m->bytes+start;*size=i+2u-start;return 1;}
    }
    return 0;
}

static const uint8_t *row(const C3BattleEntryPack *p,unsigned section,uint32_t id)
{
    const C3BattleEntryPackView *v=&p->sections[section];
    if(id>=v->count)return NULL;
    return v->bytes+(size_t)id*layout[section].stride;
}
static int message_cb(void *user,uint32_t id,const uint8_t **bytes,size_t *size)
{return c3_battle_entry_pack_message(user,id,bytes,size);}
static int roster_cb(void *user,uint16_t id,C3BattlePokemonData *out)
{
    const C3BattleEntryPack *p=user;const uint8_t *r=row(p,C3_PACK_RST,id);
    if(!r||!out)return 0;
    memset(out,0,sizeof *out);out->roster=r;
    uint16_t species=be16(r+10);
    if(species){
        const uint8_t *s=row(p,C3_PACK_SPC,species);if(!s)return 0;
        const uint8_t *g=row(p,C3_PACK_GRW,s[0]);if(!g)return 0;
        out->species=s;out->growth=g;
    }
    out->species_count=p->sections[C3_PACK_SPC].count;
    out->moves=p->sections[C3_PACK_MOV].bytes;out->move_count=p->sections[C3_PACK_MOV].count;
    out->natures=p->sections[C3_PACK_NAT].bytes;
    out->nature_rates=p->sections[C3_PACK_RAT].bytes;out->rate_count=p->sections[C3_PACK_RAT].count;
    out->message=message_cb;out->user=user;return 1;
}
static int item_cb(void *user,uint16_t id,uint32_t *name,uint8_t *kind)
{
    /* itemGetStatus(NULL,id,1/2): out-of-range IDs and slots are source NULL. */
    const C3BattleEntryPack *p=user;*name=0;*kind=0;
    const uint8_t *index=row(p,C3_PACK_ITX,id);if(!index)return 1;
    const uint8_t *r=row(p,C3_PACK_ITM,be16(index));if(!r)return 1;
    *name=be32(r+0x10);*kind=r[0];return 1;
}
static int lookup_cb(void *user,uint32_t table,uint32_t id,const uint8_t **out,size_t *size)
{
    static const unsigned map[C3_PARTY_TABLE_COUNT]={C3_PACK_TRN,C3_PACK_AI,C3_PACK_RST,C3_PACK_SPC,C3_PACK_T44};
    if(table>=C3_PARTY_TABLE_COUNT)return 0;
    const uint8_t *r=row(user,map[table],id);if(!r)return 0;
    *out=r;*size=layout[map[table]].stride;return 1;
}
/* fightTargetGetPtr(2,..) for the alternate type-score branch is not ported:
 * reaching it is an explicit failure, as in the party-sort oracle. */
static int target_side_cb(void *user,uint32_t target,uint16_t host,uint32_t *side)
{(void)user;(void)target;(void)host;(void)side;return 0;}

int c3_battle_entry_pack_config(const C3BattleEntryPack *p,uint16_t encounter_id,
    const uint8_t *body,size_t body_size,uint32_t seed,C3BattleEntrySessionConfig *c,char *error)
{
    if(!p||!body||!c||body_size!=C3_SAVE_BODY_SIZE)return fail(error,"Entry config inputs are invalid");
    const uint8_t *e=row(p,C3_PACK_ENC,encounter_id);
    if(!e)return fail(error,"Encounter is outside the original table");
    const uint8_t *type=row(p,C3_PACK_TYP,e[1]);
    const uint8_t *floor=row(p,C3_PACK_FLR,be16(e+4));
    if(!type||!floor||type[0]!=1)return fail(error,"Only one-trainer-per-side encounters are admitted");
    memset(c,0,sizeof *c);
    static const unsigned tables[C3_ENTRY_TABLE_COUNT]={C3_PACK_ENC,C3_PACK_TRN,C3_PACK_SPC,C3_PACK_SID,C3_PACK_TYP,C3_PACK_KND};
    for(unsigned i=0;i<C3_ENTRY_TABLE_COUNT;++i){const C3BattleEntryPackView *v=&p->sections[tables[i]];
        c->tables[i]=(C3BattleEntryTableView){v->bytes,v->size,v->count};}
    c->sort_data.lookup=lookup_cb;c->sort_data.target_side=target_side_cb;c->sort_data.user=(void *)p;
    static const unsigned sort_tables[C3_PARTY_TABLE_COUNT]={C3_PACK_TRN,C3_PACK_AI,C3_PACK_RST,C3_PACK_SPC,C3_PACK_T44};
    for(unsigned i=0;i<C3_PARTY_TABLE_COUNT;++i)c->sort_data.counts[i]=p->sections[sort_tables[i]].count;
    c->sort_data.battle_result=0;     /* fn_801EF62C(0) during battle setup */
    c->sort_data.host_side=e[1];      /* floor status 0x14 = encounter byte 1 */
    for(unsigned side=0;side<2;++side){
        /* buff 4 reads encounter slot 0; buff 5 slot trainer_count (here 1). */
        unsigned slot=side?type[0]:0u;
        uint16_t id=be16(e+24+8u*slot);uint32_t device=be32(e+28+8u*slot);
        const uint8_t *trainer=row(p,C3_PACK_TRN,id);
        if(!id||!trainer)return fail(error,"Encounter trainer is missing");
        C3BattleTrainerRequest *r=&c->requests[side];
        r->trainer_id=id;r->input_device=(int32_t)(uint16_t)device;memcpy(r->attest,body+SAVE_ATTEST,4);
        C3BattleTrainerData *t=&c->trainer_data[side];
        t->trainer=trainer;t->saved_player=be16(trainer+2)==1?body+SAVE_HERO:NULL;
        t->roster=roster_cb;t->message=message_cb;t->item=item_cb;t->item_count=p->item_count;
        t->special_trainer_id=p->special_trainer_id;t->special_name=NULL;t->special_name_size=0;
        t->user=(void *)p;
        c->side_data_id[side]=be16(floor+20+2u*side); /* fightFloorDataBiosGetFightSideDataId */
    }
    c->encounter_id=encounter_id;
    c->actions=p->actions;c->action_count=p->action_count;c->kinds=p->kinds;c->kind_count=p->kind_count;
    c->entry_kind=3;c->preset_mode=body[SAVE_PRESET];c->seed=seed;
    c->resources=p->sequences;c->resource_count=p->sequence_count;
    return 1;
}
