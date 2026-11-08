/* Reference comparison of the original trainer-entry CONTROL and native
 * continuation. All domain/resource services here are explicit test doubles;
 * this does not stand in for retail constructors or live GPU/model ownership. */
#include "colosseum3ds/battle_trainer_entry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef uint8_t u8; typedef int8_t s8; typedef uint16_t u16;
typedef uint32_t u32; typedef int32_t s32;
typedef C3BattleAction FightAction;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) {fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);} } while(0)
#define REF(p) ((uint32_t)(uintptr_t)(p))
#define PTR(v) ((void *)(uintptr_t)(v))
enum { BUFF=1,FLOOR,ENCOUNTER,SIDE,SIDE_VALID,SIDE_STATUS,ENCOUNTER_STATUS,DATA_VALID,
    PRESET,COPY,HERO,SEQ_INDEX,RUNTIME,TRAINER_STATUS,TRAINER_VALID,SORT,PARTY,ELIGIBLE,
    ENTERED,COUNTER,FIGHTER,GC_HERO,FRIEND,COUNT,VALID_TRAINER,GRID_PARAM,GRID_ADD,GRID_UPDATE,
    VISIBLE,RESOURCE_STATE,REQUEST,FIND,ALLOC,SEQ_FIELDS,LOAD,RELEASE,HIDE,NEGATIVE,ABORT };
typedef struct {uint32_t op,a,b,c,d;} Event;
typedef struct {
    unsigned count, maximum, active, buff, valid_side, invalid_data, invalid_runtime;
    unsigned preset, flags, gc_mask, null_sequence, wait, load_failure, sort_extra;
    uint32_t rng, counter;
    unsigned calls[64], elapsed[4], entered[2], hero_calls;
    uint32_t sequences[2];
    uint8_t initial[C3_TRAINER_ENTRY_HERO_BYTES], seen_hero[2][C3_TRAINER_ENTRY_HERO_BYTES];
    uint8_t runtime[2][10468];
    Event trace[8192]; size_t trace_count, fail_at;
    C3BattleTrainerEntryFrame *reentrant;
    unsigned reentry_checked;
} Harness;
static int record(Harness *h,unsigned op,uint32_t a,uint32_t b,uint32_t c,uint32_t d)
{
    CHECK(h->trace_count<sizeof h->trace/sizeof h->trace[0]);
    h->trace[h->trace_count++]=(Event){op,a,b,c,d};++h->calls[op];
    return h->trace_count!=h->fail_at;
}
static unsigned trainer_slot(uint32_t t){CHECK(t>=100&&t<102);return t-100;}
static int action_buff(void *u,const C3BattleAction *a,uint32_t *out)
{Harness *h=u;(void)a;*out=h->buff;return record(h,BUFF,0,0,0,0);}
static int floor_status(void *u,unsigned selector,uint32_t *out)
{
    Harness *h=u;
    switch(selector){case 0xd:*out=0x1000a;break;case 0x14:*out=0x10003;break;
    case 0x16:*out=h->count;break;case 0x17:*out=h->maximum;break;case 0x18:*out=h->active;break;
    case 0x27:*out=(h->flags&1)?0x101u:0;break;case 0x2e:*out=(h->flags&2)?0x101u:0;break;
    default:CHECK(0);}
    return record(h,FLOOR,selector,*out,0,0);
}
static int encounter(void *u,uint16_t id,uint32_t *out)
{*out=50;return record(u,ENCOUNTER,id,0,0,0);}
static int target_side(void *u,uint16_t buff,uint16_t id,uint32_t *out)
{*out=60;return record(u,SIDE,buff,id,0,0);}
static int side_valid(void *u,uint32_t side,uint8_t *out)
{Harness *h=u;*out=(uint8_t)h->valid_side;return record(h,SIDE_VALID,side,0,0,0);}
static int side_status(void *u,uint32_t side,unsigned selector,unsigned index,uint32_t *out)
{*out=selector==5?0x10001u:100u+index;return record(u,SIDE_STATUS,side,selector,index,0);}
static int encounter_status(void *u,uint32_t enc,unsigned field,uint8_t index,uint16_t *out)
{*out=(uint16_t)(field?index:10u+index);return record(u,ENCOUNTER_STATUS,enc,field,index,0);}
static int data_valid(void *u,uint16_t id,uint16_t device,uint8_t *out)
{Harness *h=u;*out=(uint8_t)(!(h->invalid_data&(1u<<device)));return record(h,DATA_VALID,id,device,0,0);}
static int preset_mode(void *u,uint8_t *out)
{Harness *h=u;*out=(uint8_t)h->preset;return record(h,PRESET,0,0,0,0);}
static int copy_preset(void *u,uint8_t index,uint8_t *hero)
{memset(hero,0x40+index,0xb18);return record(u,COPY,index,0,0,0);}
static int create_hero(void *u,uint16_t id,uint16_t device,uint8_t *hero)
{
    Harness *h=u;h->rng=h->rng*214013u+2531011u;++h->hero_calls;
    for(unsigned i=0;i<0xb18;++i)if(i%3!=device%3)hero[i]=(uint8_t)(h->rng+i+id+device);
    if(h->reentrant){
        C3BattleAction dummy={{0}};
        CHECK(c3_battle_trainer_entry_start(h->reentrant,&dummy).state==C3_BATTLE_ROUTINE_ERROR);
        CHECK(c3_battle_trainer_entry_resume(h->reentrant,&dummy).state==C3_BATTLE_ROUTINE_ERROR);
        c3_battle_trainer_entry_abort(h->reentrant,&dummy);
        CHECK(!h->reentrant->abort_called);++h->reentry_checked;
    }
    return record(h,HERO,id,device,0,0);
}
static int sequence_index(void *u,uint16_t id,uint16_t *out)
{Harness *h=u;*out=h->null_sequence?0:(uint16_t)(id-9);return record(h,SEQ_INDEX,id,0,0,0);}
static int runtime(void *u,uint32_t t,const uint8_t *hero,uint16_t id,uint16_t dev,uint32_t seq)
{
    Harness *h=u;unsigned i=trainer_slot(t);
    memcpy(h->seen_hero[i],hero,C3_TRAINER_ENTRY_HERO_BYTES);
    memcpy(h->runtime[i]+4,hero,0xb18);h->runtime[i][0]=(uint8_t)id;
    h->sequences[i]=seq;h->entered[i]=0;
    return record(h,RUNTIME,t,id,dev,seq);
}
static int trainer_status(void *u,uint32_t t,unsigned selector,unsigned index,uint32_t *out)
{
    Harness *h=u;unsigned i=trainer_slot(t);
    if(selector==0x44)*out=200+i;
    else if(selector==0x45)*out=2000+i*6+index;
    else if(selector==0x4c)*out=h->sequences[i];else CHECK(0);
    return record(h,TRAINER_STATUS,t,selector,index,0);
}
static int trainer_valid(void *u,uint32_t t,uint8_t *out)
{Harness *h=u;*out=(uint8_t)(!(h->invalid_runtime&(1u<<trainer_slot(t))));return record(h,TRAINER_VALID,t,0,0,0);}
static int sort(void *u,uint32_t t,uint16_t maximum,uint16_t active)
{Harness *h=u;h->counter+=h->sort_extra;return record(h,SORT,t,maximum,active,0);}
static int party(void *u,uint32_t hero,unsigned j,uint32_t *out)
{*out=1000+(hero-200)*6+j;return record(u,PARTY,hero,j,0,0);}
static int eligible(void *u,uint32_t p,int fight,uint8_t *out)
{unsigned j=(p-1000)%6;*out=(uint8_t)(fight?(j==1||j==4):(j!=3));return record(u,ELIGIBLE,p,(uint32_t)fight,0,0);}
static int entered(void *u,uint32_t t,uint32_t p,uint32_t *out)
{Harness *h=u;*out=h->entered[trainer_slot(t)]&(1u<<((p-1000)%6));return record(h,ENTERED,t,p,0,0);}
static int counter(void *u,uint32_t *out)
{Harness *h=u;*out=h->counter++;return record(h,COUNTER,*out,0,0,0);}
static int fighter(void *u,uint32_t d,uint32_t p,uint32_t id)
{Harness *h=u;h->entered[(d-2000)/6]|=1u<<((p-1000)%6);return record(h,FIGHTER,d,p,id,0);}
static int gc_hero(void *u,uint32_t t,uint8_t *out)
{Harness *h=u;*out=(uint8_t)(!!(h->gc_mask&(1u<<trainer_slot(t))));return record(h,GC_HERO,t,0,0,0);}
static int friend_filter(void *u,uint32_t p,uint8_t filter)
{return record(u,FRIEND,p,filter,0,0);}
static int side_count(void *u,uint32_t side,uint8_t *out)
{Harness *h=u;*out=(uint8_t)h->count;return record(h,COUNT,side,0,0,0);}
static int valid_trainer(void *u,uint32_t side,unsigned i,uint32_t *out)
{Harness *h=u;*out=(h->invalid_runtime&(1u<<i))?0:100+i;return record(h,VALID_TRAINER,side,i,0,0);}
static int grid_parameters(void *u,uint16_t side,uint8_t count,unsigned i,uint8_t *x,uint8_t *y)
{*x=(uint8_t)side;*y=(uint8_t)(i?-1:1);return record(u,GRID_PARAM,side,count,i,0);}
static int grid_add(void *u,uint32_t seq,uint8_t x,uint8_t y)
{return record(u,GRID_ADD,seq,x,y,0);}
static int grid_update(void *u){return record(u,GRID_UPDATE,0,0,0,0);}
static int visible(void *u,uint32_t seq,int v){return record(u,VISIBLE,seq,(uint32_t)v,0,0);}
static void abort_owned(void *u,const C3BattleEntryOwnership *owned,size_t n)
{Harness *h=u;record(h,ABORT,(uint32_t)n,n?owned[0].trainer:0,n?owned[0].sequence:0,0);}

/* Request/poll/load fixture. The real resource closure is separately qualified
 * by test_battle_sequence; both original-main and lowered-main use that child. */
static int32_t resource_state(void *u,uint32_t group)
{Harness *h=u;CHECK(group>=1&&group<=4);unsigned n=h->elapsed[group-1]++;int32_t s=n==0?-1:(n<=h->wait?1:0);record(h,RESOURCE_STATE,group,(uint32_t)s,0,0);return s;}
static int request(void *u,uint32_t group){return record(u,REQUEST,group,0,0,0);}
static uint32_t find(void *u,uint32_t group,uint32_t member)
{record(u,FIND,group,member,0,0);return 300+group;}
static uint32_t allocate(void *u){Harness *h=u;uint32_t v=400+h->calls[ALLOC];record(h,ALLOC,v,0,0,0);return v;}
static int fields(void *u,uint32_t seq,uint16_t index,uint16_t group,uint8_t flags)
{return record(u,SEQ_FIELDS,seq,index,group,flags);}
static int load(void *u,uint32_t seq,uint32_t resource)
{Harness *h=u;if(!record(h,LOAD,seq,resource,0,0))return -1;return !h->load_failure;}
static void release(void *u,uint32_t seq){record(u,RELEASE,seq,0,0,0);}
static int hide(void *u,uint32_t seq){return record(u,HIDE,seq,0,0,0);}
static void negative(void *u,uint32_t group){record(u,NEGATIVE,group,0,0,0);}
static const C3BattleSequenceResource resources[5]={{0,0},{1,11},{2,22},{3,33},{4,44}};
static const C3BattleSequenceServices seq_services={resource_state,request,find,allocate,fields,load,release,hide,negative};
static const C3BattleTrainerEntryServices services={action_buff,floor_status,encounter,target_side,side_valid,
    side_status,encounter_status,data_valid,preset_mode,copy_preset,create_hero,sequence_index,runtime,
    trainer_status,trainer_valid,sort,party,eligible,entered,counter,fighter,gc_hero,friend_filter,
    side_count,valid_trainer,grid_parameters,grid_add,grid_update,visible,abort_owned};

/* Host ABI bridge to the UNCHANGED original algorithm's dependency names. */
static Harness *oracle;
static void oracle_seed_stack(void *p,size_t n){memcpy(p,oracle->initial,n);}
static FightAction *fightActionBiosGetFightActionDataPtr(FightAction *a){return a;}
static u32 fightActionDataBiosGetBuff(FightAction *a){u32 v;CHECK(action_buff(oracle,a,&v));return v;}
static u32 fightFloorGetStatus(int a,int b,int selector,int index){u32 v;(void)a;(void)b;(void)index;CHECK(floor_status(oracle,(unsigned)selector,&v));return v;}
static u32 fightEncountDataBiosGetPtr(u16 id){u32 v;CHECK(encounter(oracle,id,&v));return v;}
static u32 fightTargetGetPtr(u16 buff,int unused,u16 id){u32 v;(void)unused;CHECK(target_side(oracle,buff,id,&v));return v;}
static u8 fightSideCheckValid(void *s){u8 v;CHECK(side_valid(oracle,REF(s),&v));return v;}
static u32 fightSideGetStatus(void *s,int unused,int selector,u32 i){u32 v;(void)unused;CHECK(side_status(oracle,REF(s),(unsigned)selector,i,&v));return v;}
static u16 fightEncountDataBiosGetFightTrainerDataId(void *e,int i){u16 v;CHECK(encounter_status(oracle,REF(e),0,(u8)i,&v));return v;}
static u16 fightEncountDataBiosGetGSInputDevice(void *e,int i){u16 v;CHECK(encounter_status(oracle,REF(e),1,(u8)i,&v));return v;}
static u8 fightTrainerCheckTrainerDataIdValid(u16 id,u16 dev){u8 v;CHECK(data_valid(oracle,id,dev,&v));return v;}
static u8 fn_8006B57C(void){u8 v;CHECK(preset_mode(oracle,&v));return v;}
static u32 fn_8006B0F8(int index){return (u32)index;}
static void heroBiosCopy(void *hero,u32 index){CHECK(copy_preset(oracle,(u8)index,hero));}
static void fightTrainerCreateFightTrainerDataIdToHero(u16 id,u16 dev,void *hero){CHECK(create_hero(oracle,id,dev,hero));}
static u32 fightTrainerCreateSequence(u16 id)
{
    C3BattleSequenceFrame f={0};u16 index;CHECK(sequence_index(oracle,id,&index));
    C3BattleRoutineReply r=c3_battle_sequence_begin(&f,index,resources,5,&seq_services,oracle,1);
    unsigned guard=0;while(r.state==C3_BATTLE_ROUTINE_YIELD){CHECK(++guard<1024);r=c3_battle_sequence_step(&f);}
    CHECK(r.state==C3_BATTLE_ROUTINE_RETURN);return r.value;
}
static void fightTrainerCreate(void *t,void *hero,u16 id,u16 dev,u32 seq){CHECK(runtime(oracle,REF(t),hero,id,dev,seq));}
static u32 fightTrainerGetStatus(void *t,int unused,int selector,u32 i){u32 v;(void)unused;CHECK(trainer_status(oracle,REF(t),(unsigned)selector,i,&v));return v;}
static u8 fightTrainerCheckValid(void *t){u8 v;CHECK(trainer_valid(oracle,REF(t),&v));return v;}
static void fightTrainerSortFightTrainerDataIdToHeroTemotiPokemon(void *t,int maximum,int active){CHECK(sort(oracle,REF(t),(u16)maximum,(u16)active));}
static u32 heroGetStatus(void *hero,int selector,u32 i){u32 v;CHECK(selector==3);CHECK(party(oracle,REF(hero),i,&v));return v;}
static u8 pokemonCheckFightOut(void *p){u8 v;CHECK(eligible(oracle,REF(p),1,&v));return v;}
static u8 pokemonCheckValid(void *p){u8 v;CHECK(eligible(oracle,REF(p),0,&v));return v;}
static u32 fightTrainerCheckTemotiPokemonFightEntry(void *t,void *p){u32 v;CHECK(entered(oracle,REF(t),REF(p),&v));return v;}
static u32 fightFloorGetFightPokemonEntryCntInc(int unused){u32 v;(void)unused;CHECK(counter(oracle,&v));return v;}
static void fightPokemonCreate(void *d,void *p,u32 id){CHECK(fighter(oracle,REF(d),REF(p),id));}
static u8 fightTrainerIsGcHero(void *t){u8 v;CHECK(gc_hero(oracle,REF(t),&v));return v;}
static void fightPokemonGetFriendFormPokemonFriendFilterId(void *p,int filter){CHECK(friend_filter(oracle,REF(p),(u8)filter));}
static u8 fightSideGetDoFightTrainerCount(void *s){u8 v;CHECK(side_count(oracle,REF(s),&v));return v;}
static u32 fightSideGetValidFightTrainerPtr(void *s,u32 i){u32 v;CHECK(valid_trainer(oracle,REF(s),i,&v));return v;}
static void fightSideGetFightTrainerGridParam(u32 side,u32 count,u32 i,u8 *x,u8 *y){CHECK(grid_parameters(oracle,(u16)side,(u8)count,i,x,y));}
static void battleGridAddTrainer(int seq,u8 x,u8 y){CHECK(grid_add(oracle,(u32)seq,x,y));}
static void battleGridUpdate(void){CHECK(grid_update(oracle));}
static void fn_801DA4E8(int seq,int v){CHECK(visible(oracle,(u32)seq,v));}
#include "battle_trainer_entry_reference.h"

static void init(Harness *h)
{
    memset(h,0,sizeof *h);h->count=2;h->maximum=6;h->active=2;h->buff=4;h->valid_side=1;
    h->wait=3;h->rng=0x12345678u;h->counter=65535;h->gc_mask=1;h->sort_extra=9;
    for(unsigned i=0;i<sizeof h->initial;++i)h->initial[i]=(uint8_t)(i*37u+21u);
}
static C3BattleRoutineReply run_native(Harness *h,C3BattleTrainerEntryFrame *f)
{
    C3BattleAction a={{0}};CHECK(c3_battle_trainer_entry_setup(f,&services,&seq_services,resources,5,h,h->initial));
    C3BattleRoutineReply r=c3_battle_trainer_entry_start(f,&a);unsigned guard=0;
    while(r.state==C3_BATTLE_ROUTINE_YIELD){
        CHECK(r.wait==C3_BATTLE_WAIT_RESOURCE);CHECK(++guard<2048);
        unsigned heroes=h->hero_calls;uint32_t seed=h->rng;unsigned i=f->trainer_index;
        r=c3_battle_trainer_entry_resume(f,&a);
        if(r.state==C3_BATTLE_ROUTINE_YIELD && f->trainer_index==i){CHECK(heroes==h->hero_calls);CHECK(seed==h->rng);}
    }
    return r;
}
static void compare_case(Harness *seed)
{
    Harness *a=malloc(sizeof *a),*b=malloc(sizeof *b);CHECK(a&&b);*a=*seed;*b=*seed;
    C3BattleAction action={{0}};oracle=a;u32 expected=original_trainer_entry(&action);
    C3BattleTrainerEntryFrame frame={0};C3BattleRoutineReply actual=run_native(b,&frame);
    CHECK(actual.state==C3_BATTLE_ROUTINE_RETURN);CHECK(actual.value==expected);
    if(a->trace_count!=b->trace_count || memcmp(a->trace,b->trace,a->trace_count*sizeof(Event))){
        fprintf(stderr,"trace mismatch count=%u max=%u active=%u preset=%u buff=%u\n",seed->count,seed->maximum,seed->active,seed->preset,seed->buff);
        for(size_t i=0;i<a->trace_count&&i<b->trace_count;++i)if(memcmp(a->trace+i,b->trace+i,sizeof(Event))){
            fprintf(stderr,"at %zu original %u/%u/%u/%u/%u native %u/%u/%u/%u/%u\n",i,a->trace[i].op,a->trace[i].a,a->trace[i].b,a->trace[i].c,a->trace[i].d,b->trace[i].op,b->trace[i].a,b->trace[i].b,b->trace[i].c,b->trace[i].d);break;}
        CHECK(0);
    }
    CHECK(a->rng==b->rng);CHECK(a->counter==b->counter);CHECK(!memcmp(a->runtime,b->runtime,sizeof a->runtime));
    CHECK(!memcmp(a->seen_hero,b->seen_hero,sizeof a->seen_hero));CHECK(!memcmp(a->entered,b->entered,sizeof a->entered));
    CHECK(!memcmp(frame.hero+0xb18,seed->initial+0xb18,4));
    size_t n=b->trace_count;c3_battle_trainer_entry_abort(&frame,&action);CHECK(b->trace_count==n);
    CHECK(c3_battle_trainer_entry_resume(&frame,&action).value==expected);CHECK(b->trace_count==n);
    free(a);free(b);
}
static void queue_cases(Harness *h)
{
    for(unsigned cancel=0;cancel<2;++cancel){
        init(h);C3BattleTrainerEntryFrame entry={0};C3BattleActionFrame frame={0};
        C3BattleActionQueue queue={0};C3BattleAction action={{0}};
        c3_battle_action_queue_init(&queue);c3_battle_action_set_kind(&action,0xff);
        c3_battle_action_set_word(&action,C3_ACTION_DATA,0x111);
        C3BattleActionData data={0x111,0xff,4};
        C3BattleActionKind kind={0xff,0x8020d1fc,0};
        C3BattleActionRuntime source={&data,1,&kind,1,NULL,0};
        C3BattleRoutineBinding binding={0x8020d1fc,c3_battle_trainer_entry_start,
            c3_battle_trainer_entry_resume,c3_battle_trainer_entry_abort,&entry};
        C3BattleResumeRuntime runtime={&source,&binding,1};
        CHECK(c3_battle_trainer_entry_setup(&entry,&services,&seq_services,resources,5,h,h->initial));
        C3BattleFrameResult r=c3_battle_frame_begin_flow(&frame,&queue,&action,&runtime);
        CHECK(r.state==C3_BATTLE_FRAME_READY);CHECK(h->hero_calls==0);
        unsigned guard=0;
        do{
            r=c3_battle_frame_step(&frame);CHECK(++guard<32);
            CHECK(c3_battle_action_queue_count(&queue)==1);
            CHECK(queue.read_index==0&&queue.write_index==1);
            if(cancel&&r.state==C3_BATTLE_FRAME_WAITING&&entry.trainer_index==1){
                uint32_t seed=h->rng;
                r=c3_battle_frame_abort(&frame);CHECK(r.state==C3_BATTLE_FRAME_ABORTED);
                CHECK(h->calls[ABORT]==1);CHECK(h->hero_calls==2);CHECK(h->rng==seed);
                CHECK(entry.phase==C3_TRAINER_ENTRY_ABORTED);break;
            }
        }while(r.state==C3_BATTLE_FRAME_WAITING);
        if(!cancel){CHECK(r.state==C3_BATTLE_FRAME_RETURNED&&r.source_result==1);
            CHECK(h->hero_calls==2);CHECK(h->calls[RUNTIME]==2);CHECK(h->calls[SORT]==2);
            CHECK(h->calls[LOAD]==2);CHECK(h->calls[HIDE]==2);CHECK(h->calls[VISIBLE]==2);}
        CHECK(c3_battle_action_queue_count(&queue)==1); /* no implicit reset/requeue */
    }
}
int main(void)
{
    Harness *h=malloc(sizeof *h);CHECK(h);init(h);
    const unsigned quota[]={0,1,2,6,0xffff,0x10002};
    for(unsigned n=0;n<=2;++n)for(unsigned m=0;m<6;++m)for(unsigned a=0;a<6;++a)
        for(unsigned preset=0;preset<2;++preset)for(unsigned flags=0;flags<4;++flags){
            init(h);h->count=n;h->maximum=quota[m];h->active=quota[a];h->preset=preset;h->flags=flags;
            h->buff=(flags&1)?5:0x10004;compare_case(h);
        }
    for(unsigned bad=0;bad<16;++bad){init(h);h->invalid_data=bad;compare_case(h);}
    for(unsigned bad=0;bad<4;++bad){init(h);h->invalid_runtime=bad;compare_case(h);}
    init(h);h->null_sequence=1;compare_case(h);init(h);h->load_failure=1;compare_case(h);
    init(h);h->valid_side=0;compare_case(h);init(h);h->wait=100;compare_case(h);
    init(h);h->wait=0;compare_case(h);
    /* Reentrant calls and caller-provided stack bytes remain protected. */
    C3BattleTrainerEntryFrame f={0};init(h);h->reentrant=&f;
    CHECK(run_native(h,&f).state==C3_BATTLE_ROUTINE_RETURN);CHECK(h->reentry_checked==2);
    /* Cancel while each trainer is loading; abort cannot replay constructors. */
    for(unsigned stop=0;stop<2;++stop){
        init(h);memset(&f,0,sizeof f);C3BattleAction action={{0}};
        CHECK(c3_battle_trainer_entry_setup(&f,&services,&seq_services,resources,5,h,h->initial));
        C3BattleRoutineReply r=c3_battle_trainer_entry_start(&f,&action);
        while(r.state==C3_BATTLE_ROUTINE_YIELD&&f.trainer_index<stop)r=c3_battle_trainer_entry_resume(&f,&action);
        CHECK(r.state==C3_BATTLE_ROUTINE_YIELD);CHECK(f.trainer_index==stop);
        uint32_t rng=h->rng;unsigned heroes=h->hero_calls;
        CHECK(!c3_battle_trainer_entry_setup(&f,&services,&seq_services,resources,5,h,h->initial));
        c3_battle_trainer_entry_abort(&f,&action);size_t count=h->trace_count;
        c3_battle_trainer_entry_abort(&f,&action);CHECK(h->trace_count==count);
        CHECK(h->calls[ABORT]==1);CHECK(h->trace[count-1].a==stop);
        CHECK(c3_battle_trainer_entry_resume(&f,&action).state==C3_BATTLE_ROUTINE_ERROR);
        CHECK(h->hero_calls==heroes&&h->rng==rng);
    }
    /* Inject each fallible source-domain service failure; errors remain sticky,
     * and a constructed sequence is either transferred or released once. */
    init(h);memset(&f,0,sizeof f);CHECK(run_native(h,&f).state==C3_BATTLE_ROUTINE_RETURN);
    size_t baseline=h->trace_count;
    for(size_t bad=1;bad<=baseline;++bad){
        init(h);h->fail_at=bad;memset(&f,0,sizeof f);
        C3BattleRoutineReply r=run_native(h,&f);
        if(r.state==C3_BATTLE_ROUTINE_ERROR){
            size_t n=h->trace_count;CHECK(f.phase==C3_TRAINER_ENTRY_FAILED);CHECK(f.failure);
            CHECK(c3_battle_trainer_entry_resume(&f,NULL).state==C3_BATTLE_ROUTINE_ERROR);CHECK(h->trace_count==n);
            CHECK(!c3_battle_trainer_entry_setup(&f,&services,&seq_services,resources,5,h,h->initial));
            c3_battle_trainer_entry_abort(&f,NULL);CHECK(h->calls[ABORT]==1);
            CHECK(h->calls[RELEASE]<=h->calls[ALLOC]);
        }
    }
    init(h);h->count=3;memset(&f,0,sizeof f);CHECK(run_native(h,&f).state==C3_BATTLE_ROUTINE_ERROR);CHECK(h->hero_calls==0);
    memset(&f,0,sizeof f);CHECK(!c3_battle_trainer_entry_setup(&f,&services,&seq_services,resources,5,h,f.hero));
    C3BattleTrainerEntryServices incomplete=services;incomplete.create_fighter=NULL;
    CHECK(!c3_battle_trainer_entry_setup(&f,&incomplete,&seq_services,resources,5,h,h->initial));
    queue_cases(h);
    free(h);printf("Trainer entry: %u checks against pinned original control; owner services are test doubles\n",checks);return 0;
}
