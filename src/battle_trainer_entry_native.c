/* Connect the cooperative entry to the existing source-qualified constructor
 * adapters. Byte offsets are original Hero/FightTrainer layouts, not host
 * structure overlays. All original records are big endian. */
#include "colosseum3ds/battle_trainer_entry_native.h"
#include <string.h>
#include <stdint.h>

enum { HERO_OFFSET=4, PARTY_OFFSET=0x30, FIGHT_OFFSET=0xB1C, SEQUENCE_OFFSET=0x27C0 };
_Static_assert(HERO_OFFSET+C3_BATTLE_TRAINER_BYTES==FIGHT_OFFSET,"runtime Hero extent");
_Static_assert(FIGHT_OFFSET+6*C3_BATTLE_FIGHT_POKEMON_BYTES==0x1314,"runtime party extent");
_Static_assert(PARTY_OFFSET+6*C3_BATTLE_POKEMON_BYTES<=C3_BATTLE_TRAINER_BYTES,"Hero party extent");
static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int overlap(const void *a,size_t na,const void *b,size_t nb)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return na&&nb&&(x<=y?y-x<na:x-y<nb);
}
int c3_battle_entry_native_validate(const C3BattleEntryNativeContext *c)
{
    uint32_t ids[28];size_t n=0;
    const void *spans[12];size_t sizes[12],ns=0;
    if(!c||!c->rng||!c->entry_counter||c->slot_count>2||c->hero_count>4||
       (c->slot_count&&!c->slots)||(c->hero_count&&!c->heroes))return 0;
#define SPAN(p,s) do {spans[ns]=(p);sizes[ns++]=(s);} while(0)
    SPAN(c->rng,sizeof *c->rng);SPAN(c->entry_counter,sizeof *c->entry_counter);
    for(size_t i=0;i<c->slot_count;++i){const C3BattleEntryNativeSlot *s=&c->slots[i];
        if(!s->runtime||s->runtime_size!=C3_BATTLE_RUNTIME_TRAINER_BYTES||!s->sort_state||!s->sort_data)return 0;
        ids[n++]=s->trainer;ids[n++]=s->hero;
        for(unsigned j=0;j<6;++j){ids[n++]=s->party[j];ids[n++]=s->fighter[j];}
        SPAN(s->runtime,s->runtime_size);SPAN(s->sort_state,sizeof *s->sort_state);
    }
    for(size_t i=0;i<c->hero_count;++i){const C3BattleEntryNativeHero *h=&c->heroes[i];
        if(!h->data||!h->pokemon_scratch)return 0;
        for(size_t j=0;j<i;++j)if(h->request.trainer_id==c->heroes[j].request.trainer_id&&
            h->request.input_device==c->heroes[j].request.input_device)return 0;
        /* Several calls can intentionally reuse the same parent scratch slot. */
        int shared=0;for(size_t j=0;j<i;++j)if(h->pokemon_scratch==c->heroes[j].pokemon_scratch)shared=1;
        if(!shared){SPAN(h->pokemon_scratch,C3_BATTLE_POKEMON_BYTES);}
    }
#undef SPAN
    for(size_t i=0;i<n;++i){if(!ids[i])return 0;for(size_t j=0;j<i;++j)if(ids[i]==ids[j])return 0;}
    for(size_t i=0;i<ns;++i){
        if(overlap(spans[i],sizes[i],c,sizeof *c)||
           overlap(spans[i],sizes[i],c->slots,c->slot_count*sizeof *c->slots)||
           overlap(spans[i],sizes[i],c->heroes,c->hero_count*sizeof *c->heroes))return 0;
        for(size_t j=0;j<i;++j)if(overlap(spans[i],sizes[i],spans[j],sizes[j]))return 0;
    }
    return 1;
}
static C3BattleEntryNativeSlot *slot(C3BattleEntryNativeContext *c,uint32_t ref,int hero)
{
    for(size_t i=0;i<c->slot_count;++i)if((hero?c->slots[i].hero:c->slots[i].trainer)==ref)return &c->slots[i];
    return NULL;
}
static uint8_t *pokemon(C3BattleEntryNativeContext *c,uint32_t ref,int fight)
{
    for(size_t i=0;i<c->slot_count;++i)for(unsigned j=0;j<6;++j){C3BattleEntryNativeSlot *s=&c->slots[i];
        if((fight?s->fighter[j]:s->party[j])==ref)
            return s->runtime+(fight?FIGHT_OFFSET+j*C3_BATTLE_FIGHT_POKEMON_BYTES:
                HERO_OFFSET+PARTY_OFFSET+j*C3_BATTLE_POKEMON_BYTES);
    }
    return NULL;
}
static int output_disjoint(const C3BattleEntryNativeContext *c,const void *p,size_t n)
{
    if(!p||overlap(p,n,c,sizeof *c)||overlap(p,n,c->slots,c->slot_count*sizeof *c->slots)||
       overlap(p,n,c->heroes,c->hero_count*sizeof *c->heroes)||overlap(p,n,c->rng,sizeof *c->rng)||
       overlap(p,n,c->entry_counter,sizeof *c->entry_counter))return 0;
    for(size_t i=0;i<c->slot_count;++i)if(overlap(p,n,c->slots[i].runtime,c->slots[i].runtime_size)||
        overlap(p,n,c->slots[i].sort_state,sizeof *c->slots[i].sort_state))return 0;
    for(size_t i=0;i<c->hero_count;++i)if(overlap(p,n,c->heroes[i].pokemon_scratch,C3_BATTLE_POKEMON_BYTES))return 0;
    return 1;
}
static int copy_preset(void *user,uint8_t index,uint8_t *hero)
{
    C3BattleEntryNativeContext *c=user;
    if(!c3_battle_entry_native_validate(c)||index>=4||!c->preset[index]||
       !output_disjoint(c,hero,C3_TRAINER_ENTRY_HERO_BYTES)||
       overlap(hero,C3_TRAINER_ENTRY_HERO_BYTES,c->preset[index],C3_BATTLE_TRAINER_BYTES))return 0;
    memcpy(hero,c->preset[index],C3_BATTLE_TRAINER_BYTES);return 1;
}
static int create_hero(void *user,uint16_t id,uint16_t device,uint8_t *hero)
{
    C3BattleEntryNativeContext *c=user;
    if(!c3_battle_entry_native_validate(c)||!output_disjoint(c,hero,C3_TRAINER_ENTRY_HERO_BYTES))return 0;
    for(size_t i=0;i<c->hero_count;++i){const C3BattleEntryNativeHero *h=&c->heroes[i];
        if(h->request.trainer_id==id&&h->request.input_device==(int32_t)device)
            return c3_battle_trainer_create(hero,h->pokemon_scratch,&h->request,h->data,c->rng);
    }
    return 0;
}
static int create_runtime(void *user,uint32_t ref,const uint8_t *hero,uint16_t id,uint16_t device,uint32_t sequence)
{
    C3BattleEntryNativeContext *c=user;
    if(!c3_battle_entry_native_validate(c))return 0;
    if(!ref)return c3_battle_runtime_trainer_create(NULL,hero,id,device,sequence);
    C3BattleEntryNativeSlot *s=slot(c,ref,0);
    return s&&c3_battle_runtime_trainer_create(s->runtime,hero,id,device,sequence);
}
static int trainer_status(void *user,uint32_t ref,unsigned selector,unsigned index,uint32_t *out)
{
    C3BattleEntryNativeContext *c=user;
    if(!c3_battle_entry_native_validate(c)||!output_disjoint(c,out,sizeof *out))return 0;
    C3BattleEntryNativeSlot *s=slot(c,ref,0);if(!s)return 0;
    switch(selector){
    case 0x43:if(index)return 0;*out=be16(s->runtime);return 1;
    case 0x44:if(index)return 0;*out=s->hero;return 1;
    case 0x45:if(index>=6)return 0;*out=s->fighter[index];return 1;
    case 0x4c:if(index)return 0;*out=be32(s->runtime+SEQUENCE_OFFSET);return 1;
    default:return 0;
    }
}
static int hero_pokemon(void *user,uint32_t ref,unsigned index,uint32_t *out)
{
    C3BattleEntryNativeContext *c=user;
    if(!c3_battle_entry_native_validate(c)||index>=6||!output_disjoint(c,out,sizeof *out))return 0;
    C3BattleEntryNativeSlot *s=slot(c,ref,1);if(!s)return 0;*out=s->party[index];return 1;
}
static int sort_party(void *user,uint32_t ref,uint16_t maximum,uint16_t active)
{
    C3BattleEntryNativeContext *c=user;
    if(!c3_battle_entry_native_validate(c))return 0;
    C3BattleEntryNativeSlot *s=slot(c,ref,0);if(!s)return 0;
    C3BattlePartySortState staged=*s->sort_state;
    C3BattlePartySortRequest request={0};
    request.trainer_reference=s->trainer;request.hero_reference=s->hero;
    memcpy(request.fight_references,s->fighter,sizeof request.fight_references);
    memcpy(request.party_references,s->party,sizeof request.party_references);
    request.trainer_id=be16(s->runtime);request.selection_count=maximum;request.selection_mode=active;
    memcpy(staged.hero,s->runtime+HERO_OFFSET,sizeof staged.hero);
    memcpy(staged.fight_pokemon,s->runtime+FIGHT_OFFSET,sizeof staged.fight_pokemon);
    staged.rng=*c->rng;staged.entry_counter=*c->entry_counter;
    if(!c3_battle_party_sort(&staged,&request,s->sort_data))return 0;
    memcpy(s->runtime+HERO_OFFSET,staged.hero,sizeof staged.hero);
    memcpy(s->runtime+FIGHT_OFFSET,staged.fight_pokemon,sizeof staged.fight_pokemon);
    *s->sort_state=staged;*c->rng=staged.rng;*c->entry_counter=staged.entry_counter;return 1;
}
static int next_counter(void *user,uint32_t *out)
{
    C3BattleEntryNativeContext *c=user;
    if(!c3_battle_entry_native_validate(c)||!output_disjoint(c,out,sizeof *out))return 0;
    /* Source returns SIGNED old low16, then writes incremented low16. */
    uint16_t old=*c->entry_counter;int32_t signed_old=old<0x8000u?(int32_t)old:(int32_t)old-0x10000;
    *c->entry_counter=(uint16_t)(old+1u);*out=(uint32_t)signed_old;return 1;
}
static int create_fighter(void *user,uint32_t destination,uint32_t source,uint32_t entry)
{
    C3BattleEntryNativeContext *c=user;
    if(!c3_battle_entry_native_validate(c))return 0;
    uint8_t *d=destination?pokemon(c,destination,1):NULL,*p=source?pokemon(c,source,0):NULL;
    if((destination&&!d)||(source&&!p))return 0;
    int32_t id=(int32_t)(entry&0xffffu);if(id>=0x8000)id-=0x10000;
    return c3_battle_fight_pokemon_create(d,p,source,(int16_t)id);
}
void c3_battle_entry_native_bind(C3BattleTrainerEntryServices *s)
{
    if(!s)return;
    s->copy_preset=copy_preset;s->create_hero=create_hero;s->create_runtime=create_runtime;
    s->trainer_status=trainer_status;s->hero_pokemon=hero_pokemon;s->sort_party=sort_party;
    s->next_entry_counter=next_counter;s->create_fighter=create_fighter;
}
