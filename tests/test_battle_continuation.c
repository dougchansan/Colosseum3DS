/* Integrated continuation tests. Resource/model services are deliberate test
 * doubles: this is not evidence that native battle entry loads real assets. */
#include "colosseum3ds/battle_sequence.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { \
    fprintf(stderr,"continuation:%d: %s\n",__LINE__,#x); exit(1); \
} } while (0)

typedef struct {
    unsigned requested, ready, animation_ready, starts, resumes, aborts;
    unsigned queries, allocations, loads, hides, releases, after_load;
    unsigned load_error;
    uint32_t owned;
    C3BattleSequenceFrame sequence;
} Driver;
static const C3BattleSequenceResource resources[]={{0,0},{0x9d,0x030c1e00}};
static int32_t resource_state(void *user,uint32_t group)
{
    Driver *d=user; CHECK(group==resources[1].group); ++d->queries;
    return d->ready ? 0 : d->requested ? 1 : -1;
}
static int request_group(void *user,uint32_t group)
{ Driver *d=user; CHECK(group==resources[1].group); ++d->requested; return 1; }
static uint32_t find_resource(void *user,uint32_t group,uint32_t member)
{ Driver *d=user; CHECK(group==resources[1].group && member==resources[1].member); return d->ready?72:0; }
static uint32_t allocate_sequence(void *user)
{ Driver *d=user; CHECK(!d->owned); ++d->allocations; d->owned=91; return d->owned; }
static int set_fields(void *user,uint32_t seq,uint16_t index,uint16_t group,uint8_t flags)
{ Driver *d=user; CHECK(seq==d->owned && index==1 && group==resources[1].group && !flags); return 1; }
static int load_sequence(void *user,uint32_t seq,uint32_t resource)
{ Driver *d=user; CHECK(seq==d->owned && resource==72); ++d->loads; return d->load_error?-1:1; }
static void release_sequence(void *user,uint32_t seq)
{ Driver *d=user; CHECK(seq==d->owned); d->owned=0; ++d->releases; }
static int hide_sequence(void *user,uint32_t seq)
{ Driver *d=user; CHECK(seq==d->owned); ++d->hides; return 1; }
static void negative(void *user,uint32_t group)
{ (void)user; CHECK(group==resources[1].group); }
static const C3BattleSequenceServices services={resource_state,request_group,
    find_resource,allocate_sequence,set_fields,load_sequence,release_sequence,
    hide_sequence,negative};

static C3BattleRoutineReply after_sequence(Driver *d,C3BattleAction *a,
                                          C3BattleRoutineReply r)
{
    if(r.state!=C3_BATTLE_ROUTINE_RETURN)return r;
    CHECK(r.value==91 && r.value==d->owned);
    if(!d->after_load){
        ++d->after_load; c3_battle_action_set_word(a,C3_ACTION_DISPLAY,r.value);
    }
    C3BattleRoutineReply result={d->animation_ready?C3_BATTLE_ROUTINE_RETURN:
        C3_BATTLE_ROUTINE_YIELD,d->animation_ready?C3_BATTLE_WAIT_NONE:
        C3_BATTLE_WAIT_ANIMATION,1}; return result;
}
static C3BattleRoutineReply start(void *user,C3BattleAction *a)
{
    Driver *d=user; ++d->starts;
    CHECK(c3_battle_action_word(a,C3_ACTION_ACTOR)==55);
    return after_sequence(d,a,c3_battle_sequence_begin(&d->sequence,1,resources,2,
                                                    &services,d,1));
}
static C3BattleRoutineReply resume(void *user,C3BattleAction *a)
{
    Driver *d=user; ++d->resumes;
    return after_sequence(d,a,c3_battle_sequence_step(&d->sequence));
}
static void abort_action(void *user,C3BattleAction *a)
{
    Driver *d=user; (void)a; ++d->aborts;
    (void)c3_battle_sequence_cancel(&d->sequence);
    /* This fixture has no GPU work; real scene owners must drain first. */
    if(d->owned)release_sequence(d,d->owned);
}
static const C3BattleActionData data[]={{10,1,0},{0,0,0}};
static const C3BattleActionKind kinds[]={{1,20,20}};
static C3BattleAction make_action(void)
{
    C3BattleAction a={0}; c3_battle_action_init(&a);
    c3_battle_action_set_kind(&a,1); c3_battle_action_set_word(&a,C3_ACTION_DATA,10);
    c3_battle_action_set_word(&a,C3_ACTION_ACTOR,55); return a;
}
static void wait_path(void)
{
    for(unsigned flow=0;flow<2;++flow)for(unsigned resource_ticks=0;resource_ticks<6;++resource_ticks)
    for(unsigned animation_ticks=0;animation_ticks<6;++animation_ticks){
        Driver d={0}; C3BattleActionFrame frame={0}; C3BattleActionQueue queue={0};
        C3BattleAction a=make_action(),original=a;
        C3BattleRoutineBinding binding={20,start,resume,abort_action,&d};
        C3BattleActionRuntime source={data,2,kinds,1,NULL,0};
        C3BattleResumeRuntime runtime={&source,&binding,1};
        if(!flow){queue.slots[0]=a;queue.write_index=1;}
        C3BattleFrameResult r=flow?c3_battle_frame_create_flow(&frame,&queue,&a,0,55,1,0,&runtime):
            c3_battle_frame_begin_display(&frame,&queue,&runtime);
        CHECK(r.state==C3_BATTLE_FRAME_READY);
        for(unsigned i=0;i<resource_ticks;++i){
            unsigned before=d.queries;
            r=c3_battle_frame_step(&frame);
            CHECK(r.state==C3_BATTLE_FRAME_WAITING && r.wait==C3_BATTLE_WAIT_RESOURCE);
            CHECK(d.starts==1 && d.requested==1 && !d.allocations);
            if(i)CHECK(d.queries==before+1);
            CHECK(!memcmp(&a,&original,sizeof a));
        }
        d.ready=1;
        for(unsigned i=0;i<animation_ticks;++i){
            unsigned before=d.queries;
            r=c3_battle_frame_step(&frame);
            CHECK(r.state==C3_BATTLE_FRAME_WAITING && r.wait==C3_BATTLE_WAIT_ANIMATION);
            CHECK(d.allocations==1 && d.loads==1 && d.hides==1 && d.after_load==1);
            if(i)CHECK(d.queries==before);
            CHECK(!memcmp(&a,&original,sizeof a));
        }
        d.animation_ready=1; r=c3_battle_frame_step(&frame);
        CHECK(r.state==C3_BATTLE_FRAME_RETURNED && r.source_result==1);
        CHECK(d.starts==1 && d.allocations==1 && d.loads==1 && d.hides==1);
        CHECK(d.after_load==1 && d.aborts==0 && d.releases==0);
        CHECK(queue.read_index==(flow?0u:1u) && queue.write_index==1);
        if(flow)CHECK(c3_battle_action_word(&a,C3_ACTION_DISPLAY)==91);
        else CHECK(!memcmp(&a,&original,sizeof a) && !memcmp(&queue.slots[0],&original,sizeof a));
        unsigned calls=d.resumes;
        CHECK(c3_battle_frame_step(&frame).source_result==1);
        CHECK(c3_battle_frame_abort(&frame).state==C3_BATTLE_FRAME_RETURNED);
        CHECK(d.resumes==calls && !d.aborts);
        /* Successful sequence is now owned by the enclosing session. */
        release_sequence(&d,d.owned); CHECK(d.releases==1);
    }
}
static void cancellations_and_failure(void)
{
    for(unsigned mode=0;mode<3;++mode){
        Driver d={0}; C3BattleActionFrame frame={0}; C3BattleActionQueue queue={0};
        queue.slots[0]=make_action();queue.write_index=1;
        C3BattleRoutineBinding binding={20,start,resume,abort_action,&d};
        C3BattleActionRuntime source={data,2,kinds,1,NULL,0};
        C3BattleResumeRuntime runtime={&source,&binding,1};
        CHECK(c3_battle_frame_begin_display(&frame,&queue,&runtime).state==C3_BATTLE_FRAME_READY);
        CHECK(c3_battle_frame_step(&frame).wait==C3_BATTLE_WAIT_RESOURCE);
        if(mode){
            d.ready=1;d.load_error=mode==2;
            C3BattleFrameResult r=c3_battle_frame_step(&frame);
            CHECK(r.state==(mode==2?C3_BATTLE_FRAME_FAILED:C3_BATTLE_FRAME_WAITING));
        }
        CHECK(c3_battle_frame_abort(&frame).state==C3_BATTLE_FRAME_ABORTED);
        CHECK(c3_battle_frame_abort(&frame).state==C3_BATTLE_FRAME_ABORTED);
        CHECK(d.aborts==1 && !d.owned && d.releases==(mode?1u:0u));
        CHECK(queue.read_index==1 && queue.write_index==1);
        size_t completed=99;
        CHECK(c3_battle_frame_drain(&frame,&queue,&runtime,8,&completed).state==C3_BATTLE_FRAME_ABORTED);
        CHECK(!completed && d.starts==1);
    }
}
int main(void)
{
    wait_path();cancellations_and_failure();
    printf("Action/resource/animation continuation integration: %u checks; synthetic owner services.\n",checks);
    return 0;
}
