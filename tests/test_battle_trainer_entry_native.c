/* Verify the native record/handle glue. Constructor spies test argument,
 * backing, stream and publication contracts. They are NOT substitutes linked
 * into the application and do not qualify the underlying retail algorithms. */
#include "colosseum3ds/battle_trainer_entry_native.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks,fail_constructor,hero_calls,runtime_calls,sort_calls,fighter_calls;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"%s:%d %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static C3BattleEntryNativeContext context;
static C3BattleEntryNativeSlot slots[2];
static C3BattleEntryNativeHero heroes[2];
static C3BattleTrainerData data[2];
static C3BattlePartySortData sort_data[2];
static C3BattlePartySortState sort_state[2];
static uint8_t backing[2][10468],scratch[312],initial[0xb1c],preset[0xb18];
static C3BattleRng rng;
static uint16_t entry_counter;
static uint32_t expected_sequence,expected_entry,expected_source;
static unsigned expected_slot;

int c3_battle_trainer_create(uint8_t hero[C3_BATTLE_TRAINER_BYTES],uint8_t work[C3_BATTLE_POKEMON_BYTES],const C3BattleTrainerRequest *r,const C3BattleTrainerData *d,C3BattleRng *random)
{
    CHECK(d==heroes[expected_slot].data);CHECK(r==&heroes[expected_slot].request);
    CHECK(work==scratch);CHECK(random==&rng);CHECK(hero==initial);++hero_calls;
    if(fail_constructor)return 0;
    memset(hero,0x5c,2840);work[311]^=0x89;rng.seed+=17;++rng.draws;return 1;
}
int c3_battle_runtime_trainer_create(uint8_t out[C3_BATTLE_RUNTIME_TRAINER_BYTES],const uint8_t hero[C3_BATTLE_TRAINER_BYTES],uint16_t id,uint32_t dev,uint32_t seq)
{
    CHECK(seq==expected_sequence);CHECK(id==10);CHECK(dev==0xfffe);++runtime_calls;
    if(!out||!hero)return 1;
    CHECK(out==backing[expected_slot]);CHECK(hero==initial);
    if(fail_constructor)return 0;
    memcpy(out+4,hero,2840);out[0]=(uint8_t)(id>>8);out[1]=(uint8_t)id;
    out[0x27c0]=(uint8_t)(seq>>24);out[0x27c1]=(uint8_t)(seq>>16);out[0x27c2]=(uint8_t)(seq>>8);out[0x27c3]=(uint8_t)seq;
    return 1;
}
int c3_battle_party_sort(C3BattlePartySortState *s,const C3BattlePartySortRequest *r,const C3BattlePartySortData *d)
{
    C3BattleEntryNativeSlot *slot=&slots[expected_slot];++sort_calls;
    CHECK(d==slot->sort_data);CHECK(r->trainer_reference==slot->trainer);CHECK(r->hero_reference==slot->hero);
    CHECK(!memcmp(r->fight_references,slot->fighter,sizeof r->fight_references));
    CHECK(!memcmp(r->party_references,slot->party,sizeof r->party_references));
    CHECK(r->trainer_id==10);CHECK(r->selection_count==6);CHECK(r->selection_mode==2);
    CHECK(!memcmp(s->hero,slot->runtime+4,2840));CHECK(!memcmp(s->fight_pokemon,slot->runtime+0xb1c,680));
    CHECK(s->rng.seed==rng.seed&&s->rng.draws==rng.draws);CHECK(s->entry_counter==entry_counter);
    CHECK(!memcmp(s->scratch,slot->sort_state->scratch,sizeof s->scratch));
    if(fail_constructor)return 0;
    memset(s->hero,0x71,sizeof s->hero);memset(s->fight_pokemon,0x72,sizeof s->fight_pokemon);
    s->scratch[5][311]=0xee;s->entry_counter=(uint16_t)(s->entry_counter+9);s->rng.seed+=13;s->rng.draws+=2;
    return 1;
}
int c3_battle_fight_pokemon_create(uint8_t out[C3_BATTLE_FIGHT_POKEMON_BYTES],const uint8_t source[C3_BATTLE_POKEMON_BYTES],uint32_t ref,int16_t id)
{
    ++fighter_calls;CHECK(ref==expected_source);CHECK((uint16_t)id==(uint16_t)expected_entry);
    if(!out||!source)return 1;
    CHECK(out==backing[1]+0xb1c+5*340);CHECK(source==backing[0]+4+0x30+3*312);
    if(fail_constructor)return 0;
    memcpy(out+4,source,312);return 1;
}
static void init(void)
{
    memset(&context,0,sizeof context);memset(slots,0,sizeof slots);memset(data,0,sizeof data);
    memset(sort_data,0,sizeof sort_data);memset(heroes,0,sizeof heroes);
    memset(backing,0x8b,sizeof backing);memset(sort_state,0xa2,sizeof sort_state);
    memset(initial,0xb3,sizeof initial);memset(scratch,0xc4,sizeof scratch);memset(preset,0xd5,sizeof preset);
    for(unsigned i=0;i<2;++i){
        slots[i].trainer=100+i;slots[i].hero=200+i;slots[i].runtime=backing[i];slots[i].runtime_size=sizeof backing[i];
        slots[i].sort_state=&sort_state[i];slots[i].sort_data=&sort_data[i];
        heroes[i].request.trainer_id=(uint16_t)(10+i);heroes[i].request.input_device=(int32_t)i;
        heroes[i].data=&data[i];heroes[i].pokemon_scratch=scratch;
        for(unsigned j=0;j<6;++j){slots[i].party[j]=1000+i*6+j;slots[i].fighter[j]=2000+i*6+j;}
    }
    rng.seed=0x12345678u;rng.draws=90;entry_counter=0xffff;
    context.slots=slots;context.slot_count=2;context.heroes=heroes;context.hero_count=2;
    context.rng=&rng;context.entry_counter=&entry_counter;context.preset[3]=preset;
    fail_constructor=hero_calls=runtime_calls=sort_calls=fighter_calls=0;expected_slot=0;
}
int main(void)
{
    init();CHECK(c3_battle_entry_native_validate(&context));
    C3BattleTrainerEntryServices s={0};c3_battle_entry_native_bind(&s);
    CHECK(!s.action_buff&&!s.floor_status&&!s.sequence_index&&!s.abort_owned);
    /* No fallback preview context: unresolved requests fail without draws. */
    uint8_t before[sizeof initial];memcpy(before,initial,sizeof before);C3BattleRng old=rng;
    CHECK(!s.create_hero(&context,99,0,initial));CHECK(!memcmp(before,initial,sizeof before));CHECK(rng.seed==old.seed&&rng.draws==old.draws);
    expected_slot=1;CHECK(s.create_hero(&context,11,1,initial));CHECK(hero_calls==1);
    CHECK(rng.seed==old.seed+17&&rng.draws==old.draws+1);CHECK(!memcmp(initial+2840,before+2840,4));
    fail_constructor=1;memcpy(before,initial,sizeof before);old=rng;
    CHECK(!s.create_hero(&context,11,1,initial));CHECK(!memcmp(before,initial,sizeof before));CHECK(rng.seed==old.seed&&rng.draws==old.draws);
    fail_constructor=0;CHECK(!s.create_hero(&context,11,1,backing[0]));
    CHECK(s.copy_preset(&context,3,initial));CHECK(!memcmp(initial,preset,2840));CHECK(!memcmp(initial+2840,before+2840,4));
    CHECK(!s.copy_preset(&context,2,initial));CHECK(!s.copy_preset(&context,4,initial));
    expected_sequence=0xdeadbeef;expected_slot=0;
    CHECK(s.create_runtime(&context,100,initial,10,0xfffe,expected_sequence));
    uint32_t v=0;CHECK(s.trainer_status(&context,100,0x43,0,&v)&&v==10);
    CHECK(s.trainer_status(&context,100,0x44,0,&v)&&v==200);
    CHECK(s.trainer_status(&context,100,0x45,5,&v)&&v==2005);
    CHECK(s.trainer_status(&context,100,0x4c,0,&v)&&v==expected_sequence);
    CHECK(!s.trainer_status(&context,100,0x45,6,&v));CHECK(!s.trainer_status(&context,100,0x44,1,&v));
    CHECK(!s.trainer_status(&context,999,0x44,0,&v));CHECK(s.hero_pokemon(&context,201,5,&v)&&v==1011);
    CHECK(!s.hero_pokemon(&context,201,6,&v));
    CHECK(!s.trainer_status(&context,100,0x44,0,(uint32_t *)backing[0]));
    uint8_t saved[2][10468];memcpy(saved,backing,sizeof backing);C3BattlePartySortState saved_sort=sort_state[0];old=rng;
    fail_constructor=1;CHECK(!s.sort_party(&context,100,6,2));CHECK(!memcmp(backing,saved,sizeof saved));
    CHECK(!memcmp(&saved_sort,&sort_state[0],sizeof saved_sort));CHECK(entry_counter==0xffff&&rng.seed==old.seed);
    fail_constructor=0;CHECK(s.sort_party(&context,100,6,2));CHECK(entry_counter==8);
    CHECK(rng.seed==old.seed+13&&rng.draws==old.draws+2);CHECK(sort_state[0].scratch[5][311]==0xee);
    CHECK(!memcmp(backing[0],saved[0],4));CHECK(!memcmp(backing[0]+0xdc4,saved[0]+0xdc4,10468-0xdc4));
    CHECK(!memcmp(backing[1],saved[1],10468));
    for(unsigned i=0;i<65536;++i){entry_counter=(uint16_t)i;CHECK(s.next_entry_counter(&context,&v));
        int32_t expected=i<32768?(int32_t)i:(int32_t)i-65536;
        CHECK(v==(uint32_t)expected);CHECK(entry_counter==(uint16_t)(i+1));}
    expected_source=1003;expected_entry=0xffff;
    CHECK(s.create_fighter(&context,2011,expected_source,expected_entry));
    CHECK(!memcmp(backing[1]+0xb1c+5*340+4,backing[0]+4+0x30+3*312,312));
    CHECK(!s.create_fighter(&context,999,expected_source,expected_entry));
    CHECK(!s.create_fighter(&context,2011,999,expected_entry));
    /* Malformed native bindings reject before any constructor is called. */
    slots[1].party[3]=slots[0].fighter[2];CHECK(!c3_battle_entry_native_validate(&context));
    unsigned calls=fighter_calls;CHECK(!s.create_fighter(&context,2011,expected_source,expected_entry));CHECK(fighter_calls==calls);
    init();slots[1].runtime=backing[0];CHECK(!c3_battle_entry_native_validate(&context));
    init();slots[0].runtime_size--;CHECK(!c3_battle_entry_native_validate(&context));
    init();heroes[1].request=heroes[0].request;CHECK(!c3_battle_entry_native_validate(&context));
    init();context.entry_counter=(uint16_t *)backing[0];CHECK(!c3_battle_entry_native_validate(&context));
    init();slots[1].sort_state=slots[0].sort_state;CHECK(!c3_battle_entry_native_validate(&context));
    init();context.hero_count=5;CHECK(!c3_battle_entry_native_validate(&context));
    printf("Native entry glue: %u checks; constructor dependency spies, not original execution\n",checks);return 0;
}
