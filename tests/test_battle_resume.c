#include "colosseum3ds/battle_resume.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"resume:%d: %s\n",__LINE__,#x);exit(1);} }while(0)
typedef struct {
    unsigned starts,resumes,aborts,waits;
    uint32_t result;
    int error,invalid_wait;
    C3BattleActionFrame *frame;
} Fixture;
static uint32_t sync_call(void *u,C3BattleAction *a)
{
    Fixture *x=u;++x->starts;
    c3_battle_action_set_word(a,C3_ACTION_DISPLAY,0x11223344);
    return x->result;
}
static C3BattleRoutineReply async_start(void *u,C3BattleAction *a)
{
    Fixture *x=u;++x->starts;
    c3_battle_action_set_word(a,C3_ACTION_DISPLAY,0x11223344);
    if(x->frame){
        CHECK(c3_battle_frame_step(x->frame).status==C3_ACTION_BAD_ARGUMENT);
        CHECK(!c3_battle_frame_reset(x->frame));
        CHECK(c3_battle_frame_abort(x->frame).status==C3_ACTION_BAD_ARGUMENT);
    }
    C3BattleRoutineReply r={x->waits?C3_BATTLE_ROUTINE_YIELD:C3_BATTLE_ROUTINE_RETURN,
        x->waits?C3_BATTLE_WAIT_RESOURCE:C3_BATTLE_WAIT_NONE,x->result};
    return r;
}
static C3BattleRoutineReply async_resume(void *u,C3BattleAction *a)
{
    Fixture *x=u;++x->resumes;
    CHECK(c3_battle_action_word(a,C3_ACTION_DISPLAY)==0x11223344);
    if(x->error){C3BattleRoutineReply r={C3_BATTLE_ROUTINE_ERROR,C3_BATTLE_WAIT_NONE,0};return r;}
    if(x->invalid_wait){C3BattleRoutineReply r={C3_BATTLE_ROUTINE_YIELD,C3_BATTLE_WAIT_NONE,0};return r;}
    if(x->resumes<x->waits){C3BattleRoutineReply r={C3_BATTLE_ROUTINE_YIELD,C3_BATTLE_WAIT_ANIMATION,0};return r;}
    C3BattleRoutineReply r={C3_BATTLE_ROUTINE_RETURN,C3_BATTLE_WAIT_NONE,x->result};return r;
}
static void async_abort(void *u,C3BattleAction *a)
{ Fixture *x=u;++x->aborts;CHECK(c3_battle_action_word(a,C3_ACTION_DISPLAY)==0x11223344); }
static C3BattleAction action(unsigned v)
{
    C3BattleAction a;memset(&a,0xa5,sizeof a);c3_battle_action_init(&a);
    c3_battle_action_set_kind(&a,1);c3_battle_action_set_word(&a,C3_ACTION_DATA,100);
    c3_battle_action_set_word(&a,C3_ACTION_ACTOR,v);return a;
}
static const C3BattleActionData data[]={{100,1,0},{0,0,0}};
static const C3BattleActionKind kinds[]={{1,7,7}};
static void sync_parity(void)
{
    const uint32_t values[]={0,1,2,3,4,0x101,0x102,0xffffffffu};
    for(unsigned ri=0;ri<32;++ri)for(unsigned n=0;n<32;++n)
    for(unsigned val=0;val<sizeof values/sizeof *values;++val){
        C3BattleActionQueue q1={0},q2;C3BattleAction a1=action(55),a2=a1;
        c3_battle_action_queue_init(&q1);q1.read_index=ri;q1.write_index=(ri+n)%32;
        for(unsigned i=0;i<32;++i)q1.slots[i]=action(i);
        q2=q1;
        Fixture x1={0},x2={0};x1.result=x2.result=values[val];
        C3BattleActionBinding b1={7,sync_call,&x1},b2={7,sync_call,&x2};
        C3BattleActionRuntime r1={data,2,kinds,1,&b1,1},r2={data,2,kinds,1,&b2,1};
        C3BattleResumeRuntime rr={&r2,NULL,0};C3BattleActionFrame frame={0};
        C3BattleActionResult old=c3_battle_action_flow(&q1,&a1,&r1);
        C3BattleFrameResult now=c3_battle_frame_begin_flow(&frame,&q2,&a2,&rr);
        CHECK(now.status==C3_ACTION_OK);
        if(now.state==C3_BATTLE_FRAME_READY)now=c3_battle_frame_step(&frame);
        CHECK(now.state==C3_BATTLE_FRAME_RETURNED);CHECK(old.source_result==now.source_result);
        CHECK(!memcmp(&q1,&q2,sizeof q1));CHECK(!memcmp(&a1,&a2,sizeof a1));CHECK(x1.starts==x2.starts);
        unsigned calls=x2.starts;CHECK(c3_battle_frame_step(&frame).source_result==now.source_result);CHECK(x2.starts==calls);
        CHECK(c3_battle_frame_reset(&frame));
        old=c3_battle_action_display(&q1,&r1,32);
        size_t completed=99;now=c3_battle_frame_drain(&frame,&q2,&rr,32,&completed);
        CHECK(now.state==C3_BATTLE_FRAME_RETURNED);CHECK(old.source_result==now.source_result);
        CHECK(old.dispatched==completed);CHECK(!memcmp(&q1,&q2,sizeof q1));CHECK(x1.starts==x2.starts);
    }
}
static void waiting_and_failure(void)
{
    for(unsigned flow=0;flow<2;++flow)for(unsigned value=0;value<5;++value){
        Fixture x={0};x.waits=7;x.result=value;
        C3BattleRoutineBinding binding={7,async_start,async_resume,async_abort,&x};
        C3BattleActionRuntime source={data,2,kinds,1,NULL,0};
        C3BattleResumeRuntime runtime={&source,&binding,1};
        C3BattleActionQueue q={0};c3_battle_action_queue_init(&q);
        C3BattleAction a=action(42),initial=a;C3BattleActionFrame frame={0};x.frame=&frame;
        if(!flow){q.slots[0]=a;q.write_index=1;}
        C3BattleFrameResult out=flow?c3_battle_frame_begin_flow(&frame,&q,&a,&runtime):
                                     c3_battle_frame_begin_display(&frame,&q,&runtime);
        CHECK(out.state==C3_BATTLE_FRAME_READY);CHECK(x.starts==0);
        unsigned read=q.read_index,write=q.write_index;
        for(unsigned i=0;i<7;++i){
            out=c3_battle_frame_step(&frame);CHECK(out.state==C3_BATTLE_FRAME_WAITING);
            CHECK(out.status==C3_ACTION_OK);CHECK(q.read_index==read && q.write_index==write);
            CHECK(x.starts==1);CHECK(!c3_battle_frame_reset(&frame));
            CHECK(c3_battle_frame_begin_display(&frame,&q,&runtime).status==C3_ACTION_BAD_ARGUMENT);
        }
        out=c3_battle_frame_step(&frame);CHECK(out.state==C3_BATTLE_FRAME_RETURNED);
        CHECK(out.source_result==value);CHECK(x.starts==1 && x.resumes==7);
        if(flow){CHECK(c3_battle_action_word(&a,C3_ACTION_DISPLAY)==0x11223344);
            CHECK(q.read_index==(value==1?0u:1u));
        }else{CHECK(!memcmp(&q.slots[0],&initial,sizeof initial));CHECK(q.read_index==1);}
        CHECK(c3_battle_frame_abort(&frame).state==C3_BATTLE_FRAME_RETURNED);CHECK(!x.aborts);
    }
    Fixture x={0};x.waits=10;
    C3BattleRoutineBinding binding={7,async_start,async_resume,async_abort,&x};
    C3BattleActionRuntime source={data,2,kinds,1,NULL,0};C3BattleResumeRuntime runtime={&source,&binding,1};
    C3BattleActionQueue q={0};c3_battle_action_queue_init(&q);q.slots[0]=action(1);q.write_index=1;
    C3BattleActionFrame f={0};CHECK(c3_battle_frame_begin_display(&f,&q,&runtime).state==C3_BATTLE_FRAME_READY);
    CHECK(c3_battle_frame_step(&f).state==C3_BATTLE_FRAME_WAITING);
    /* The popped slot can be overwritten without changing the retained local. */
    memset(&q.slots[0],0xcc,sizeof q.slots[0]);
    CHECK(c3_battle_frame_step(&f).state==C3_BATTLE_FRAME_WAITING);
    CHECK(c3_battle_action_word(&f.action,C3_ACTION_ACTOR)==1);
    x.error=1;CHECK(c3_battle_frame_step(&f).state==C3_BATTLE_FRAME_FAILED);
    unsigned resumes=x.resumes;CHECK(c3_battle_frame_step(&f).state==C3_BATTLE_FRAME_FAILED);
    CHECK(x.resumes==resumes);CHECK(!c3_battle_frame_reset(&f));
    CHECK(c3_battle_frame_abort(&f).state==C3_BATTLE_FRAME_ABORTED);CHECK(x.aborts==1);
    CHECK(c3_battle_frame_abort(&f).state==C3_BATTLE_FRAME_ABORTED);CHECK(x.aborts==1);
    CHECK(q.read_index==1 && q.write_index==1);CHECK(c3_battle_frame_reset(&f));
    q.slots[1]=action(2);q.write_index=2;x.error=0;x.invalid_wait=1;
    CHECK(c3_battle_frame_begin_display(&f,&q,&runtime).state==C3_BATTLE_FRAME_READY);
    CHECK(c3_battle_frame_step(&f).state==C3_BATTLE_FRAME_WAITING);
    CHECK(c3_battle_frame_step(&f).status==C3_ACTION_BAD_ARGUMENT);
    CHECK(c3_battle_frame_abort(&f).state==C3_BATTLE_FRAME_ABORTED);CHECK(x.aborts==2);
}
static void admission_and_budget(void)
{
    C3BattleActionQueue q={0};c3_battle_action_queue_init(&q);C3BattleAction a=action(3);
    C3BattleActionRuntime source={data,2,kinds,1,NULL,0};C3BattleResumeRuntime runtime={&source,NULL,0};
    C3BattleActionFrame f={0};C3BattleActionQueue before=q;
    CHECK(c3_battle_frame_begin_flow(&f,&q,&a,&runtime).status==C3_ACTION_UNIMPLEMENTED);
    CHECK(!memcmp(&before,&q,sizeof q));CHECK(c3_battle_frame_reset(&f));
    q.slots[0]=a;q.write_index=1;before=q;
    CHECK(c3_battle_frame_begin_display(&f,&q,&runtime).status==C3_ACTION_UNIMPLEMENTED);
    CHECK(!memcmp(&before,&q,sizeof q));CHECK(c3_battle_frame_reset(&f));
    Fixture x={0};x.result=1;
    C3BattleRoutineBinding asyncs[]={ {7,async_start,async_resume,async_abort,&x},
                                     {7,async_start,async_resume,async_abort,&x} };
    runtime.routines=asyncs;runtime.routine_count=2;
    CHECK(c3_battle_frame_begin_display(&f,&q,&runtime).status==C3_ACTION_AMBIGUOUS_REFERENCE);
    CHECK(q.read_index==0);CHECK(c3_battle_frame_reset(&f));
    runtime.routine_count=1;C3BattleActionBinding sync={7,sync_call,&x};source.bindings=&sync;source.binding_count=1;
    CHECK(c3_battle_frame_begin_display(&f,&q,&runtime).status==C3_ACTION_AMBIGUOUS_REFERENCE);
    CHECK(c3_battle_frame_reset(&f));source.bindings=NULL;source.binding_count=0;
    asyncs[0].abort=NULL;
    CHECK(c3_battle_frame_begin_display(&f,&q,&runtime).status==C3_ACTION_UNIMPLEMENTED);
    CHECK(c3_battle_frame_reset(&f));asyncs[0].abort=async_abort;
    size_t completed=99;before=q;
    CHECK(c3_battle_frame_drain(&f,&q,&runtime,0,&completed).state==C3_BATTLE_FRAME_IDLE);
    CHECK(completed==0);CHECK(!memcmp(&before,&q,sizeof q));CHECK(x.starts==0);
    q.slots[1]=a;q.write_index=2;
    CHECK(c3_battle_frame_drain(&f,&q,&runtime,1,&completed).state==C3_BATTLE_FRAME_RETURNED);
    CHECK(completed==1 && q.read_index==1 && x.starts==1);
    CHECK(c3_battle_frame_drain(&f,&q,&runtime,1,&completed).state==C3_BATTLE_FRAME_RETURNED);
    CHECK(completed==1 && q.read_index==2 && x.starts==2);
    CHECK(c3_battle_frame_drain(&f,&q,&runtime,1,&completed).source_result==3);
    CHECK(completed==0 && x.starts==2);
    CHECK(c3_battle_frame_reset(&f));
    C3BattleActionKind nullkind={1,0,0};source.kinds=&nullkind;
    q.slots[2]=a;q.write_index=3;
    CHECK(c3_battle_frame_drain(&f,&q,&runtime,2,&completed).source_result==3);
    CHECK(completed==1 && x.starts==2);
    CHECK(c3_battle_frame_reset(&f));
    q.slots[3]=a;c3_battle_action_set_kind(&q.slots[3],0);q.write_index=4;
    CHECK(c3_battle_frame_drain(&f,&q,&runtime,2,&completed).source_result==0);
    CHECK(completed==1 && x.starts==2);
    CHECK(c3_battle_frame_step(NULL).status==C3_ACTION_BAD_ARGUMENT);
    CHECK(!c3_battle_frame_reset(NULL));
}
static void create_flow_parity(void)
{
    for(unsigned n=0;n<32;++n)for(unsigned wanted=0;wanted<3;++wanted)
    for(unsigned result=0;result<5;++result){
        C3BattleActionQueue a={0},b;C3BattleAction x=action(71),y=x;
        c3_battle_action_queue_init(&a);a.read_index=20;a.write_index=(20+n)%32;
        for(unsigned i=0;i<32;++i)a.slots[i]=action(i);
        b=a;Fixture f1={0},f2={0};f1.result=f2.result=result;
        C3BattleActionBinding b1={7,sync_call,&f1},b2={7,sync_call,&f2};
        C3BattleActionRuntime r1={data,2,kinds,1,&b1,1},r2={data,2,kinds,1,&b2,1};
        C3BattleResumeRuntime rr={&r2,NULL,0};C3BattleActionFrame frame={0};
        C3BattleActionResult old=c3_battle_action_create_flow(&a,&x,11,22,(uint16_t)wanted,0,&r1);
        C3BattleFrameResult now=c3_battle_frame_create_flow(&frame,&b,&y,11,22,(uint16_t)wanted,0,&rr);
        if(now.state==C3_BATTLE_FRAME_READY)now=c3_battle_frame_step(&frame);
        CHECK(now.status==old.status && now.source_result==old.source_result);
        CHECK(!memcmp(&a,&b,sizeof a) && !memcmp(&x,&y,sizeof x));
        CHECK(f1.starts==f2.starts);
    }
    C3BattleActionQueue queue={0};C3BattleAction out=action(90),saved=out;
    C3BattleActionFrame frame={0};Fixture fixture={0};fixture.result=1;fixture.waits=2;
    C3BattleActionRuntime source={data,2,kinds,1,NULL,0};
    C3BattleRoutineBinding routine={7,async_start,async_resume,async_abort,&fixture};
    C3BattleResumeRuntime runtime={&source,&routine,1};
    CHECK(c3_battle_frame_create_flow(&frame,&queue,&out,12,13,1,0,&runtime).state==C3_BATTLE_FRAME_READY);
    CHECK(!memcmp(&out,&saved,sizeof out));
    CHECK(c3_battle_frame_step(&frame).state==C3_BATTLE_FRAME_WAITING);
    CHECK(!memcmp(&out,&saved,sizeof out));
    CHECK(c3_battle_frame_step(&frame).state==C3_BATTLE_FRAME_WAITING);
    CHECK(c3_battle_frame_step(&frame).source_result==1);
    CHECK(c3_battle_action_word(&out,C3_ACTION_ACTOR)==13);
    CHECK(c3_battle_action_word(&out,C3_ACTION_MOTO)==12);
    CHECK(c3_battle_action_word(&out,C3_ACTION_DISPLAY)==0x11223344);
    CHECK(!memcmp(&queue.slots[0],&out,sizeof out));
    CHECK(c3_battle_frame_reset(&frame));source.data_count=1;
    saved=out;C3BattleActionQueue old=queue;
    CHECK(c3_battle_frame_create_flow(&frame,&queue,&out,12,13,1,0,&runtime).status==C3_ACTION_BAD_ARGUMENT);
    CHECK(!memcmp(&queue,&old,sizeof old) && !memcmp(&out,&saved,sizeof out));
}
int main(void)
{
    sync_parity();waiting_and_failure();admission_and_budget();create_flow_parity();
    printf("Battle resumable actions: %u checks; no scene or story-result binding.\n",checks);return 0;
}
