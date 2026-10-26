#include "colosseum3ds/battle_trainer.h"
#include "dolphin/types.h"
#include <stdatomic.h>
#include <string.h>

typedef struct C3ItemWord { u8 bytes[4]; } C3ItemWord;
static atomic_flag in_use=ATOMIC_FLAG_INIT;
static struct {
    const C3BattleTrainerData *data;
    const C3BattleTrainerRequest *request;
    C3BattleRng rng;
    u8 *scratch;
    const u8 *normal_name,*aux_name;
    C3BattlePokemonData current;
    struct { u16 species; u32 name; } known[7];
    unsigned known_count,calls;
    u32 species_count;
    int error;
    void *outputs[3]; size_t output_sizes[3];
} context;
static u16 be16(const u8 *p){return (u16)((u16)p[0]<<8|p[1]);}
static u32 be32(const u8 *p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static void put16(u8 *p,u32 v){p[0]=(u8)(v>>8);p[1]=(u8)v;}
static void put32(u8 *p,u32 v){p[0]=(u8)(v>>24);p[1]=(u8)(v>>16);p[2]=(u8)(v>>8);p[3]=(u8)v;}
static int overlap(const void *a,size_t as,const void *b,size_t bs)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return as&&bs&&(x>=y?x-y<bs:y-x<as);}
static int borrowed(const void *p,size_t size)
{
    if(!p)return !size;
    for(unsigned i=0;i<3;i++)if(overlap(p,size,context.outputs[i],context.output_sizes[i]))return 0;
    return 1;
}
static int plain_name(const u8 *p,size_t size)
{
    if(!p||size<2||(size&1)||!borrowed(p,size))return 0;
    for(size_t i=0;i<size;i+=2){u16 n=be16(p+i);if(!n)return i+2==size;if(n==0xffff)return 0;}
    return 0;
}
static void name_copy(u8 *dst,const u8 *src)
{
    if(!src)return;
    size_t i=0;for(;i<10&&be16(src+2*i);i++)memcpy(dst+2*i,src+2*i,2);
    memset(dst+2*i,0,22-2*i);
}
static uintptr_t c3t_src_heroGetStatus(u8 *hero,u32 field,u32 index)
{
    static const unsigned item_offsets[]={0x780,0x7d0,0x87c,0x8bc,0x9bc,0xa74,0xa9a};
    static const unsigned item_counts[]={20,43,16,64,46,3,10};
    if(!hero){context.error=1;return 0;}
    if(field==1)return (uintptr_t)hero;
    if(field==2)return be32(hero+0x2c);
    if(field==3){if(index<6)return (uintptr_t)(hero+0x30+312u*index);}
    if(field>=4&&field<=10&&index<item_counts[field-4])return (uintptr_t)(hero+item_offsets[field-4]+index*4);
    if(field==0xb)return hero[0xa80];
    context.error=1;return 0;
}
static void c3t_src_heroSetStatus(u8 *hero,u32 field,uintptr_t v)
{
    if(!hero){context.error=1;return;}
    if(field==1){name_copy(hero,(const u8*)v);return;}
    if(field==2){put32(hero+0x2c,(u32)v);return;}
    if(field==0xb){hero[0xa80]=(u8)v;return;}
    if(field>=0xc&&field<=0xe){s32 n=(s32)v;if(n<0)n=0;if(n>0x98967f)n=0x98967f;put32(hero+0xa84+(field-0xc)*4,(u32)n);return;}
    if(field>=0xf&&field<=0x16){hero[0xa90+field-0xf]=(u8)v;return;}
    if(field==0x17){name_copy(hero+0xac2,(const u8*)v);return;}
    if(field==0x18){hero[0xa98]=(u8)v;return;}
    if(field==0x19){hero[0xa81]=(u8)v;return;}
    context.error=1;
}
static u32 c3t_src_fn_800E0C54(void)
{u16 n=0;if(!c3_battle_rng_next16(&context.rng,&n))context.error=1;return n;}
static uintptr_t c3t_src_GSmsgGetGSchar(u32 id)
{
    if(id==be32(context.data->trainer+8))return (uintptr_t)context.normal_name;
    if(id==0xfa2)return (uintptr_t)context.aux_name;
    context.error=1;return 0;
}
static u32 c3t_src_fn_800896B8(void){return context.data->special_trainer_id;}
static void *c3t_src_fn_800896C8(void){return (void*)context.data->special_name;}
static void *c3t_src_savedataGetStatus(u32 arg,u32 field)
{if(arg||field!=2){context.error=1;return NULL;}return (void*)context.data->saved_player;}
static void c3t_src_pokemonBiosCopy(void *dst,const void *src){if(dst&&src)memmove(dst,src,312);}
static void c3t_src_pokemonInitAry(uintptr_t ptr,u16 count)
{
    if(!ptr)return;
    for(u16 i=0;i<count;i++)if(!c3_battle_pokemon_init((u8*)ptr+312u*i))context.error=1;
}
static uintptr_t c3t_src_pokemonGetStatus(u8 *p,u32 id,u32 field,u32 index)
{
    (void)index;
    if(!p){if(field==1)for(unsigned i=0;i<context.known_count;i++)if(context.known[i].species==id)return context.known[i].name;context.error=1;return 0;}
    if(field==0x6e)return be16(p);
    if(field==0x70)return (uintptr_t)(p+8);
    if(field==0x7a)return p[0x60];
    if(field==0xb8)return p[0xcd];
    context.error=1;return 0;
}
static void c3t_src_pokemonSetStatus(u8 *p,u32 id,u32 field,u32 index,uintptr_t v)
{
    (void)id;(void)index;
    if(!p){context.error=1;return;}
    switch(field){
    case 0x71:put16(p+0xc,(u16)v);return;
    case 0x72:p[0xe]=(u8)v;return;case 0x73:p[0xf]=(u8)v;return;
    case 0x74:p[0x10]=(u8)v;return;case 0x75:put32(p+0x14,(u32)v);return;
    case 0x76:name_copy(p+0x18,(const u8*)v);return;
    case 0xc9:put16(p+0xd6,(u16)v);return;
    default:context.error=1;
    }
}
static u32 c3t_src_gamedataAttestCheckValid(uintptr_t ptr)
{const u8 *p=(const u8*)ptr;return p&&p[0]&&p[1]&&p[2]&&p[3];}
static u32 c3t_src_pcboxAddPokemon(u32 a,void *p,s32 slot)
{(void)a;(void)p;(void)slot;context.error=1;return 0;}
static uintptr_t c3t_src_fightTrainerGetStatus(void *ptr,u16 id,u32 field,u16 index)
{
    if(ptr){context.error=1;return 0;}
    if(field==0x12){if(!context.current.roster){context.error=1;return 0;}return be16(context.current.roster+12);}
    if(id!=context.request->trainer_id){context.error=1;return 0;}
    const u8 *t=context.data->trainer;
    switch(field){case 1:return t[0];case 2:return be16(t+6);case 3:return be32(t+8);case 4:return be16(t+2);case 5:return be16(t+4);case 6:if(index<8)return be16(t+0x14+2u*index);break;}
    context.error=1;return 0;
}
static int pokemon_message(void *user,u32 id,const u8 **bytes,size_t *size)
{
    const C3BattlePokemonData *d=user;
    if(!d->message||!d->message(d->user,id,bytes,size)||!plain_name(*bytes,*size)){context.error=1;return 0;}
    return 1;
}
static u8 c3t_src_fightTrainerCreateFightTrainerPokemonDataIdToPokemon(u16 id,void *backing,u32 trainer)
{
    if(context.error)return 0;
    memset(&context.current,0,sizeof(context.current));
    if(++context.calls>7||!context.data->roster||!context.data->roster(context.data->user,id,&context.current)||!context.current.roster||!borrowed(context.current.roster,80)){context.error=1;return 0;}
    if(!be16(context.current.roster+10))return 0;
    const C3BattlePokemonData *d=&context.current;
    const void *rows[]={d->species,d->moves,d->growth,d->natures,d->nature_rates};
    if(d->move_count>65536||d->rate_count>256){context.error=1;return 0;}
    const size_t sizes[]={284,(size_t)d->move_count*56,404,1000,(size_t)d->rate_count*2};
    for(unsigned i=0;i<5;i++)if(!rows[i]||!borrowed(rows[i],sizes[i])){context.error=1;return 0;}
    if(context.species_count&&context.species_count!=d->species_count){context.error=1;return 0;}
    context.species_count=d->species_count;
    if(context.known_count>=7){context.error=1;return 0;}
    context.known[context.known_count].species=be16(d->roster+10);
    context.known[context.known_count++].name=be32(d->species+24);
    C3BattlePokemonRequest req={id,trainer,{0}};memcpy(req.attest,context.request->attest,4);
    C3BattlePokemonData bound=*d;bound.message=pokemon_message;bound.user=&context.current;
    if(!c3_battle_pokemon_create_pre_catch(backing,&req,&bound,&context.rng)){context.error=1;return 0;}
    return 1;
}
static s32 c3t_src_itemGetStatus(void *entry,u16 id,u16 field,u32 index)
{
    (void)index;
    if(entry){if(field==0x1b)return be16(entry);if(field==0x1c)return be16((u8*)entry+2);context.error=1;return 0;}
    if(id>=context.data->item_count){
        if(field==2)context.error=1;
        return 0;
    }
    u32 name=0;u8 kind=0;
    if(!context.data->item||!context.data->item(context.data->user,id,&name,&kind)){context.error=1;return 0;}
    if(field==1)return (s32)name;
    if(field==2)return kind;
    context.error=1;return 0;
}
static void c3t_src_fn_80142B24(void *entry,u32 a,u16 field,u32 index,u32 value)
{
    (void)a;(void)index;
    if(!entry)return;
    if(field==0x1b)put16(entry,value);
    else if(field==0x1c)put16((u8*)entry+2,value);
    else context.error=1;
}
#include "battle_trainer_original.inc"

int c3_battle_trainer_create(u8 hero[C3_BATTLE_TRAINER_BYTES],u8 scratch[312],
    const C3BattleTrainerRequest *request,const C3BattleTrainerData *data,C3BattleRng *rng)
{
    if(!hero||!scratch||!request||!data||!data->trainer||!rng||data->item_count>65536)return 0;
    void *outputs[]={hero,scratch,rng};const size_t sizes[]={0xb18,312,sizeof(*rng)};
    for(unsigned i=0;i<3;i++){
        for(unsigned j=i+1;j<3;j++)if(overlap(outputs[i],sizes[i],outputs[j],sizes[j]))return 0;
        if(overlap(outputs[i],sizes[i],request,sizeof(*request))||overlap(outputs[i],sizes[i],data,sizeof(*data)))return 0;
    }
    if(atomic_flag_test_and_set_explicit(&in_use,memory_order_acquire))return 0;
    memset(&context,0,sizeof(context));context.data=data;context.request=request;context.rng=*rng;
    for(unsigned i=0;i<3;i++){context.outputs[i]=outputs[i];context.output_sizes[i]=sizes[i];}
    int ok=borrowed(data->trainer,52)&&borrowed(data->saved_player,data->saved_player?0xb18:0);
    u16 selector2=be16(data->trainer+6),kind=be16(data->trainer+2);
    int generated=request->trainer_id&&(selector2||request->input_device)&&kind!=1;
    if(ok&&generated){
        size_t size=0;
        ok=data->message&&data->message(data->user,be32(data->trainer+8),&context.normal_name,&size)&&plain_name(context.normal_name,size);
        if(ok)ok=data->message(data->user,0xfa2,&context.aux_name,&size)&&plain_name(context.aux_name,size);
        if(ok&&request->trainer_id==data->special_trainer_id)ok=plain_name(data->special_name,data->special_name_size);
    }
    u8 staged_hero[0xb18],staged_scratch[312];
    memcpy(staged_hero,hero,sizeof(staged_hero));memcpy(staged_scratch,scratch,sizeof(staged_scratch));context.scratch=staged_scratch;
    if(ok)c3t_src_fightTrainerCreateFightTrainerDataIdToHero(request->trainer_id,request->input_device,staged_hero);
    ok=ok&&!context.error;
    if(ok){memcpy(hero,staged_hero,sizeof(staged_hero));memcpy(scratch,staged_scratch,sizeof(staged_scratch));*rng=context.rng;}
    memset(&context,0,sizeof(context));atomic_flag_clear_explicit(&in_use,memory_order_release);
    return ok;
}

int c3_battle_trainer_init_hero(u8 hero[C3_BATTLE_TRAINER_BYTES])
{
    if(!hero||atomic_flag_test_and_set_explicit(&in_use,memory_order_acquire))return 0;
    u8 staged[C3_BATTLE_TRAINER_BYTES];memcpy(staged,hero,sizeof(staged));
    memset(&context,0,sizeof(context));
    c3t_src_heroInit(staged);
    int ok=!context.error;
    if(ok)memcpy(hero,staged,sizeof(staged));
    memset(&context,0,sizeof(context));
    atomic_flag_clear_explicit(&in_use,memory_order_release);
    return ok;
}
