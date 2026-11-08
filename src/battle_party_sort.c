#include "colosseum3ds/battle_party_sort.h"
#include "dolphin/types.h"
#include <string.h>

typedef struct FightTrainerPokemonCopy { u8 data[312]; } FightTrainerPokemonCopy;
typedef struct C3SortContext {
    C3BattlePartySortState *state;
    const C3BattlePartySortState *output;
    const C3BattlePartySortRequest *request;
    const C3BattlePartySortData *data;
    uintptr_t selected[6], candidates[6], sink;
    struct { u32 reference; u8 token; } unknown[12];
    unsigned unknown_count;
    u16 candidate_count;
    u8 category_sink, trainer_token;
    int error;
} C3SortContext;

static u16 be16(const u8 *p) { return (u16)((u16)p[0] << 8 | p[1]); }
static u32 be32(const u8 *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }
static int overlaps(const void *a, size_t as, const void *b, size_t bs)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b; return as&&bs&&(x>=y?x-y<bs:y-x<as); }
static uintptr_t trainer_ptr(C3SortContext *c)
{ return c->request->trainer_reference ? (uintptr_t)&c->trainer_token : 0; }
static uintptr_t hero_ptr(C3SortContext *c)
{ return c->request->hero_reference ? (uintptr_t)c->state->hero : 0; }
static uintptr_t fight_ptr(C3SortContext *c, unsigned i)
{ return i<2&&c->request->fight_references[i] ? (uintptr_t)c->state->fight_pokemon[i] : 0; }
static uintptr_t party_ptr(C3SortContext *c, unsigned i)
{ return i<6&&hero_ptr(c) ? (uintptr_t)(c->state->hero+0x30+i*312u) : 0; }

static uintptr_t from_reference(C3SortContext *c, u32 reference)
{
    if(!reference)return 0;
    if(reference==c->request->trainer_reference)return trainer_ptr(c);
    if(reference==c->request->hero_reference)return hero_ptr(c);
    for(unsigned i=0;i<2;i++)if(reference==c->request->fight_references[i])return fight_ptr(c,i);
    for(unsigned i=0;i<6;i++)if(reference==c->request->party_references[i]&&party_ptr(c,i))return party_ptr(c,i);
    for(unsigned i=0;i<c->unknown_count;i++)if(c->unknown[i].reference==reference)return (uintptr_t)&c->unknown[i].token;
    if(c->unknown_count>=12){c->error=1;return 0;}
    unsigned i=c->unknown_count++;c->unknown[i].reference=reference;
    return (uintptr_t)&c->unknown[i].token;
}

static u32 to_reference(C3SortContext *c, uintptr_t p)
{
    if(!p)return 0;
    if(p==trainer_ptr(c))return c->request->trainer_reference;
    if(p==hero_ptr(c))return c->request->hero_reference;
    for(unsigned i=0;i<2;i++)if(p==fight_ptr(c,i))return c->request->fight_references[i];
    for(unsigned i=0;i<6;i++)if(p==party_ptr(c,i))return c->request->party_references[i];
    for(unsigned i=0;i<c->unknown_count;i++)if(p==(uintptr_t)&c->unknown[i].token)return c->unknown[i].reference;
    c->error=1;return 0;
}

static const u8 *row(C3SortContext *c, u32 table, u32 id)
{
    static const size_t sizes[]={52,40,80,284,44};
    if(table>=C3_PARTY_TABLE_COUNT){c->error=1;return NULL;}
    if(id>=c->data->counts[table])return NULL;
    const u8 *p=NULL;size_t size=0;
    if(c->error || !c->data->lookup || !c->data->lookup(c->data->user,table,id,&p,&size) ||
       !p || size<sizes[table] || overlaps(p,sizes[table],c->output,sizeof(*c->output))){c->error=1;return NULL;}
    return p;
}

static uintptr_t *c3s_slot(C3SortContext *c, uintptr_t base, size_t index)
{
    if(index<6&&(base==(uintptr_t)c->selected||base==(uintptr_t)c->candidates))
        return (uintptr_t*)base+index;
    c->error=1;c->sink=0;return &c->sink;
}
static u8 *c3s_category(C3SortContext *c,u8 *categories,size_t index)
{
    if(index<6)return categories+index;
    c->error=1;c->category_sink=0;return &c->category_sink;
}
static u16 c3s_type_value(C3SortContext *c,u32 a,u32 b)
{ const u8 *p=row(c,C3_PARTY_TYPE,a);return p?be16(p+8+2u*b):0; }

static uintptr_t c3s_src_fightTrainerGetStatus(C3SortContext *c,uintptr_t p,u16 id,u32 field,u16 index)
{
    if(field<0xa){
        const u8 *t=row(c,C3_PARTY_TRAINER,id);if(!t)return 0;
        if(field==2)return be16(t+6);
        if(field==4)return be16(t+2);
    } else if(field==0x1c){
        const u8 *t=row(c,C3_PARTY_ROSTER,id);return t?t[5]:0;
    } else if(field>=0x1f&&field<=0x2a){
        const u8 *t=row(c,C3_PARTY_AI,id);if(!t)return 0;
        switch(field){case 0x1f:return t[0];case 0x21:return t[2];case 0x22:return t[3];case 0x23:return t[4];case 0x2a:return t[11];default:break;}
    } else if(!p&&(field==0x43||field==0x44||field==0x45)){
        return 0;
    } else if(p==trainer_ptr(c)&&p){
        if(field==0x43)return c->request->trainer_id;
        if(field==0x44)return hero_ptr(c);
        if(field==0x45&&index<2)return fight_ptr(c,index);
    }
    c->error=1;return 0;
}

static uintptr_t c3s_src_heroGetStatus(C3SortContext *c,uintptr_t p,u32 field,u32 index)
{
    if(!p)return 0;
    if(p!=hero_ptr(c)){c->error=1;return 0;}
    if(field==1)return p;
    if(field==3&&index<6)return party_ptr(c,index);
    if(field==0xb)return c->state->hero[0xa80];
    c->error=1;return 0;
}

static int is_pokemon(C3SortContext *c,uintptr_t p)
{
    for(unsigned i=0;i<6;i++)if(p==party_ptr(c,i)&&p)return 1;
    for(unsigned i=0;i<2;i++)if(p==fight_ptr(c,i)+4&&fight_ptr(c,i))return 1;
    return 0;
}
static uintptr_t c3s_src_pokemonGetStatus(C3SortContext *c,uintptr_t p,u32 id,u32 field,u32 index)
{
    if(!p){
        const u8 *s=row(c,C3_PARTY_SPECIES,id);if(!s)return 0;
        if(field==1)return be32(s+24);
        if(field==0x16&&index<2)return s[0x30+index];
        c->error=1;return 0;
    }
    if(!is_pokemon(c,p)){c->error=1;return 0;}
    const u8 *b=(const u8*)p;
    switch(field){case 0x6e:return be16(b);case 0x70:return p+8;case 0x7b:return be16(b+0x8a)==0;
    case 0xb6:return b[0xcb];case 0xb8:return b[0xcd];case 0xc9:return be16(b+0xd6);default:c->error=1;return 0;}
}
static u32 c3s_src_gamedataAttestCheckValid(C3SortContext *c,uintptr_t p)
{
    for(unsigned i=0;i<6;i++)if(p==party_ptr(c,i)+8&&party_ptr(c,i)){
        const u8 *b=(const u8*)p;return b[0]&&b[1]&&b[2]&&b[3];
    }
    c->error=1;return 0;
}
static s32 c3s_src_GScharCmp(C3SortContext *c,uintptr_t a,const u16 *empty)
{
    if(a!=hero_ptr(c)||!a||*empty){c->error=1;return 0;}
    return be16((const u8*)a)!=0;
}
static u16 c3s_src_fn_801EF634(C3SortContext *c,uintptr_t ignored)
{ (void)ignored;return c->data->battle_result; }
static u32 c3s_src_fightFloorGetStatus(C3SortContext *c,u32 p,u32 slot,u32 field,u32 index)
{
    if(p||slot||field!=0x14||index){c->error=1;return 0;}
    return c->data->host_side;
}
static s16 c3s_src_fightFloorGetFightPokemonEntryCntInc(C3SortContext *c,u32 ignored)
{
    if(ignored)c->error=1;
    u16 value=c->state->entry_counter;c->state->entry_counter=(u16)(value+1u);
    return value<0x8000u?(s16)value:(s16)((s32)value-65536);
}
static u32 c3s_src_fn_800E0C54(C3SortContext *c)
{u16 value=0;if(c->error||!c3_battle_rng_next16(&c->state->rng,&value))c->error=1;return value;}
static void c3s_src_pokemonInit(C3SortContext *c,uintptr_t p)
{
    for(unsigned i=0;i<6;i++)if(p==(uintptr_t)c->state->scratch[i]){
        if(!c3_battle_pokemon_init((u8*)p))c->error=1;
        return;
    }
    c->error=1;
}
static uintptr_t c3s_src_fightPokemonGetPokemonPtr(C3SortContext *c,uintptr_t p)
{
    if(!p)return 0;
    for(unsigned i=0;i<2;i++)if(p==fight_ptr(c,i))return p+4;
    c->error=1;return 0;
}
static void c3s_src_fightPokemonCreate(C3SortContext *c,uintptr_t dst,uintptr_t source,u32 id)
{
    if(c->error)return;
    if(dst!=fight_ptr(c,0)||!dst||!is_pokemon(c,source)){c->error=1;return;}
    u32 reference=to_reference(c,source);
    s16 entry=(id&65535u)<32768u?(s16)(id&65535u):(s16)((s32)(id&65535u)-65536);
    if(!reference||!c3_battle_fight_pokemon_create((u8*)dst,(const u8*)source,reference,entry))c->error=1;
}
static u8 c3s_src_fightTrainerIsAllyFightTargetPtr(C3SortContext *c,uintptr_t trainer,uintptr_t pokemon,u32 host)
{
    if(!trainer||!pokemon)return 0;
    u32 first=0,second=0;
    u32 a=to_reference(c,trainer),b=to_reference(c,pokemon);
    if(c->error||!c->data->target_side||!c->data->target_side(c->data->user,a,(u16)host,&first)||
       !c->data->target_side(c->data->user,b,(u16)host,&second)){c->error=1;return 0;}
    return first==second;
}

#include "battle_party_sort_original.inc"

static int execute(C3BattlePartySortState *state,const C3BattlePartySortRequest *request,
                   const C3BattlePartySortData *data,unsigned operation)
{
    if(!state||!request||!data||overlaps(state,sizeof(*state),request,sizeof(*request))||
       overlaps(state,sizeof(*state),data,sizeof(*data))||request->filter>3||request->selection_mode>6||
       (operation!=0&&request->selection_count>6))return 0;
    for(unsigned i=0;i<C3_PARTY_TABLE_COUNT;i++)if(data->counts[i]>65536u)return 0;
    if(data->counts[C3_PARTY_TYPE]>18u)return 0;
    const u32 refs[]={request->trainer_reference,request->hero_reference,request->fight_references[0],request->fight_references[1],
        request->party_references[0],request->party_references[1],request->party_references[2],request->party_references[3],request->party_references[4],request->party_references[5]};
    for(unsigned i=0;i<10;i++)for(unsigned j=0;j<i;j++)if(refs[i]&&refs[i]==refs[j])return 0;
    if(request->hero_reference)for(unsigned i=0;i<6;i++)if(!request->party_references[i])return 0;
    C3BattlePartySortState staged=*state;
    C3SortContext c={0};c.state=&staged;c.output=state;c.request=request;c.data=data;
    c.candidate_count=state->candidate_count;
    for(unsigned i=0;i<6;i++){c.selected[i]=from_reference(&c,state->selected[i]);c.candidates[i]=from_reference(&c,state->candidates[i]);}
    if(operation==0)c3s_src_fightTrainerSortFightTrainerDataIdToHeroTemotiPokemon(&c,trainer_ptr(&c),request->selection_count,request->selection_mode);
    else if(operation==1)c3s_src_fn_802331F4(&c,trainer_ptr(&c),hero_ptr(&c),(uintptr_t)c.selected,request->selection_count,request->selection_mode);
    else c3s_src_fn_80233DB0(&c,trainer_ptr(&c),hero_ptr(&c),(uintptr_t)c.selected,request->selection_count,(char)request->filter,(uintptr_t)c.candidates);
    for(unsigned i=0;i<6;i++){staged.selected[i]=to_reference(&c,c.selected[i]);staged.candidates[i]=to_reference(&c,c.candidates[i]);}
    staged.candidate_count=c.candidate_count;
    if(c.error)return 0;
    *state=staged;return 1;
}
int c3_battle_party_sort(C3BattlePartySortState *s,const C3BattlePartySortRequest *r,const C3BattlePartySortData *d)
{return execute(s,r,d,0);}
int c3_battle_party_select(C3BattlePartySortState *s,const C3BattlePartySortRequest *r,const C3BattlePartySortData *d)
{return execute(s,r,d,1);}
int c3_battle_party_candidates(C3BattlePartySortState *s,const C3BattlePartySortRequest *r,const C3BattlePartySortData *d)
{return execute(s,r,d,2);}
