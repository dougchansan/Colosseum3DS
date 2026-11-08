/* Source-reference predicates over endian-explicit fixtures. Constructor and
 * scene dependencies are test spies ONLY in the integrated entry test. */
#include "colosseum3ds/battle_entry_records.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef uint8_t u8;typedef int8_t s8;typedef uint16_t u16;typedef int16_t s16;
typedef uint32_t u32;typedef int32_t s32;typedef int BOOL;
#define TRUE 1
#define FALSE 0
static unsigned checks,constructor_calls,model_loads,requests,hidden,shown,grids,aborted,friend_calls;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static C3BattleEntryRecords records;
static C3BattleEntryNativeContext native;
static C3BattleEntryNativeSlot slots[2];
static C3BattleEntryNativeHero hero_data[2];
static C3BattleTrainerData trainer_data[2];
static C3BattlePartySortState sorts[2];
static C3BattlePartySortData sort_data[2];
static uint8_t runtime[2][10468],pokemon_scratch[312],heroes[2][2840];
static uint8_t encounters[16][56],trainers[16][52],species[414][284],sides[4][20],types[4][8],kinds[3][32];
static C3BattleActionData actions[2]={{800,1,4},{801,1,5}};
static C3BattleRng rng;
static uint16_t counter;
static C3BattleTrainerEntryServices service;
typedef struct PokemonRangeObjHeader {u32 count;} PokemonRangeObjHeader;
static PokemonRangeObjHeader species_header;
static void *lbl_80478F90=&species_header;
static int integrating,resource_ready;
static u16 r16(const void *q){const u8*p=q;return (u16)((u16)p[0]<<8|p[1]);}
static u32 r32(const void *q){const u8*p=q;return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static void w16(void *q,u32 n){u8*p=q;p[0]=(u8)(n>>8);p[1]=(u8)n;}
static void w32(void *q,u32 n){u8*p=q;p[0]=(u8)(n>>24);p[1]=(u8)(n>>16);p[2]=(u8)(n>>8);p[3]=(u8)n;}
static uint8_t *party_ptr(unsigned t,unsigned j){return runtime[t]+4+0x30+j*312u;}
static uint8_t *fight_ptr(unsigned t,unsigned j){return runtime[t]+0xb1c+j*340u;}
static uint8_t *source_ptr(u32 ref)
{for(unsigned t=0;t<2;++t)for(unsigned j=0;j<6;++j)if(slots[t].party[j]==ref)return party_ptr(t,j);return NULL;}

/* Independent host accessors for the extracted original control. They do not
 * call production predicates and never supply expected decisions. */
static uintptr_t ref_pokemon_scalar(u8 *p,u32 id,u32 field,u32 index)
{
    (void)index;if(!p){CHECK(field==1);return id<414?r32(species[id]+24):0;}
    switch(field){case 0x6e:return r16(p);case 0x70:return (uintptr_t)(p+8);
    case 0x7b:return r16(p+0x8a)==0;case 0xb6:return p[0xcb];case 0xb8:return p[0xcd];default:CHECK(0);return 0;}
}
static u32 ref_attest(uintptr_t value){const u8*p=(const u8*)value;return p&&p[0]&&p[1]&&p[2]&&p[3];}
static u16 ref_result(void){return records.battle_result;}
static void *ref_pokemon_pointer(void *q,u32 id,u32 field,u32 index)
{
    (void)id;(void)index;u8*p=q;if(!p)return NULL;
    switch(field){case 0xcb:return source_ptr(r32(p));case 0xcc:return p+4;
    case 0xce:{u16 n=r16(p+0x14c);intptr_t signed_n=n<32768?n:(intptr_t)n-65536;return (void*)signed_n;}
    case 0xd2:return (void*)(uintptr_t)p[0x151];default:CHECK(0);return NULL;}
}
static uintptr_t ref_hero_status(u8 *p,u32 field,u32 index)
{(void)index;if(!p)return 0;CHECK(field==1||field==0xb);return field==1?(uintptr_t)p:p[0xa80];}
static s32 ref_char_cmp(uintptr_t a,u16 *b){CHECK(b&&*b==0);return a?r16((u8*)a)!=0:0;}
static void *ref_trainer_pointer(void *q,u32 id,u32 field,u32 index)
{
    (void)id;u8*p=q;if(!p)return NULL;
    if(field==0x43)return (void*)(uintptr_t)r16(p);
    if(field==0x44)return p+4;
    if(field==0x45){CHECK(index<6);return p+0xb1c+index*340;}
    CHECK(0);return NULL;
}
static u32 ref_trainer_scalar(void *q,u32 id,u32 field,u32 index)
{
    (void)index;if(field==0x43)return q?r16(q):0;
    CHECK(field==2||field==4);return id<16?r16(trainers[id]+(field==2?6:2)):0;
}
static u32 ref_grid_row(u32 id){return id<4?id+1:0;}
static u32 ref_grid_yaw(u32 ref){CHECK(ref&&ref<=4);return r16(sides[ref-1]);}
#include "battle_entry_records_reference.h"

/* The production entry adapters call these real symbol names. Here they are
 * instrumented fixtures, NOT a qualification of the source constructors. */
int c3_battle_trainer_create(uint8_t out[C3_BATTLE_TRAINER_BYTES],uint8_t scratch[C3_BATTLE_POKEMON_BYTES],const C3BattleTrainerRequest *req,
    const C3BattleTrainerData *data,C3BattleRng *random)
{
    CHECK(integrating);CHECK(scratch==pokemon_scratch);CHECK(random==&rng);
    unsigned i=req->trainer_id==10?0:1;CHECK(data==&trainer_data[i]);
    memcpy(out,heroes[i],2840);++constructor_calls;++random->draws;random->seed+=17;return 1;
}
int c3_battle_runtime_trainer_create(uint8_t out[C3_BATTLE_RUNTIME_TRAINER_BYTES],const uint8_t hero[C3_BATTLE_TRAINER_BYTES],uint16_t id,uint32_t device,uint32_t seq)
{
    CHECK(integrating);CHECK(out&&hero);memcpy(out+4,hero,2840);w16(out,id);out[0x27bc]=(u8)device;
    w32(out+0x27c0,seq);for(unsigned i=0;i<6;++i){u8*fp=out+0xb1c+i*340;w32(fp,0);w16(fp+0x14c,0xffff);}
    return 1;
}
int c3_battle_party_sort(C3BattlePartySortState *s,const C3BattlePartySortRequest *q,const C3BattlePartySortData *d)
{CHECK(integrating);CHECK(q->selection_count==6&&q->selection_mode==2);CHECK(d==&sort_data[0]||d==&sort_data[1]);(void)s;return 1;}
int c3_battle_fight_pokemon_create(uint8_t out[C3_BATTLE_FIGHT_POKEMON_BYTES],const uint8_t p[C3_BATTLE_POKEMON_BYTES],uint32_t ref,int16_t entry)
{CHECK(integrating);memcpy(out+4,p,312);w32(out,ref);w16(out+0x14c,(u16)entry);out[0x151]=0;return 1;}

static void seed_pokemon(u8*p,u16 id)
{memset(p,0,312);w16(p,id);memset(p+8,1,4);w16(p+0x8a,20);}
static void init(void)
{
    memset(&records,0,sizeof records);memset(&native,0,sizeof native);memset(slots,0,sizeof slots);
    memset(runtime,0,sizeof runtime);memset(sorts,0,sizeof sorts);memset(sort_data,0,sizeof sort_data);
    memset(hero_data,0,sizeof hero_data);memset(trainer_data,0,sizeof trainer_data);
    memset(encounters,0,sizeof encounters);memset(trainers,0,sizeof trainers);memset(species,0,sizeof species);
    memset(sides,0,sizeof sides);memset(types,0,sizeof types);memset(kinds,0,sizeof kinds);
    records.native=&native;native.owner=&records;native.slots=slots;native.slot_count=2;
    native.rng=&rng;native.entry_counter=&counter;native.heroes=hero_data;native.hero_count=2;
    records.tables[C3_ENTRY_ENCOUNTER]=(C3BattleEntryTableView){&encounters[0][0],sizeof encounters,16};
    records.tables[C3_ENTRY_TRAINER]=(C3BattleEntryTableView){&trainers[0][0],sizeof trainers,16};
    records.tables[C3_ENTRY_SPECIES]=(C3BattleEntryTableView){&species[0][0],sizeof species,414};
    records.tables[C3_ENTRY_SIDE]=(C3BattleEntryTableView){&sides[0][0],sizeof sides,4};
    records.tables[C3_ENTRY_TYPE]=(C3BattleEntryTableView){&types[0][0],sizeof types,3};
    records.tables[C3_ENTRY_KIND]=(C3BattleEntryTableView){&kinds[0][0],sizeof kinds,2};
    records.action_data=actions;records.action_count=2;records.encounter_id=10;records.encounter_reference=700;
    encounters[10][0]=1;encounters[10][1]=2;w16(encounters[10]+24,10);w16(encounters[10]+32,1);
    w32(encounters[10]+36,1);types[2][0]=1;types[2][1]=6;types[2][2]=2;
    w16(trainers[10]+6,4);w16(trainers[10]+2,2);w16(trainers[10]+16,0xa5a5);w16(trainers[10]+18,38);
    w16(trainers[1]+2,1);w16(trainers[1]+16,0x5a5a);w16(trainers[1]+18,1);w16(sides[1],1);w16(sides[2],2);
    for(unsigned i=0;i<414;++i)w32(species[i]+24,100+i);
    for(unsigned i=0;i<2;++i){
        slots[i].trainer=100+i;slots[i].hero=200+i;slots[i].runtime=runtime[i];slots[i].runtime_size=sizeof runtime[i];
        slots[i].sort_state=&sorts[i];slots[i].sort_data=&sort_data[i];
        records.sides[i]=(C3BattleEntrySideView){300+i,(u16)(i+1),{100+i,0}};
        for(unsigned j=0;j<6;++j){slots[i].party[j]=1000+i*10+j;slots[i].fighter[j]=2000+i*10+j;w16(fight_ptr(i,j)+0x14c,0xffff);}
        w16(runtime[i],i?1:10);w16(runtime[i]+4,'W');
        seed_pokemon(party_ptr(i,0),i?197:288);seed_pokemon(party_ptr(i,1),i?196:288);
        memcpy(heroes[i],runtime[i]+4,2840);
        hero_data[i].request.trainer_id=(u16)(i?1:10);hero_data[i].request.input_device=(int32_t)i;
        hero_data[i].data=&trainer_data[i];hero_data[i].pokemon_scratch=pokemon_scratch;
    }
    rng=(C3BattleRng){0x12345678,0};counter=1;species_header.count=414;
    constructor_calls=model_loads=requests=hidden=shown=grids=aborted=friend_calls=0;
    integrating=resource_ready=0;memset(&service,0,sizeof service);
    c3_battle_entry_native_bind(&service);c3_battle_entry_records_bind(&service);
    CHECK(c3_battle_entry_records_validate(&records));
}
static void test_predicates(void)
{
    init();u8 answer;uint32_t value;u8 *p=party_ptr(0,0),*fp=fight_ptr(0,0);
    for(unsigned id=0;id<65536;++id){w16(p,id);
        CHECK(service.pokemon_eligible(&native,slots[0].party[0],0,&answer));CHECK(answer==ref_pokemonCheckValid(p));
        CHECK(service.pokemon_eligible(&native,slots[0].party[0],1,&answer));CHECK(answer==ref_pokemonCheckFightOut(p));
    }
    w16(p,288);
    const unsigned bytes[]={8,9,10,11,0xcb,0xcd};
    for(unsigned k=0;k<sizeof bytes/sizeof *bytes;++k)for(unsigned v=0;v<256;++v){u8 old=p[bytes[k]];p[bytes[k]]=(u8)v;
        for(int active=0;active<2;++active){CHECK(service.pokemon_eligible(&native,slots[0].party[0],active,&answer));
            CHECK(answer==(active?ref_pokemonCheckFightOut(p):ref_pokemonCheckValid(p)));}
        p[bytes[k]]=old;
    }
    w32(species[288]+24,0);CHECK(service.pokemon_eligible(&native,slots[0].party[0],0,&answer)&&!answer);w32(species[288]+24,1);
    for(unsigned hp=0;hp<65536;++hp){w16(p+0x8a,hp);CHECK(service.pokemon_eligible(&native,slots[0].party[0],1,&answer));CHECK(answer==ref_pokemonCheckFightOut(p));}
    w16(p+0x8a,20);w32(fp,slots[0].party[0]);memcpy(fp+4,p,312);
    for(unsigned id=0;id<65536;++id){w16(fp+0x14c,id);
        CHECK(service.already_entered(&native,slots[0].trainer,slots[0].party[0],&value));
        void *original=ref_fightTrainerCheckTemotiPokemonFightEntry(runtime[0],p);
        CHECK(value==(original?slots[0].fighter[0]:0));
    }
    w16(fp+0x14c,0);
    for(unsigned result=0;result<4;++result)for(unsigned disabled=0;disabled<3;++disabled){records.battle_result=(u16)result;fp[0x151]=(u8)disabled;
        CHECK(service.side_fighting_count(&native,records.sides[0].reference,&answer));CHECK(answer==ref_fightPokemonCheckFightOutInline(fp));
    }
    records.battle_result=0;fp[0x151]=0;
    /* A duplicate is found by stable source identity, not matching species. */
    CHECK(service.already_entered(&native,100,slots[0].party[1],&value)&&!value);
    memcpy(fp+4,party_ptr(0,1),312);CHECK(service.already_entered(&native,100,slots[0].party[0],&value)&&value==2000);
    w32(fp,0xdeadbeef);value=42;CHECK(!service.already_entered(&native,100,slots[0].party[0],&value)&&value==42);
    records.battle_result=1;CHECK(service.already_entered(&native,100,slots[0].party[0],&value)&&value==0);
    records.battle_result=0;w32(fp,0);value=42;CHECK(!service.already_entered(&native,999,slots[0].party[0],&value)&&value==42);
    for(unsigned id=0;id<17;++id)for(unsigned dev=0;dev<3;++dev){CHECK(service.trainer_data_valid(&native,(u16)id,(u16)dev,&answer));
        CHECK(answer==ref_fightTrainerCheckTrainerDataIdValid(id,(s32)dev));}
    for(unsigned result=0;result<3;++result)for(unsigned id=0;id<3;++id)for(unsigned name=0;name<2;++name)for(unsigned state=0;state<4;++state){
        records.battle_result=(u16)result;w16(runtime[0],id);w16(runtime[0]+4,name?'W':0);runtime[0][4+0xa80]=(u8)state;
        CHECK(service.trainer_valid(&native,100,&answer));CHECK(answer==ref_fightTrainerCheckValid(runtime[0]));
        CHECK(service.valid_trainer(&native,300,0,&value)&&value==(answer?100u:0u));
    }
    records.battle_result=0;
    for(unsigned id=0;id<17;++id)for(unsigned kind=0;kind<5;++kind){if(id<16)w16(trainers[id]+2,kind);w16(runtime[0],id);
        CHECK(service.is_gc_hero(&native,100,&answer));CHECK(answer==ref_fightTrainerIsGcHero(runtime[0]));}
}
static void test_tables(void)
{
    init();u32 v;u16 small;u8 a,b;
    CHECK(service.floor_status(&native,0xd,&v)&&v==10);CHECK(service.floor_status(&native,0x14,&v)&&v==2);
    for(unsigned i=0;i<3;++i)CHECK(service.floor_status(&native,0x16+i,&v)&&v==types[2][i]);
    for(unsigned i=0;i<256;++i){kinds[1][11]=(u8)i;kinds[1][18]=(u8)(255-i);
        CHECK(service.floor_status(&native,0x27,&v)&&v==i);CHECK(service.floor_status(&native,0x2e,&v)&&v==255-i);}
    const u32 device[]={0,1,0x12340001,0xffff,0xffffffff};
    for(unsigned i=0;i<4;++i)for(unsigned j=0;j<5;++j){w16(encounters[10]+24+8*i,0x1200+i);w32(encounters[10]+28+8*i,device[j]);
        CHECK(service.encounter_status(&native,700,0,(u8)i,&small)&&small==0x1200+i);
        CHECK(service.encounter_status(&native,700,1,(u8)i,&small)&&small==(u16)device[j]);}
    for(unsigned i=4;i<256;++i)CHECK(service.encounter_status(&native,700,1,(u8)i,&small)&&small==0);
    for(unsigned type=0;type<6;++type){
        CHECK(service.target_side(&native,4,(u16)type,&v)&&v==(type<=3?300u:0u));
        CHECK(service.target_side(&native,5,(u16)type,&v)&&v==(type<=3?301u:0u));}
    v=42;CHECK(!service.target_side(&native,6,2,&v)&&v==42);
    /* Inclusive TYPE/KIND bounds must have real complete backing, not a
     * conventional count check that wrongly suppresses the final row. */
    encounters[10][1]=3;types[3][0]=2;CHECK(service.floor_status(&native,0x16,&v)&&v==2);
    encounters[10][0]=2;kinds[2][11]=7;CHECK(service.floor_status(&native,0x27,&v)&&v==7);
    records.tables[C3_ENTRY_TYPE].byte_size--;CHECK(!c3_battle_entry_records_validate(&records));records.tables[C3_ENTRY_TYPE].byte_size++;
    records.encounter_id=16;CHECK(service.encounter(&native,16,&v)&&!v);CHECK(service.floor_status(&native,0x14,&v)&&!v);
    records.encounter_id=10;v=42;CHECK(!service.encounter(&native,11,&v)&&v==42);CHECK(service.encounter(&native,10,&v)&&v==700);
    CHECK(service.sequence_index(&native,10,&small)&&small==38);CHECK(service.sequence_index(&native,99,&small)&&small==0);
    const unsigned yaws[]={0,1,2,255,256,257,65535};
    for(unsigned j=0;j<7;++j)for(unsigned count=0;count<4;++count)for(unsigned slot=0;slot<5;++slot){
        w16(sides[1],yaws[j]);u8 expected_x;s8 expected_y;
        ref_fightSideGetFightTrainerGridParam(1,(u16)count,(u16)slot,&expected_x,&expected_y);
        CHECK(service.grid_parameters(&native,1,(u8)count,slot,&a,&b));CHECK(a==expected_x&&b==(u8)expected_y);}
    CHECK(service.grid_parameters(&native,4,2,0,&a,&b)&&a==0&&b==255);
    a=42;CHECK(!service.grid_parameters(&native,1,2,0,&a,&a)&&a==42);
    C3BattleAction action={{0}};c3_battle_action_set_word(&action,C3_ACTION_DATA,800);
    c3_battle_action_set_word(&action,C3_ACTION_BUFF,99);
    CHECK(service.action_buff(&native,&action,&v)&&v==4); /* uses data->buff, not action buff */
    c3_battle_action_set_word(&action,C3_ACTION_DATA,802);v=42;CHECK(!service.action_buff(&native,&action,&v)&&v==42);
    c3_battle_action_set_word(&action,C3_ACTION_DATA,0);CHECK(service.action_buff(&native,&action,&v)&&v==0);
    C3BattleActionData duplicate[2]={{800,1,4},{800,1,5}};records.action_data=duplicate;
    c3_battle_action_set_word(&action,C3_ACTION_DATA,800);v=42;CHECK(!service.action_buff(&native,&action,&v)&&v==42);
    CHECK(!service.action_buff(&native,&action,(u32*)action.bytes));
}
static int32_t state(void *u,u32 group){CHECK(u==&native);CHECK(group==20||group==21);return resource_ready?0:1;}
static int request(void *u,u32 group){(void)u;(void)group;++requests;return 1;}
static u32 find_resource(void *u,u32 group,u32 member){CHECK(u==&native);CHECK(member==123||member==124);return group+100;}
static u32 allocate(void *u){CHECK(u==&native);return 500+model_loads;}
static int set_fields(void *u,u32 seq,u16 index,u16 group,u8 flags)
{CHECK(u==&native);CHECK(seq>=500&&seq<=501);CHECK(index==1||index==38);CHECK(group==20||group==21);CHECK(flags==0);return 1;}
static int load(void *u,u32 seq,u32 res){CHECK(u==&native);CHECK(seq>=500);CHECK(res>=120);++model_loads;return 1;}
static void release(void *u,u32 seq){CHECK(u==&native);CHECK(seq>=500);++aborted;}
static int hide(void *u,u32 seq){CHECK(u==&native);CHECK(seq>=500);++hidden;return 1;}
static void report(void *u,u32 group){(void)u;(void)group;CHECK(0);}
static int friend_filter(void *u,u32 p,u8 id){(void)u;(void)p;(void)id;++friend_calls;return 0;}
static int grid_add(void *u,u32 seq,u8 x,u8 y){CHECK(u==&native);CHECK(seq>=500);CHECK(x==1||x==2);CHECK(y==0);++grids;return 1;}
static int grid_update(void *u){CHECK(u==&native);return 1;}
static int visible(void *u,u32 seq,int yes){CHECK(u==&native);CHECK(seq>=500);CHECK(yes==1);++shown;return 1;}
static void abort_owned(void *u,const C3BattleEntryOwnership *own,size_t count){CHECK(u==&native);CHECK(own||!count);aborted+=(unsigned)count;}
static void test_entry(void)
{
    init();integrating=1;service.friend_filter=friend_filter;service.grid_add=grid_add;service.grid_update=grid_update;
    service.set_visible=visible;service.abort_owned=abort_owned;
    C3BattleSequenceServices seq={state,request,find_resource,allocate,set_fields,load,release,hide,report};
    C3BattleSequenceResource resource[39]={{0}};resource[38]=(C3BattleSequenceResource){20,123};resource[1]=(C3BattleSequenceResource){21,124};
    for(unsigned side_id=0;side_id<2;++side_id){C3BattleTrainerEntryFrame f={0};C3BattleAction action={{0}};u8 initial[0xb1c];memset(initial,0xa9,sizeof initial);
        c3_battle_action_set_word(&action,C3_ACTION_DATA,800+side_id);resource_ready=0;
        CHECK(c3_battle_trainer_entry_setup(&f,&service,&seq,resource,39,&native,initial));
        C3BattleRoutineReply reply=c3_battle_trainer_entry_start(&f,&action);CHECK(reply.state==C3_BATTLE_ROUTINE_YIELD);
        CHECK(constructor_calls==side_id+1);u32 seed=rng.seed;uint64_t draws=rng.draws;
        for(unsigned wait=0;wait<30;++wait){reply=c3_battle_trainer_entry_resume(&f,&action);CHECK(reply.state==C3_BATTLE_ROUTINE_YIELD);
            CHECK(constructor_calls==side_id+1&&rng.seed==seed&&rng.draws==draws);}
        resource_ready=1;reply=c3_battle_trainer_entry_resume(&f,&action);CHECK(reply.state==C3_BATTLE_ROUTINE_RETURN&&reply.value==1);
        CHECK(model_loads==side_id+1&&hidden==side_id+1&&shown==side_id+1&&grids==side_id+1);CHECK(!friend_calls);
        CHECK(!memcmp(f.hero+2840,initial+2840,4));CHECK(r16(runtime[side_id])==(side_id?1:10));
        CHECK(r32(runtime[side_id]+0x27c0)==500+side_id);
        for(unsigned j=0;j<2;++j){CHECK(r32(fight_ptr(side_id,j))==slots[side_id].party[j]);CHECK(r16(fight_ptr(side_id,j)+0x14c)==1+side_id*2+j);}
        c3_battle_trainer_entry_abort(&f,&action);CHECK(!aborted);
    }
    CHECK(counter==5&&constructor_calls==2&&requests==0);
    /* Source floor flags must not be erased to skip an absent friendship
     * owner. This fixture deliberately fails that callback and keeps it an
     * explicit ERROR, even though construction has already started. */
    init();integrating=1;resource_ready=1;kinds[1][11]=kinds[1][18]=1;
    service.friend_filter=friend_filter;service.grid_add=grid_add;
    service.grid_update=grid_update;service.set_visible=visible;service.abort_owned=abort_owned;
    C3BattleTrainerEntryFrame f={0};C3BattleAction action={{0}};u8 initial[0xb1c]={0};
    c3_battle_action_set_word(&action,C3_ACTION_DATA,801);
    CHECK(c3_battle_trainer_entry_setup(&f,&service,&seq,resource,39,&native,initial));
    C3BattleRoutineReply reply=c3_battle_trainer_entry_start(&f,&action);
    CHECK(reply.state==C3_BATTLE_ROUTINE_ERROR&&friend_calls==1);
    CHECK(f.failure&&!strcmp(f.failure,"friend_filter"));
    c3_battle_trainer_entry_abort(&f,&action);unsigned cleaned=aborted;
    c3_battle_trainer_entry_abort(&f,&action);CHECK(aborted==cleaned&&cleaned!=0);
}
static void test_admission_and_immutability(void)
{
    init();u8 snapshot[sizeof runtime];memcpy(snapshot,runtime,sizeof snapshot);C3BattleRng saved_rng=rng;
    u32 value=0;u8 small=0;
    CHECK(service.trainer_valid(&native,0,&small)&&!small);CHECK(service.pokemon_eligible(&native,0,0,&small)&&!small);
    CHECK(service.valid_trainer(&native,0,1,&value)&&!value);CHECK(service.side_fighting_count(&native,0,&small)&&!small);
    CHECK(service.side_status(&native,300,7,2,&value)&&!value);
    CHECK(!service.floor_status(&native,0x99,&value));CHECK(!service.pokemon_eligible(&native,9999,0,&small));
    CHECK(!service.floor_status(&native,0xd,(u32*)runtime[0]));
    CHECK(!service.floor_status(&native,0xd,&rng.seed));CHECK(!service.floor_status(&native,0xd,(u32*)trainers));
    CHECK(!service.floor_status(&native,0xd,(u32*)&records));CHECK(!memcmp(snapshot,runtime,sizeof snapshot));
    CHECK(rng.seed==saved_rng.seed&&rng.draws==saved_rng.draws&&counter==1);CHECK(!constructor_calls);
    records.sides[1].reference=records.sides[0].reference;CHECK(!c3_battle_entry_records_validate(&records));
    init();records.sides[1].trainers[0]=records.sides[0].trainers[0];CHECK(!c3_battle_entry_records_validate(&records));
    init();records.sides[1].trainers[0]=999;CHECK(!c3_battle_entry_records_validate(&records));
    init();records.sides[0].reference=0;CHECK(!c3_battle_entry_records_validate(&records));
    init();records.encounter_reference=slots[0].party[0];CHECK(!c3_battle_entry_records_validate(&records));
    init();records.tables[C3_ENTRY_KIND].limit=65536;CHECK(!c3_battle_entry_records_validate(&records));
    init();records.tables[C3_ENTRY_TRAINER].bytes=runtime[0];CHECK(!c3_battle_entry_records_validate(&records));
    init();records.tables[C3_ENTRY_TRAINER].bytes=pokemon_scratch;CHECK(!c3_battle_entry_records_validate(&records));
    init();records.tables[C3_ENTRY_TRAINER].bytes=(u8*)slots;CHECK(!c3_battle_entry_records_validate(&records));
    init();records.action_data=(C3BattleActionData*)runtime[0];CHECK(!c3_battle_entry_records_validate(&records));
    init();records.tables[C3_ENTRY_TRAINER].bytes=(u8*)hero_data;CHECK(!c3_battle_entry_records_validate(&records));
    init();records.tables[C3_ENTRY_SIDE].bytes=(u8*)sort_data;CHECK(!c3_battle_entry_records_validate(&records));
    init();native.owner=NULL;CHECK(!c3_battle_entry_records_validate(&records));
    init();records.action_count=65537;CHECK(!c3_battle_entry_records_validate(&records));
    init();records.tables[C3_ENTRY_TRAINER].byte_size=0;CHECK(!c3_battle_entry_records_validate(&records));
}
int main(void)
{
    test_predicates();test_tables();test_entry();test_admission_and_immutability();
    printf("Battle entry records: %u checks; source predicates and synthetic owner integration, not retail full-entry qualification\n",checks);
    return 0;
}
