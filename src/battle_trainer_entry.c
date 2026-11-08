/* Source lowering: fight_action.c, 8020D1FC. The only cooperative boundary
 * inside this action is trainer sequence creation. All source locals needed
 * after it, especially hero[0xB1C], live in the typed frame. */
#include "colosseum3ds/battle_trainer_entry.h"
#include <string.h>
#include <stdint.h>

static C3BattleRoutineReply reply(C3BattleRoutineState state, C3BattleWaitKind wait, uint32_t value)
{ C3BattleRoutineReply r = {state, wait, value}; return r; }
static C3BattleRoutineReply error_reply(void)
{ return reply(C3_BATTLE_ROUTINE_ERROR, C3_BATTLE_WAIT_NONE, 0); }
static C3BattleRoutineReply failed(C3BattleTrainerEntryFrame *f, const char *where)
{
    f->failure = where; f->phase = C3_TRAINER_ENTRY_FAILED; f->in_call = 0;
    return error_reply();
}
static C3BattleRoutineReply done(C3BattleTrainerEntryFrame *f, uint32_t value)
{
    f->source_result = value; f->phase = C3_TRAINER_ENTRY_DONE; f->in_call = 0;
    return reply(C3_BATTLE_ROUTINE_RETURN, C3_BATTLE_WAIT_NONE, value);
}
static int overlaps(const void *a, size_t an, const void *b, size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x<=y ? y-x<an : x-y<bn);
}
static int valid_services(const C3BattleTrainerEntryServices *s)
{
    return s && s->action_buff && s->floor_status && s->encounter && s->target_side &&
        s->side_valid && s->side_status && s->encounter_status && s->trainer_data_valid &&
        s->preset_mode && s->copy_preset && s->create_hero && s->sequence_index &&
        s->create_runtime && s->trainer_status && s->trainer_valid && s->sort_party &&
        s->hero_pokemon && s->pokemon_eligible && s->already_entered && s->next_entry_counter &&
        s->create_fighter && s->is_gc_hero && s->friend_filter && s->side_fighting_count &&
        s->valid_trainer && s->grid_parameters && s->grid_add && s->grid_update &&
        s->set_visible && s->abort_owned;
}
int c3_battle_trainer_entry_setup(C3BattleTrainerEntryFrame *f,
    const C3BattleTrainerEntryServices *s, const C3BattleSequenceServices *seq,
    const C3BattleSequenceResource *resources, size_t count, void *user,
    const uint8_t initial[C3_TRAINER_ENTRY_HERO_BYTES])
{
    if (!f || f->in_call || (f->phase != C3_TRAINER_ENTRY_IDLE &&
        f->phase != C3_TRAINER_ENTRY_DONE && f->phase != C3_TRAINER_ENTRY_ABORTED) ||
        !valid_services(s) || !seq || !initial || (count && !resources) || count>65536u ||
        !seq->resource_state || !seq->request_group || !seq->find_resource || !seq->allocate ||
        !seq->set_fields || !seq->load || !seq->release || !seq->hide || !seq->report_negative_state ||
        overlaps(f,sizeof *f,s,sizeof *s) || overlaps(f,sizeof *f,seq,sizeof *seq) ||
        overlaps(f,sizeof *f,initial,C3_TRAINER_ENTRY_HERO_BYTES) ||
        overlaps(f,sizeof *f,resources,count*sizeof *resources)) return 0;
    memset(f,0,sizeof *f);
    f->services=*s; f->sequence_services=*seq; f->resources=resources; f->resource_count=count;
    f->user=user; memcpy(f->hero,initial,sizeof f->hero); f->phase=C3_TRAINER_ENTRY_READY;
    return 1;
}
#define CALL(name, ...) do { if (!f->services.name(f->user, __VA_ARGS__)) return failed(f,#name); } while (0)

/* Original two passes: eligible living entries first (active quota), then any
 * valid unentered party member up to maximum. Counter is advanced at exactly
 * the original construction call, not during predicate checks or a wait. */
static int finish_trainer(C3BattleTrainerEntryFrame *f, uint32_t sequence)
{
    uint8_t valid, gc; uint32_t v, pokemon, fighter, entry; unsigned count=0;
    if (!f->services.create_runtime(f->user,f->trainer,f->hero,f->trainer_id,f->device,sequence)) {
        f->failure="create_runtime"; return 0;
    }
    f->ownership[f->ownership_count++]=(C3BattleEntryOwnership){f->trainer,sequence};
    f->owned_sequence=0; /* transfer only after successful construction */
    if (!f->services.trainer_status(f->user,f->trainer,0x44,0,&f->hero_reference)) {
        f->failure="trainer_hero"; return 0;
    }
    if (!f->services.trainer_valid(f->user,f->trainer,&valid)) {f->failure="trainer_valid";return 0;}
    if (!valid) return 1;
    if (!f->services.sort_party(f->user,f->trainer,f->maximum,f->active)) {f->failure="sort_party";return 0;}
    for (unsigned pass=0; pass<2; ++pass) {
        for (unsigned j=0; j<6; ++j) {
            if ((!pass && count>=f->active) || count>=f->maximum || count>=6) break;
            if (!f->services.hero_pokemon(f->user,f->hero_reference,j,&pokemon)) {f->failure="hero_pokemon";return 0;}
            if (!f->services.pokemon_eligible(f->user,pokemon,pass==0,&valid)) {f->failure="pokemon_eligible";return 0;}
            if (!valid) continue;
            if (!f->services.already_entered(f->user,f->trainer,pokemon,&v)) {f->failure="already_entered";return 0;}
            if (v) continue;
            if (!f->services.trainer_status(f->user,f->trainer,0x45,count,&fighter)) {f->failure="trainer_fighter";return 0;}
            if (!f->services.next_entry_counter(f->user,&entry)) {f->failure="entry_counter";return 0;}
            if (!f->services.create_fighter(f->user,fighter,pokemon,entry)) {f->failure="create_fighter";return 0;}
            /* Short-circuit and read these mutable globals at their SOURCE
             * point after construction, not from an entry-time snapshot. */
            if (!f->services.floor_status(f->user,0x27,&v)) {f->failure="floor27";return 0;}
            if ((uint8_t)v==1) {
                if (!f->services.floor_status(f->user,0x2e,&v)) {f->failure="floor2e";return 0;}
                if ((uint8_t)v==1) {
                    if (!f->services.is_gc_hero(f->user,f->trainer,&gc)) {f->failure="is_gc_hero";return 0;}
                    if (gc==1 && !f->services.friend_filter(f->user,fighter,3)) {f->failure="friend_filter";return 0;}
                }
            }
            ++count;
        }
    }
    return 1;
}
static C3BattleRoutineReply continue_entry(C3BattleTrainerEntryFrame *f, int resume)
{
    C3BattleRoutineReply child;
    uint8_t valid, mode, fighting, x, y; uint32_t sequence;
    if (resume) {
        child=c3_battle_sequence_step(&f->child);
        if (child.state==C3_BATTLE_ROUTINE_YIELD) {f->in_call=0;return child;}
        if (child.state!=C3_BATTLE_ROUTINE_RETURN) return failed(f,"sequence_resume");
        f->owned_sequence=child.value;
        if (!finish_trainer(f,child.value)) return failed(f,f->failure);
        ++f->trainer_index; f->phase=C3_TRAINER_ENTRY_RUNNING;
    }
    while (f->trainer_index<f->trainer_count) {
        uint8_t index=(uint8_t)(f->trainer_index+f->offset);
        CALL(side_status,f->side,7,f->trainer_index,&f->trainer);
        CALL(encounter_status,f->encounter,0,index,&f->trainer_id);
        CALL(encounter_status,f->encounter,1,index,&f->device);
        CALL(trainer_data_valid,f->trainer_id,f->device,&valid);
        if (!valid) {++f->trainer_index;continue;}
        CALL(preset_mode,&mode);
        if (mode==1) {CALL(copy_preset,index,f->hero);}
        else {CALL(create_hero,f->trainer_id,f->device,f->hero);}
        CALL(sequence_index,f->trainer_id,&f->sequence_index);
        child=c3_battle_sequence_begin(&f->child,f->sequence_index,f->resources,f->resource_count,
            &f->sequence_services,f->user,1);
        if (child.state==C3_BATTLE_ROUTINE_YIELD) {
            f->phase=C3_TRAINER_ENTRY_WAIT_SEQUENCE;f->in_call=0;return child;
        }
        if (child.state!=C3_BATTLE_ROUTINE_RETURN) return failed(f,"sequence_begin");
        f->owned_sequence=child.value;
        if (!finish_trainer(f,child.value)) return failed(f,f->failure);
        ++f->trainer_index;
    }
    CALL(side_fighting_count,f->side,&fighting);
    for (unsigned i=0;i<f->trainer_count;++i) {
        uint32_t trainer;
        CALL(valid_trainer,f->side,i,&trainer);
        if (!trainer) continue;
        CALL(trainer_status,trainer,0x4c,0,&sequence);
        if (!sequence) continue;
        CALL(grid_parameters,f->grid_side,fighting,i,&x,&y);
        CALL(grid_add,sequence,x,y);
        if (!f->services.grid_update(f->user)) return failed(f,"grid_update");
        CALL(set_visible,sequence,1);
    }
    return done(f,1);
}
C3BattleRoutineReply c3_battle_trainer_entry_start(void *frame, C3BattleAction *action)
{
    C3BattleTrainerEntryFrame *f=frame; uint32_t v; uint8_t valid;
    if (!f || !action || f->in_call || f->phase!=C3_TRAINER_ENTRY_READY ||
        overlaps(f,sizeof *f,action,sizeof *action)) return error_reply();
    f->in_call=1; f->phase=C3_TRAINER_ENTRY_RUNNING;
    CALL(action_buff,action,&v); f->buff=(uint16_t)v;
    CALL(floor_status,0xd,&v); CALL(encounter,(uint16_t)v,&f->encounter);
    CALL(floor_status,0x14,&v); f->side_id=(uint16_t)v;
    CALL(floor_status,0x16,&v); f->trainer_count=(uint16_t)v;
    CALL(floor_status,0x17,&v); f->maximum=(uint16_t)v;
    CALL(floor_status,0x18,&v); f->active=(uint16_t)v;
    CALL(target_side,f->buff,f->side_id,&f->side);
    CALL(side_valid,f->side,&valid);
    if (!valid) return done(f,0);
    if (f->trainer_count>C3_TRAINER_ENTRY_MAX_TRAINERS) return failed(f,"trainer_count");
    CALL(side_status,f->side,5,0,&v); f->grid_side=(uint16_t)v;
    f->offset=f->buff==4?0:f->trainer_count;
    return continue_entry(f,0);
}
C3BattleRoutineReply c3_battle_trainer_entry_resume(void *frame, C3BattleAction *action)
{
    C3BattleTrainerEntryFrame *f=frame;(void)action;
    if (!f || f->in_call) return error_reply();
    if (f->phase==C3_TRAINER_ENTRY_DONE) return reply(C3_BATTLE_ROUTINE_RETURN,C3_BATTLE_WAIT_NONE,f->source_result);
    if (f->phase!=C3_TRAINER_ENTRY_WAIT_SEQUENCE) return error_reply();
    f->in_call=1; return continue_entry(f,1);
}
void c3_battle_trainer_entry_abort(void *frame, C3BattleAction *action)
{
    C3BattleTrainerEntryFrame *f=frame;(void)action;
    if (!f || f->in_call || f->abort_called || f->phase==C3_TRAINER_ENTRY_IDLE ||
        f->phase==C3_TRAINER_ENTRY_DONE || f->phase==C3_TRAINER_ENTRY_ABORTED) return;
    f->in_call=1;f->abort_called=1;
    if (f->child.phase==C3_SEQUENCE_POLL_RESOURCE) (void)c3_battle_sequence_cancel(&f->child);
    if (f->owned_sequence) {f->sequence_services.release(f->user,f->owned_sequence);f->owned_sequence=0;}
    f->services.abort_owned(f->user,f->ownership,f->ownership_count);
    f->phase=C3_TRAINER_ENTRY_ABORTED;f->in_call=0;
}
