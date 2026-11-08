#include "colosseum3ds/battle_runtime_trainer.h"
#include "colosseum3ds/battle_fight_pokemon.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void put16(uint8_t *p,uint16_t n){p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n;}
static void put32(uint8_t *p,uint32_t n){p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static void status_reference(uint8_t *p)
{memset(p,0,7);memset(p+8,0,6);}
static void pokemon_reference(uint8_t *p)
{
    /* Independent setter write map; suffix/padding never initialized. */
    const unsigned ranges[][2]={{0,2},{4,0x11},{0x14,0x5a},{0x5c,0x61},
        {0x64,0x6b},{0x6c,0x72},{0x74,0xd1},{0xd2,0xda},
        {0xdc,0xe6},{0xe8,0xef},{0xf0,0xf6},{0xf8,0xfc}};
    for(unsigned i=0;i<sizeof(ranges)/sizeof(*ranges);i++)memset(p+ranges[i][0],0,ranges[i][1]-ranges[i][0]);
    p[0x10]=2;p[0xd0]=255;put32(p+0xdc,(uint32_t)-100);
}
static void hero_reference(uint8_t *p)
{
    memset(p,0,22);put32(p+0x2c,0);
    for(unsigned i=0;i<6;i++)pokemon_reference(p+0x30+i*312);
    memset(p+0x780,0,0x300);p[0xa80]=2;p[0xa81]=0;
    memset(p+0xa84,0,12);memset(p+0xa90,1,8);p[0xa98]=0;
    memset(p+0xa9a,0,40);memset(p+0xac2,0,22);
}
static void fp_reference(uint8_t *p)
{
    put32(p,0);pokemon_reference(p+4);status_reference(p+0x13c);
    put16(p+0x14c,65535);memset(p+0x14e,0,4);
}
static void action_reference(uint8_t *p)
{put16(p,0);memset(p+4,0,24);put32(p+28,UINT32_MAX);memset(p+32,0,16);}
static void waza_reference(uint8_t *p)
{
    p[0]=255;memset(p+2,0,6);
    for(unsigned i=0;i<9;i++)status_reference(p+8+16*i);
    /* Original table3F routes waza status0; kaisuu0 min=max=-1. */
    put16(p+8,0x3f);p[12]=255;
    p[0x98]=1;p[0x99]=1;put32(p+0x9c,0);put32(p+0xa0,0);
    put16(p+0xa4,0);put16(p+0xa6,9);p[0xa8]=0;p[0xa9]=0;
}
static void out_reference(uint8_t *p)
{
    memset(p,0,8);fp_reference(p+8);
    for(unsigned i=0;i<52;i++)status_reference(p+0x15c+16*i);
    waza_reference(p+0x49c);waza_reference(p+0x548);
    memset(p+0x5f4,0,4);put32(p+0x5f8,UINT32_MAX);put32(p+0x5fc,0);
    put32(p+0x600,0);memset(p+0x604,6,7);put16(p+0x60c,0);put16(p+0x610,2);
    for(unsigned i=0;i<12;i++)put16(p+0x612+2*i,65535);
    action_reference(p+0x62c);put16(p+0x65c,9);put16(p+0x65e,9);put16(p+0x660,0);put32(p+0x664,0);
    memset(p+0x668,0,18);put16(p+0x672,9);p[0x67a]=0;p[0x67e]=0;put16(p+0x680,0);
    memset(p+0x682,0,29);put32(p+0x6a0,0);memset(p+0x6a4,0,8);p[0x6ac]=0;put16(p+0x6ae,65535);
    for(unsigned i=0;i<4;i++)memset(p+0x6b0+12*i,0,10);
}
static void runtime_reference(uint8_t *p)
{
    hero_reference(p+4);put16(p,0);
    for(unsigned i=0;i<6;i++)fp_reference(p+0xb1c+i*0x154);
    for(unsigned i=0;i<3;i++)out_reference(p+0x1314+i*0x6e0);
    p[0x27b4]=1;p[0x27b5]=0;put32(p+0x27b8,0);p[0x27bc]=0;put32(p+0x27c0,0);
    action_reference(p+0x27c4);
    for(unsigned i=0;i<12;i++){uint8_t *e=p+0x27f4+20*i;put16(e,65535);memset(e+2,0,17);}
}
static void check_bytes(const uint8_t *a,const uint8_t *b,size_t size)
{
    for(size_t i=0;i<size;i++)if(a[i]!=b[i]){fprintf(stderr,"byte %zx actual %02x expected %02x\n",i,a[i],b[i]);assert(a[i]==b[i]);}
}
static void pattern(uint8_t *p,size_t size)
{for(size_t i=0;i<size;i++)p[i]=(uint8_t)(0x39u+i*29u);}
static void complete_write_map(void)
{
    struct {uint8_t left[19],body[10468],right[23];} guarded;
    uint8_t expected[10468];pattern((uint8_t*)&guarded,sizeof(guarded));memcpy(expected,guarded.body,sizeof(expected));
    uint8_t left[19],right[23];memcpy(left,guarded.left,sizeof(left));memcpy(right,guarded.right,sizeof(right));
    runtime_reference(expected);assert(c3_battle_runtime_trainer_init(guarded.body));check_bytes(guarded.body,expected,sizeof(expected));
    assert(!memcmp(guarded.left,left,sizeof(left))&&!memcmp(guarded.right,right,sizeof(right)));
    /* Reinitialization is idempotent, including retained unknown bytes. */
    assert(c3_battle_runtime_trainer_init(guarded.body));check_bytes(guarded.body,expected,sizeof(expected));
    assert(c3_battle_runtime_trainer_init(NULL));
}
static void constructor_inputs(void)
{
    uint8_t p[10468],expected[10468],hero[2840],hero_before[2840];
    pattern(p,sizeof(p));pattern(hero,sizeof(hero));memcpy(hero_before,hero,sizeof(hero));memcpy(expected,p,sizeof(p));
    runtime_reference(expected);memcpy(expected+4,hero,sizeof(hero));put16(expected,0xabcd);expected[0x27bc]=0x76;put32(expected+0x27c0,0x80401234);
    assert(c3_battle_runtime_trainer_create(p,hero,0xabcd,0x12345676,0x80401234));check_bytes(p,expected,sizeof(p));assert(!memcmp(hero,hero_before,sizeof(hero)));
    pattern(p,sizeof(p));memcpy(expected,p,sizeof(p));runtime_reference(expected);memcpy(expected+4,hero,sizeof(hero));put16(expected,0);expected[0x27bc]=0;put32(expected+0x27c0,0);
    assert(c3_battle_runtime_trainer_create(p,hero,0,0,0));check_bytes(p,expected,sizeof(p));
    memcpy(expected,p,sizeof(p));assert(c3_battle_runtime_trainer_create(p,NULL,1,1,1));assert(!memcmp(p,expected,sizeof(p)));
    assert(c3_battle_runtime_trainer_create(NULL,hero,1,1,1));
    assert(!c3_battle_runtime_trainer_create(p,p+4,1,1,1));assert(!memcmp(p,expected,sizeof(p)));
}
static void hero_initializer(void)
{
    struct {uint8_t before[11],hero[2840],after[13];} g;
    uint8_t expected[2840];memset(&g,0xa7,sizeof(g));memcpy(expected,g.hero,sizeof(expected));hero_reference(expected);
    assert(!c3_battle_trainer_init_hero(NULL));assert(c3_battle_trainer_init_hero(g.hero));check_bytes(g.hero,expected,sizeof(expected));
    for(unsigned i=0;i<sizeof(g.before);i++)assert(g.before[i]==0xa7);
    for(unsigned i=0;i<sizeof(g.after);i++)assert(g.after[i]==0xa7);
}
static uint8_t busy_runtime[10468],busy_hero[2840];
static unsigned busy_calls;
static int busy_message(void *user,uint32_t id,const uint8_t **bytes,size_t *size)
{
    (void)user;(void)id;static const uint8_t name[]={0,65,0,0};
    busy_calls++;
    assert(!c3_battle_trainer_init_hero(busy_hero));
    assert(!c3_battle_runtime_trainer_init(busy_runtime));
    assert(!c3_battle_runtime_trainer_create(busy_runtime,busy_hero,10,1,0x80401234));
    for(unsigned i=0;i<sizeof(busy_runtime);i++)assert(busy_runtime[i]==0x73);
    for(unsigned i=0;i<sizeof(busy_hero);i++)assert(busy_hero[i]==0x84);
    *bytes=name;*size=sizeof(name);return 1;
}
static void shared_initializer_failure(void)
{
    uint8_t roster[80]={0},species[284]={0},moves[56*360]={0},growth[404]={0},natures[1000]={0},pokemon[312];
    static const uint8_t rates[]={100,100};
    roster[0]=roster[1]=roster[2]=255;roster[4]=1;put16(roster+8,65535);put16(roster+10,1);put32(roster+16,UINT32_MAX);
    for(unsigned i=0;i<4;i++)put32(roster+52+i*8,UINT32_MAX);
    species[2]=127;species[0x32]=1;put32(species+24,1);put16(species+8,70);
    for(unsigned i=0;i<6;i++)put16(species+0x84+2*i,40);
    for(unsigned i=0;i<101;i++)put32(growth+4*i,i*i*i);
    C3BattlePokemonData data={roster,species,413,moves,360,growth,natures,rates,1,busy_message,NULL};
    C3BattlePokemonRequest request={238,123,{1,2,3,4}};C3BattleRng rng={456,0};
    memset(pokemon,0x95,sizeof(pokemon));memset(busy_runtime,0x73,sizeof(busy_runtime));memset(busy_hero,0x84,sizeof(busy_hero));busy_calls=0;
    assert(c3_battle_pokemon_create_pre_catch(pokemon,&request,&data,&rng)&&busy_calls==1);
    /* All nested guards release after failed staging. */
    assert(c3_battle_trainer_init_hero(busy_hero));
    assert(c3_battle_runtime_trainer_init(busy_runtime));
}

int main(void)
{
    hero_initializer();complete_write_map();constructor_inputs();shared_initializer_failure();
    puts("battle_runtime_trainer: all 10468 bytes, nested status3F, identity/controller, hero reset, padding/canaries, no-ops and overlap passed");return 0;
}
