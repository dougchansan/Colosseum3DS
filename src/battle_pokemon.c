#include "colosseum3ds/battle_pokemon.h"
#include "dolphin/types.h"
#include <stdatomic.h>
#include <string.h>

/* The source status ABI mixes scalars and addresses. uintptr_t is used only
 * inside this host boundary; original target addresses are never host pointers. */
static atomic_flag in_use = ATOMIC_FLAG_INIT;
static struct {
    const C3BattlePokemonRequest *request;
    const C3BattlePokemonData *data;
    C3BattleRng rng;
    unsigned attempts;
    int error;
    const uint8_t *names[2];
    size_t name_sizes[2];
} context;

static u16 read16(const u8 *p) { return (u16)((u16)p[0] << 8 | p[1]); }
static u32 read32(const u8 *p)
{ return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | p[3]; }
static void write16(u8 *p, u32 v) { p[0]=(u8)(v>>8); p[1]=(u8)v; }
static void write32(u8 *p, u32 v)
{ p[0]=(u8)(v>>24); p[1]=(u8)(v>>16); p[2]=(u8)(v>>8); p[3]=(u8)v; }
static s32 signed8(u8 v) { return v<128 ? v : (s32)v-256; }
static s32 signed16(const u8 *p) { u32 v=read16(p); return v<32768?(s32)v:(s32)v-65536; }

static uintptr_t c3p_src_pokemonGetStatus(u8*,u32,u32,u32);
static void c3p_src_pokemonSetStatus(u8*,u32,u32,u32,uintptr_t);
static u8 c3p_src_pokemonGetLevelFromExp(u8*);

static u8 move_pp(u16 id)
{
    if(!id&&!context.data)return 0;
    if (id>=context.data->move_count) { context.error=1; return 0; }
    return context.data->moves[(size_t)id*56+1];
}
static u8 c3p_src_wazaGetMaxPP(u16 id,u8 ups)
{ u8 pp=move_pp(id); return (u8)(pp+ups*(pp*20)/100); }
static u16 c3p_src_fn_800E0C54(void)
{
    u16 v=0;
    if (!c3_battle_rng_next16(&context.rng,&v)) context.error=1;
    return v;
}
static u32 c3p_personality(void)
{
    /* Original PPC calls the right operand first. C operand order is not an ABI. */
    u32 high=c3p_src_fn_800E0C54();
    u32 low=c3p_src_fn_800E0C54();
    return high<<16 | low;
}
static u8 c3p_src_sexGetPokemonSexRaitoKotei(u32 id)
{ static const u8 fixed[3]={0,254,255}; if(id>2){context.error=1;return 0;} return fixed[id]; }
static u8 *c3p_src_pokemonGrowDataBiosGetPtr(u8 id)
{ if(id!=context.data->species[0]){context.error=1;return NULL;} return (u8*)context.data->growth; }
static u32 c3p_src_pokemonGrowDataBiosGetExp(void *row,u8 level)
{ if(!row||level>100){context.error=1;return 0;}return read32((u8*)row+4u*level); }
static u8 *c3p_src_pokemonSeikakuDataBiosGetPtr(u32 id)
{ if(id>=25){context.error=1;return NULL;}return (u8*)context.data->natures+40u*id; }
static u8 *c3p_src_pokemonSeikakuRateDataBiosGetPtr(u32 id)
{ if(id>=context.data->rate_count){context.error=1;return NULL;}return (u8*)context.data->nature_rates+2u*id; }
static u8 c3p_src_pokemonSeikakuDataBiosGetPhyAtkRateDataId(u8 *p){return p[5];}
static u8 c3p_src_pokemonSeikakuDataBiosGetPhyDefRateDataId(u8 *p){return p[6];}
static u8 c3p_src_pokemonSeikakuDataBiosGetSpeAtkRateDataId(u8 *p){return p[7];}
static u8 c3p_src_pokemonSeikakuDataBiosGetSpeDefRateDataId(u8 *p){return p[8];}
static u8 c3p_src_pokemonSeikakuDataBiosGetNimblenessRateDataId(u8 *p){return p[9];}
static u8 c3p_src_pokemonSeikakuRateDataBiosGetKake(u8 *p){return p[0];}
static u8 c3p_src_pokemonSeikakuRateDataBiosGetWaru(u8 *p){return p[1];}

static uintptr_t c3p_src_gamedataGetStatus(u32 kind,u32 field)
{ if(kind||field!=1)context.error=1;return (uintptr_t)context.request->attest; }
static void c3p_src_gamedataAttestBiosCopy(void *dst,const void *src)
{ if(!dst||!src){context.error=1;return;}memcpy(dst,src,4); }
static void c3p_src_gamedataAttestInit(uintptr_t ptr)
{ memset((void*)ptr,0,4); }
static void c3p_src_fn_8011B950(uintptr_t ptr,u16 count)
{
    /* Seven source setters clear fields, preserving pad7 and padE. */
    u8 *p=(u8*)ptr;
    for(u16 i=0;i<count;i++,p+=16){memset(p,0,7);memset(p+8,0,6);}
}
static void c3p_src_pokemonBiosSetEventGetFlag(u8 *p,u8 value){write32(p+0xf8,value);}
static u8 *c3p_src_pokemonBiosGetPokemonWazaPtr(void *ptr,u32 slot,u32 mode)
{ (void)mode;if(slot>=4){context.error=1;return NULL;}return (u8*)ptr+0x78+slot*4; }
static void c3p_src_pokemonWazaBiosCopy(void *dst,const void *src){memmove(dst,src,4);}

typedef struct FightTrainerStatusIds { u16 id[6]; } FightTrainerStatusIds;
static const FightTrainerStatusIds lbl_80279C48={{0x93,0x94,0x95,0x96,0x97,0x98}};
static const FightTrainerStatusIds lbl_80279C54={{0x8d,0x8e,0x8f,0x90,0x91,0x92}};
static s32 c3p_src_fightTrainerGetStatus(void *ptr,u16 roster,u32 field,u16 index)
{
    const u8 *p=context.data->roster;
    if(ptr||roster!=context.request->roster_id){context.error=1;return 0;}
    switch(field){
    case 0xe:return (s32)read32(p+20);
    case 0xf:if(index<6)return signed8(p[28+index]);break;
    case 0x10:if(index<6)return signed16(p+34+index*2);break;
    case 0x11:return p[4];case 0x13:return p[3];case 0x14:return signed8(p[0]);
    case 0x15:return read16(p+10);case 0x16:return (s32)read32(p+16);
    case 0x17:if(index<4)return (s32)read32(p+52+index*8);break;
    case 0x18:if(index<4)return p[48+index*8];break;
    case 0x19:return signed16(p+8);case 0x1a:return signed8(p[1]);case 0x1b:return signed8(p[2]);
    }
    context.error=1;return 0;
}
static uintptr_t c3p_src_GSmsgGetGSchar(u32 id)
{
    if(id==read32(context.data->species+24))return (uintptr_t)context.names[0];
    if(id==read32(context.data->roster+20))return (uintptr_t)context.names[1];
    context.error=1;return 0;
}
static void name_copy(u8 *dst,const u8 *src)
{
    /* Admitted plain GSchar strings. Original LenCpy zero-fills all 11 words. */
    size_t i=0;
    for(;i<10&&read16(src+i*2);i++)memcpy(dst+i*2,src+i*2,2);
    memset(dst+i*2,0,22-i*2);
}
/* Shadow services must remain unreachable: no fake cache/PID/IV authority. */
static u8 c3p_src_fn_801EE8F4(u16 id){(void)id;context.error=1;return 0;}
static u32 c3p_src_fn_801EE750(u16 id){(void)id;context.error=1;return 0;}
static s8 c3p_src_fn_801EE824(u16 id,u16 i){(void)id;(void)i;context.error=1;return 0;}
static void c3p_src_fn_801EE7BC(u16 id,u32 v){(void)id;(void)v;context.error=1;}
static void c3p_src_fn_801EE894(u16 id,u16 i,s8 v){(void)id;(void)i;(void)v;context.error=1;}
static void c3p_src_pokemonSetDarkPokemonStatus(void *p,u16 id){(void)p;(void)id;context.error=1;}

static int field_offset(u32 selector,u32 *offset,u32 *width)
{
    *width=2;
    if(selector>=0x87&&selector<=0x98){*offset=0x8c+(selector-0x87)*2;return 1;}
    if(selector>=0xa3&&selector<=0xaf){*offset=0xbd+selector-0xa3;*width=1;return 1;}
    if(selector>=0xb0&&selector<=0xb4){*offset=0xb7+selector-0xb0;*width=1;return 1;}
    if(selector>=0xb5&&selector<=0xb9){*offset=0xca+selector-0xb5;*width=1;return 1;}
    switch(selector){
    case 0x6e:*offset=0;break;case 0x6f:*offset=4;*width=4;break;
    case 0x71:*offset=0xc;break;case 0x72:*offset=0xe;*width=1;break;
    case 0x73:*offset=0xf;*width=1;break;case 0x74:*offset=0x10;*width=1;break;
    case 0x75:*offset=0x14;*width=4;break;case 0x79:*offset=0x5c;*width=4;break;
    case 0x7a:*offset=0x60;*width=1;break;case 0x7d:*offset=0x74;*width=4;break;
    case 0x82:*offset=0x88;break;case 0x83:*offset=0x8a;break;case 0x99:*offset=0xb0;break;
    case 0x9c:case 0x9d:case 0x9e:case 0x9f:case 0xa0:*offset=0xb2+selector-0x9c;*width=1;break;
    case 0xa1:*offset=0xbc;*width=1;break;case 0xbb:*offset=0xcf;*width=1;break;
    case 0xbc:*offset=0xd0;*width=1;break;case 0xbd:*offset=0xd2;break;case 0xbe:*offset=0xd4;break;
    case 0xc9:*offset=0xd6;break;case 0xc3:*offset=0xd8;break;
    case 0xc5:*offset=0xdc;*width=4;break;case 0xc6:*offset=0xe0;*width=4;break;
    case 0xc7:*offset=0xe4;break;default:return 0;
    }return 1;
}

static uintptr_t c3p_src_pokemonGetStatus(u8 *p,u32 id,u32 selector,u32 index)
{
    u32 offset,width;
    if(!p){
        const u8 *s=context.data->species;
        if(id!=read16(context.data->roster+10)){context.error=1;return 0;}
        if(selector==1)return read32(s+24);
        if(selector>=3&&selector<=8)return read16(s+0x84+(selector-3)*2);
        switch(selector){
        case 0x11:return s[0];case 0x13:return s[2];case 0x14:return read16(s+8);
        case 0x17:if(index<2)return s[0x32+index];break;
        case 0x1d:if(index<20)return s[0xba+4*index];break;
        case 0x1e:if(index<20)return read16(s+0xbc+4*index);break;
        }context.error=1;return 0;
    }
    if(selector==0x70)return (uintptr_t)(p+8);
    if(selector==0x7c)return (uintptr_t)(p+0x64);
    if(selector==0xc8)return (uintptr_t)(p+0xe8);
    if(selector==0xbf)return read32(p+4)%25;
    if(selector==0xc0)return c3p_src_pokemonGetLevelFromExp(p);
    if(selector>=0x7f&&selector<=0x81){
        if(index>=4)index-=4;
        if(index>=4){context.error=1;return 0;}
        p+=0x78+4*index;
        return selector==0x7f?read16(p):p[selector-0x7e];
    }
    if(!field_offset(selector,&offset,&width)){context.error=1;return 0;}
    return width==4?read32(p+offset):width==2?read16(p+offset):p[offset];
}

static void c3p_src_pokemonSetStatus(u8 *p,u32 id,u32 selector,u32 index,uintptr_t value)
{
    u32 offset,width,v=(u32)value;(void)id;
    if(selector==0x76){name_copy(p+0x18,(const u8*)value);return;}
    if(selector==0x77){name_copy(p+0x2e,(const u8*)value);name_copy(p+0x44,(const u8*)value);return;}
    if(selector>=0x7f&&selector<=0x81){
        if(index>=4){context.error=1;return;}u8 *m=p+0x78+index*4;
        if(selector==0x7f)write16(m,v);
        else if(selector==0x80){u8 max=c3p_src_wazaGetMaxPP(read16(m),m[3]);m[2]=(u8)(v>max?max:v);}
        else {v=(u8)v;if(v>3)v=3;u16 move=read16(m);if(move&&move!=0x163&&move_pp(move)<=4)return;m[3]=(u8)v;}
        return;
    }
    if(!field_offset(selector,&offset,&width)){context.error=1;return;}
    if(selector==0x7a&&v>100)v=100;
    if((selector>=0x8d&&selector<=0x92)||selector==0x99||selector==0xc7){v=(u16)v;if(v>255)v=255;}
    if(selector>=0x93&&selector<=0x98){v=(u16)v;if(v>31)v=31;}
    if(selector==0x83){v=(u16)v;u16 max=read16(p+0x8c);if(v>max)v=max;}
    if(selector==0xb7&&context.data&&!context.data->species[0x33])v=0;
    if(selector==0xc5&&(s32)v>=0x639c)v=0x639c;
    if(width==4)write32(p+offset,v);else if(width==2)write16(p+offset,v);else p[offset]=(u8)v;
}

#include "battle_pokemon_original.inc"

static int overlaps(const void *a,size_t as,const void *b,size_t bs)
{ uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return as&&bs&&(x>=y?x-y<bs:y-x<as); }
static int plain_name(u32 id,const C3BattlePokemonData *d,const u8 **p,size_t *size)
{
    if(!id||!d->message(d->user,id,p,size)||!*p||*size<2||(*size&1))return 0;
    for(size_t i=0;i<*size;i+=2){u16 code=read16(*p+i);if(!code)return i+2==*size;if(code==0xffff)return 0;}
    return 0;
}
int c3_battle_pokemon_create_pre_catch(u8 backing[C3_BATTLE_POKEMON_BYTES],
                                     const C3BattlePokemonRequest *request,
                                     const C3BattlePokemonData *data,C3BattleRng *rng)
{
    if(!backing||!request||!data||!rng||!request->roster_id||!data->roster||!data->species||
       !data->moves||!data->growth||!data->natures||!data->nature_rates||!data->message||
       !data->move_count||data->move_count>65536||!data->rate_count||data->rate_count>256||
       overlaps(backing,312,rng,sizeof(*rng))||overlaps(backing,312,request,sizeof(*request))||
       overlaps(backing,312,data,sizeof(*data))||overlaps(rng,sizeof(*rng),request,sizeof(*request))||
       overlaps(rng,sizeof(*rng),data,sizeof(*data)))return 0;
    const u8 *rows[]={data->roster,data->species,data->moves,data->growth,data->natures,data->nature_rates};
    const size_t sizes[]={80,284,(size_t)data->move_count*56,404,1000,(size_t)data->rate_count*2};
    for(unsigned i=0;i<6;i++)if(overlaps(backing,312,rows[i],sizes[i])||overlaps(rng,sizeof(*rng),rows[i],sizes[i]))return 0;
    const u8 *r=data->roster;u16 species=read16(r+10);
    if(r[3]||!species||species>=data->species_count||!read32(data->species+24)||r[4]>100||
       signed8(r[0])>1||signed8(r[1])>2||signed8(r[2])>24)return 0;
    for(unsigned i=0;i<6;i++)if(signed8(r[28+i])>31)return 0;
    for(unsigned i=0;i<25;i++)for(unsigned j=5;j<=9;j++)if(data->natures[i*40+j]>=data->rate_count)return 0;
    if(atomic_flag_test_and_set_explicit(&in_use,memory_order_acquire))return 0;
    context.data=data;context.request=request;context.rng=*rng;context.error=0;context.attempts=0;
    int ok=plain_name(read32(data->species+24),data,&context.names[0],&context.name_sizes[0]);
    if(ok&&read32(r+20))ok=plain_name(read32(r+20),data,&context.names[1],&context.name_sizes[1]);
    for(unsigned i=0;ok&&i<2;i++)if(context.names[i]&&
       (overlaps(backing,312,context.names[i],context.name_sizes[i])||
        overlaps(rng,sizeof(*rng),context.names[i],context.name_sizes[i])))ok=0;
    u8 staged[312];memcpy(staged,backing,312);
    if(ok)ok=c3p_src_fightTrainerCreateFightTrainerPokemonDataIdToPokemon(request->roster_id,staged,request->opponent_trainer_id);
    ok=ok&&!context.error;
    if(ok){memcpy(backing,staged,312);*rng=context.rng;}
    memset(&context,0,sizeof(context));
    atomic_flag_clear_explicit(&in_use,memory_order_release);
    return ok;
}

int c3_battle_pokemon_init(u8 backing[C3_BATTLE_POKEMON_BYTES])
{
    if(!backing || atomic_flag_test_and_set_explicit(&in_use,memory_order_acquire))return 0;
    u8 staged[C3_BATTLE_POKEMON_BYTES];
    memcpy(staged,backing,sizeof(staged));
    memset(&context,0,sizeof(context));
    c3p_src_pokemonInit(staged);
    int ok=!context.error;
    if(ok)memcpy(backing,staged,sizeof(staged));
    memset(&context,0,sizeof(context));
    atomic_flag_clear_explicit(&in_use,memory_order_release);
    return ok;
}
