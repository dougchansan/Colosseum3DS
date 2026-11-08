/* Source-derived entry accessors and predicates. Exact source pins and
 * independent compiled-source references: prepare_battle_entry_records_reference.py.
 * This is an ordinary native record view, NOT original memory emulation. */
#include "colosseum3ds/battle_entry_records.h"
#include <stdint.h>
#include <string.h>

static const size_t strides[C3_ENTRY_TABLE_COUNT]={56,52,284,20,8,32};
static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int overlaps(const void *a,size_t an,const void *b,size_t bn)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an&&bn&&(x<=y?y-x<an:x-y<bn);}
static size_t rows(const C3BattleEntryRecords *r,unsigned table)
{return (size_t)r->tables[table].limit+(table==C3_ENTRY_TYPE||table==C3_ENTRY_KIND);}
static const uint8_t *row(const C3BattleEntryRecords *r,unsigned table,uint16_t id)
{return (size_t)id<rows(r,table)?r->tables[table].bytes+(size_t)id*strides[table]:NULL;}
static const C3BattleEntryNativeSlot *trainer(const C3BattleEntryRecords *r,uint32_t ref)
{
    for(size_t i=0;i<r->native->slot_count;++i)
        if(r->native->slots[i].trainer==ref)return &r->native->slots[i];
    return NULL;
}
static const C3BattleEntrySideView *side(const C3BattleEntryRecords *r,uint32_t ref)
{for(unsigned i=0;i<2;++i)if(ref&&r->sides[i].reference==ref)return &r->sides[i];return NULL;}
static const uint8_t *party(const C3BattleEntryRecords *r,uint32_t ref)
{
    for(size_t i=0;i<r->native->slot_count;++i)for(unsigned j=0;j<6;++j)
        if(r->native->slots[i].party[j]==ref)return r->native->slots[i].runtime+4+0x30+j*312u;
    return NULL;
}
/* Also protects read-only descriptor objects from the constructor services
 * that run between these reads. A declared extent must be backed by storage;
 * C cannot recover allocation lengths from borrowed pointers. */
static int native_disjoint(const C3BattleEntryNativeContext *c,const void *p,size_t size)
{
    if(overlaps(p,size,c,sizeof *c)||
       overlaps(p,size,c->slots,c->slot_count*sizeof *c->slots)||
       overlaps(p,size,c->heroes,c->hero_count*sizeof *c->heroes)||
       overlaps(p,size,c->rng,sizeof *c->rng)||overlaps(p,size,c->entry_counter,sizeof *c->entry_counter))return 0;
    for(size_t i=0;i<c->slot_count;++i)
        if(overlaps(p,size,c->slots[i].runtime,c->slots[i].runtime_size)||
           overlaps(p,size,c->slots[i].sort_state,sizeof *c->slots[i].sort_state)||
           overlaps(p,size,c->slots[i].sort_data,sizeof *c->slots[i].sort_data))return 0;
    for(size_t i=0;i<c->hero_count;++i)
        if(overlaps(p,size,c->heroes[i].pokemon_scratch,C3_BATTLE_POKEMON_BYTES)||
           overlaps(p,size,c->heroes[i].data,sizeof *c->heroes[i].data))return 0;
    for(unsigned i=0;i<4;++i)if(c->preset[i]&&overlaps(p,size,c->preset[i],C3_BATTLE_TRAINER_BYTES))return 0;
    return 1;
}
static int output_ok(const C3BattleEntryRecords *r,const void *p,size_t size)
{
    if(!p||overlaps(p,size,r,sizeof *r)||!native_disjoint(r->native,p,size)||
       overlaps(p,size,r->action_data,r->action_count*sizeof *r->action_data))return 0;
    for(unsigned t=0;t<C3_ENTRY_TABLE_COUNT;++t)
        if(overlaps(p,size,r->tables[t].bytes,r->tables[t].byte_size))return 0;
    return 1;
}
int c3_battle_entry_records_validate(const C3BattleEntryRecords *r)
{
    if(!r||!r->native||r->native->owner!=r||!c3_battle_entry_native_validate(r->native)||
       !r->encounter_reference||r->action_count>65536u||(r->action_count&&!r->action_data)||
       !native_disjoint(r->native,r,sizeof *r)||
       !native_disjoint(r->native,r->action_data,r->action_count*sizeof *r->action_data)||
       overlaps(r->action_data,r->action_count*sizeof *r->action_data,r,sizeof *r))return 0;
    for(unsigned t=0;t<C3_ENTRY_TABLE_COUNT;++t){
        const C3BattleEntryTableView *v=&r->tables[t];
        if(v->limit>(t==C3_ENTRY_TYPE||t==C3_ENTRY_KIND?65535u:65536u))return 0;
        size_t n=rows(r,t)*strides[t];
        if(v->byte_size<n||(v->byte_size&&!v->bytes))return 0;
    }
    uint32_t refs[31];size_t nr=0;
    refs[nr++]=r->encounter_reference;
    for(size_t i=0;i<r->native->slot_count;++i){const C3BattleEntryNativeSlot *s=&r->native->slots[i];
        refs[nr++]=s->trainer;refs[nr++]=s->hero;
        for(unsigned j=0;j<6;++j){refs[nr++]=s->party[j];refs[nr++]=s->fighter[j];}
    }
    for(unsigned i=0;i<2;++i){const C3BattleEntrySideView *s=&r->sides[i];
        if(s->reference)refs[nr++]=s->reference;
        else if(s->data_id||s->trainers[0]||s->trainers[1])return 0;
        for(unsigned j=0;j<2;++j){uint32_t ref=s->trainers[j];
            if(ref&&!trainer(r,ref))return 0;
            for(unsigned k=0;k<i*2+j;++k)
                if(ref&&ref==r->sides[k/2].trainers[k%2])return 0;
        }
    }
    for(size_t i=0;i<nr;++i)for(size_t j=0;j<i;++j)if(refs[i]==refs[j])return 0;
    /* Tables may intentionally share read-only backing, but cannot overlap
     * live mutable records, their descriptors, or native-layout action rows. */
    for(unsigned t=0;t<C3_ENTRY_TABLE_COUNT;++t){const C3BattleEntryTableView *v=&r->tables[t];
        if(overlaps(v->bytes,v->byte_size,r,sizeof *r)||
           overlaps(v->bytes,v->byte_size,r->action_data,r->action_count*sizeof *r->action_data)||
           !native_disjoint(r->native,v->bytes,v->byte_size))return 0;
    }
    return 1;
}
static const C3BattleEntryRecords *context(void *user)
{
    const C3BattleEntryNativeContext *c=user;
    const C3BattleEntryRecords *r=c?c->owner:NULL;
    return r&&r->native==c&&c3_battle_entry_records_validate(r)?r:NULL;
}
/* An empty GSchar string is one zero word. For this comparison only, the
 * original GScharCmp/heroCheckValid closure needs no host text conversion. */
static int trainer_valid_value(const C3BattleEntryRecords *r,uint32_t ref,uint8_t *valid)
{
    *valid=0;if(!ref)return 1;
    const C3BattleEntryNativeSlot *s=trainer(r,ref);if(!s)return 0;
    if(r->battle_result==1||!be16(s->runtime))return 1;
    const uint8_t *h=s->runtime+4;
    *valid=(uint8_t)(be16(h)!=0&&h[0xa80]!=2);return 1;
}
static uint8_t pokemon_valid_value(const C3BattleEntryRecords *r,const uint8_t *p,int active)
{
    if(!p)return 0;
    uint16_t species=be16(p);const uint8_t *s=row(r,C3_ENTRY_SPECIES,species);
    if(!species||!s||!be32(s+24)||!p[8]||!p[9]||!p[10]||!p[11]||p[0xcd]==1)return 0;
    if(active&&(species==0x19c||p[0xcb]==1||be16(p+0x8a)==0))return 0;
    return 1;
}
static int fighter_valid_value(const C3BattleEntryRecords *r,const uint8_t *fp,int active,uint8_t *valid)
{
    *valid=0;if(r->battle_result==1)return 1;
    uint32_t ref=be32(fp);if(!ref)return 1;
    const uint8_t *p=party(r,ref);if(!p)return 0;
    if(!pokemon_valid_value(r,p,0)||!pokemon_valid_value(r,fp+4,0)||be16(fp+0x14c)>=0x8000u)return 1;
    if(active&&(fp[0x151]==1||!pokemon_valid_value(r,fp+4,1)))return 1;
    *valid=1;return 1;
}
static int action_buff(void *user,const C3BattleAction *action,uint32_t *out)
{
    const C3BattleEntryRecords *r=context(user);
    if(!r||!action||!output_ok(r,out,sizeof *out)||overlaps(out,sizeof *out,action,sizeof *action))return 0;
    uint32_t ref=c3_battle_action_word(action,C3_ACTION_DATA),value=0;size_t found=0;
    if(!ref){*out=0;return 1;}
    for(size_t i=0;i<r->action_count;++i)if(r->action_data[i].ref==ref){value=r->action_data[i].buff;++found;}
    if(found!=1)return 0;
    *out=value;return 1;
}
static int floor_status(void *user,unsigned field,uint32_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out))return 0;
    uint32_t value=0;const uint8_t *e=row(r,C3_ENTRY_ENCOUNTER,r->encounter_id),*d;
    switch(field){
    case 0xd:value=r->encounter_id;break;
    case 0x14:value=e?e[1]:0;break;
    case 0x16:case 0x17:case 0x18:
        d=row(r,C3_ENTRY_TYPE,e?e[1]:0);value=d?d[field-0x16]:0;break;
    case 0x27:case 0x2e:
        d=row(r,C3_ENTRY_KIND,e?e[0]:0);value=d?d[field==0x27?11:18]:0;break;
    default:return 0;
    }
    *out=value;return 1;
}
static int encounter(void *user,uint16_t id,uint32_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out))return 0;
    if(!row(r,C3_ENTRY_ENCOUNTER,id)){*out=0;return 1;}
    if(id!=r->encounter_id)return 0;
    *out=r->encounter_reference;return 1;
}
static int target_side(void *user,uint16_t buff,uint16_t type,uint32_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out))return 0;
    if(!row(r,C3_ENTRY_TYPE,type)){*out=0;return 1;}
    if(buff!=4&&buff!=5)return 0; /* other target graphs need their own adapter */
    *out=r->sides[buff-4].reference;return 1;
}
static int side_valid(void *user,uint32_t ref,uint8_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out))return 0;
    if(!ref){*out=0;return 1;}const C3BattleEntrySideView *s=side(r,ref);if(!s)return 0;
    *out=(uint8_t)(r->battle_result!=1&&s->data_id!=0);return 1;
}
static int side_status(void *user,uint32_t ref,unsigned field,unsigned index,uint32_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out))return 0;
    if(field!=5&&field!=7)return 0;
    if(!ref){*out=0;return 1;}const C3BattleEntrySideView *s=side(r,ref);if(!s)return 0;
    if(field==5){if(index)return 0;*out=s->data_id;return 1;}
    *out=index<2?s->trainers[index]:0;return 1;
}
static int encounter_status(void *user,uint32_t ref,unsigned field,uint8_t index,uint16_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out)||field>1)return 0;
    if(ref&&ref!=r->encounter_reference)return 0;
    const uint8_t *p=ref?row(r,C3_ENTRY_ENCOUNTER,r->encounter_id):NULL;
    *out=p&&index<4?(field? (uint16_t)be32(p+28+8u*index):be16(p+24+8u*index)):0;return 1;
}
static int trainer_data_valid(void *user,uint16_t id,uint16_t device,uint8_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out))return 0;
    const uint8_t *p=row(r,C3_ENTRY_TRAINER,id);
    *out=(uint8_t)(id!=0&&((p&&be16(p+6)!=0)||device!=0));return 1;
}
static int preset_mode(void *user,uint8_t *out)
{const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out))return 0;*out=r->preset_mode;return 1;}
static int sequence_index(void *user,uint16_t id,uint16_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out))return 0;
    const uint8_t *p=row(r,C3_ENTRY_TRAINER,id);*out=p?(uint16_t)be32(p+0x10):0;return 1; /* (u16)selector9: low half of BE32 +0x10 */
}
static int trainer_valid(void *user,uint32_t ref,uint8_t *out)
{
    const C3BattleEntryRecords *r=context(user);uint8_t v;
    if(!r||!output_ok(r,out,sizeof *out)||!trainer_valid_value(r,ref,&v))return 0;
    *out=v;return 1;
}
static int pokemon_eligible(void *user,uint32_t ref,int active,uint8_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out)||(active!=0&&active!=1))return 0;
    const uint8_t *p=ref?party(r,ref):NULL;if(ref&&!p)return 0;
    *out=pokemon_valid_value(r,p,active);return 1;
}
static int already_entered(void *user,uint32_t ref,uint32_t source,uint32_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out))return 0;
    if(source&&!party(r,source))return 0;
    if(!ref){*out=0;return 1;}const C3BattleEntryNativeSlot *s=trainer(r,ref);if(!s)return 0;
    uint32_t value=0;
    for(unsigned j=0;j<6;++j){const uint8_t *fp=s->runtime+0xb1c+j*340u;uint8_t valid;
        if(!fighter_valid_value(r,fp,0,&valid))return 0;
        if(valid&&be32(fp)==source){value=s->fighter[j];break;}
    }
    *out=value;return 1;
}
static int is_gc_hero(void *user,uint32_t ref,uint8_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out))return 0;
    const C3BattleEntryNativeSlot *s=ref?trainer(r,ref):NULL;if(ref&&!s)return 0;
    const uint8_t *p=row(r,C3_ENTRY_TRAINER,s?be16(s->runtime):0);
    *out=(uint8_t)(p&&be16(p+2)==1);return 1;
}
static int side_fighting_count(void *user,uint32_t ref,uint8_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out))return 0;
    if(!ref){*out=0;return 1;}const C3BattleEntrySideView *s=side(r,ref);if(!s)return 0;
    uint8_t count=0;
    for(unsigned i=0;i<2;++i){uint8_t valid;
        if(!trainer_valid_value(r,s->trainers[i],&valid))return 0;
        if(!valid)continue;
        const C3BattleEntryNativeSlot *t=trainer(r,s->trainers[i]);unsigned fighting=0;
        for(unsigned j=0;j<6;++j){
            if(!fighter_valid_value(r,t->runtime+0xb1c+j*340u,1,&valid))return 0;
            if(valid)++fighting;
        }
        if(fighting)++count;
    }
    *out=count;return 1;
}
static int valid_trainer(void *user,uint32_t ref,unsigned index,uint32_t *out)
{
    const C3BattleEntryRecords *r=context(user);if(!r||!output_ok(r,out,sizeof *out))return 0;
    if(!ref){*out=0;return 1;}const C3BattleEntrySideView *s=side(r,ref);if(!s)return 0;
    uint32_t value=index<2?s->trainers[index]:0;uint8_t valid;
    if(!trainer_valid_value(r,value,&valid))return 0;
    *out=valid?value:0;return 1;
}
static int grid_parameters(void *user,uint16_t id,uint8_t count,unsigned index,uint8_t *x,uint8_t *y)
{
    const C3BattleEntryRecords *r=context(user);
    if(!r||!output_ok(r,x,1)||!output_ok(r,y,1)||x==y||index>65535u)return 0;
    const uint8_t *p=row(r,C3_ENTRY_SIDE,id);uint8_t yaw=p?(uint8_t)be16(p):0;int offset=0;
    if(count==2){if(index==0)offset=yaw==1?1:-1;else if(index==1)offset=yaw==1?-1:1;}
    *x=yaw;*y=(uint8_t)offset;return 1;
}
void c3_battle_entry_records_bind(C3BattleTrainerEntryServices *s)
{
    if(!s)return;
    s->action_buff=action_buff;s->floor_status=floor_status;s->encounter=encounter;
    s->target_side=target_side;s->side_valid=side_valid;s->side_status=side_status;
    s->encounter_status=encounter_status;s->trainer_data_valid=trainer_data_valid;
    s->preset_mode=preset_mode;s->sequence_index=sequence_index;s->trainer_valid=trainer_valid;
    s->pokemon_eligible=pokemon_eligible;s->already_entered=already_entered;s->is_gc_hero=is_gc_hero;
    s->side_fighting_count=side_fighting_count;s->valid_trainer=valid_trainer;s->grid_parameters=grid_parameters;
}
