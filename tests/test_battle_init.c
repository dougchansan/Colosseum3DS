#include "colosseum3ds/battle_init.h"
#include "save_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); exit(1); } } while(0)
static void put16(uint8_t *p,unsigned n) { p[0]=(uint8_t)(n>>8);p[1]=(uint8_t)n; }
static void put32(uint8_t *p,uint32_t n) { p[0]=(uint8_t)(n>>24);p[1]=(uint8_t)(n>>16);p[2]=(uint8_t)(n>>8);p[3]=(uint8_t)n; }
static uint16_t read16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]*256+p[1]); }

/* Independently executed pinned personality-filter body. The only bridge is
 * its source accessors and deterministic HSD leaf; no native initializer
 * implementation is substituted for the reference filter. */
typedef uint8_t u8; typedef int8_t s8; typedef uint16_t u16; typedef uint32_t u32;
static uint32_t ref_seed,ref_initial;
static unsigned ref_ratio,ref_draws;
static uint16_t fn_800E0C54(void)
{ ref_seed=ref_seed*214013u+2531011u;++ref_draws;return (uint16_t)(ref_seed>>16); }
static u32 pokemonGetStatus(u8 *object,u32 id,u32 selector,u32 index)
{
    (void)id;CHECK(index==0);
    if (selector==0x6e) { CHECK(object!=NULL);return 288; }
    if (selector==0x6f) { CHECK(object!=NULL);return ref_initial; }
    CHECK(object==NULL&&selector==0x13);return ref_ratio;
}
static u8 sexGetPokemonSexRaitoKotei(unsigned sex)
{ const u8 ratio[]={0,254,255};CHECK(sex<3);return ratio[sex]; }
#include "battle_init_reference.inc"

static void seal(C3BattleInitTables *tables,const C3BattleRequest *request)
{
    c3_save_codec_sha1(tables->bytes+56,tables->bytes+128,640);
    c3_save_codec_sha1(tables->bytes+76,(const uint8_t *)&request->data,sizeof request->data);
}
static void fixture(C3BattleInitTables *tables,C3BattleRequest *request)
{
    static const uint8_t source[]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    static const uint8_t common[]={0xe8,0x16,0x76,0x18,0x13,0x90,0x0a,0xa0,0xfd,0x4f,0x87,0xd0,0x12,0xb3,0xb2,0xa1,0x9d,0x2f,0x00,0x3e};
    memset(tables,0,sizeof *tables);memset(request,0,sizeof *request);
    memcpy(tables->bytes,"C3I1",4);put32(tables->bytes+4,1);put32(tables->bytes+8,768);put32(tables->bytes+12,10);
    memcpy(tables->bytes+16,source,20);memcpy(tables->bytes+36,common,20);
    uint8_t *t=tables->bytes+128;
    put16(t,288);t[2]=127;t[3]=42;t[4]=43;put16(t+20,70);
    for(unsigned i=0;i<6;++i)put16(t+8+i*2,50+i*7); /* Synthetic stats. */
    for(unsigned n=0;n<25;++n)for(unsigned i=0;i<5;++i){
        unsigned up=n/5,down=n%5;
        t[32+n*10+i*2]=(uint8_t)(up==down?100:i==up?110:i==down?90:100);
        t[33+n*10+i*2]=100;
    }
    for(unsigned i=1;i<358;++i)t[282+i]=20;
    C3BattleData *d=&request->data;
    d->encounter[0]=2;d->encounter[1]=d->encounter[2]=1;put16(d->encounter+4,5);
    put16(d->floor+2,200);put16(d->encounter+24,1);put32(d->encounter+28,1);put16(d->encounter+32,10);
    put16(d->trainer[0]+2,1);put16(d->trainer[1]+2,26);put16(d->trainer[1]+4,238);
    d->type[0]=1;d->type[1]=6;d->type[2]=2;
    for(unsigned i=0;i<2;++i){
        uint8_t *r=d->roster[i];r[1]=(uint8_t)i;r[4]=24;put16(r+10,288);
        put32(r+52,33);put32(r+60,i?39:45);
    }
    request->status=C3_BATTLE_WAITING_BACKEND;request->native98_args[0]=10;request->native98_args[1]=1;
    request->player_party_count=2;
    for(unsigned i=0;i<2;++i){
        uint8_t *p=request->player_party[i];memset(p,0x39+i,312);
        put16(p,197-i);p[0x60]=(uint8_t)(26-i);p[0xcd]=0;
        put16(p+0x8c,70);put16(p+0x8a,i?0:35); /* Preserve damage AND fainting. */
    }
    seal(tables,request);
}
static void same_rng(const C3BattleRng *a,const C3BattleRng *b)
{ CHECK(a->seed==b->seed&&a->draws==b->draws); }
static void compare_reference(C3BattleInitTables *tables,C3BattleRequest *request,uint32_t seed,uint32_t trainer)
{
    C3BattleRng rng={seed,17};C3BattleInitialState out={0};char error[C3_BATTLE_ERROR_SIZE];
    C3BattleRequest saved=*request;C3BattleInitTables saved_tables=*tables;
    CHECK(c3_battle_init_prepare(&out,request,tables,trainer,&rng,error));
    CHECK(!error[0]);CHECK(out.stage==C3_BATTLE_INIT_PARTY_PARAMETERS&&out.opponent_count==2);
    CHECK(!memcmp(request,&saved,sizeof saved));CHECK(!memcmp(tables,&saved_tables,sizeof saved_tables));
    CHECK(!memcmp(&out.request,request,sizeof *request));CHECK(out.request.status==C3_BATTLE_WAITING_BACKEND);
    CHECK(out.opponent_trainer_id==trainer);CHECK(out.rng_before.seed==seed&&out.rng_before.draws==17);
    ref_seed=seed;ref_draws=0;ref_ratio=tables->bytes[128+2];
    const unsigned order[]={0,1,2,4,5,3};
    for(unsigned i=0;i<2;++i){
        u8 object=1;const uint8_t *r=request->data.roster[i];const uint8_t *t=tables->bytes+128;
        const C3BattleInitialOpponent *p=out.opponents+i;
        ref_initial=(u32)fn_800E0C54()<<16;ref_initial|=fn_800E0C54();
        CHECK(p->initial_personality==ref_initial);
        u32 a=fn_800E0C54(),b=fn_800E0C54(),ability=fn_800E0C54()&1;
        const unsigned ivs[]={a&31,(a>>5)&31,(a>>10)&31,(b>>5)&31,(b>>10)&31,b&31};
        unsigned before=ref_draws;
        uint32_t value=pokemonCreateRndFit(&object,(s8)r[1],(s8)r[2],0,trainer);
        CHECK(p->personality==value);CHECK(p->personality_attempts==(ref_draws-before)/2);
        CHECK(p->sex==pokemonGetSexFromRnd(&object,value));CHECK(p->nature==value%25);
        CHECK(p->roster_id==238+i&&p->pokemon.species_id==288&&p->pokemon.level==24);
        unsigned flag=t[4]?((s8)r[0]>=0?r[0]:ability):0;
        CHECK(p->ability_flag==flag&&p->ability_id==t[3+flag]);
        CHECK(p->held_item==0);CHECK(p->friendship==(read16(r+8)==65535?read16(t+20):read16(r+8)));
        for(unsigned stat=0;stat<6;++stat){
            unsigned iv=r[28+stat]==255?ivs[stat]:r[28+stat];
            unsigned ev=read16(r+34+2*stat);if(ev==65535)ev=0;
            unsigned base=read16(t+8+2*stat),expected=(2*base+iv+ev/4)*24/100;
            expected+=stat?5:34;
            if(stat)expected=expected*t[32+p->nature*10+(stat-1)*2]/100;
            CHECK(p->pokemon.iv[order[stat]]==iv&&p->pokemon.ev[order[stat]]==ev);
            CHECK(p->pokemon.stats[order[stat]]==expected);
        }
        CHECK(p->pokemon.current_hp==p->pokemon.stats[C3_STAT_HP]);
        CHECK(p->moves[0].id==33&&p->moves[0].pp==t[282+33]);
        CHECK(p->moves[1].id==(i?39:45));CHECK(p->moves[2].id==0&&p->moves[2].pp==0);
    }
    CHECK(rng.seed==ref_seed&&rng.draws==17+ref_draws);
    CHECK(out.rng_after.seed==rng.seed&&out.rng_after.draws==rng.draws);
}
static void differential(void)
{
    C3BattleInitTables t;C3BattleRequest r;fixture(&t,&r);
    for(unsigned i=0;i<400;++i)compare_reference(&t,&r,i*2654435761u,0xa4b8c012u);
    for(unsigned n=0;n<25;++n)for(unsigned sex=0;sex<4;++sex){
        for(unsigned i=0;i<2;++i){
            r.data.roster[i][0]=255;r.data.roster[i][1]=(uint8_t)(sex==3?255:sex);
            r.data.roster[i][2]=(uint8_t)n;
            memset(r.data.roster[i]+28,255,6);
            for(unsigned j=0;j<6;++j)put16(r.data.roster[i]+34+2*j,j==5?65535:20);
        }
        seal(&t,&r);compare_reference(&t,&r,n*1234567u+sex,0x12345678);
    }
    for(unsigned i=0;i<3;++i){const uint8_t ratios[]={0,254,255};t.bytes[130]=ratios[i];t.bytes[132]=0;
        seal(&t,&r);compare_reference(&t,&r,0xdeadbeef,0x03040506);}
}
static void failure(C3BattleInitTables *t,C3BattleRequest *r,C3BattleRng *rng)
{
    C3BattleInitialState out,saved;memset(&out,0x5a,sizeof out);saved=out;
    C3BattleRng prior=*rng;C3BattleRequest request=*r;C3BattleInitTables tables=*t;
    char error[192]={0};
    CHECK(!c3_battle_init_prepare(&out,r,t,1,rng,error));CHECK(error[0]);
    CHECK(!memcmp(&out,&saved,sizeof out));same_rng(rng,&prior);
    CHECK(!memcmp(r,&request,sizeof *r));CHECK(!memcmp(t,&tables,sizeof *t));
}
static void failures(void)
{
    C3BattleInitTables t;C3BattleRequest r;C3BattleRng rng={1,0};fixture(&t,&r);
    t.bytes[767]^=1;failure(&t,&r,&rng);fixture(&t,&r);
    r.data.roster[0][1]=2;failure(&t,&r,&rng); /* stale encounter digest */
    fixture(&t,&r);r.native98_args[1]=2;failure(&t,&r,&rng);
    fixture(&t,&r);r.status=C3_BATTLE_NO_REQUEST;failure(&t,&r,&rng);
    fixture(&t,&r);r.player_party_count=3;failure(&t,&r,&rng);
    fixture(&t,&r);r.player_party[0][0xcd]=1;failure(&t,&r,&rng);
    fixture(&t,&r);put16(r.player_party[0]+0x8a,71);failure(&t,&r,&rng);
    fixture(&t,&r);r.data.roster[0][3]=1;seal(&t,&r);failure(&t,&r,&rng);
    fixture(&t,&r);put32(r.data.roster[0]+16,1);seal(&t,&r);failure(&t,&r,&rng);
    fixture(&t,&r);put32(r.data.roster[0]+52,UINT32_MAX);seal(&t,&r);failure(&t,&r,&rng);
    fixture(&t,&r);r.data.roster[0][48]=1;seal(&t,&r);failure(&t,&r,&rng);
    fixture(&t,&r);r.data.roster[0][28]=32;seal(&t,&r);failure(&t,&r,&rng);
    fixture(&t,&r);put16(r.data.roster[0]+34,256);seal(&t,&r);failure(&t,&r,&rng);
    fixture(&t,&r);for(unsigned i=0;i<6;++i)put16(r.data.roster[0]+34+2*i,100);
    seal(&t,&r);failure(&t,&r,&rng);
    fixture(&t,&r);rng.draws=UINT64_MAX-4;failure(&t,&r,&rng);
    /* Exhaust during the SECOND opponent, after first construction succeeded. */
    C3BattleInitialState good;char error[192];rng.draws=0;
    CHECK(c3_battle_init_prepare(&good,&r,&t,1,&rng,error));
    rng.seed=1;rng.draws=UINT64_MAX-(5+2*good.opponents[0].personality_attempts)-4;
    failure(&t,&r,&rng);
    fixture(&t,&r);rng=(C3BattleRng){1,0};C3BattleRng prior=rng;
    CHECK(!c3_battle_init_prepare((C3BattleInitialState *)(void *)&r,&r,&t,1,&rng,error));
    same_rng(&rng,&prior);
    CHECK(!c3_battle_init_prepare(&good,&r,&t,1,(C3BattleRng *)(void *)&good,error));
    CHECK(!c3_battle_init_prepare(NULL,&r,&t,1,&rng,error));
    C3BattleInitTables copy=t;
    for(unsigned i=0;i<768;++i){
        if(i>=76&&i<96)continue; /* Encounter binding is checked against request. */
        C3BattleInitTables broken=t;broken.bytes[i]^=0x80;
        CHECK(!c3_battle_init_decode(&copy,broken.bytes,768,error));CHECK(!memcmp(&copy,&t,sizeof t));
    }
    CHECK(!c3_battle_init_decode(&copy,t.bytes,767,error));CHECK(!memcmp(&copy,&t,sizeof t));
    CHECK(c3_battle_init_decode(&copy,copy.bytes,768,error));
}
static void private_case(const char *path,const char *battle_path)
{
    C3BattleInitTables t;C3BattleRequest r;fixture(&t,&r);char error[192];
    CHECK(c3_battle_init_load(&t,path,error));CHECK(c3_battle_load(&r.data,battle_path,error));
    C3BattleRng rng={0x12345678u,0};C3BattleInitialState out={0};
    CHECK(c3_battle_init_prepare(&out,&r,&t,0x11223344u,&rng,error));
    compare_reference(&t,&r,0x12345678u,0x11223344u);
    printf("Private canonical encounter profile: seed=%08x draws=%llu\n",rng.seed,(unsigned long long)rng.draws);
    for(unsigned i=0;i<2;++i){const C3BattleInitialOpponent *p=out.opponents+i;
        printf("roster=%u pid=%08x tries=%u sex=%u nature=%u ability=%u hp=%u stats=%u,%u,%u,%u,%u moves=%u/%u,%u/%u\n",
            p->roster_id,p->personality,p->personality_attempts,p->sex,p->nature,p->ability_id,p->pokemon.current_hp,
            p->pokemon.stats[1],p->pokemon.stats[2],p->pokemon.stats[3],p->pokemon.stats[4],p->pokemon.stats[5],
            p->moves[0].id,p->moves[0].pp,p->moves[1].id,p->moves[1].pp);
    }
}
int main(int argc,char **argv)
{
    differential();failures();
    if(argc==3)private_case(argv[1],argv[2]);
    else CHECK(argc==1);
    printf("Battle initialization: %u checks passed; source-parity party parameters only, no turns or writeback\n",checks);
    return 0;
}
