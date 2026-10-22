#include "colosseum3ds/battle_preview.h"
#include "colosseum3ds/battle_preview_menu.h"
#include "colosseum3ds/game_state.h"
#include "save_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"line %d: %s\n",__LINE__,#x);exit(1);} }while(0)
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static const unsigned ids[12]={33,39,44,45,93,115,165,216,269,270,289,290};
static void seal_preview(C3BattlePreviewTables *p,const C3BattleRequest *r)
{c3_save_codec_sha1(p->bytes+56,p->bytes+128,896);c3_save_codec_sha1(p->bytes+76,(const uint8_t *)&r->data,sizeof r->data);}
static void seal_init(C3BattleInitTables *p,const C3BattleRequest *r)
{c3_save_codec_sha1(p->bytes+56,p->bytes+128,640);c3_save_codec_sha1(p->bytes+76,(const uint8_t *)&r->data,sizeof r->data);}
/* Deliberately synthetic species/nature/move parameters, not original packs. */
static void fixture(C3BattleRequest *r,C3BattleInitTables *init,C3BattlePreviewTables *p)
{
    static const uint8_t pin[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    static const uint8_t common[20]={0xe8,0x16,0x76,0x18,0x13,0x90,0x0a,0xa0,0xfd,0x4f,0x87,0xd0,0x12,0xb3,0xb2,0xa1,0x9d,0x2f,0x00,0x3e};
    static const uint8_t dol[20]={0x87,0x0e,0x8b,0x96,0x93,0xca,0x78,0x07,0x82,0xd8,0x0f,0x22,0xa6,0xa4,0x57,0x2d,0x8b,0xa9,0x45,0x8f};
    memset(r,0,sizeof *r);memset(init,0,sizeof *init);memset(p,0,sizeof *p);
    r->status=C3_BATTLE_WAITING_BACKEND;r->native98_args[0]=10;r->native98_args[1]=1;r->player_party_count=2;
    C3BattleData *d=&r->data;
    d->encounter[0]=2;d->encounter[1]=d->encounter[2]=1;put16(d->encounter+4,5);put16(d->floor+2,200);
    put16(d->encounter+24,1);put32(d->encounter+28,1);put16(d->encounter+32,10);
    put16(d->trainer[0]+2,1);put16(d->trainer[1]+2,26);put16(d->trainer[1]+4,238);
    d->type[0]=1;d->type[1]=6;d->type[2]=2;
    for(unsigned i=0;i<2;++i){uint8_t *v=d->roster[i];v[1]=(uint8_t)i;v[4]=24;put16(v+10,288);put32(v+52,33);put32(v+60,i?39:45);}
    const unsigned moves[2][4]={{44,290,269,289},{93,216,115,270}};
    for(unsigned i=0;i<2;++i){uint8_t *v=r->player_party[i];put16(v,197-i);v[0x60]=(uint8_t)(26-i);
        v[8]=11;v[9]=3;v[10]=v[11]=2;put16(v+0x8a,100);put16(v+0xb0,200);
        for(unsigned j=0;j<6;++j)put16(v+0x8c+2*j,j?70:100);
        for(unsigned j=0;j<4;++j){put16(v+0x78+4*j,moves[i][j]);v[0x7a+4*j]=30;}
    }
    memcpy(init->bytes,"C3I1",4);put32(init->bytes+4,1);put32(init->bytes+8,768);put32(init->bytes+12,10);
    memcpy(init->bytes+16,pin,20);memcpy(init->bytes+36,common,20);
    uint8_t *t=init->bytes+128;put16(t,288);t[2]=127;t[3]=53;
    for(unsigned i=0;i<6;++i)put16(t+8+2*i,60);
    put16(t+20,70);
    for(unsigned i=0;i<250;++i)t[32+i]=100;
    for(unsigned i=1;i<358;++i)t[282+i]=30;
    seal_init(init,r);
    memcpy(p->bytes,"C3P1",4);put32(p->bytes+4,1);put32(p->bytes+8,1024);put32(p->bytes+12,10);
    memcpy(p->bytes+16,pin,20);memcpy(p->bytes+36,common,20);memcpy(p->bytes+96,dol,20);
    for(unsigned i=0;i<12;++i){uint8_t *v=p->bytes+128+60*i;unsigned id=ids[i];put16(v,id);v+=4;
        v[0]=(uint8_t)(id==270?5:id==289?4:0);v[1]=30;v[2]=(uint8_t)(id==44||id==269||id==289?17:id==93||id==115?14:0);
        v[3]=(uint8_t)(id==39||id==45?4:id==115||id==270?5:id==289?1:0);
        v[4]=(uint8_t)(id==115||id==270||id==289?0:100);
        v[5]=(uint8_t)(id==44||id==93||id==290?30:0);v[9]=id==115;
        put16(v+22,id==33||id==44||id==93||id==165||id==290?40:id==216?1:0);
    }
    for(unsigned i=0;i<3;++i){uint8_t *v=p->bytes+848+12*i;put16(v,i<2?197-i:288);
        v[2]=v[3]=(uint8_t)(i==0?17:i==1?14:0);v[4]=(uint8_t)(i<2?28:53);}
    for(unsigned i=0;i<13;++i){p->bytes[884+2*i]=(uint8_t)(i<6?10:10+5*(i-6));p->bytes[885+2*i]=(uint8_t)(i<6?40-5*i:10);
        p->bytes[910+2*i]=(uint8_t)(i<6?3:i-3);p->bytes[911+2*i]=(uint8_t)(i<6?9-i:3);}
    p->bytes[936]=16;p->bytes[937]=27;seal_preview(p,r);
}
static void begin(C3BattlePreview *b,C3BattlePreviewTables *t,C3BattleInitTables *i,C3BattleRequest *r,uint32_t seed)
{char e[192];CHECK(c3_battle_preview_begin(b,r,i,t,seed,e));CHECK(!e[0]);CHECK(!memcmp(r,&b->original_request,sizeof *r));}
static C3BattlePreviewCommand command(const C3BattlePreview *b,const C3BattlePreviewTables *t,unsigned a,unsigned slot)
{C3BattlePreviewCommand c={(uint8_t)slot,(uint8_t)(b->mons[2].hp?2:3)};if(!c3_battle_preview_legal_moves(b,t,a))c.slot=4;return c;}
static int contains(const C3BattlePreview *b,const char *s)
{for(unsigned i=0;i<b->event_count;++i)if(strstr(b->events[i].text,s))return 1;return 0;}
static void damage(void)
{
    C3BattlePreview b;C3BattlePreviewTables t;C3BattleInitTables i;C3BattleRequest r;fixture(&r,&i,&t);begin(&b,&t,&i,&r,1);
    uint32_t value=999;for(unsigned a=0;a<4;++a){b.mons[a].level=50;for(unsigned s=0;s<6;++s)b.mons[a].stats[s]=100;}
    CHECK(c3_battle_preview_base_damage(&b,&t,0,2,33,50,0,1,&value));CHECK(value==24);
    b.mons[0].stages[0]=-2;b.mons[2].stages[1]=2;
    CHECK(c3_battle_preview_base_damage(&b,&t,0,2,33,50,0,1,&value));CHECK(value==7);
    CHECK(c3_battle_preview_base_damage(&b,&t,0,2,33,50,0,2,&value));CHECK(value==24);
    b.reflect[1]=5;
    CHECK(c3_battle_preview_base_damage(&b,&t,0,2,33,50,0,1,&value));CHECK(value==4);
    b.mons[0].stages[0]=0;b.mons[2].stages[1]=0;
    CHECK(c3_battle_preview_base_damage(&b,&t,0,2,33,50,0,1,&value));CHECK(value==16);
    b.mons[3].hp=0;
    CHECK(c3_battle_preview_base_damage(&b,&t,0,2,33,50,0,1,&value));CHECK(value==13);
    b.mons[0].stats[C3_STAT_ATTACK]=1;b.mons[2].stats[C3_STAT_DEFENSE]=999;
    CHECK(c3_battle_preview_base_damage(&b,&t,0,2,44,50,17,1,&value));CHECK(value==24); /* Dark uses SPECIAL, no Reflect. */
    value=0x1234;t.bytes[885]=0;CHECK(!c3_battle_preview_base_damage(&b,&t,0,2,44,50,17,1,&value));CHECK(value==0x1234);
}
static void reject_and_replay(void)
{
    C3BattlePreview b,copy,other;C3BattlePreviewTables t,broken;C3BattleInitTables i;C3BattleRequest r;char e[192];
    fixture(&r,&i,&t);begin(&b,&t,&i,&r,1234);copy=b;
    C3BattlePreviewCommand c[2]={{0,0},{0,2}};
    CHECK(!c3_battle_preview_turn(&b,&t,c,e));CHECK(e[0]);CHECK(!memcmp(&b,&copy,sizeof b));
    c[0].target=2;c[0].slot=4;CHECK(!c3_battle_preview_turn(&b,&t,c,e));CHECK(!memcmp(&b,&copy,sizeof b));
    c[0].slot=0;b.rng.draws=UINT64_MAX;copy=b;CHECK(!c3_battle_preview_turn(&b,&t,c,e));CHECK(!memcmp(&b,&copy,sizeof b));
    begin(&b,&t,&i,&r,1234);other=b;
    for(unsigned n=0;n<20&&b.outcome==C3_PREVIEW_RUNNING;++n){c[0]=command(&b,&t,0,0);c[1]=command(&b,&t,1,0);
        CHECK(c3_battle_preview_turn(&b,&t,c,e));CHECK(c3_battle_preview_turn(&other,&t,c,e));CHECK(!memcmp(&b,&other,sizeof b));
        CHECK(!memcmp(&b.original_request,&r,sizeof r));}
    CHECK(b.outcome!=C3_PREVIEW_RUNNING);CHECK(contains(&b,"No rewards"));
    copy=b;CHECK(!c3_battle_preview_turn(&b,&t,c,e));CHECK(!memcmp(&b,&copy,sizeof b));
    for(unsigned k=0;k<1024;++k){broken=t;broken.bytes[k]^=128;C3BattlePreviewTables untouched=t;
        if(k>=76&&k<96)continue; /* Cross-bound with the actual encounter at begin. */
        CHECK(!c3_battle_preview_decode(&untouched,broken.bytes,1024,e));CHECK(!memcmp(&untouched,&t,sizeof t));}
    begin(&b,&t,&i,&r,2);copy=b;put16(r.player_party[0]+0x88,1);
    CHECK(!c3_battle_preview_begin(&b,&r,&i,&t,2,e));CHECK(!memcmp(&b,&copy,sizeof b));
    fixture(&r,&i,&t);put16(r.player_party[0]+0x8a,0);put16(r.player_party[1]+0x8a,0);
    CHECK(!c3_battle_preview_begin(&b,&r,&i,&t,2,e));CHECK(!memcmp(&b,&copy,sizeof b));
}
static void effects_and_completion(void)
{
    C3BattlePreview b;C3BattlePreviewTables t;C3BattleInitTables init;C3BattleRequest r;char e[192];fixture(&r,&init,&t);
    C3BattlePreviewCommand c[2]={{3,2},{2,2}};
    begin(&b,&t,&init,&r,7);CHECK(c3_battle_preview_turn(&b,&t,c,e));CHECK(contains(&b,"snatched REFLECT"));CHECK(b.reflect[0]==4);
    begin(&b,&t,&init,&r,8);c[0].slot=0;c[1].slot=3;CHECK(c3_battle_preview_turn(&b,&t,c,e));CHECK(contains(&b,"ready to help"));CHECK(b.mons[0].helped);
    begin(&b,&t,&init,&r,9);c[0].slot=2;c[1].slot=2;CHECK(c3_battle_preview_turn(&b,&t,c,e));CHECK(b.mons[2].taunt==1);
    CHECK(c3_battle_preview_legal_moves(&b,&t,2)==1);
    b.mons[0].taunt=1;CHECK(c3_battle_preview_legal_moves(&b,&t,0)==3);
    for(unsigned a=0;a<4;++a)for(unsigned j=0;j<4;++j)b.mons[a].moves[j].pp=0;
    CHECK(!c3_battle_preview_legal_moves(&b,&t,0));c[0].slot=c[1].slot=4;
    CHECK(c3_battle_preview_turn(&b,&t,c,e));CHECK(contains(&b,"STRUGGLE"));CHECK(contains(&b,"recoil"));
    unsigned wins=0,losses=0;
    for(unsigned seed=0;seed<100;++seed){begin(&b,&t,&init,&r,seed);
        if(seed&1){b.mons[0].hp=b.mons[1].hp=1;b.mons[2].stats[C3_STAT_SPEED]=999;b.mons[3].stats[C3_STAT_SPEED]=998;}
        for(unsigned turn=0;turn<100&&b.outcome==C3_PREVIEW_RUNNING;++turn){
            c[0]=command(&b,&t,0,seed%2);c[1]=command(&b,&t,1,seed%2);
            CHECK(c3_battle_preview_turn(&b,&t,c,e));CHECK(b.event_count>0&&b.event_count<=64);
            CHECK(!memcmp(&b.original_request,&r,sizeof r));
            for(unsigned a=0;a<4;++a){CHECK(b.mons[a].hp<=b.mons[a].maximum_hp);for(unsigned j=0;j<7;++j)CHECK(b.mons[a].stages[j]>=-6&&b.mons[a].stages[j]<=6);}
        }
        CHECK(b.outcome!=C3_PREVIEW_RUNNING);wins+=b.outcome==C3_PREVIEW_PLAYER_WON;losses+=b.outcome==C3_PREVIEW_PLAYER_LOST;
    }
    CHECK(wins&&losses);
    printf("Preview synthetic complete encounters: %u wins / %u losses\n",wins,losses);
}
static void menu_control(void)
{
    C3BattlePreview b,saved;C3BattlePreviewTables t;C3BattleInitTables init;C3BattleRequest r;C3BattlePreviewMenu m;char e[192];
    fixture(&r,&init,&t);begin(&b,&t,&init,&r,1);saved=b;c3_battle_preview_menu_init(&m);
    CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));
    CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));
    CHECK(m.page==C3_PREVIEW_MENU_MOVE&&m.actor==0);
    CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));CHECK(m.page==C3_PREVIEW_MENU_TARGET);
    CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_DOWN,e));CHECK(m.target==3);
    CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_B,e));CHECK(m.page==C3_PREVIEW_MENU_MOVE);
    CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_X,e));CHECK(m.page==C3_PREVIEW_MENU_LEAVE);
    CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_B,e));CHECK(m.page==C3_PREVIEW_MENU_MOVE);
    CHECK(!memcmp(&b,&saved,sizeof b));
    CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));
    CHECK(m.page==C3_PREVIEW_MENU_MOVE&&m.actor==1);
    CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));
    CHECK(m.page==C3_PREVIEW_MENU_CONFIRM);CHECK(!memcmp(&b,&saved,sizeof b));
    CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));CHECK(b.turn==1&&m.page==C3_PREVIEW_MENU_EVENTS);
    for(unsigned n=0;n<1000&&m.page!=C3_PREVIEW_MENU_RESULT;++n)CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));
    CHECK(m.page==C3_PREVIEW_MENU_RESULT&&b.outcome!=C3_PREVIEW_RUNNING);
    CHECK(!memcmp(&b.original_request,&r,sizeof r));CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));CHECK(m.page==C3_PREVIEW_MENU_EXIT);
    begin(&b,&t,&init,&r,1);b.mons[0].hp=0;for(unsigned j=0;j<4;++j)b.mons[1].moves[j].pp=0;
    c3_battle_preview_menu_init(&m);CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));
    CHECK(m.actor==1&&m.selection==4);CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));CHECK(m.page==C3_PREVIEW_MENU_TARGET);
    CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));CHECK(m.page==C3_PREVIEW_MENU_CONFIRM);
    CHECK(c3_battle_preview_menu_step(&m,&b,&t,C3_PREVIEW_A,e));CHECK(contains(&b,"STRUGGLE"));
}
static void private_run(const char *directory)
{
    char path[1024],error[192];C3GameState state={0};C3BattleData d;C3BattleRequest r;C3BattleInitTables i;C3BattlePreviewTables t;C3BattlePreview b;
    snprintf(path,sizeof path,"%s/newgame.c3g",directory);CHECK(c3_game_state_new(&state,"WES",0x12345678,path,error));
    snprintf(path,sizeof path,"%s/willie.c3b",directory);CHECK(c3_battle_load(&d,path,error));
    snprintf(path,sizeof path,"%s/willie.c3i",directory);CHECK(c3_battle_init_load(&i,path,error));
    snprintf(path,sizeof path,"%s/willie.c3p",directory);CHECK(c3_battle_preview_load(&t,path,error));
    size_t n;const uint8_t *body=c3_game_state_body(&state,&n);uint8_t *before=malloc(n);CHECK(before);memcpy(before,body,n);
    const uint32_t args[]={10,1,0};CHECK(c3_battle_request(&r,&d,args,body,n,error));begin(&b,&t,&i,&r,0x12345678);
    for(unsigned a=0;a<4;++a)printf("Actor%u %s: HP%u atk%u def%u spa%u spd%u spe%u\n",a,c3_battle_preview_name(a),b.mons[a].hp,b.mons[a].stats[1],b.mons[a].stats[2],b.mons[a].stats[4],b.mons[a].stats[5],b.mons[a].stats[3]);
    for(unsigned turn=0;turn<30&&b.outcome==C3_PREVIEW_RUNNING;++turn){C3BattlePreviewCommand c[2]={command(&b,&t,0,0),command(&b,&t,1,0)};
        if(!c3_battle_preview_turn(&b,&t,c,error)){fprintf(stderr,"private: %s\n",error);CHECK(0);}
        printf("TURN %u seed=%08x draws=%llu\n",b.turn,b.rng.seed,(unsigned long long)b.rng.draws);
        for(unsigned e=0;e<b.event_count;++e)printf("  %s HP=%u,%u,%u,%u\n",b.events[e].text,b.events[e].hp[0],b.events[e].hp[1],b.events[e].hp[2],b.events[e].hp[3]);
        CHECK(!memcmp(body,before,n));CHECK(!memcmp(&r,&b.original_request,sizeof r));
    }
    CHECK(b.outcome!=C3_PREVIEW_RUNNING);CHECK(!memcmp(body,before,n));free(before);c3_game_state_free(&state);
}
int main(int argc,char **argv)
{damage();reject_and_replay();effects_and_completion();menu_control();if(argc==2)private_run(argv[1]);printf("Battle preview: %u checks passed; no retail AI, GSVM completion or save writes\n",checks);return 0;}
