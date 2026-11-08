#include "colosseum3ds/battle_party_sort.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t trainers[2][52], ai[2][40], rosters[8][80], species[5][284], types[18][44];
static unsigned lookups, fail_at, side_calls;
static int fail_side, alias_row;
static C3BattlePartySortState *alias_state;
static void put16(uint8_t *p,uint16_t n){p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n;}
static void put32(uint8_t *p,uint32_t n){p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n;}
static int lookup(void *user,uint32_t table,uint32_t id,const uint8_t **row,size_t *size)
{
    (void)user;
    if(++lookups==fail_at)return 0;
    if(alias_row){*row=alias_state->hero;*size=284;return 1;}
    switch(table){
    case C3_PARTY_TRAINER:*row=trainers[id];*size=52;break;
    case C3_PARTY_AI:*row=ai[id];*size=40;break;
    case C3_PARTY_ROSTER:*row=rosters[id];*size=80;break;
    case C3_PARTY_SPECIES:*row=species[id];*size=284;break;
    case C3_PARTY_TYPE:*row=types[id];*size=44;break;
    default:return 0;
    }
    return 1;
}
static int side(void *user,uint32_t target,uint16_t host,uint32_t *value)
{
    (void)user;assert(host==7);assert(target==100||target==102||target==103);
    side_calls++;if(fail_side)return 0;*value=5;return 1;
}
static void setup(C3BattlePartySortState *s,C3BattlePartySortRequest *r,C3BattlePartySortData *d)
{
    memset(s,0,sizeof(*s));memset(r,0,sizeof(*r));memset(d,0,sizeof(*d));
    memset(trainers,0,sizeof(trainers));memset(ai,0,sizeof(ai));memset(rosters,0,sizeof(rosters));
    memset(species,0,sizeof(species));memset(types,0,sizeof(types));
    lookups=fail_at=side_calls=0;fail_side=alias_row=0;
    r->trainer_reference=100;r->hero_reference=101;r->fight_references[0]=102;r->fight_references[1]=103;
    for(unsigned i=0;i<6;i++)r->party_references[i]=200+i;
    r->trainer_id=1;r->selection_count=6;r->selection_mode=2;
    d->counts[0]=2;d->counts[1]=2;d->counts[2]=8;d->counts[3]=5;d->counts[4]=18;
    d->lookup=lookup;d->target_side=side;d->host_side=7;
    put16(trainers[1]+2,26);put16(trainers[1]+6,1);
    ai[1][2]=ai[1][3]=ai[1][4]=ai[1][11]=1;
    put16(s->hero,'X');
    for(unsigned i=1;i<5;i++)put32(species[i]+24,1000+i);
    for(unsigned i=0;i<2;i++){
        uint8_t *p=s->hero+0x30+312*i;
        put16(p,(uint16_t)(i+1));memset(p+8,1,4);put16(p+0x8a,20);put16(p+0xd6,(uint16_t)(i+1));
    }
    for(unsigned i=0;i<sizeof(s->fight_pokemon);i++)((uint8_t*)s->fight_pokemon)[i]=(uint8_t)(i*29+17);
    for(unsigned i=0;i<sizeof(s->scratch);i++)((uint8_t*)s->scratch)[i]=(uint8_t)(i*13+7);
    for(unsigned i=0;i<6;i++){s->selected[i]=0xabcd0000u+i;s->candidates[i]=0x98760000u+i;}
    s->rng=(C3BattleRng){0x12345678,19};s->entry_counter=65532;s->candidate_count=99;
}
#ifndef C3_TEST_PARTY_DEPENDENCY_FAILURE
static void ordinary_wrapper(void)
{
    C3BattlePartySortState s,before;C3BattlePartySortRequest r;C3BattlePartySortData d;
    setup(&s,&r,&d);before=s;
    assert(c3_battle_party_sort(&s,&r,&d));
    assert(s.rng.draws==before.rng.draws+2&&s.entry_counter==(uint16_t)(before.entry_counter+9u));
    assert(s.selected[0]==200&&s.selected[1]==201);
    for(unsigned i=2;i<6;i++)assert(!s.selected[i]);
    assert(!memcmp(s.fight_pokemon,before.fight_pokemon,sizeof(s.fight_pokemon)));
    assert(!memcmp(s.hero,before.hero,0x30));
    assert(!memcmp(s.hero+0x30,before.hero+0x30,624));
    assert(!memcmp(s.hero+0x30+1872,before.hero+0x30+1872,2840-0x30-1872));
    for(unsigned i=0;i<6;i++)assert(!memcmp(s.scratch[i],s.hero+0x30+312*i,312));
    const unsigned pads[]={2,3,0x11,0x12,0x13,0x61,0x62,0x63,0x6b,0xe6,0xe7,0xef,0xf6,0xf7,0x137};
    for(unsigned i=2;i<6;i++)for(unsigned j=0;j<sizeof(pads)/sizeof(*pads);j++)assert(s.scratch[i][pads[j]]==before.scratch[i][pads[j]]);
    assert(!side_calls);
    setup(&s,&r,&d);r.selection_count=2;before=s;
    assert(c3_battle_party_sort(&s,&r,&d));
    assert(s.rng.draws==21&&s.entry_counter==5);
    assert(s.selected[0]==200&&s.selected[1]==201);
    (void)before;
}
static void enumeration_filters(void)
{
    for(unsigned filter=0;filter<4;filter++){
        C3BattlePartySortState s,before;C3BattlePartySortRequest r;C3BattlePartySortData d;
        setup(&s,&r,&d);r.filter=(uint8_t)filter;rosters[1][5]=2;rosters[2][5]=3;before=s;
        assert(c3_battle_party_candidates(&s,&r,&d));
        assert(s.entry_counter==(uint16_t)(before.entry_counter+2u));
        assert(!memcmp(&s.rng,&before.rng,sizeof(s.rng)));
        assert(!memcmp(s.hero,before.hero,sizeof(s.hero)));
        assert(!memcmp(s.fight_pokemon,before.fight_pokemon,sizeof(s.fight_pokemon)));
        assert(!memcmp(s.selected,before.selected,sizeof(s.selected)));
        if(filter==0){assert(s.candidate_count==2&&s.candidates[0]==200&&s.candidates[1]==201);}
        if(filter==1){assert(s.candidate_count==1&&s.candidates[0]==200);}
        if(filter==2){assert(s.candidate_count==1&&s.candidates[0]==201);}
        if(filter==3)assert(!s.candidate_count);
        for(unsigned i=s.candidate_count;i<6;i++)assert(!s.candidates[i]);
    }
    C3BattlePartySortState s;C3BattlePartySortRequest r;C3BattlePartySortData d;
    setup(&s,&r,&d);s.selected[0]=200;
    assert(c3_battle_party_candidates(&s,&r,&d));
    assert(s.candidate_count==1&&s.candidates[0]==201&&s.entry_counter==65533);
    setup(&s,&r,&d);put16(s.hero+0x30+312+0x8a,0);
    assert(c3_battle_party_candidates(&s,&r,&d));assert(s.candidate_count==1);
    setup(&s,&r,&d);s.hero[0x30+0xcb]=1;s.hero[0x30+312+0xcd]=1;
    assert(c3_battle_party_candidates(&s,&r,&d));assert(!s.candidate_count&&s.entry_counter==65532);
}
static void branches_and_ordering(void)
{
    C3BattlePartySortState s,before;C3BattlePartySortRequest r;C3BattlePartySortData d;
    setup(&s,&r,&d);ai[1][0]=1;before=s;
    assert(c3_battle_party_select(&s,&r,&d));
    assert(s.rng.draws==21&&s.entry_counter==1); /* enum2+1, selected2 */
    assert(s.selected[0]!=s.selected[1]&&(s.selected[0]==200||s.selected[0]==201));
    assert(!memcmp(s.hero,before.hero,sizeof(s.hero))&&!memcmp(s.scratch,before.scratch,sizeof(s.scratch)));
    assert(!memcmp(s.fight_pokemon,before.fight_pokemon,sizeof(s.fight_pokemon)));
    setup(&s,&r,&d);rosters[1][5]=2;rosters[2][5]=3;
    assert(c3_battle_party_select(&s,&r,&d));assert(s.selected[0]==200&&s.selected[1]==201&&s.rng.draws==21);
    setup(&s,&r,&d);rosters[1][5]=2;rosters[2][5]=2;
    ai[1][3]=0;r.selection_mode=0; /* neither quota phase; unrestricted fill */
    assert(c3_battle_party_select(&s,&r,&d));assert(s.rng.draws==21&&s.selected[0]==200&&s.selected[1]==201);
    setup(&s,&r,&d);ai[1][2]=0;rosters[1][5]=3;rosters[2][5]=2;
    assert(c3_battle_party_select(&s,&r,&d));assert(s.selected[0]==201&&s.selected[1]==200);
    setup(&s,&r,&d);ai[1][2]=0;
    /* Types30/31 differ from abilities32/33. Party types score negative,
     * saved FP1's types score positive; swapping those projections reverses
     * the expected result and must fail this test. */
    species[1][0x30]=0;species[1][0x31]=2;
    species[2][0x30]=2;species[2][0x31]=0;
    species[3][0x30]=1;species[3][0x31]=3;
    species[1][0x32]=species[2][0x32]=1;
    species[1][0x33]=species[2][0x33]=3;
    species[3][0x32]=0;species[3][0x33]=2;
    put16(s.fight_pokemon[1]+4,3);
    for(unsigned i=0;i<18;i++){
        put16(types[i]+8,0x41);put16(types[i]+10,0x43);
        put16(types[i]+12,0x41);put16(types[i]+14,0x42);
    }
    assert(c3_battle_party_select(&s,&r,&d));assert(side_calls>0&&s.rng.draws==21);
    assert(s.selected[0]==200&&s.selected[1]==201); /* negative type-score reverses the seeded201,200 draw order */
    setup(&s,&r,&d);ai[1][2]=0;ai[1][11]=0;
    assert(c3_battle_party_select(&s,&r,&d));assert(!side_calls);
}
static void noops_and_rollback(void)
{
    C3BattlePartySortState s,before;C3BattlePartySortRequest r;C3BattlePartySortData d;
    setup(&s,&r,&d);d.battle_result=1;before=s;
    assert(c3_battle_party_sort(&s,&r,&d));
    assert(!memcmp(s.hero,before.hero,sizeof(s.hero))&&!memcmp(s.selected,before.selected,sizeof(s.selected)));
    assert(!memcmp(s.scratch,before.scratch,sizeof(s.scratch))&&s.entry_counter==before.entry_counter&&s.rng.draws==before.rng.draws);
    setup(&s,&r,&d);put16(trainers[1]+2,1);before=s;assert(c3_battle_party_sort(&s,&r,&d));
    assert(!memcmp(s.hero,before.hero,sizeof(s.hero))&&!memcmp(s.selected,before.selected,sizeof(s.selected)));
    setup(&s,&r,&d);r.trainer_reference=0;before=s;assert(c3_battle_party_sort(&s,&r,&d));assert(!memcmp(s.hero,before.hero,sizeof(s.hero)));
    setup(&s,&r,&d);r.trainer_reference=0;r.hero_reference=0;
    s.selected[0]=r.party_references[0];s.candidates[0]=r.party_references[1];before=s;
    assert(c3_battle_party_sort(&s,&r,&d));assert(!memcmp(&s,&before,sizeof(s)));
    setup(&s,&r,&d);r.fight_references[1]=0;before=s;assert(c3_battle_party_select(&s,&r,&d));assert(!memcmp(s.selected,before.selected,sizeof(s.selected)));
    for(unsigned failure=1;failure<=12;failure++){
        setup(&s,&r,&d);fail_at=failure;before=s;assert(!c3_battle_party_sort(&s,&r,&d));assert(!memcmp(&s,&before,sizeof(s)));
    }
    setup(&s,&r,&d);s.rng.draws=UINT64_MAX;before=s;assert(!c3_battle_party_sort(&s,&r,&d));assert(!memcmp(&s,&before,sizeof(s)));
    setup(&s,&r,&d);alias_row=1;alias_state=&s;before=s;assert(!c3_battle_party_sort(&s,&r,&d));assert(!memcmp(&s,&before,sizeof(s)));
    setup(&s,&r,&d);ai[1][2]=0;fail_side=1;before=s;assert(!c3_battle_party_select(&s,&r,&d));assert(!memcmp(&s,&before,sizeof(s)));
    setup(&s,&r,&d);r.party_references[1]=r.party_references[0];before=s;assert(!c3_battle_party_sort(&s,&r,&d));assert(!memcmp(&s,&before,sizeof(s)));
    setup(&s,&r,&d);r.selection_count=7;before=s;assert(!c3_battle_party_candidates(&s,&r,&d));assert(!memcmp(&s,&before,sizeof(s)));
    setup(&s,&r,&d);r.selection_count=UINT16_MAX;assert(c3_battle_party_sort(&s,&r,&d));assert(s.selected[0]==200&&s.selected[1]==201);
}
int main(void)
{
    ordinary_wrapper();enumeration_filters();branches_and_ordering();noops_and_rollback();
    puts("battle_party_sort: source branches, BE copy/padding, logical handles, snapshots, RNG/counter and rollback passed");
    return 0;
}
#else
static unsigned dependency_mode, dependency_calls;
int c3_battle_pokemon_init(uint8_t p[C3_BATTLE_POKEMON_BYTES])
{
    dependency_calls++;
    if(dependency_mode==1){memset(p,0x6a,312);return 0;}
    return 1;
}
int c3_battle_fight_pokemon_create(uint8_t p[C3_BATTLE_FIGHT_POKEMON_BYTES],
    const uint8_t source[C3_BATTLE_POKEMON_BYTES],uint32_t reference,int16_t entry)
{
    (void)source;(void)reference;(void)entry;
    dependency_calls++;memset(p,0x6b,340);return 0;
}
int main(void)
{
    C3BattlePartySortState s,before;C3BattlePartySortRequest r;C3BattlePartySortData d;
    setup(&s,&r,&d);before=s;dependency_mode=1;
    assert(!c3_battle_party_sort(&s,&r,&d));assert(dependency_calls>0);assert(!memcmp(&s,&before,sizeof(s)));
    setup(&s,&r,&d);before=s;dependency_mode=2;dependency_calls=0;
    assert(!c3_battle_party_candidates(&s,&r,&d));assert(dependency_calls>0);assert(!memcmp(&s,&before,sizeof(s)));
    puts("battle_party_sort: mutating initializer/constructor failure rolls back all state");
    return 0;
}
#endif
