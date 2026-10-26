#include "colosseum3ds/battle_trainer.h"
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

static uint8_t trainer[52],terminal[80];
static int fail_roster,many_rosters,fail_item,invalid_item_kind;
static unsigned roster_calls;
static C3BattlePokemonData pd;
static int trainer_messages(void *user,uint32_t id,const uint8_t **p,size_t *size)
{
    (void)user;
    if(id==0x901||id==0xfa2){*p=name;*size=sizeof(name);return 1;}
    return messages(NULL,id,p,size);
}
static int get_roster(void *user,uint16_t id,C3BattlePokemonData *out)
{
    (void)user;roster_calls++;
    if(fail_roster&&id==239)return 0;
    *out=pd;
    if(id==238||id==239||(many_rosters&&id>=238&&id<=244))return 1;
    if(id==240){out->roster=terminal;return 1;}
    return 0;
}
static int items(void *user,uint16_t id,uint32_t *msg,uint8_t *kind)
{
    (void)user;if(fail_item)return 0;*msg=id?1000u+id:0;*kind=(uint8_t)(invalid_item_kind?0:id<7?3:2);return 1;
}
static C3BattleTrainerData trainer_setup(void)
{
    pd=setup();memset(trainer,0,sizeof(trainer));memset(terminal,0,sizeof(terminal));
    put16(trainer+2,2);put16(trainer+4,238);put16(trainer+6,2);put32(trainer+8,0x901);trainer[0]=1;
    put16(roster+12,4);fail_roster=0;many_rosters=0;fail_item=0;invalid_item_kind=0;roster_calls=0;
    return (C3BattleTrainerData){trainer,NULL,get_roster,trainer_messages,items,100,0,NULL,0,NULL};
}
static void original_parent_fields(void)
{
    C3BattleTrainerData d=trainer_setup();C3BattleTrainerRequest req={77,0,{1,2,3,4}};
    struct {uint8_t left[7],hero[0xb18],right[11];} guarded;
    uint8_t scratch[312];memset(&guarded,0xa6,sizeof(guarded));memset(scratch,0xd7,sizeof(scratch));
    C3BattleRng rng={0x12345678,21};
    assert(c3_battle_trainer_create(guarded.hero,scratch,&req,&d,&rng));
    uint32_t seed=0x12345678;uint32_t low=draw(&seed),high=draw(&seed),tid=high<<16|low;
    assert(be32(guarded.hero+0x2c)==tid&&guarded.hero[0xa80]==1&&roster_calls==3);
    assert(!memcmp(guarded.hero,name,sizeof(name))&&!memcmp(guarded.hero+0xac2,name,sizeof(name)));
    for(unsigned i=sizeof(name);i<22;i++)assert(!guarded.hero[i]&&!guarded.hero[0xac2+i]);
    for(unsigned i=22;i<44;i++)assert(guarded.hero[i]==0xa6);
    for(unsigned slot=0;slot<2;slot++){
        uint8_t *p=guarded.hero+0x30+312u*slot;
        assert(be16(p)==288&&be16(p+0xd6)==238+slot&&!memcmp(p+8,req.attest,4));
        assert(!be16(p+0xc)&&p[0xe]==5&&p[0xf]==4&&p[0x10]==1&&be32(p+0x14)==tid);
        assert(!memcmp(p+0x18,name,sizeof(name)));
        for(unsigned i=0xfc;i<312;i++)assert(p[i]==0xd7);
    }
    assert(be16(scratch+0xd6)==239&&!be32(scratch+0x14)&&scratch[0x10]==2&&scratch[0xf]==0);
    for(unsigned slot=2;slot<6;slot++){
        uint8_t *p=guarded.hero+0x30+312u*slot;
        assert(!be16(p)&&p[0x10]==2&&p[0xd0]==255&&be32(p+0xdc)==(uint32_t)-100);
        for(unsigned i=0xfc;i<312;i++)assert(p[i]==0xa6);
    }
    for(unsigned i=0x780;i<0xa80;i++)assert(!guarded.hero[i]);
    for(unsigned i=0xa84;i<0xa99;i++)assert(!guarded.hero[i]);
    for(unsigned i=0xa9a;i<0xac2;i++)assert(!guarded.hero[i]);
    const unsigned preserved[]={0xa82,0xa83,0xa99,0xad8,0xb17};
    for(unsigned i=0;i<sizeof(preserved)/sizeof(*preserved);i++)assert(guarded.hero[preserved[i]]==0xa6);
    for(unsigned i=0;i<sizeof(guarded.left);i++)assert(guarded.left[i]==0xa6);
    for(unsigned i=0;i<sizeof(guarded.right);i++)assert(guarded.right[i]==0xa6);
    /* Parent RNG equals independent LOW/HIGH hero draws then delegated closure. */
    uint8_t expected[312];memset(expected,0xd7,sizeof(expected));C3BattleRng rr={seed,23};
    C3BattlePokemonRequest pr={238,tid,{1,2,3,4}};
    assert(c3_battle_pokemon_create_pre_catch(expected,&pr,&pd,&rr));put16(expected+0xd6,238);
    pr.roster_id=239;assert(c3_battle_pokemon_create_pre_catch(expected,&pr,&pd,&rr));put16(expected+0xd6,239);
    assert(!memcmp(scratch,expected,312)&&rng.seed==rr.seed&&rng.draws==rr.draws);
}
static void player_and_early_returns(void)
{
    C3BattleTrainerData d=trainer_setup();C3BattleTrainerRequest req={77,0,{0}};
    uint8_t hero[0xb18],scratch[312],saved[0xb18];memset(hero,0x19,sizeof(hero));memset(scratch,0x47,sizeof(scratch));
    for(unsigned i=0;i<sizeof(saved);i++)saved[i]=(uint8_t)(i*17u+3u);
    C3BattleRng rng={123,99};put16(trainer+2,1);d.saved_player=saved;
    assert(c3_battle_trainer_create(hero,scratch,&req,&d,&rng));assert(!memcmp(hero,saved,sizeof(hero))&&rng.seed==123&&rng.draws==99&&!roster_calls);
    for(unsigned i=0;i<312;i++)assert(scratch[i]==0x47);
    memset(hero,0x19,sizeof(hero));req.trainer_id=0;
    assert(c3_battle_trainer_create(hero,scratch,&req,&d,&rng));
    for(unsigned i=0;i<sizeof(hero);i++)assert(hero[i]==0x19);
    req.trainer_id=77;put16(trainer+6,0);
    assert(c3_battle_trainer_create(hero,scratch,&req,&d,&rng));
    for(unsigned i=0;i<sizeof(hero);i++)assert(hero[i]==0x19);
    req.input_device=1;assert(c3_battle_trainer_create(hero,scratch,&req,&d,&rng));assert(!memcmp(hero,saved,sizeof(hero)));
    d.saved_player=NULL;memset(hero,0x19,sizeof(hero));
    assert(c3_battle_trainer_create(hero,scratch,&req,&d,&rng));
    for(unsigned i=0;i<sizeof(hero);i++)assert(hero[i]==0x19);
}
static void items_special_and_full_party(void)
{
    C3BattleTrainerData d=trainer_setup();C3BattleTrainerRequest req={77,0,{1,2,3,4}};
    uint8_t hero[0xb18],scratch[312];memset(hero,0x91,sizeof(hero));memset(scratch,0x62,sizeof(scratch));
    const uint16_t ids[]={9,7,9,6,5,5};for(unsigned i=0;i<6;i++)put16(trainer+0x14+i*2,ids[i]);
    d.special_trainer_id=77;d.special_name=alias;d.special_name_size=sizeof(alias);
    C3BattleRng rng={567,0};assert(c3_battle_trainer_create(hero,scratch,&req,&d,&rng));
    assert(!memcmp(hero,alias,sizeof(alias))&&!memcmp(hero+0x30+0x18,alias,sizeof(alias)));
    assert(be16(hero+0x780)==9&&be16(hero+0x782)==2&&be16(hero+0x784)==7&&be16(hero+0x786)==1);
    assert(be16(hero+0x9bc)==5&&be16(hero+0x9be)==2&&be16(hero+0x9c0)==6&&be16(hero+0x9c2)==1);
    d=trainer_setup();many_rosters=1;memset(hero,0x91,sizeof(hero));memset(scratch,0x62,sizeof(scratch));rng=(C3BattleRng){567,0};
    assert(c3_battle_trainer_create(hero,scratch,&req,&d,&rng));assert(roster_calls==7&&be16(scratch+0xd6)==244);
    for(unsigned i=0;i<6;i++)assert(be16(hero+0x30+312*i+0xd6)==238+i);
}
static void rollback(void)
{
    C3BattleTrainerData d=trainer_setup();C3BattleTrainerRequest req={77,0,{1,2,3,4}};
    uint8_t hero[0xb18],scratch[312],before[0xb18];memset(hero,0xac,sizeof(hero));memset(scratch,0x43,sizeof(scratch));memcpy(before,hero,sizeof(hero));
    C3BattleRng rng={99,17};fail_roster=1;
    assert(!c3_battle_trainer_create(hero,scratch,&req,&d,&rng));assert(!memcmp(hero,before,sizeof(hero))&&rng.seed==99&&rng.draws==17);
    for(unsigned i=0;i<312;i++)assert(scratch[i]==0x43);
    fail_roster=0;rng.draws=UINT64_MAX-1;
    assert(!c3_battle_trainer_create(hero,scratch,&req,&d,&rng));assert(!memcmp(hero,before,sizeof(hero))&&rng.draws==UINT64_MAX-1);
    rng.draws=17;d.special_trainer_id=77;d.special_name=control_name;d.special_name_size=sizeof(control_name);
    assert(!c3_battle_trainer_create(hero,scratch,&req,&d,&rng));assert(!memcmp(hero,before,sizeof(hero)));
    d.special_trainer_id=0;put16(trainer+0x14,101);
    assert(!c3_battle_trainer_create(hero,scratch,&req,&d,&rng));assert(!memcmp(hero,before,sizeof(hero)));
    put16(trainer+0x14,9);d.item=NULL;
    assert(!c3_battle_trainer_create(hero,scratch,&req,&d,&rng));assert(!memcmp(hero,before,sizeof(hero))&&rng.draws==17);
    d.item=items;fail_item=1;
    assert(!c3_battle_trainer_create(hero,scratch,&req,&d,&rng));assert(!memcmp(hero,before,sizeof(hero))&&rng.draws==17);
    fail_item=0;invalid_item_kind=1;
    assert(!c3_battle_trainer_create(hero,scratch,&req,&d,&rng));assert(!memcmp(hero,before,sizeof(hero))&&rng.draws==17);
    invalid_item_kind=0;
    put16(trainer+0x14,0);d.trainer=hero;
    assert(!c3_battle_trainer_create(hero,scratch,&req,&d,&rng));
    d.trainer=trainer;assert(!c3_battle_trainer_create(hero,hero,&req,&d,&rng));
    roster[3]=1;assert(!c3_battle_trainer_create(hero,scratch,&req,&d,&rng));assert(!memcmp(hero,before,sizeof(hero)));
}
static uint8_t nested_hero[0xb18],nested_scratch[312];
static C3BattleTrainerRequest nested_request;
static C3BattleTrainerData nested_data;
static C3BattleRng nested_rng;
static unsigned nested_calls;
static int reentrant_message(void *user,uint32_t id,const uint8_t **p,size_t *size)
{
    (void)user;assert(++nested_calls<=2);
    assert(!c3_battle_trainer_create(nested_hero,nested_scratch,&nested_request,&nested_data,&nested_rng));
    assert(nested_rng.seed==44&&nested_rng.draws==13);
    for(unsigned i=0;i<sizeof(nested_hero);i++)assert(nested_hero[i]==0x28);
    for(unsigned i=0;i<sizeof(nested_scratch);i++)assert(nested_scratch[i]==0x39);
    return trainer_messages(NULL,id,p,size);
}
static void busy_context(void)
{
    C3BattleTrainerData d=trainer_setup();C3BattleTrainerRequest req={77,0,{1,2,3,4}};
    uint8_t hero[0xb18],scratch[312];memset(hero,0x51,sizeof(hero));memset(scratch,0x61,sizeof(scratch));
    memset(nested_hero,0x28,sizeof(nested_hero));memset(nested_scratch,0x39,sizeof(nested_scratch));
    nested_data=d;nested_request=req;nested_rng=(C3BattleRng){44,13};nested_calls=0;
    d.message=reentrant_message;C3BattleRng rng={123,0};
    assert(c3_battle_trainer_create(hero,scratch,&req,&d,&rng)&&nested_calls==2);
    assert(c3_battle_trainer_create(nested_hero,nested_scratch,&nested_request,&nested_data,&nested_rng));
}

int main(void)
{
    original_parent_fields();player_and_early_returns();items_special_and_full_party();rollback();busy_context();
    puts("battle_trainer: parent/hero RNG, catch metadata, six slots, scratch, items, special name, saved player and rollback passed");return 0;
}
