#include "colosseum3ds/battle_sequence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks;if(!(x)){fprintf(stderr,"sequence:%d: %s\n",__LINE__,#x);exit(1);} }while(0)
enum { EV_STATE=1,EV_REQUEST,EV_FIND,EV_ALLOC,EV_LOAD,EV_RELEASE,EV_HIDE,EV_LOG,EV_YIELD };
typedef struct { uint32_t kind,a,b,c; } Event;
typedef struct {
    int32_t states[16];unsigned state_count,state_at,finds,allocs,loads,frees,hides;
    unsigned missing_first,missing_all,allocation_failed,load_failed,request_failed;
    uint16_t selector;
    uint8_t sequence[0x8c];
    Event events[128];unsigned count;
} Fixture;
static void log_event(Fixture *f,uint32_t k,uint32_t a,uint32_t b,uint32_t c)
{ CHECK(f->count<128);Event e={k,a,b,c};f->events[f->count++]=e; }
static int32_t state(void *u,uint32_t group)
{
    Fixture *f=u;CHECK(f->state_at<16);
    int32_t n=f->state_at<f->state_count?f->states[f->state_at++]:0;
    log_event(f,EV_STATE,group,(uint32_t)n,0);return n;
}
static int request(void *u,uint32_t group)
{ Fixture *f=u;log_event(f,EV_REQUEST,group,0,0);return !f->request_failed; }
static uint32_t find(void *u,uint32_t group,uint32_t member)
{
    Fixture *f=u;unsigned first=f->finds++==0;
    uint32_t r=(f->missing_all || (first && f->missing_first))?0:0x99;
    log_event(f,EV_FIND,group,member,r);return r;
}
static uint32_t allocate(void *u)
{ Fixture *f=u;++f->allocs;log_event(f,EV_ALLOC,0,0,0);return f->allocation_failed?0:0x61; }
static int fields(void *u,uint32_t seq,uint16_t index,uint16_t group,uint8_t flags)
{ Fixture *f=u;CHECK(seq==0x61);memcpy(f->sequence+0x70,&index,2);memcpy(f->sequence+0x72,&group,2);f->sequence[0x75]=flags;return 1; }
static int load(void *u,uint32_t seq,uint32_t resource)
{
    Fixture *f=u;uint16_t index,group;CHECK(seq==0x61);
    memcpy(&index,f->sequence+0x70,2);memcpy(&group,f->sequence+0x72,2);
    CHECK(f->sequence[0x75]==0);++f->loads;log_event(f,EV_LOAD,resource,index,group);
    return !f->load_failed;
}
static void release(void *u,uint32_t seq)
{ Fixture *f=u;CHECK(seq==0x61);++f->frees;log_event(f,EV_RELEASE,seq,0,0); }
static int hide(void *u,uint32_t seq)
{ Fixture *f=u;CHECK(seq==0x61);++f->hides;log_event(f,EV_HIDE,seq,0,0);return 1; }
static void negative(void *u,uint32_t group)
{ log_event(u,EV_LOG,group,0,0); }
static const C3BattleSequenceServices services={state,request,find,allocate,fields,load,release,hide,negative};

/* Reference executes the original unmodified control against the SAME scripted
 * service results. These are explicit test doubles, not live asset loading. */
typedef uint8_t u8;typedef uint16_t u16;typedef uint32_t u32;typedef int32_t s32;
typedef struct { u32 group,resource,name; } SequenceResourcePair;
static Fixture *reference;
u32 lbl_80478CC8;
SequenceResourcePair lbl_80370840[8];
const char lbl_802799C8[]="test diagnostic";
static uint32_t ref_group;
s32 fn_8017B2CC(u32 group){ref_group=group;return state(reference,group);}
void fn_8017B3E4(u32 group){CHECK(request(reference,group));}
void *GSresGetResource(u32 group,u32 member){return (void *)(uintptr_t)find(reference,group,member);}
void *fn_801DB154(void){return allocate(reference)?reference->sequence:NULL;}
u8 sequenceLoad(void *seq,void *resource){CHECK(seq==reference->sequence);return (u8)load(reference,0x61,(uint32_t)(uintptr_t)resource);}
void fn_801DB100(void *seq){CHECK(seq==reference->sequence);release(reference,0x61);}
void *fn_801DA4E8(void *seq,u32 visible){CHECK(seq==reference->sequence && !visible);CHECK(hide(reference,0x61));return seq;}
void *fightTrainerGetStatus(void *ctx,void *id,u32 field,u32 idx)
{ CHECK(!ctx && id==(void *)(uintptr_t)10 && field==9 && !idx);return (void *)(uintptr_t)reference->selector; }
void GSlogWrite(const char *message){CHECK(message==lbl_802799C8);negative(reference,ref_group);}
void _threadSwitch(void){log_event(reference,EV_YIELD,0,0,0);}
#include "battle_sequence_reference.h"

static void parity(void)
{
    for(unsigned trainer=0;trainer<2;++trainer)for(unsigned variant=0;variant<7;++variant)
    for(unsigned outcome=0;outcome<4;++outcome)for(unsigned delays=0;delays<5;++delays){
        C3BattleSequenceResource table[5]={{0,0},{0x9d,0x030c1e00},{0x2db,0x058a1e00},{0,4},{7,0}};
        uint16_t index=variant<5?(uint16_t)variant:(variant==5?5:UINT16_MAX);
        if(variant==4)table[1].member=0;
        Fixture a={0},b;memset(a.sequence,0xa5,sizeof a.sequence);a.selector=index;
        a.states[0]=outcome==0?-1:0;a.states[1]=0;a.state_count=2;
        /* Request branch performs only one initial status query when negative. */
        if(a.states[0]<0)a.state_count=1;
        for(unsigned d=0;d<delays;++d)a.states[a.state_count++]=d%2?1:-2;
        a.states[a.state_count++]=0;
        a.allocation_failed=outcome==1;a.load_failed=outcome==2;a.missing_all=outcome==3;
        a.missing_first=outcome==2;b=a;
        lbl_80478CC8=5;for(unsigned i=0;i<5;++i){lbl_80370840[i].group=table[i].group;lbl_80370840[i].resource=table[i].member;}
        reference=&a;
        void *original=trainer?fightTrainerCreateSequence((void *)(uintptr_t)10):fn_801DE418(index);
        C3BattleSequenceFrame frame={0};
        C3BattleRoutineReply r=c3_battle_sequence_begin(&frame,index,table,5,&services,&b,(int)trainer);
        unsigned steps=0;
        while(r.state==C3_BATTLE_ROUTINE_YIELD){
            CHECK(r.wait==C3_BATTLE_WAIT_RESOURCE);log_event(&b,EV_YIELD,0,0,0);
            CHECK(++steps<20);r=c3_battle_sequence_step(&frame);
        }
        CHECK(r.state==C3_BATTLE_ROUTINE_RETURN);CHECK(r.value==(original?0x61u:0u));
        CHECK(a.count==b.count);CHECK(!memcmp(a.events,b.events,a.count*sizeof *a.events));
        CHECK(!memcmp(a.sequence,b.sequence,sizeof a.sequence));
        CHECK(a.allocs==b.allocs && a.loads==b.loads && a.frees==b.frees && a.hides==b.hides);
        unsigned count=b.count;CHECK(c3_battle_sequence_step(&frame).value==r.value);CHECK(count==b.count);
        CHECK(c3_battle_sequence_cancel(&frame).value==r.value);CHECK(count==b.count);
    }
}
static int broken_load(void *u,uint32_t seq,uint32_t resource)
{ (void)load(u,seq,resource);return -1; }
static int broken_hide(void *u,uint32_t seq)
{ (void)hide(u,seq);return 0; }
static void native_errors(void)
{
    C3BattleSequenceResource table[]={{0,0},{0x12345,7}};
    Fixture x={0};C3BattleSequenceFrame f={0};C3BattleSequenceServices s=services;
    x.states[0]=-1;x.states[1]=1;x.states[2]=0;x.state_count=3;
    CHECK(c3_battle_sequence_begin(&f,1,table,2,&s,&x,1).state==C3_BATTLE_ROUTINE_YIELD);
    unsigned count=x.count;CHECK(c3_battle_sequence_begin(&f,1,table,2,&s,&x,1).state==C3_BATTLE_ROUTINE_ERROR);
    CHECK(x.count==count);CHECK(c3_battle_sequence_cancel(&f).state==C3_BATTLE_ROUTINE_ERROR);
    CHECK(c3_battle_sequence_step(&f).state==C3_BATTLE_ROUTINE_ERROR);CHECK(!x.allocs && !x.frees);
    x=(Fixture){0};f=(C3BattleSequenceFrame){0};s.load=broken_load;
    CHECK(c3_battle_sequence_begin(&f,1,table,2,&s,&x,1).state==C3_BATTLE_ROUTINE_ERROR);
    CHECK(x.allocs==1 && x.loads==1 && x.frees==1 && !x.hides);
    count=x.count;CHECK(c3_battle_sequence_step(&f).state==C3_BATTLE_ROUTINE_ERROR);CHECK(x.count==count);
    x=(Fixture){0};f=(C3BattleSequenceFrame){0};s=services;s.hide=broken_hide;
    CHECK(c3_battle_sequence_begin(&f,1,table,2,&s,&x,1).state==C3_BATTLE_ROUTINE_ERROR);
    CHECK(x.frees==1 && x.hides==1);
    x=(Fixture){0};f=(C3BattleSequenceFrame){0};s=services;s.load=NULL;
    CHECK(c3_battle_sequence_begin(&f,1,table,2,&s,&x,1).state==C3_BATTLE_ROUTINE_ERROR);CHECK(!x.count);
    s=services;x.states[0]=-1;x.state_count=1;x.request_failed=1;
    CHECK(c3_battle_sequence_begin(&f,1,table,2,&s,&x,1).state==C3_BATTLE_ROUTINE_ERROR);CHECK(!x.allocs);
    CHECK(c3_battle_sequence_step(NULL).state==C3_BATTLE_ROUTINE_ERROR);
}
int main(void)
{
    parity();native_errors();
    printf("Trainer sequence continuation: %u checks against pinned source control; services are test doubles.\n",checks);
    return 0;
}
