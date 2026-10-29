/* Cooperative source lowering: sequence.c:fn_801DE418 and sequenceWaitResource;
 * fight_trainer_range_801F7F80.c:fightTrainerCreateSequence. See source manifest. */
#include "colosseum3ds/battle_sequence.h"
#include <string.h>

static C3BattleRoutineReply reply(C3BattleRoutineState state,C3BattleWaitKind wait,uint32_t value)
{ C3BattleRoutineReply r={state,wait,value};return r; }
static C3BattleRoutineReply error(void)
{ return reply(C3_BATTLE_ROUTINE_ERROR,C3_BATTLE_WAIT_NONE,0); }
static C3BattleRoutineReply waiting(void)
{ return reply(C3_BATTLE_ROUTINE_YIELD,C3_BATTLE_WAIT_RESOURCE,0); }
static C3BattleRoutineReply returned(uint32_t result)
{ return reply(C3_BATTLE_ROUTINE_RETURN,C3_BATTLE_WAIT_NONE,result); }
static int valid_services(const C3BattleSequenceServices *s,int trainer)
{
    return s && s->resource_state && s->request_group && s->find_resource &&
        s->allocate && s->set_fields && s->load && s->release &&
        s->report_negative_state && (!trainer || s->hide);
}
static C3BattleRoutineReply fail(C3BattleSequenceFrame *f)
{
    if(f->owned_sequence){f->services.release(f->user,f->owned_sequence);f->owned_sequence=0;}
    f->in_call=0;f->phase=C3_SEQUENCE_FAILED;return error();
}
C3BattleRoutineReply c3_battle_sequence_begin(C3BattleSequenceFrame *f,
    uint16_t index,const C3BattleSequenceResource *table,size_t count,
    const C3BattleSequenceServices *s,void *user,int trainer)
{
    C3BattleSequenceFrame n={0};
    if(!f || f->in_call || f->phase==C3_SEQUENCE_POLL_RESOURCE ||
       !valid_services(s,trainer) || (count && !table) || count>65536u)return error();
    n.services=*s;n.user=user;n.index=index;n.trainer=(uint8_t)(trainer!=0);
    if(!index || (size_t)index>=count){n.phase=C3_SEQUENCE_DONE;*f=n;return returned(0);}
    n.group=table[index].group;n.member=table[index].member;
    if(!n.group || !n.member){
        n.group=table[1].group;n.member=table[1].member;
        if(!n.group || !n.member){n.phase=C3_SEQUENCE_DONE;*f=n;return returned(0);}
    }
    n.phase=C3_SEQUENCE_POLL_RESOURCE;n.in_call=1;*f=n;
    /* Preserve the second query (do not collapse it to a cached state). */
    int request=f->services.resource_state(user,f->group)<0;
    if(!request && f->services.resource_state(user,f->group)==0 && f->member &&
       !f->services.find_resource(user,f->group,f->member))request=1;
    if(request && !f->services.request_group(user,f->group))return fail(f);
    f->in_call=0;return c3_battle_sequence_step(f);
}
C3BattleRoutineReply c3_battle_sequence_step(C3BattleSequenceFrame *f)
{
    if(!f || f->in_call)return error();
    if(f->phase==C3_SEQUENCE_DONE)return returned(f->result);
    if(f->phase!=C3_SEQUENCE_POLL_RESOURCE)return error();
    f->in_call=1;
    int32_t state=f->services.resource_state(f->user,f->group);
    if(state<0)f->services.report_negative_state(f->user,f->group);
    if(state){f->in_call=0;return waiting();}
    uint32_t resource=f->services.find_resource(f->user,f->group,f->member);
    if(!resource){f->phase=C3_SEQUENCE_DONE;f->in_call=0;return returned(0);}
    f->owned_sequence=f->services.allocate(f->user);
    if(!f->owned_sequence){f->phase=C3_SEQUENCE_DONE;f->in_call=0;return returned(0);}
    if(!f->services.set_fields(f->user,f->owned_sequence,f->index,(uint16_t)f->group,0))return fail(f);
    int loaded=f->services.load(f->user,f->owned_sequence,resource);
    if(loaded!=0 && loaded!=1)return fail(f);
    if(!loaded){
        f->services.release(f->user,f->owned_sequence);f->owned_sequence=0;
        f->phase=C3_SEQUENCE_DONE;f->in_call=0;return returned(0);
    }
    if(f->trainer && !f->services.hide(f->user,f->owned_sequence))return fail(f);
    f->result=f->owned_sequence;f->owned_sequence=0;
    f->phase=C3_SEQUENCE_DONE;f->in_call=0;return returned(f->result);
}
C3BattleRoutineReply c3_battle_sequence_cancel(C3BattleSequenceFrame *f)
{
    if(!f || f->in_call)return error();
    if(f->phase==C3_SEQUENCE_DONE)return returned(f->result);
    /* Allocation/load are synchronous, so a legal pending cancellation owns
     * no model or GPU work. Do not cancel a shared global archive request. */
    f->phase=C3_SEQUENCE_CANCELLED;return error();
}
