#include "colosseum3ds/battle_pokemon.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t roster[80],species[284],moves[56*360],growth[404],natures[1000];
static const uint8_t rates[]={100,100},name[]={0,65,0,66,0,0},alias[]={0,88,0,0};
static const uint8_t control_name[]={255,255,0,0};
static uint16_t be16(const uint8_t *p){return (uint16_t)(p[0]*256u+p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put16(uint8_t *p,uint16_t n){p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n;}
static void put32(uint8_t *p,uint32_t n){p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static int messages(void *user,uint32_t id,const uint8_t **p,size_t *size)
{
    if(user==(void*)1)return 0;
    if(user==(void*)2){*p=control_name;*size=sizeof(control_name);return 1;}
    if(id==0x1234){*p=name;*size=sizeof(name);return 1;}
    if(id==0x5678){*p=alias;*size=sizeof(alias);return 1;}
    return 0;
}
static C3BattlePokemonData setup(void)
{
    memset(roster,0,sizeof(roster));memset(species,0,sizeof(species));
    memset(moves,0,sizeof(moves));memset(growth,0,sizeof(growth));memset(natures,0,sizeof(natures));
    roster[0]=roster[1]=roster[2]=255;roster[4]=5;put16(roster+8,65535);put16(roster+10,288);put32(roster+16,UINT32_MAX);
    for(unsigned i=0;i<6;i++){roster[28+i]=255;put16(roster+34+i*2,65535);put16(species+0x84+i*2,(uint16_t)(40+i*10));}
    for(unsigned i=0;i<4;i++)put32(roster+52+i*8,UINT32_MAX);
    species[2]=127;species[0x32]=1;species[0x33]=2;put16(species+8,70);put32(species+24,0x1234);
    for(unsigned i=0;i<101;i++)put32(growth+i*4,i*i*i);
    for(unsigned i=0;i<6;i++){species[0xba+i*4]=(uint8_t)(i<5?i+1:5);put16(species+0xbc+i*4,(uint16_t)(i<5?i+1:5));}
    for(unsigned i=1;i<360;i++)moves[i*56+1]=(uint8_t)(i==1?4:20);
    return (C3BattlePokemonData){roster,species,413,moves,360,growth,natures,rates,1,messages,NULL};
}
static uint16_t draw(uint32_t *seed){*seed=*seed*214013u+2531011u;return (uint16_t)(*seed>>16);}
static void basic_closure(void)
{
    C3BattlePokemonData d=setup();C3BattlePokemonRequest req={238,0x13572468,{1,2,3,4}};
    struct {uint8_t left[13],body[312],right[17];} guarded;
    memset(&guarded,0xa5,sizeof(guarded));C3BattleRng rng={0x12345678,11};
    assert(c3_battle_pokemon_create_pre_catch(guarded.body,&req,&d,&rng));
    const uint8_t *p=guarded.body;
    uint32_t seed=0x12345678;uint16_t discarded_high=draw(&seed),discarded_low=draw(&seed);
    (void)discarded_high;(void)discarded_low;uint16_t iv1=draw(&seed),iv2=draw(&seed);draw(&seed);
    uint32_t high=draw(&seed),low=draw(&seed),pid=high<<16|low;
    assert(be32(p+4)==pid&&rng.seed==seed&&rng.draws==18);
    assert(be16(p)==288&&!memcmp(p+8,req.attest,4)&&p[0x60]==5&&be32(p+0x5c)==125);
    assert(be16(p+0xa4)==(iv1&31)&&be16(p+0xa6)==((iv1>>5)&31)&&be16(p+0xa8)==((iv1>>10)&31));
    assert(be16(p+0xae)==(iv2&31)&&be16(p+0xaa)==((iv2>>5)&31)&&be16(p+0xac)==((iv2>>10)&31));
    assert(be16(p+0xb0)==70&&be16(p+0xd6)==0&&be32(p+0xdc)==(uint32_t)-100);
    assert(p[0x10]==2&&be32(p+0x14)==0&&be16(p+0xc)==0&&p[0xe]==0&&p[0xf]==0);
    for(unsigned i=0;i<4;i++){assert(be16(p+0x78+i*4)==i+2);assert(p[0x7a+i*4]==20&&p[0x7b+i*4]==0);}
    assert(!memcmp(p+0x2e,name,sizeof(name))&&!memcmp(p+0x44,name,sizeof(name)));
    for(unsigned i=6;i<22;i++)assert(!p[0x2e + i]&&!p[0x44+i]);
    const unsigned unchanged[]={2,3,0x11,0x12,0x13,0x5a,0x5b,0x61,0x62,0x63,0x6b,0x72,0x73,0xd1,0xda,0xdb,0xe6,0xe7,0xef,0xf6,0xf7};
    for(unsigned i=0;i<sizeof(unchanged)/sizeof(*unchanged);i++)assert(p[unchanged[i]]==0xa5);
    /* Independent source setter write map, including the constructor's full
     * 312-byte backing. Every byte outside these ranges must survive. */
    const unsigned ranges[][2]={{0,2},{4,0x11},{0x14,0x5a},{0x5c,0x61},
        {0x64,0x6b},{0x6c,0x72},{0x74,0xd1},{0xd2,0xda},
        {0xdc,0xe6},{0xe8,0xef},{0xf0,0xf6},{0xf8,0xfc}};
    for(unsigned b=0;b<312;b++){
        int written=0;for(unsigned i=0;i<sizeof(ranges)/sizeof(*ranges);i++)written|=b>=ranges[i][0]&&b<ranges[i][1];
        if(!written&&p[b]!=0xa5){fprintf(stderr,"Unexpected source write at %x: %x\n",b,p[b]);assert(p[b]==0xa5);}
    }
    for(unsigned i=0xfc;i<312;i++)assert(p[i]==0xa5);
    for(unsigned i=0;i<sizeof(guarded.left);i++)assert(guarded.left[i]==0xa5);
    for(unsigned i=0;i<sizeof(guarded.right);i++)assert(guarded.right[i]==0xa5);
    uint16_t hp=(uint16_t)(15+5*((iv1&31)+80)/100);
    assert(be16(p+0x8c)==hp&&be16(p+0x8a)==hp);
}
static void overrides_and_failure(void)
{
    C3BattlePokemonData d=setup();C3BattlePokemonRequest req={239,0xabcdef01,{9,8,7,6}};
    uint8_t p[312],before[312];memset(p,0x6c,sizeof(p));C3BattleRng rng={10,0};
    put32(roster+20,0x5678);roster[0]=1;roster[1]=1;roster[2]=7;put16(roster+8,250);put32(roster+16,42);
    for(unsigned i=0;i<6;i++){roster[28+i]=31;put16(roster+34+2*i,300);}
    for(unsigned i=0;i<4;i++){put32(roster+52+8*i,i==0?1:i+6);roster[48+i*8]=3;}
    assert(c3_battle_pokemon_create_pre_catch(p,&req,&d,&rng));
    uint32_t pid=be32(p+4);assert((pid&255)<127&&pid%25==7&&p[0xcc]==1&&be16(p+0x88)==42);
    assert(be16(p+0xb0)==250&&!memcmp(p+0x2e,alias,sizeof(alias))&&p[0x5a]==0x6c);
    assert(be16(p+0x78)==1&&p[0x7a]==4&&p[0x7b]==0); /* <=4 PP rejects PP Ups */
    assert(be16(p+0x7c)==7&&p[0x7e]==20&&p[0x7f]==3); /* setting Ups doesn't refill */
    for(unsigned i=0;i<6;i++)assert(be16(p+0x98+2*i)==255&&be16(p+0xa4+2*i)==31);
    memcpy(before,p,sizeof(p));C3BattleRng saved=rng;
    roster[3]=1;assert(!c3_battle_pokemon_create_pre_catch(p,&req,&d,&rng));roster[3]=0;
    assert(!memcmp(p,before,312)&&!memcmp(&rng,&saved,sizeof(rng)));
    d.user=(void*)1;assert(!c3_battle_pokemon_create_pre_catch(p,&req,&d,&rng));d.user=NULL;
    assert(!memcmp(p,before,312)&&!memcmp(&rng,&saved,sizeof(rng)));
    d.user=(void*)2;assert(!c3_battle_pokemon_create_pre_catch(p,&req,&d,&rng));d.user=NULL;
    assert(!memcmp(p,before,312)&&!memcmp(&rng,&saved,sizeof(rng)));
    natures[5]=1;assert(!c3_battle_pokemon_create_pre_catch(p,&req,&d,&rng));natures[5]=0;
    assert(!memcmp(p,before,312)&&!memcmp(&rng,&saved,sizeof(rng)));
    d.move_count=2;assert(!c3_battle_pokemon_create_pre_catch(p,&req,&d,&rng));d.move_count=360;
    assert(!memcmp(p,before,312)&&!memcmp(&rng,&saved,sizeof(rng)));
    rng.draws=UINT64_MAX-2;saved=rng;
    assert(!c3_battle_pokemon_create_pre_catch(p,&req,&d,&rng));assert(!memcmp(p,before,312)&&!memcmp(&rng,&saved,sizeof(rng)));
    union {uint8_t bytes[312];C3BattleRng rng;} overlapping;
    overlapping.rng=rng;
    assert(!c3_battle_pokemon_create_pre_catch(overlapping.bytes,&req,&d,&overlapping.rng));
    assert(!c3_battle_pokemon_create_pre_catch(p,NULL,&d,&rng));
}

static void retry_budget_and_boundary(void)
{
    C3BattlePokemonData d=setup();
    C3BattlePokemonRequest req={238,0x13572468,{1,2,3,4}};
    /* This admitted variable-sex ratio allows female only for low byte zero.
     * Independently inspect the source stream, not the wrapper's retry result:
     * the first female, nature-5, nonrare candidate is number 79,739. Thus no
     * candidate can satisfy the filter within the 65,536-attempt budget. */
    species[2]=1;roster[1]=1;roster[2]=5;
    uint32_t reference_seed=1113,first_candidate=0,accepted_pid=0;
    for(unsigned i=0;i<5;i++)draw(&reference_seed);
    for(unsigned attempt=1;attempt<=100000;attempt++){
        uint32_t high=draw(&reference_seed),low=draw(&reference_seed),pid=high<<16|low;
        uint32_t rare=(req.opponent_trainer_id>>16)^(req.opponent_trainer_id&65535)^high^low;
        if((pid&255)==0&&pid%25==5&&rare>=8){first_candidate=attempt;accepted_pid=pid;break;}
    }
    assert(first_candidate==79739&&first_candidate>C3_BATTLE_POKEMON_RETRY_LIMIT);
    assert(accepted_pid==0x475d2900);
    struct {uint8_t left[9],body[312],right[11];} backing,before;
    memset(&backing,0x93,sizeof(backing));memcpy(&before,&backing,sizeof(backing));
    C3BattleRng rng={1113,19},saved=rng;
    assert(!c3_battle_pokemon_create_pre_catch(backing.body,&req,&d,&rng));
    assert(!memcmp(&backing,&before,sizeof(backing))&&!memcmp(&rng,&saved,sizeof(rng)));

    /* Move that same first matching candidate onto the final allowed attempt.
     * This must succeed, ruling out an off-by-one limit or blanket rejection
     * of the restrictive but valid source filter. */
    uint32_t shifted_seed=1113;
    for(unsigned i=0;i<2*(first_candidate-C3_BATTLE_POKEMON_RETRY_LIMIT);i++)draw(&shifted_seed);
    rng=(C3BattleRng){shifted_seed,19};
    assert(c3_battle_pokemon_create_pre_catch(backing.body,&req,&d,&rng));
    assert(be32(backing.body+4)==accepted_pid&&rng.seed==reference_seed);
    assert(rng.draws==19+5+2*C3_BATTLE_POKEMON_RETRY_LIMIT);
    assert(!memcmp(backing.left,before.left,sizeof(backing.left)));
    assert(!memcmp(backing.right,before.right,sizeof(backing.right)));
}

typedef struct ReentrantMessageContext {
    const C3BattlePokemonRequest *request;
    const C3BattlePokemonData *data;
    uint8_t nested[312];
    C3BattleRng rng;
    unsigned calls;
} ReentrantMessageContext;

static int reentrant_messages(void *user,uint32_t id,const uint8_t **bytes,size_t *size)
{
    ReentrantMessageContext *context=user;
    uint8_t before[312];memcpy(before,context->nested,sizeof(before));
    C3BattleRng saved=context->rng;
    /* The nested callback must never execute: the outer call owns the guard. */
    assert(++context->calls<=2);
    assert(!c3_battle_pokemon_create_pre_catch(context->nested,context->request,
                                             context->data,&context->rng));
    assert(!memcmp(before,context->nested,sizeof(before)));
    assert(!memcmp(&saved,&context->rng,sizeof(saved)));
    return messages(NULL,id,bytes,size);
}

static void reentrant_busy_guard(void)
{
    C3BattlePokemonData d=setup();
    C3BattlePokemonRequest req={239,0x13572468,{1,2,3,4}};
    put32(roster+20,0x5678);
    uint8_t expected[312],actual[312];memset(expected,0xb6,sizeof(expected));
    memcpy(actual,expected,sizeof(actual));
    C3BattleRng expected_rng={0x12345678,23},actual_rng=expected_rng;
    assert(c3_battle_pokemon_create_pre_catch(expected,&req,&d,&expected_rng));
    ReentrantMessageContext context={.request=&req,.data=&d,.rng={0x99887766,7}};
    memset(context.nested,0x47,sizeof(context.nested));
    d.message=reentrant_messages;d.user=&context;
    assert(c3_battle_pokemon_create_pre_catch(actual,&req,&d,&actual_rng));
    assert(context.calls==2&&!memcmp(actual,expected,sizeof(actual)));
    assert(!memcmp(&actual_rng,&expected_rng,sizeof(actual_rng)));
    /* Completing the outer call releases the guard for subsequent calls. */
    d.message=messages;d.user=NULL;
    assert(c3_battle_pokemon_create_pre_catch(context.nested,&req,&d,&context.rng));
}

int main(void)
{
    basic_closure();overrides_and_failure();retry_budget_and_boundary();reentrant_busy_guard();
    puts("battle_pokemon: closure, BE write map, learned moves, names, RNG, retry boundary, busy guard and rollback passed");
    return 0;
}
