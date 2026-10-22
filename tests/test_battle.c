#include "colosseum3ds/battle.h"
#include "colosseum3ds/save.h"
#include "../src/save_codec.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static void w16(uint8_t *p,unsigned v) {p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void w32(uint8_t *p,uint32_t v)
{p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void sign_pack(uint8_t *p)
{c3_save_codec_sha1(p+56,p+128,C3_BATTLE_PACK_SIZE-128);}

/* Deliberately synthetic structural fixture; no retail records or dialogue
 * are embedded. The optional argv[1] exercises the privately prepared pack. */
static void synthetic(uint8_t *pack)
{
    static const uint8_t pin[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,
        0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    static const uint8_t common[20]={0xe8,0x16,0x76,0x18,0x13,0x90,0x0a,0xa0,
        0xfd,0x4f,0x87,0xd0,0x12,0xb3,0xb2,0xa1,0x9d,0x2f,0x00,0x3e};
    C3BattleData data={0};
    memset(pack,0,C3_BATTLE_PACK_SIZE+1);memcpy(pack,"C3B1",4);
    w32(pack+4,1);w32(pack+8,C3_BATTLE_PACK_SIZE);w32(pack+12,10);
    memcpy(pack+16,pin,20);memcpy(pack+36,common,20);
    data.encounter[0]=2;data.encounter[1]=1;data.encounter[2]=1;
    w16(data.encounter+4,5);w16(data.floor+2,200);
    w16(data.encounter+24,1);w32(data.encounter+28,1);w16(data.encounter+32,10);
    w16(data.trainer[0]+2,1);w16(data.trainer[1]+2,26);w16(data.trainer[1]+4,238);
    data.type[0]=1;data.type[1]=6;data.type[2]=2;
    for(unsigned i=0;i<2;++i){w16(data.roster[i]+10,288);data.roster[i][4]=24;}
    memcpy(pack+128,&data,sizeof(data));sign_pack(pack);
}

static void rejected(const uint8_t *pack,size_t size)
{
    C3BattleData out,prior;char error[C3_BATTLE_ERROR_SIZE];
    memset(&out,0xa5,sizeof(out));prior=out;
    CHECK(!c3_battle_decode(&out,pack,size,error));CHECK(error[0]);
    CHECK(!memcmp(&out,&prior,sizeof(out)));
}

static void request_checks(const C3BattleData *data)
{
    uint8_t *body=calloc(1,C3_SAVE_BODY_SIZE),*before=malloc(C3_SAVE_BODY_SIZE);
    C3BattleRequest request={0},prior;char error[C3_BATTLE_ERROR_SIZE];
    uint32_t args[3]={10,0,0};CHECK(body&&before);
    memcpy(body,"\x0b\x03\x02\x02",4);
    for(unsigned i=0;i<2;++i) {
        uint8_t *p=body+0xa0+i*C3_BATTLE_POKEMON_SIZE;
        w16(p,197-i);p[0x60]=(uint8_t)(26-i);w16(p+0x8a,70-i);w16(p+0x8c,86-i);
        /* Preserve fields the adapter does not interpret. */
        p[0xf1]=(uint8_t)(0x84+i);w32(p+4,0x12345678u+i);
    }
    memcpy(before,body,C3_SAVE_BODY_SIZE);
    CHECK(c3_battle_request(&request,data,args,body,C3_SAVE_BODY_SIZE,error));
    CHECK(!error[0]&&request.status==C3_BATTLE_WAITING_BACKEND&&request.player_party_count==2);
    CHECK(!memcmp(request.native98_args,args,sizeof(args)));
    CHECK(!memcmp(request.player_party,body+0xa0,sizeof(request.player_party)));
    CHECK(!memcmp(body,before,C3_SAVE_BODY_SIZE));
    prior=request;args[0]=11;
    CHECK(!c3_battle_request(&request,data,args,body,C3_SAVE_BODY_SIZE,error));
    CHECK(!memcmp(&request,&prior,sizeof(request)));args[0]=10;
    CHECK(!c3_battle_request(&request,data,args,body,C3_SAVE_BODY_SIZE-1,error));
    CHECK(!memcmp(&request,&prior,sizeof(request)));
    w16(body+0xa0+0x8a,999);
    CHECK(!c3_battle_request(&request,data,args,body,C3_SAVE_BODY_SIZE,error));
    CHECK(!memcmp(&request,&prior,sizeof(request)));
    memcpy(body,before,C3_SAVE_BODY_SIZE);
    /* Fainted party members remain fainted: the backend owns battle rules. */
    w16(body+0xa0+0x8a,0);
    CHECK(c3_battle_request(&request,data,args,body,C3_SAVE_BODY_SIZE,error));
    CHECK(!request.player_party[0][0x8a]&&!request.player_party[0][0x8b]);
    memset(body+0xa0,0,sizeof(request.player_party));prior=request;
    CHECK(!c3_battle_request(&request,data,args,body,C3_SAVE_BODY_SIZE,error));
    CHECK(!memcmp(&request,&prior,sizeof(request)));
    free(before);free(body);
}

int main(int argc,char **argv)
{
    uint8_t pack[C3_BATTLE_PACK_SIZE+1],changed[C3_BATTLE_PACK_SIZE+1];
    C3BattleData data;C3BattleSummary info;char error[C3_BATTLE_ERROR_SIZE];
    synthetic(pack);CHECK(c3_battle_decode(&data,pack,C3_BATTLE_PACK_SIZE,error));
    CHECK(c3_battle_summary(&data,&info));CHECK(info.encounter_id==10&&info.battle_floor==200);
    CHECK(info.active_per_trainer==2&&info.party_limit==6);
    for(size_t n=0;n<C3_BATTLE_PACK_SIZE;++n)rejected(pack,n);
    rejected(pack,C3_BATTLE_PACK_SIZE+1);rejected(NULL,C3_BATTLE_PACK_SIZE);
    static const unsigned corrupt[]={0,4,8,12,16,36,56,76,127,128,300,643};
    for(unsigned i=0;i<sizeof(corrupt)/sizeof(*corrupt);++i) {
        memcpy(changed,pack,sizeof(pack));changed[corrupt[i]]^=1;
        rejected(changed,C3_BATTLE_PACK_SIZE);
    }
    memcpy(changed,pack,sizeof(pack));changed[128]=3;sign_pack(changed);
    rejected(changed,C3_BATTLE_PACK_SIZE);
    C3BattleData invalid=data;invalid.type[2]=3;
    CHECK(!c3_battle_summary(&invalid,&info));
    request_checks(&data);
    if(argc>1) {
        CHECK(c3_battle_load(&data,argv[1],error));CHECK(c3_battle_summary(&data,&info));
        CHECK(info.trainer_id[0]==1&&info.trainer_id[1]==10);
        CHECK(info.input_device[0]==1&&info.input_device[1]==0);
        CHECK(info.prize_multiplier==5&&info.opponent_name_message==0x1014);
        CHECK(info.lose_returns_to_healing&&!info.can_run&&info.can_use_items&&info.grants_experience);
        CHECK(info.opponent[0].record_id==238&&info.opponent[1].record_id==239);
        CHECK(info.opponent[0].species==288&&info.opponent[1].species==288);
        CHECK(info.opponent[0].level==24&&info.opponent[1].level==24);
        CHECK(info.opponent[0].moves[0]==33&&info.opponent[0].moves[1]==45);
        CHECK(info.opponent[1].moves[0]==33&&info.opponent[1].moves[1]==39);
        request_checks(&data);
    }
    printf("first-battle data/request: %u checks passed; backend remains blocked\n",checks);
    return 0;
}
