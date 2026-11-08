#include "colosseum3ds/battle.h"
#include "colosseum3ds/save.h"
#include "save_codec.h"

#include <stdio.h>
#include <string.h>

_Static_assert(sizeof(C3BattleData) == C3_BATTLE_PACK_SIZE - 128u, "Battle record layout");
static const uint8_t source_pin[20] = {0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,
    0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
static const uint8_t common_pin[20] = {0xe8,0x16,0x76,0x18,0x13,0x90,0x0a,0xa0,
    0xfd,0x4f,0x87,0xd0,0x12,0xb3,0xb2,0xa1,0x9d,0x2f,0x00,0x3e};
static uint16_t be16(const uint8_t *p) { return (uint16_t)((unsigned)p[0]*256u+p[1]); }
static uint32_t be32(const uint8_t *p)
{ return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3]; }
static int bad(char *error, const char *message)
{ if (error) snprintf(error,C3_BATTLE_ERROR_SIZE,"%s",message); return 0; }

static int valid_data(const C3BattleData *d)
{
    if (!d || d->encounter[0]!=2 || d->encounter[1]!=1 || d->encounter[2]!=1 ||
        be16(d->encounter+4)!=5 || be16(d->floor+2)!=200 ||
        be16(d->encounter+24)!=1 || be32(d->encounter+28)!=1 ||
        be16(d->encounter+32)!=10 || be32(d->encounter+36)!=0 ||
        be16(d->encounter+40) || be16(d->encounter+48) ||
        be16(d->trainer[0]+2)!=1 || be16(d->trainer[1]+2)!=26 ||
        be16(d->trainer[1]+4)!=238 || d->type[0]!=1 ||
        d->type[1]!=6 || d->type[2]!=2 || be16(d->roster[2]+10)) return 0;
    for (unsigned i=0;i<25;++i) if(d->kind[i]>1) return 0;
    for (unsigned i=0;i<2;++i) {
        const uint8_t *p=d->roster[i];
        if(be16(p+10)!=288 || p[4]!=24 || p[3]!=0) return 0;
        for(unsigned j=0;j<4;++j) {
            uint32_t move=be32(p+52+j*8);
            if(move!=UINT32_MAX && move>354) return 0;
        }
    }
    return 1;
}

int c3_battle_decode(C3BattleData *data,const void *pack,size_t size,char error[C3_BATTLE_ERROR_SIZE])
{
    const uint8_t *p=pack; uint8_t digest[20]; C3BattleData next;
    if(!data || !p || size!=C3_BATTLE_PACK_SIZE) return bad(error,"Invalid bounded battle pack size.");
    if(memcmp(p,"C3B1",4) || be32(p+4)!=1 || be32(p+8)!=size || be32(p+12)!=10 ||
       memcmp(p+16,source_pin,20) || memcmp(p+36,common_pin,20))
        return bad(error,"Battle pack source/profile differs from admitted encounter 10.");
    for(unsigned i=76;i<128;++i) if(p[i]) return bad(error,"Unsupported battle pack header fields.");
    c3_save_codec_sha1(digest,p+128,(uint32_t)(size-128));
    if(memcmp(digest,p+56,20)) return bad(error,"Battle pack checksum mismatch.");
    memcpy(&next,p+128,sizeof(next));
    if(!valid_data(&next)) return bad(error,"Unsupported or inconsistent first-battle records.");
    *data=next; if(error)error[0]=0; return 1;
}

int c3_battle_load(C3BattleData *data,const char *path,char error[C3_BATTLE_ERROR_SIZE])
{
    uint8_t pack[C3_BATTLE_PACK_SIZE];
    if(!data || !path) return bad(error,"Missing first-battle data path.");
    FILE *f=fopen(path,"rb"); if(!f)return bad(error,"First-battle data pack is unavailable.");
    size_t size=fread(pack,1,sizeof(pack),f);
    int extra=fgetc(f),failed=ferror(f); if(fclose(f))failed=1;
    if(size!=sizeof(pack)||extra!=EOF||failed)return bad(error,"Truncated or oversized first-battle data pack.");
    return c3_battle_decode(data,pack,sizeof(pack),error);
}

int c3_battle_summary(const C3BattleData *data,C3BattleSummary *summary)
{
    if(!summary||!valid_data(data))return 0;
    C3BattleSummary s={0};
    s.encounter_id=10;s.floor_data_id=be16(data->encounter+4);s.battle_floor=be16(data->floor+2);
    s.trainer_kind=be16(data->trainer[1]+2);s.prize_multiplier=be16(data->opponent_kind);
    s.opponent_name_message=be32(data->trainer[1]+8);
    s.trainers_per_side=data->type[0];s.party_limit=data->type[1];s.active_per_trainer=data->type[2];
    s.lose_returns_to_healing=data->encounter[2];s.can_run=data->kind[6];
    s.can_use_items=data->kind[4];s.grants_experience=data->kind[8];
    for(unsigned i=0;i<2;++i) {
        s.trainer_id[i]=be16(data->encounter+24+i*8);
        s.input_device[i]=be32(data->encounter+28+i*8);
        s.opponent[i].record_id=(uint16_t)(be16(data->trainer[1]+4)+i);
        s.opponent[i].species=be16(data->roster[i]+10);s.opponent[i].level=data->roster[i][4];
        for(unsigned j=0;j<4;++j) {
            uint32_t value=be32(data->roster[i]+52+j*8);
            s.opponent[i].moves[j]=value==UINT32_MAX?-1:(int32_t)value;
        }
    }
    *summary=s;return 1;
}

int c3_battle_request(C3BattleRequest *request,const C3BattleData *data,const uint32_t args[3],
                     const void *body,size_t size,char error[C3_BATTLE_ERROR_SIZE])
{
    C3BattleRequest next={0};const uint8_t *bytes=body;
    if(!request||!valid_data(data)||!args||args[0]!=10||args[1]>255u||
       !bytes||size!=C3_SAVE_BODY_SIZE)return bad(error,"Invalid encounter request or original save body.");
    if(bytes[0]!=0x0b||!bytes[1]||bytes[2]!=2||bytes[3]!=2)
        return bad(error,"Unsupported player save body for first battle.");
    for(unsigned i=0;i<C3_BATTLE_PARTY_MAX;++i) {
        const uint8_t *p=bytes+0xa0u+i*C3_BATTLE_POKEMON_SIZE;
        uint16_t species=be16(p);
        if(species) {
            if(species>=413||p[0x60]<1||p[0x60]>100||p[0xcd]==1||
               !be16(p+0x8c)||be16(p+0x8a)>be16(p+0x8c))
                return bad(error,"Invalid player party record for first battle.");
            ++next.player_party_count;
        }
        memcpy(next.player_party[i],p,C3_BATTLE_POKEMON_SIZE);
    }
    if(!next.player_party_count)return bad(error,"First battle requires the current player party.");
    memcpy(next.native98_args,args,sizeof(next.native98_args));next.data=*data;
    next.status=C3_BATTLE_WAITING_BACKEND;*request=next;if(error)error[0]=0;return 1;
}
