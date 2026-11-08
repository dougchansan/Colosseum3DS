/* Cooperative lowering of fightActionFlowFifo / fightActionDispFifoAll.
 * Source identities and the limits of this lowering: battle-resume.md. */
#include "colosseum3ds/battle_resume.h"
#include <string.h>

static int overlap(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x>=y ? x-y<bn : y-x<an);
}
static int active(const C3BattleActionFrame *f)
{ return f->state==C3_BATTLE_FRAME_READY || f->state==C3_BATTLE_FRAME_WAITING; }
static int queue_valid(const C3BattleActionQueue *q)
{ return q && q->write_index<C3_ACTION_SLOTS && q->read_index<C3_ACTION_SLOTS; }
static int action_valid(const C3BattleAction *a)
{ return c3_battle_action_kind(a) && c3_battle_action_word(a,C3_ACTION_DATA); }
static C3BattleFrameResult report(const C3BattleActionFrame *f)
{
    C3BattleFrameResult r={f->state,f->wait,f->status,f->source_result};return r;
}
static C3BattleFrameResult rejected(void)
{
    C3BattleFrameResult r={C3_BATTLE_FRAME_FAILED,C3_BATTLE_WAIT_NONE,
                          C3_ACTION_BAD_ARGUMENT,0};return r;
}
static int runtime_valid(const C3BattleResumeRuntime *r)
{
    if(!r || !r->source || (r->routine_count && !r->routines))return 0;
    const C3BattleActionRuntime *s=r->source;
    return (!s->data_count || s->data) && (!s->kind_count || s->kinds) &&
           (!s->binding_count || s->bindings);
}
/* This deliberately follows the existing bounded source resolver. Null is
 * legitimate only after the data/kind records were successfully resolved. */
static C3BattleActionStatus resolve(C3BattleActionFrame *f,
                                    const C3BattleResumeRuntime *r)
{
    const C3BattleActionRuntime *s=r->source;
    const C3BattleActionData *data=NULL;
    const C3BattleActionKind *kind=NULL;
    uint32_t ref=c3_battle_action_word(&f->action,C3_ACTION_DATA);
    for(size_t i=0;i<s->data_count;++i)if(ref && s->data[i].ref==ref){
        if(data)return C3_ACTION_AMBIGUOUS_REFERENCE;
        data=&s->data[i];
    }
    if(!data)return C3_ACTION_UNKNOWN_DATA;
    for(size_t i=0;i<s->kind_count;++i)if(s->kinds[i].kind==data->kind){
        if(kind)return C3_ACTION_AMBIGUOUS_REFERENCE;
        kind=&s->kinds[i];
    }
    if(!kind)return C3_ACTION_UNKNOWN_KIND;
    ref=f->mode==C3_BATTLE_FRAME_FLOW?kind->flow_ref:kind->display_ref;
    if(!ref)return C3_ACTION_OK;
    unsigned found=0;
    for(size_t i=0;i<s->binding_count;++i)if(s->bindings[i].ref==ref){
        if(found++)return C3_ACTION_AMBIGUOUS_REFERENCE;
        f->synchronous=s->bindings[i];
    }
    for(size_t i=0;i<r->routine_count;++i)if(r->routines[i].ref==ref){
        if(found++)return C3_ACTION_AMBIGUOUS_REFERENCE;
        f->routine=r->routines[i];f->asynchronous=1;
    }
    if(!found)return C3_ACTION_UNIMPLEMENTED;
    if(f->asynchronous)return f->routine.start && f->routine.resume && f->routine.abort?
        C3_ACTION_OK:C3_ACTION_UNIMPLEMENTED;
    return f->synchronous.call?C3_ACTION_OK:C3_ACTION_UNIMPLEMENTED;
}
int c3_battle_frame_reset(C3BattleActionFrame *f)
{
    if(!f || f->in_call || active(f) ||
       (f->state==C3_BATTLE_FRAME_FAILED && f->started && !f->abort_called))return 0;
    memset(f,0,sizeof *f);return 1;
}
static int admit(C3BattleActionFrame *f,C3BattleActionQueue *q,
                 const C3BattleResumeRuntime *r)
{
    return f && !f->in_call && f->state==C3_BATTLE_FRAME_IDLE && queue_valid(q) &&
           runtime_valid(r) && !overlap(f,sizeof *f,q,sizeof *q);
}
static C3BattleFrameResult begin(C3BattleActionFrame *f,C3BattleActionQueue *q,
    C3BattleAction *a,const C3BattleResumeRuntime *r,C3BattleFrameMode mode)
{
    C3BattleActionFrame next={0};
    if(!admit(f,q,r))return rejected();
    if(mode==C3_BATTLE_FRAME_FLOW && (!a || overlap(f,sizeof *f,a,sizeof *a) ||
                                     overlap(q,sizeof *q,a,sizeof *a)))return rejected();
    next.mode=mode;next.queue=q;next.flow_output=a;
    if(mode==C3_BATTLE_FRAME_FLOW &&
       (q->write_index+1u)%C3_ACTION_SLOTS==q->read_index){
        next.state=C3_BATTLE_FRAME_RETURNED;next.source_result=2;*f=next;return report(f);
    }
    if(mode==C3_BATTLE_FRAME_DISPLAY && q->write_index==q->read_index){
        next.state=C3_BATTLE_FRAME_RETURNED;next.source_result=3;*f=next;return report(f);
    }
    next.action=mode==C3_BATTLE_FRAME_FLOW?*a:q->slots[q->read_index];
    if(action_valid(&next.action)){
        next.status=resolve(&next,r);
        if(next.status!=C3_ACTION_OK){
            next.state=C3_BATTLE_FRAME_FAILED;*f=next;return report(f);
        }
    }
    if(mode==C3_BATTLE_FRAME_FLOW){
        next.slot=q->write_index;
        c3_battle_action_set_word(&next.action,C3_ACTION_FIFO_INDEX,next.slot);
        q->slots[next.slot]=next.action;
        q->write_index=(q->write_index+1u)%C3_ACTION_SLOTS;
        c3_battle_action_set_word(a,C3_ACTION_FIFO_INDEX,next.slot);
    }else{
        next.slot=q->read_index;
        q->read_index=(q->read_index+1u)%C3_ACTION_SLOTS;
    }
    next.state=C3_BATTLE_FRAME_READY;*f=next;return report(f);
}
C3BattleFrameResult c3_battle_frame_begin_flow(C3BattleActionFrame *f,
    C3BattleActionQueue *q,C3BattleAction *a,const C3BattleResumeRuntime *r)
{ return begin(f,q,a,r,C3_BATTLE_FRAME_FLOW); }
C3BattleFrameResult c3_battle_frame_create_flow(C3BattleActionFrame *f,
    C3BattleActionQueue *q,C3BattleAction *a,uint32_t moto,uint32_t actor,
    uint16_t kind,uint32_t buff,const C3BattleResumeRuntime *r)
{
    const C3BattleActionData *found=NULL;
    if(!admit(f,q,r) || !a || overlap(f,sizeof *f,a,sizeof *a) ||
       overlap(q,sizeof *q,a,sizeof *a))return rejected();
    const C3BattleActionRuntime *source=r->source;
    if(!source->data_count || source->data[source->data_count-1].kind)
        return rejected();
    C3BattleAction candidate=*a;
    c3_battle_action_init(&candidate);
    for(size_t i=0;i<source->data_count && source->data[i].kind;++i){
        if(kind && source->data[i].kind==kind && source->data[i].buff==buff){
            found=&source->data[i];break;
        }
    }
    if(!found || !found->ref){
        *a=candidate;f->state=C3_BATTLE_FRAME_RETURNED;f->source_result=4;
        return report(f);
    }
    c3_battle_action_set_kind(&candidate,kind);
    c3_battle_action_set_word(&candidate,C3_ACTION_BUFF,buff);
    c3_battle_action_set_word(&candidate,C3_ACTION_DATA,found->ref);
    c3_battle_action_set_word(&candidate,C3_ACTION_ACTOR,actor);
    c3_battle_action_set_word(&candidate,C3_ACTION_BUFF_ID,buff);
    c3_battle_action_set_word(&candidate,C3_ACTION_MOTO,moto);
    C3BattleFrameResult out=begin(f,q,&candidate,r,C3_BATTLE_FRAME_FLOW);
    if(out.status==C3_ACTION_OK){
        /* begin copied candidate; never retain the automatic address. */
        f->flow_output=a;
        if(out.state==C3_BATTLE_FRAME_RETURNED)*a=candidate;
    }else f->flow_output=NULL;
    return out;
}
C3BattleFrameResult c3_battle_frame_begin_display(C3BattleActionFrame *f,
    C3BattleActionQueue *q,const C3BattleResumeRuntime *r)
{ return begin(f,q,NULL,r,C3_BATTLE_FRAME_DISPLAY); }
C3BattleFrameResult c3_battle_frame_step(C3BattleActionFrame *f)
{
    C3BattleRoutineReply reply={C3_BATTLE_ROUTINE_RETURN,C3_BATTLE_WAIT_NONE,0};
    if(!f || f->in_call)return rejected();
    if(!active(f))return report(f);
    if(!queue_valid(f->queue)){
        f->status=C3_ACTION_BAD_ARGUMENT;f->state=C3_BATTLE_FRAME_FAILED;return report(f);
    }
    f->in_call=1;
    if(f->asynchronous){
        C3BattleRoutineCall call=f->started?f->routine.resume:f->routine.start;
        f->started=1;reply=call(f->routine.user,&f->action);
    }else{
        f->started=1;
        if(action_valid(&f->action))reply.value=f->synchronous.call?
            f->synchronous.call(f->synchronous.user,&f->action):1;
    }
    f->in_call=0;
    if(!queue_valid(f->queue) || /* Unsigned compare also rejects negative values where the enum is signed. */
       (unsigned)reply.state>(unsigned)C3_BATTLE_ROUTINE_ERROR ||
       (reply.state==C3_BATTLE_ROUTINE_YIELD &&
        (reply.wait<=C3_BATTLE_WAIT_NONE || reply.wait>C3_BATTLE_WAIT_CHILD)) ||
       (reply.state!=C3_BATTLE_ROUTINE_YIELD && reply.wait!=C3_BATTLE_WAIT_NONE)){
        f->state=C3_BATTLE_FRAME_FAILED;f->status=C3_ACTION_BAD_ARGUMENT;return report(f);
    }
    if(reply.state==C3_BATTLE_ROUTINE_ERROR){
        f->state=C3_BATTLE_FRAME_FAILED;f->status=C3_ACTION_UNIMPLEMENTED;return report(f);
    }
    if(reply.state==C3_BATTLE_ROUTINE_YIELD){
        f->wait=reply.wait;f->state=C3_BATTLE_FRAME_WAITING;return report(f);
    }
    if(f->mode==C3_BATTLE_FRAME_FLOW){
        *f->flow_output=f->action;
        if((uint8_t)reply.value==1)f->queue->slots[f->slot]=f->action;
        else if(f->queue->write_index!=f->queue->read_index)
            f->queue->read_index=(f->queue->read_index+1u)%C3_ACTION_SLOTS;
    }
    f->source_result=reply.value;f->wait=C3_BATTLE_WAIT_NONE;
    f->state=C3_BATTLE_FRAME_RETURNED;return report(f);
}
C3BattleFrameResult c3_battle_frame_abort(C3BattleActionFrame *f)
{
    if(!f || f->in_call)return rejected();
    if(f->state==C3_BATTLE_FRAME_RETURNED || f->state==C3_BATTLE_FRAME_IDLE ||
       f->state==C3_BATTLE_FRAME_ABORTED)return report(f);
    if(f->started && f->asynchronous && !f->abort_called){
        f->abort_called=1;f->in_call=1;f->routine.abort(f->routine.user,&f->action);f->in_call=0;
    }
    f->abort_called=1;f->wait=C3_BATTLE_WAIT_NONE;f->state=C3_BATTLE_FRAME_ABORTED;
    return report(f);
}
C3BattleFrameResult c3_battle_frame_drain(C3BattleActionFrame *f,
    C3BattleActionQueue *q,const C3BattleResumeRuntime *r,size_t budget,size_t *completed)
{
    if(!f || !completed || f->in_call || !queue_valid(q) || !runtime_valid(r) ||
       overlap(f,sizeof *f,completed,sizeof *completed) ||
       overlap(q,sizeof *q,completed,sizeof *completed) ||
       overlap(f,sizeof *f,q,sizeof *q) ||
       (active(f) && (f->mode!=C3_BATTLE_FRAME_DISPLAY || f->queue!=q)))return rejected();
    *completed=0;
    if(f->state==C3_BATTLE_FRAME_FAILED || f->state==C3_BATTLE_FRAME_ABORTED)return report(f);
    if(!budget)return report(f);
    if(f->state==C3_BATTLE_FRAME_RETURNED)c3_battle_frame_reset(f);
    while(budget--){
        C3BattleFrameResult out;
        if(f->state==C3_BATTLE_FRAME_IDLE){
            out=c3_battle_frame_begin_display(f,q,r);
            if(out.state!=C3_BATTLE_FRAME_READY)return out;
        }
        out=c3_battle_frame_step(f);
        if(out.state!=C3_BATTLE_FRAME_RETURNED)return out;
        ++*completed;
        if((uint8_t)out.source_result!=1)return out;
        if(!budget)return out;
        c3_battle_frame_reset(f);
    }
    return report(f);
}
