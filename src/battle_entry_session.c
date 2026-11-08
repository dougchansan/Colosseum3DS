/* Isolated two-side trainer-entry session owner. See battle_entry_session.h
 * for the native choices that are not original observations. */
#include "colosseum3ds/battle_entry_session.h"
#include <stddef.h>
#include <string.h>

#define ENTRY_FLOW_REF 0x8020D1FCu /* fightActionFlowKaisiNyuujouTrainer */

static C3BattleEntrySession *owner_of(void *user)
{
    C3BattleEntryNativeContext *native=user;
    if(!native||!native->owner)return NULL;
    return (C3BattleEntrySession *)((char *)native->owner-offsetof(C3BattleEntrySession,records));
}
static void *scene_of(void *user)
{
    C3BattleEntrySession *s=owner_of(user);return s?s->config.scene:NULL;
}

/* Scene services receive the native context from the entry action; unwrap it
 * so scene code sees its own object. */
static int32_t seq_state(void *u,uint32_t group)
{C3BattleEntrySession *s=owner_of(u);return s&&s->config.sequence.resource_state?s->config.sequence.resource_state(s->config.scene,group):-1;}
static int seq_request(void *u,uint32_t group)
{C3BattleEntrySession *s=owner_of(u);return s&&s->config.sequence.request_group&&s->config.sequence.request_group(s->config.scene,group);}
static uint32_t seq_find(void *u,uint32_t group,uint32_t member)
{C3BattleEntrySession *s=owner_of(u);return s&&s->config.sequence.find_resource?s->config.sequence.find_resource(s->config.scene,group,member):0;}
static uint32_t seq_allocate(void *u)
{C3BattleEntrySession *s=owner_of(u);return s&&s->config.sequence.allocate?s->config.sequence.allocate(s->config.scene):0;}
static int seq_fields(void *u,uint32_t sequence,uint16_t index,uint16_t group,uint8_t flags)
{C3BattleEntrySession *s=owner_of(u);return s&&s->config.sequence.set_fields&&s->config.sequence.set_fields(s->config.scene,sequence,index,group,flags);}
static int seq_load(void *u,uint32_t sequence,uint32_t resource)
{C3BattleEntrySession *s=owner_of(u);return s&&s->config.sequence.load?s->config.sequence.load(s->config.scene,sequence,resource):-1;}
static void seq_release(void *u,uint32_t sequence)
{C3BattleEntrySession *s=owner_of(u);if(s&&s->config.sequence.release)s->config.sequence.release(s->config.scene,sequence);}
static int seq_hide(void *u,uint32_t sequence)
{C3BattleEntrySession *s=owner_of(u);return s&&s->config.sequence.hide&&s->config.sequence.hide(s->config.scene,sequence);}
static void seq_report(void *u,uint32_t group)
{C3BattleEntrySession *s=owner_of(u);if(s&&s->config.sequence.report_negative_state)s->config.sequence.report_negative_state(s->config.scene,group);}

static int grid_add(void *u,uint32_t sequence,uint8_t x,uint8_t y)
{C3BattleEntrySession *s=owner_of(u);return s&&s->config.grid_add(scene_of(u),sequence,x,y);}
static int grid_update(void *u)
{C3BattleEntrySession *s=owner_of(u);return s&&s->config.grid_update(scene_of(u));}
static int set_visible(void *u,uint32_t sequence,int visible)
{C3BattleEntrySession *s=owner_of(u);return s&&s->config.set_visible(scene_of(u),sequence,visible);}
static void abort_owned(void *u,const C3BattleEntryOwnership *own,size_t count)
{C3BattleEntrySession *s=owner_of(u);if(s)s->config.abort_owned(scene_of(u),own,count);}
/* Friendship filtering (fightPokemonGetFriendFormPokemonFriendFilterId) is not
 * ported. The source only calls it when both floor kind flags are 1; reaching
 * it is an explicit failure, never a silent success or a flag edit. */
static int friend_filter(void *u,uint32_t fighter,uint8_t filter)
{(void)u;(void)fighter;(void)filter;return 0;}

static int config_valid(const C3BattleEntrySessionConfig *c)
{
    const C3BattleSequenceServices *q=&c->sequence;
    if(!c->grid_add||!c->grid_update||!c->set_visible||!c->abort_owned||
       !q->resource_state||!q->request_group||!q->find_resource||!q->allocate||
       !q->set_fields||!q->load||!q->release||!q->hide||!q->report_negative_state||
       (c->resource_count&&!c->resources)||!c->side_data_id[0]||!c->side_data_id[1]||
       !c->actions||!c->action_count||c->actions[c->action_count-1].kind||
       !c->kinds||c->entry_kind>=c->kind_count||c->kinds[c->entry_kind].kind!=c->entry_kind||
       c->kinds[c->entry_kind].flow_ref!=ENTRY_FLOW_REF)return 0;
    for(unsigned i=0;i<2;++i)if(!c->trainer_data[i].trainer)return 0;
    return 1;
}
/* _fightInitialize: fightActionCreateAndFlowFifo(.., 0, 0, 3, 5, data) and
 * then (.., 3, 4, ..) into the same FIFO. The preceding Kaijou action (kind
 * 1, venue presentation) is owned by the stage and is not run here. */
static int start_flow(C3BattleEntrySession *s)
{
    s->side=s->flows==0?1u:0u;
    memset(&s->entry,0,sizeof s->entry);
    if(!c3_battle_frame_reset(&s->frame))return 0;
    if(!c3_battle_trainer_entry_setup(&s->entry,&s->services,&s->sequence,s->config.resources,
        s->config.resource_count,&s->native,s->initial_hero))return 0;
    C3BattleFrameResult r=c3_battle_frame_create_flow(&s->frame,&s->queue,&s->action,0,0,
        s->config.entry_kind,4u+s->side,&s->resume_runtime);
    ++s->flows;
    return r.state==C3_BATTLE_FRAME_READY;
}
int c3_battle_entry_session_begin(C3BattleEntrySession *s,const C3BattleEntrySessionConfig *c)
{
    if(!s||!c||s->state!=C3_ENTRY_SESSION_IDLE||!config_valid(c))return 0;
    s->config=*c;
    if(!c3_battle_rng_init(&s->rng,c->seed))return 0;
    s->entry_counter=0;
    memset(s->runtime,0,sizeof s->runtime);memset(s->sort,0,sizeof s->sort);
    memset(s->pokemon_scratch,0,sizeof s->pokemon_scratch);memset(s->initial_hero,0,sizeof s->initial_hero);
    for(unsigned i=0;i<2;++i){
        uint32_t base=i*C3_ENTRY_SESSION_SIDE_STRIDE;C3BattleEntryNativeSlot *slot=&s->slots[i];
        memset(slot,0,sizeof *slot);
        /* fightSideCreate runs fightTrainerInit on each side's trainer record. */
        if(!c3_battle_runtime_trainer_init(s->runtime[i]))return 0;
        slot->trainer=C3_ENTRY_SESSION_TRAINER+base;slot->hero=C3_ENTRY_SESSION_HERO+base;
        for(unsigned j=0;j<6;++j){slot->party[j]=C3_ENTRY_SESSION_PARTY+base+j;slot->fighter[j]=C3_ENTRY_SESSION_FIGHTER+base+j;}
        slot->runtime=s->runtime[i];slot->runtime_size=sizeof s->runtime[i];
        slot->sort_state=&s->sort[i];slot->sort_data=&s->config.sort_data;
        s->heroes[i].request=c->requests[i];s->heroes[i].data=&s->config.trainer_data[i];
        s->heroes[i].pokemon_scratch=s->pokemon_scratch; /* one reused parent stack slot */
        s->records.sides[i].reference=C3_ENTRY_SESSION_SIDE+base;
        s->records.sides[i].data_id=c->side_data_id[i];
        s->records.sides[i].trainers[0]=slot->trainer;s->records.sides[i].trainers[1]=0;
    }
    memset(&s->native,0,sizeof s->native);
    s->native.slots=s->slots;s->native.slot_count=2;s->native.heroes=s->heroes;s->native.hero_count=2;
    s->native.rng=&s->rng;s->native.entry_counter=&s->entry_counter;s->native.owner=&s->records;
    s->records.native=&s->native;
    memcpy(s->records.tables,c->tables,sizeof s->records.tables);
    s->records.action_data=c->actions;s->records.action_count=c->action_count;
    s->records.encounter_reference=C3_ENTRY_SESSION_ENCOUNTER;s->records.encounter_id=c->encounter_id;
    s->records.battle_result=0;s->records.preset_mode=c->preset_mode;s->records.scene_user=c->scene;
    if(!c3_battle_entry_native_validate(&s->native)||!c3_battle_entry_records_validate(&s->records))return 0;

    memset(&s->services,0,sizeof s->services);
    c3_battle_entry_native_bind(&s->services);c3_battle_entry_records_bind(&s->services);
    s->services.friend_filter=friend_filter;s->services.grid_add=grid_add;
    s->services.grid_update=grid_update;s->services.set_visible=set_visible;
    s->services.abort_owned=abort_owned;
    s->sequence=(C3BattleSequenceServices){seq_state,seq_request,seq_find,seq_allocate,
        seq_fields,seq_load,seq_release,seq_hide,seq_report};
    s->action_runtime=(C3BattleActionRuntime){c->actions,c->action_count,c->kinds,c->kind_count,NULL,0};
    s->binding=(C3BattleRoutineBinding){ENTRY_FLOW_REF,c3_battle_trainer_entry_start,
        c3_battle_trainer_entry_resume,c3_battle_trainer_entry_abort,&s->entry};
    s->resume_runtime=(C3BattleResumeRuntime){&s->action_runtime,&s->binding,1};
    memset(&s->frame,0,sizeof s->frame);
    c3_battle_action_queue_init(&s->queue);c3_battle_action_init(&s->action);
    s->flows=0;s->owned_count=0;s->steps=s->waits=0;s->failure=NULL;
    s->state=C3_ENTRY_SESSION_RUNNING;
    if(!start_flow(s)){s->failure="begin_flow";s->state=C3_ENTRY_SESSION_FAILED;return 0;}
    return 1;
}
int c3_battle_entry_session_step(C3BattleEntrySession *s)
{
    if(!s)return -1;
    if(s->state==C3_ENTRY_SESSION_DONE)return 0;
    if(s->state!=C3_ENTRY_SESSION_RUNNING)return -1;
    C3BattleFrameResult r=c3_battle_frame_step(&s->frame);++s->steps;
    if(r.state==C3_BATTLE_FRAME_WAITING){++s->waits;return 1;}
    if(r.state!=C3_BATTLE_FRAME_RETURNED){
        s->failure=s->entry.failure?s->entry.failure:"action_frame";
        s->state=C3_ENTRY_SESSION_FAILED;return -1;
    }
    s->source_result[s->side]=r.source_result;
    /* A returned side's trainers now belong to this session, not the action. */
    for(size_t i=0;i<s->entry.ownership_count&&s->owned_count<C3_TRAINER_ENTRY_MAX_TRAINERS;++i)
        s->owned[s->owned_count++]=s->entry.ownership[i];
    if(s->flows<2){
        if(!start_flow(s)){s->failure="begin_flow";s->state=C3_ENTRY_SESSION_FAILED;return -1;}
        return 1;
    }
    s->state=C3_ENTRY_SESSION_DONE;return 0;
}
void c3_battle_entry_session_abort(C3BattleEntrySession *s)
{
    if(!s||s->state==C3_ENTRY_SESSION_IDLE||s->state==C3_ENTRY_SESSION_ABORTED)return;
    if(s->state==C3_ENTRY_SESSION_RUNNING||s->state==C3_ENTRY_SESSION_FAILED)
        (void)c3_battle_frame_abort(&s->frame); /* hands the active side's trainers over */
    if(s->state!=C3_ENTRY_SESSION_DONE&&s->owned_count)
        s->config.abort_owned(s->config.scene,s->owned,s->owned_count);
    s->owned_count=0;s->state=C3_ENTRY_SESSION_ABORTED;
}
const uint8_t *c3_battle_entry_session_runtime(const C3BattleEntrySession *s,unsigned side)
{
    return s&&side<2?s->runtime[side]:NULL;
}
const uint8_t *c3_battle_entry_session_fighter(const C3BattleEntrySession *s,unsigned side,unsigned slot)
{
    /* FightTrainer FightPokemon array starts at 0xB1C in the runtime record. */
    return s&&side<2&&slot<6?s->runtime[side]+0xB1Cu+slot*C3_BATTLE_FIGHT_POKEMON_BYTES:NULL;
}
