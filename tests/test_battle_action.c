#include "colosseum3ds/battle_action.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while (0)

/* Independent byte accessors for the compiled original-source oracle. Only
 * the source's native table pointers cross this shim; records hold ref IDs. */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;
typedef struct { u16 kind, pad; u32 buff; } SourceData;
typedef u32 (*SourceCallback)(void*);
typedef struct { SourceCallback flow, display; } SourceKind;
static SourceData original_data[] = {{1,0,0},{2,0,9},{3,0,0},{4,0,0},{0,0,0}};
static SourceKind original_kinds[5];
static u32 read_word(const void *ptr, unsigned off)
{ const u8 *a=ptr; return (u32)a[off]<<24|(u32)a[off+1]<<16|(u32)a[off+2]<<8|a[off+3]; }
static void write_word(void *ptr,unsigned off,u32 v)
{ u8 *a=ptr;a[off]=(u8)(v>>24);a[off+1]=(u8)(v>>16);a[off+2]=(u8)(v>>8);a[off+3]=(u8)v; }
u32 fightActionBiosGetKind(void *p) { const u8 *a=p;return (u32)a[0]<<8|a[1]; }
void fightActionBiosSetKind(void *p,u32 v) {u8 *a=p;a[0]=(u8)(v>>8);a[1]=(u8)v;}
void fightActionBiosSetFifoBanme(void *p,s32 v){write_word(p,28,(u32)v);}
void fightActionBiosSetDispBuff(void *p,u32 i,u32 v){write_word(p,32+4*i,v);}
void fightActionBiosSetBuff(void *p,u32 v){write_word(p,4,v);}
s32 fightActionBiosGetBuff(void *p){return (s32)read_word(p,4);}
void fightActionBiosSetBuffDataId(void *p,u32 v){write_word(p,16,v);}
void fightActionBiosSetBuffDataPtr(void *p,void *v){CHECK(v==NULL);write_word(p,12,0);}
void fightActionBiosSetMotoFightActionDataPtr(void *p,void *v){write_word(p,24,(u32)(uintptr_t)v);}
void fightActionBiosSetActorFightTargetPtr(void *p,void *v){write_word(p,20,(u32)(uintptr_t)v);}
void fightActionBiosSetFightActionDataPtr(void *p,void *v)
{write_word(p,8,v?(u32)((SourceData*)v-original_data)+1:0);}
void *fightActionBiosGetFightActionDataPtr(void *p)
{u32 id=read_word(p,8);CHECK(id<=5);return id?original_data+id-1:NULL;}
u32 fightActionDataBiosGetKind(void *p){return ((SourceData*)p)->kind;}
s32 fightActionDataBiosGetBuff(void *p){return (s32)((SourceData*)p)->buff;}
void *fightActionKindDataBiosGetPtr(u32 kind){CHECK(kind<5);return original_kinds+kind;}
SourceCallback fightActionKindDataBiosGetFlowFuncPtr(void *p){return ((SourceKind*)p)->flow;}
SourceCallback fightActionKindDataBiosGetDispFuncPtr(void *p){return ((SourceKind*)p)->display;}
void fn_8020D968(void *dst,void *src){memcpy(dst,src,48);}

#include "battle_action_original.inc"
FightActionFifoEntry lbl_8046D790[32];
u32 lbl_8047B5E8,lbl_8047B5EC;

typedef struct {u32 flow_result,display_result;unsigned flow_calls,display_calls;u32 events[512];unsigned n;} Calls;
static Calls source_calls;
static u32 mutate(Calls *calls,void *action,int display)
{
    CHECK(calls->n<512);
    calls->events[calls->n++]=(display?0x10000:0)|fightActionBiosGetKind(action);
    write_word(action,12,read_word(action,12)+0x01020304);
    write_word(action,44,0x89abcdef);
    if(display){++calls->display_calls;return calls->display_result;}
    ++calls->flow_calls;return calls->flow_result;
}
static u32 original_flow(void *a){return mutate(&source_calls,a,0);}
static u32 original_display(void *a){return mutate(&source_calls,a,1);}
static u32 candidate_flow(void *p,C3BattleAction *a){return mutate(p,a,0);}
static u32 candidate_display(void *p,C3BattleAction *a){return mutate(p,a,1);}
static C3BattleActionData data[]={{1,1,0},{2,2,9},{3,3,0},{4,4,0},{5,0,0}};
static C3BattleActionKind kinds[]={{1,0,0},{2,10,20},{3,10,20},{4,10,20}};
static C3BattleActionBinding bindings[2];
static C3BattleActionRuntime runtime={data,5,kinds,4,bindings,2};
static Calls calls;

static void reset(C3BattleActionQueue *q)
{
    memset(q,0xa5,sizeof *q);memset(lbl_8046D790,0xa5,sizeof lbl_8046D790);
    c3_battle_action_queue_init(q);fightActionFifoInit();
    memset(&calls,0,sizeof calls);memset(&source_calls,0,sizeof source_calls);
    calls.flow_result=source_calls.flow_result=1;
    calls.display_result=source_calls.display_result=1;
    bindings[0]=(C3BattleActionBinding){10,candidate_flow,&calls};
    bindings[1]=(C3BattleActionBinding){20,candidate_display,&calls};
    original_kinds[1]=(SourceKind){NULL,NULL};
    for(unsigned i=2;i<5;++i)original_kinds[i]=(SourceKind){original_flow,original_display};
}
static void same(const C3BattleActionQueue *q)
{
    CHECK(q->write_index==lbl_8047B5E8);CHECK(q->read_index==lbl_8047B5EC);
    CHECK(memcmp(q->slots,lbl_8046D790,sizeof q->slots)==0);
    CHECK(memcmp(&calls,&source_calls,sizeof calls)==0);
}
static C3BattleAction action(unsigned kind)
{
    C3BattleAction a;memset(&a,0x5a,sizeof a);
    c3_battle_action_set_kind(&a,(u16)kind);c3_battle_action_set_word(&a,8,kind);
    return a;
}
static void flow_pair(C3BattleActionQueue *q,unsigned kind,u32 value)
{
    C3BattleAction a=action(kind);
    FightActionFifoEntry b;memcpy(&b,&a,sizeof b);
    calls.flow_result=source_calls.flow_result=value;
    C3BattleActionResult r=c3_battle_action_flow(q,&a,&runtime);
    u32 original=fightActionFlowFifo(&b);
    CHECK(r.status==C3_ACTION_OK);CHECK(r.source_result==original);
    CHECK(memcmp(&a,&b,sizeof a)==0);same(q);
}
static void display_pair(C3BattleActionQueue *q,u32 value)
{
    calls.display_result=source_calls.display_result=value;
    C3BattleActionResult r=c3_battle_action_display(q,&runtime,32);
    fightActionDispFifoAll();CHECK(r.status==C3_ACTION_OK);same(q);
}
static void differential(void)
{
    C3BattleActionQueue q;reset(&q);same(&q);
    CHECK(q.slots[0].bytes[2]==0xa5&&q.slots[31].bytes[3]==0xa5);
    for(unsigned i=0;i<31;++i)flow_pair(&q,2,1);
    CHECK(c3_battle_action_queue_count(&q)==31);
    unsigned before=calls.flow_calls;flow_pair(&q,2,1);CHECK(calls.flow_calls==before);
    display_pair(&q,2);CHECK(c3_battle_action_queue_count(&q)==30);
    flow_pair(&q,3,0x101);CHECK(q.write_index==0); /* low byte controls success */
    display_pair(&q,1);CHECK(c3_battle_action_queue_count(&q)==0);
    /* Non-success removes the oldest record, not the newly appended tail. */
    flow_pair(&q,1,1);flow_pair(&q,2,2);CHECK(c3_battle_action_queue_count(&q)==1);
    CHECK(c3_battle_action_kind(&q.slots[q.read_index])==2);
    display_pair(&q,3);CHECK(c3_battle_action_queue_count(&q)==0);
    /* Invalid actions follow original result0/head-drop behavior. */
    flow_pair(&q,1,1);flow_pair(&q,0,1);display_pair(&q,1);
    reset(&q);
    u32 rng=0x92ce4a12;
    for(unsigned i=0;i<4000;++i){
        rng=rng*1664525u+1013904223u;
        if(i%40==0){display_pair(&q,1);calls.n=source_calls.n=0;}
        if(rng&1)flow_pair(&q,1+(rng>>3)%4,(rng>>7)%3==0?2:0x101);
        else display_pair(&q,(rng>>8)%3==0?2:1);
    }
    reset(&q);
    for(unsigned i=0;i<50;++i){
        C3BattleAction a=action(4);
        FightActionFifoEntry b;memcpy(&b,&a,sizeof b);
        unsigned kind=i%6;u32 buff=kind==2?9:0;
        C3BattleActionResult r=c3_battle_action_create_flow(&q,&a,0x1234,0x5678,(u16)kind,buff,&runtime);
        u32 original=fightActionCreateAndFlowFifo(&b,(void*)(uintptr_t)0x1234,(void*)(uintptr_t)0x5678,kind,buff,original_data);
        CHECK(r.status==C3_ACTION_OK&&r.source_result==original);
        CHECK(memcmp(&a,&b,48)==0);same(&q);
        if(i%5==0)display_pair(&q,2);
    }
}
static void rejection_and_budget(void)
{
    C3BattleActionQueue q,other;reset(&q);memset(&other,0,sizeof other);c3_battle_action_queue_init(&other);
    C3BattleAction a=action(2);C3BattleActionQueue saved=q;
    C3BattleActionRuntime missing=runtime;missing.binding_count=0;
    CHECK(c3_battle_action_flow(&q,&a,&missing).status==C3_ACTION_UNIMPLEMENTED);
    CHECK(memcmp(&q,&saved,sizeof q)==0);CHECK(calls.flow_calls==0);
    missing=runtime;missing.kind_count=0;
    CHECK(c3_battle_action_flow(&q,&a,&missing).status==C3_ACTION_UNKNOWN_KIND);
    missing=runtime;missing.data_count=0;
    CHECK(c3_battle_action_flow(&q,&a,&missing).status==C3_ACTION_UNKNOWN_DATA);
    CHECK(c3_battle_action_flow(&q,&a,&runtime).status==C3_ACTION_OK);
    saved=q;missing=runtime;missing.binding_count=0;
    CHECK(c3_battle_action_display(&q,&missing,10).status==C3_ACTION_UNIMPLEMENTED);
    CHECK(memcmp(&q,&saved,sizeof q)==0);
    CHECK(c3_battle_action_display(&q,&runtime,0).status==C3_ACTION_BUDGET);
    CHECK(memcmp(&q,&saved,sizeof q)==0);
    CHECK(c3_battle_action_display(&q,&runtime,1).source_result==3);
    CHECK(c3_battle_action_queue_count(&q)==0&&c3_battle_action_queue_count(&other)==0);
    /* NULL callbacks are a known source completion, not an unknown ref. */
    a=action(1);CHECK(c3_battle_action_flow(&other,&a,&runtime).source_result==1);
    CHECK(c3_battle_action_queue_count(&other)==1&&c3_battle_action_queue_count(&q)==0);
    saved=other;CHECK(c3_battle_action_display(&other,&runtime,0).status==C3_ACTION_BUDGET);
    CHECK(memcmp(&other,&saved,sizeof other)==0);
    CHECK(c3_battle_action_display(&other,&runtime,1).source_result==3);
    /* Missing sentinel and malformed queue are bounded argument errors. */
    missing=runtime;missing.data_count=4;
    CHECK(c3_battle_action_create_flow(&q,&a,0,0,1,0,&missing).status==C3_ACTION_BAD_ARGUMENT);
    q.read_index=32;CHECK(c3_battle_action_flow(&q,&a,&runtime).status==C3_ACTION_BAD_ARGUMENT);
    CHECK(c3_battle_action_word(NULL,4)==0&&c3_battle_action_word(&a,47)==0);
}

static void ambiguous_references(void)
{
    C3BattleActionQueue q;reset(&q);C3BattleAction a=action(2),before=a;
    C3BattleActionQueue saved=q;
    C3BattleActionData duplicate_data[]={{2,2,9},{2,3,0},{5,0,0}};
    C3BattleActionRuntime bad=runtime;bad.data=duplicate_data;bad.data_count=3;
    CHECK(c3_battle_action_flow(&q,&a,&bad).status==C3_ACTION_AMBIGUOUS_REFERENCE);
    CHECK(memcmp(&q,&saved,sizeof q)==0&&memcmp(&a,&before,48)==0);
    CHECK(c3_battle_action_create_flow(&q,&a,0,0,3,0,&bad).status==C3_ACTION_AMBIGUOUS_REFERENCE);
    CHECK(memcmp(&q,&saved,sizeof q)==0&&memcmp(&a,&before,48)==0);
    C3BattleActionKind duplicate_kinds[]={{2,10,20},{2,0,0}};
    bad=runtime;bad.kinds=duplicate_kinds;bad.kind_count=2;
    CHECK(c3_battle_action_flow(&q,&a,&bad).status==C3_ACTION_AMBIGUOUS_REFERENCE);
    CHECK(memcmp(&q,&saved,sizeof q)==0&&memcmp(&a,&before,48)==0);
    C3BattleActionBinding duplicate_bindings[]={{10,NULL,NULL},{10,candidate_flow,&calls},{20,candidate_display,&calls}};
    bad=runtime;bad.bindings=duplicate_bindings;bad.binding_count=3;
    CHECK(c3_battle_action_flow(&q,&a,&bad).status==C3_ACTION_AMBIGUOUS_REFERENCE);
    CHECK(memcmp(&q,&saved,sizeof q)==0&&memcmp(&a,&before,48)==0);
    CHECK(calls.n==0);
    CHECK(c3_battle_action_flow(&q,&a,&runtime).status==C3_ACTION_OK);
    saved=q;
    duplicate_bindings[0]=(C3BattleActionBinding){20,NULL,NULL};
    CHECK(c3_battle_action_display(&q,&bad,1).status==C3_ACTION_AMBIGUOUS_REFERENCE);
    CHECK(memcmp(&q,&saved,sizeof q)==0&&calls.display_calls==0);
    CHECK(c3_battle_action_flow(&q,&q.slots[0],&runtime).status==C3_ACTION_BAD_ARGUMENT);
    CHECK(c3_battle_action_create_flow(&q,&q.slots[0],0,0,1,0,&runtime).status==C3_ACTION_BAD_ARGUMENT);
    CHECK(memcmp(&q,&saved,sizeof q)==0);
}

typedef struct {C3BattleActionQueue *queue;const C3BattleActionRuntime *runtime;unsigned calls;} Producer;
static u32 produce(void *user,C3BattleAction *current)
{
    Producer *p=user;C3BattleAction next=action(4);++p->calls;
    write_word(current,44,0xfeedface); /* only the popped local copy changes */
    CHECK(c3_battle_action_flow(p->queue,&next,p->runtime).source_result==1);
    return 1;
}
static void bounded_producer(void)
{
    C3BattleActionQueue q;memset(&q,0,sizeof q);c3_battle_action_queue_init(&q);
    C3BattleActionKind kind={4,0,30};Producer producer={&q,NULL,0};
    C3BattleActionBinding binding={30,produce,&producer};
    C3BattleActionRuntime r={data,5,&kind,1,&binding,1};producer.runtime=&r;
    C3BattleAction a=action(4);CHECK(c3_battle_action_flow(&q,&a,&r).source_result==1);
    C3BattleActionResult got=c3_battle_action_display(&q,&r,7);
    CHECK(got.status==C3_ACTION_BUDGET&&got.dispatched==7&&producer.calls==7);
    CHECK(c3_battle_action_queue_count(&q)==1);
    CHECK(c3_battle_action_word(&q.slots[q.read_index],44)==0x5a5a5a5a);
}
int main(void)
{
    CHECK(sizeof(C3BattleAction)==48);differential();rejection_and_budget();
    ambiguous_references();bounded_producer();
    printf("battle action: %u checks passed against compiled pinned queue; no combat backend\n",checks);
    return 0;
}
