#include "colosseum3ds/game_items.h"
#include "game_state_internal.h"
#include "save_codec.h"
#include "dolphin/types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ITEM_MAGIC UINT32_C(0x43334954)
#define ITEM_HEADER 128u
#define ITEM_STRIDE 128u
#define HERO 0x70u
#define PARTY 0xa0u
#define POKEMON_BYTES 0x138u
#define NORMAL_BYTES 80u
#define NORMAL_OFFSET (HERO + 0x780u)

typedef struct ItemCatalogImpl {
    uint32_t magic;
    uint8_t bytes[C3_ITEM_CATALOG_SIZE];
} ItemCatalogImpl;

static uint16_t be16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0]<<8 | p[1]); }
static uint32_t be32(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void put16(uint8_t *p,uint16_t v)
{ p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static int fail(char *error,const char *message)
{ if(error)snprintf(error,C3_GAME_ERROR_SIZE,"%s",message); return 0; }
static void clear_error(char *error) { if(error)error[0]=0; }

/* PokemonBios byte accessors for the unchanged pinned HP helper. There is no
 * global context, pointer truncation, battle object or RNG dependency. */
static u16 c3fi_src_pokemonBiosGetHp(void *p) { return be16((uint8_t*)p+0x8a); }
static u16 c3fi_src_pokemonBiosGetMaxHp(void *p) { return be16((uint8_t*)p+0x8c); }
static void c3fi_src_pokemonBiosSetHp(void *p,u16 hp) { put16((uint8_t*)p+0x8a,hp); }
/* Programming-invariant traps, NOT successful combat stubs. The sole call
 * below passes fightPokemon=NULL, so original control flow cannot reach these.
 * No external caller can invoke the generated helper through this module. */
static void *c3fi_src_fightFloorGetFightPokemonPtrToFightTrainerPtr(s32 n,void *p)
{ (void)n; (void)p; abort(); }
static void *c3fi_src_fightTrainerCheckFightPokemonFightOut(void *t,void *p)
{ (void)t; (void)p; abort(); }
static void c3fi_src_fn_802331A4(void *p,s32 n)
{ (void)p; (void)n; abort(); }
#include "field_items_original.h"

static const uint8_t *record(const C3ItemCatalog *catalog,unsigned id)
{
    const ItemCatalogImpl *c=catalog?catalog->impl:NULL;
    if(!c || c->magic!=ITEM_MAGIC || id>=C3_ITEM_COUNT)return NULL;
    return c->bytes+ITEM_HEADER+id*ITEM_STRIDE;
}
static int supported(const uint8_t *r,unsigned id)
{
    /* Field-only normal restoratives. No berries, herbs/friendship, PP, level,
     * EV, evolution, key-item callbacks, or battle effects are approximated. */
    if(!r || id<13 || id>29 || r[0]!=2 || r[1] || r[2] ||
       be32(r+28)!=0x80023b9cu || r[36] || r[37] || r[38])return 0;
    const uint8_t *p=r+40;
    for(unsigned i=0;i<16;++i)
        if(i!=3 && i!=4 && i!=10 && p[i])return 0;
    if((p[3]&0xc0u) || (p[4]&~0x40u) || p[10]==0xfdu)return 0;
    return p[10]!=0 || (p[3]&0x3eu)!=0;
}
void c3_item_catalog_free(C3ItemCatalog *catalog)
{
    if(catalog){free(catalog->impl); catalog->impl=NULL;}
}
int c3_item_catalog_decode(C3ItemCatalog *catalog,const void *data,size_t length,
                           char error[C3_GAME_ERROR_SIZE])
{
    static const uint8_t pin[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,
        0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    static const uint8_t dol[20]={0x87,0x0e,0x8b,0x96,0x93,0xca,0x78,0x07,0x82,0xd8,
        0x0f,0x22,0xa6,0xa4,0x57,0x2d,0x8b,0xa9,0x45,0x8f};
    static const uint8_t rel[20]={0xe8,0x16,0x76,0x18,0x13,0x90,0x0a,0xa0,0xfd,0x4f,
        0x87,0xd0,0x12,0xb3,0xb2,0xa1,0x9d,0x2f,0x00,0x3e};
    const uint8_t *p=data;uint8_t digest[20];
    if(!catalog || !data || length!=C3_ITEM_CATALOG_SIZE)
        return fail(error,"Invalid item catalog size/arguments.");
    if(memcmp(p,"C3IT",4) || be32(p+4)!=1 || be32(p+8)!=length ||
       be32(p+12)!=C3_ITEM_COUNT || be32(p+16)!=ITEM_STRIDE || be32(p+20)!=ITEM_HEADER ||
       memcmp(p+24,pin,20) || memcmp(p+44,dol,20) || memcmp(p+64,rel,20))
        return fail(error,"Unsupported item catalog source/layout.");
    for(unsigned i=104;i<128;++i)if(p[i])return fail(error,"Invalid catalog reserved header.");
    c3_save_codec_sha1(digest,p+ITEM_HEADER,(uint32_t)length-ITEM_HEADER);
    if(memcmp(digest,p+84,20))return fail(error,"Item catalog checksum failed.");
    for(unsigned i=0;i<C3_ITEM_COUNT;++i){
        const uint8_t *r=p+ITEM_HEADER+i*ITEM_STRIDE;unsigned j=0;
        if(r[0]>6 || r[4]>=64)return fail(error,"Invalid item pocket/parameter index.");
        for(;j<64 && r[56+j];++j)
            if(r[56+j]<32 || r[56+j]>126)return fail(error,"Invalid item display name.");
        if(j==64 || (be32(r+16)!=0 && j==0))
            return fail(error,"Unterminated or missing item display name.");
        for(;j<64;++j)if(r[56+j])return fail(error,"Invalid name padding.");
        for(j=120;j<128;++j)if(r[j])return fail(error,"Invalid item reserved bytes.");
    }
    ItemCatalogImpl *c=malloc(sizeof *c);
    if(!c)return fail(error,"Item catalog allocation failed.");
    c->magic=ITEM_MAGIC;memcpy(c->bytes,p,length);
    free(catalog->impl);catalog->impl=c;clear_error(error);return 1;
}
int c3_item_catalog_load(C3ItemCatalog *catalog,const char *path,char error[C3_GAME_ERROR_SIZE])
{
    if(!catalog || !path)return fail(error,"Invalid catalog path.");
    FILE *f=fopen(path,"rb");
    if(!f)return fail(error,"Missing items.c3it: prepare the private field-item catalog.");
    uint8_t *bytes=malloc(C3_ITEM_CATALOG_SIZE);
    if(!bytes){fclose(f);return fail(error,"Item catalog allocation failed.");}
    size_t n=fread(bytes,1,C3_ITEM_CATALOG_SIZE,f);
    int extra=fgetc(f),bad=ferror(f);if(fclose(f))bad=1;
    int ok=0;
    if(n!=C3_ITEM_CATALOG_SIZE || extra!=EOF || bad)fail(error,"Truncated or oversized item catalog.");
    else ok=c3_item_catalog_decode(catalog,bytes,n,error);
    free(bytes);return ok;
}
int c3_item_info(const C3ItemCatalog *catalog,uint16_t id,C3ItemInfo *out)
{
    const uint8_t *r=record(catalog,id);
    if(!r || !out || !id || !r[0] || !be32(r+16))return 0;
    C3ItemInfo info={0};info.id=id;info.pocket=r[0];
    info.field_supported=(uint8_t)supported(r,id);memcpy(info.name,r+56,64);
    *out=info;return 1;
}

typedef struct PocketLayout { uint16_t offset,capacity,maximum; } PocketLayout;
static const PocketLayout pockets[]={
    {0,0,0},{0x87c,16,99},{0x780,20,99},{0x9bc,46,999},
    {0x8bc,64,99},{0x7d0,43,99},{0xa74,3,99}
};
const char *c3_bag_pocket_name(unsigned kind)
{
    static const char *const names[]={"","POKE BALLS","ITEMS","BERRIES","TMs","KEY ITEMS","COLOGNES"};
    return kind>0 && kind<7?names[kind]:"UNKNOWN POCKET";
}
static int read_pocket(const uint8_t *body,const C3ItemCatalog *catalog,unsigned kind,
                       C3BagView *out,char *error)
{
    if(kind<1 || kind>6 || !body || !record(catalog,0) || !out)
        return fail(error,"Invalid bag pocket/catalog.");
    PocketLayout l=pockets[kind];C3BagView view={0};
    view.pocket=(uint8_t)kind;view.capacity=(uint8_t)l.capacity;
    for(unsigned i=0;i<l.capacity;++i){
        const uint8_t *p=body+HERO+l.offset+i*4;C3ItemInfo info;
        uint16_t id=be16(p),qty=be16(p+2);
        if((!id && qty) || (id && (!qty || qty>l.maximum ||
            !c3_item_info(catalog,id,&info) || info.pocket!=kind)))
            return fail(error,"Invalid saved bag slot; no data was changed.");
        view.slots[i].id=id;view.slots[i].quantity=qty;view.occupied+=(id!=0);
    }
    *out=view;clear_error(error);return 1;
}
int c3_game_bag_view(const C3GameState *state,const C3ItemCatalog *catalog,unsigned pocket,
                     C3BagView *out,char error[C3_GAME_ERROR_SIZE])
{
    const GameStateImpl *s=state?state->impl:NULL;
    if(!s || !s->body)return fail(error,"No game is loaded.");
    return read_pocket(s->body,catalog,pocket,out,error);
}
int c3_game_bag_party(const C3GameState *state,C3BagPokemon out[6],char error[C3_GAME_ERROR_SIZE])
{
    const GameStateImpl *s=state?state->impl:NULL;C3BagPokemon party[6]={0};
    if(!s || !s->body || !out)return fail(error,"No game party is loaded.");
    for(unsigned i=0;i<6;++i){
        const uint8_t *p=s->body+PARTY+i*POKEMON_BYTES;
        C3BagPokemon *q=party+i;
        q->species=be16(p);q->level=p[0x60];q->hp=be16(p+0x8a);
        q->maximum_hp=be16(p+0x8c);q->condition=be16(p+0x64);
        q->egg=p[0xcb];q->invalid=p[0xcd];q->hyper=(uint8_t)(be16(p+0xe8)==0x3e);
        for(unsigned j=0;j<11;++j){
            uint16_t ch=be16(p+0x2e + j*2);if(!ch)break;
            q->name[j]=(ch>=32 && ch<127)?(char)ch:'?';
        }
        if(q->species && !q->name[0])snprintf(q->name,sizeof q->name,"POKEMON %u",q->species);
    }
    memcpy(out,party,sizeof party);clear_error(error);return 1;
}
static void clear_condition(uint8_t *p)
{
    /* fn_8011B788 clears seven fields, NOT the padding at +7 and +14..15. */
    memset(p+0x64,0,7);memset(p+0x6c,0,6);
}
C3ItemUseStatus c3_game_item_use(C3GameState *state,const C3ItemCatalog *catalog,
                                 unsigned pocket,unsigned slot,uint16_t expected_item,
                                 unsigned party_slot,int dry_run,C3ItemUseResult *out,
                                 char error[C3_GAME_ERROR_SIZE])
{
    GameStateImpl *s=state?state->impl:NULL;C3BagView view;C3ItemInfo info;
    uint8_t pokemon[POKEMON_BYTES],bag[NORMAL_BYTES];C3ItemUseResult effect={0};
    if(!s || !s->body || !out || party_slot>=6 || (dry_run!=0 && dry_run!=1)){
        fail(error,"Invalid item-use arguments.");return C3_ITEM_ERROR;
    }
    if(!read_pocket(s->body,catalog,pocket,&view,error))return C3_ITEM_ERROR;
    if(slot>=view.capacity || !expected_item || view.slots[slot].id!=expected_item){
        fail(error,"The selected bag entry changed or is empty.");return C3_ITEM_ERROR;
    }
    if(!c3_item_info(catalog,expected_item,&info) || !info.field_supported || pocket!=2){
        fail(error,"This item's field action is not implemented.");return C3_ITEM_UNSUPPORTED;
    }
    const uint8_t *r=record(catalog,expected_item),*param=r+40;
    const uint8_t *source=s->body+PARTY+party_slot*POKEMON_BYTES;
    uint16_t hp=be16(source+0x8a),maxhp=be16(source+0x8c),condition=be16(source+0x64);
    if(!be16(source) || be16(source)>=413 || !source[8] || !source[9] ||
       !source[10] || !source[11] || source[0xcb] || source[0xcd] ||
       !source[0x60] || source[0x60]>100 || !maxhp || maxhp>32767 || hp>maxhp){
        fail(error,"Choose a valid, non-Egg party Pokemon.");return C3_ITEM_ERROR;
    }
    if(be16(source+0xe8)==0x3e){
        fail(error,"This Pokemon cannot accept the item in Hyper Mode.");return C3_ITEM_NO_EFFECT;
    }
    memcpy(pokemon,source,sizeof pokemon);memcpy(bag,s->body+NORMAL_OFFSET,sizeof bag);
    effect.item=expected_item;effect.party_slot=(uint8_t)party_slot;
    effect.quantity_before=view.slots[slot].quantity;
    effect.hp_before=hp;effect.condition_before=condition;
    unsigned mask=condition==3 || condition==4?0x10u:
                  condition==5?2u:condition==6?8u:condition==7?4u:condition==8?0x20u:0u;
    if(param[3]&mask){clear_condition(pokemon);effect.cured=1;}
    c3fi_src_ItemUsePokemonLog log[32]={{0}};s16 count=0;
    c3fi_src_hpRecover__FP20ITEMUSE2POKEMON_LOG1PsP7PokemonUcbUsP12FightPokemon(
        log,&count,pokemon,param[10],(uint8_t)((param[4]>>6)&1u),0,NULL);
    effect.hp_after=be16(pokemon+0x8a);effect.condition_after=be16(pokemon+0x64);
    effect.revived=(uint8_t)(hp==0 && effect.hp_after!=0);
    if(!effect.cured && count==0){
        fail(error,"It would have no effect. No item was used.");return C3_ITEM_NO_EFFECT;
    }
    effect.quantity_after=(uint16_t)(effect.quantity_before-1u);
    put16(bag+slot*4+2,effect.quantity_after);
    if(!effect.quantity_after){
        put16(bag+slot*4,0);
        unsigned write=0;
        /* fn_80140ACC -> fn_801425E8(sortMode=0): stable normal-pocket compaction. */
        for(unsigned read=0;read<20;++read)if(be16(bag+read*4)){
            if(write!=read)memcpy(bag+write*4,bag+read*4,4);
            ++write;
        }
        memset(bag+write*4,0,(20u-write)*4u);
    }
    if(!dry_run){
        memcpy(s->body+PARTY+party_slot*POKEMON_BYTES,pokemon,sizeof pokemon);
        memcpy(s->body+NORMAL_OFFSET,bag,sizeof bag);
    }
    *out=effect;clear_error(error);return C3_ITEM_APPLIED;
}
