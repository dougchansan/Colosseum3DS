#include "colosseum3ds/battle_runtime_trainer.h"
#include "colosseum3ds/battle_fight_pokemon.h"
#include "dolphin/types.h"
#include <stdatomic.h>
#include <string.h>

typedef struct StatusIdTable7 {u16 id[7];} StatusIdTable7;
typedef struct C3OutEnemy {u8 bytes[12];} C3OutEnemy;
static atomic_flag in_use=ATOMIC_FLAG_INIT;
static int boundary_error;
static u16 be16(const u8 *p){return (u16)((u16)p[0]<<8|p[1]);}
static u32 be32(const u8 *p){return (u32)p[0]<<24|(u32)p[1]<<16|(u32)p[2]<<8|p[3];}
static s32 signed32(const u8 *p){u32 n=be32(p);return n<=INT32_MAX?(s32)n:(s32)(-1-(s32)(UINT32_MAX-n));}
static void put16(u8 *p,u32 n){p[0]=(u8)(n>>8);p[1]=(u8)n;}
static void put32(u8 *p,u32 n){p[0]=(u8)(n>>24);p[1]=(u8)(n>>16);p[2]=(u8)(n>>8);p[3]=(u8)n;}
static void *c3r_src_fightTrainerGetStatus(void *p,u32 slot,u32 field,u32 index)
{
    (void)slot;u8 *b=p;if(!b)return 0;
    switch(field){
    case 0x44:return (void*)(b+4);
    case 0x45:if(index<6)return (void*)(b+0xb1c+index*0x154);break;
    case 0x46:if(index<2)return (void*)(b+0x1314+index*0x6e0);break;
    case 0x47:return (void*)(b+0x20d4);
    case 0x4d:return (void*)(b+0x27c4);
    case 0x4e:if(index<12)return (void*)(b+0x27f4+index*0x14);break;
    }boundary_error=1;return 0;
}
static void c3r_src_fightTrainerSetStatus(void *p,u32 slot,u32 field,u32 index,u32 n)
{
    (void)slot;(void)index;if(!p)return;u8 *b=p;
    switch(field){
    case 0x43:put16(b,n);return;case 0x48:b[0x27b4]=(u8)n;return;
    case 0x49:put32(b+0x27b8,n);return;case 0x4a:b[0x27b5]=(u8)n;return;
    case 0x4b:b[0x27bc]=(u8)n;return;case 0x4c:put32(b+0x27c0,n);return;
    }boundary_error=1;
}
static void c3r_src_heroInit(void *p){if(!c3_battle_trainer_init_hero(p))boundary_error=1;}
static void c3r_src_heroBiosCopy(void *dst,const void *src){if(dst&&src)memmove(dst,src,0xb18);}
static void c3r_src_pokemonInit(void *p){if(!c3_battle_pokemon_init(p))boundary_error=1;}
static void c3r_src_fightPokemonInitAry(void *p,u32 count)
{if(!p)return;for(u32 i=0;i<count;i++)if(!c3_battle_fight_pokemon_init((u8*)p+i*0x154))boundary_error=1;}
static void *c3r_src_pokemonGetStatus(void *p,u32 slot,u32 field,u32 index)
{
    (void)slot;if(!p)return 0;u8 *b=p;
    switch(field){
    case 0xcc:return (void*)(b+4);case 0xcd:if(index<1)return (void*)(b+0x13c);break;
    case 0xd7:return (void*)(b+8);
    case 0xd8:if(index<52)return (void*)(b+0x15c+index*16);break;
    case 0xd9:return (void*)(b+0x49c);case 0xe5:return (void*)(b+0x5f4);
    case 0xf8:return (void*)(b+0x548);case 0xfe:return (void*)(b+0x62c);
    case 0x101:return (void*)(b+0x664);case 0x122:if(index<4)return (void*)(b+0x6b0+index*12);break;
    }boundary_error=1;return 0;
}
static int out_field(u32 field,u32 index,u32 *offset,u32 *width)
{
    *width=2;
    if(field>=0xe6&&field<=0xec){*offset=0x604+field-0xe6;*width=1;return 1;}
    if(field>=0xef&&field<=0xf7){*offset=0x668+(field-0xef)*2;return 1;}
    if(field>=0x102&&field<=0x105){*offset=0x682+(field-0x102)*2;return 1;}
    if(field>=0x106&&field<=0x11a){*offset=0x68a+field-0x106;*width=1;return 1;}
    if(field>=0x11c&&field<=0x11f){*offset=0x6a4+(field-0x11c)*2;return 1;}
    switch(field){
    case 0xcb:case 0xd5:*offset=0;*width=4;return 1;case 0xd6:*offset=4;*width=4;return 1;
    case 0xce:*offset=0x14c;return 1;
    case 0xcf:case 0xd0:case 0xd1:case 0xd2:*offset=0x14e + field-0xcf;*width=1;return 1;
    case 0xed:*offset=0x610;return 1;case 0xee:*offset=0x600;*width=4;return 1;
    case 0xfd:if(index<12){*offset=0x612+2*index;return 1;}return 0;
    case 0xff:if(index<2){*offset=0x65c+2*index;return 1;}return 0;
    case 0x100:*offset=0x660;return 1;case 0xf9:*offset=0x67a;*width=1;return 1;
    case 0xfb:*offset=0x680;return 1;case 0xfc:*offset=0x67e;*width=1;return 1;
    case 0x11b:*offset=0x6a0;*width=4;return 1;case 0x120:*offset=0x6ac;*width=1;return 1;
    case 0x121:*offset=0x6ae;return 1;
    }return 0;
}
static void c3r_src_pokemonSetStatus(void *p,u32 slot,u32 field,u32 index,uintptr_t n)
{
    (void)slot;if(!p)return;u32 offset,width;
    if(!out_field(field,index,&offset,&width)){boundary_error=1;return;}
    u8 *b=(u8*)p+offset;if(width==4)put32(b,(u32)n);else if(width==2)put16(b,(u32)n);else *b=(u8)n;
}
static void c3r_src_fn_801FD830(void *p,u16 n){if(p)put16((u8*)p+0x60c,n);}
static void c3r_src_fn_80119FF0(u8 *p,u16 n){if(p)put16(p,n);}
static void c3r_src_fn_80119FE0(u8 *p,u16 n){if(p)put16(p+2,n);}
static void c3r_src_fn_80119FD0(u8 *p,s8 n){if(p)p[4]=(u8)n;}
static void c3r_src_fn_80119FC0(u8 *p,u8 n){if(p)p[5]=n;}
static void c3r_src_fn_80119FB0(u8 *p,u8 n){if(p)p[6]=n;}
static void c3r_src_fn_80119FA0(u8 *p,u32 n){if(p)put32(p+8,n);}
static void c3r_src_fn_80119F90(u8 *p,u16 n){if(p)put16(p+12,n);}
static u32 c3r_src_fn_8011A030(u8 *p){return p?p[6]:0;}
static void *c3r_src_wazaGetStatus(void *p,u32 id,u32 field,u32 index)
{(void)id;if(p&&field==0x2a&&index<9)return (void*)((u8*)p+8+16*index);boundary_error=1;return 0;}
static void c3r_src_wazaSetStatus(void *p,u32 id,u32 field,u32 index,u32 n)
{
    (void)id;(void)index;if(!p)return;u8 *b=p;
    if(field==0x26){b[0]=(u8)n;return;}
    if(field>=0x27&&field<=0x29){put16(b+2*(field-0x26),n);return;}
    switch(field){
    case 0x2b:b[0x98]=(u8)n;return;case 0x2c:b[0x99]=(u8)n;return;
    case 0x2d:put32(b+0x9c,n);return;case 0x2e:put32(b+0xa0,n);return;
    case 0x2f:put16(b+0xa4,n);return;case 0x30:put16(b+0xa6,n);return;
    case 0x31:b[0xa8]=(u8)n;return;case 0x32:b[0xa9]=(u8)n;return;
    }boundary_error=1;
}
static void *c3r_src_statusGetStatus(u32 category,void *obj,u32 id,u32 field,u32 index)
{if(category==6)return c3r_src_wazaGetStatus(obj,id,field,index);boundary_error=1;return 0;}
static u32 c3r_src_fn_800E0C54(void)
{
    /* Pinned status3F selects kaisuu0: min=max=-1, so this branch is
     * unreachable. Never fabricate RNG for a different source profile. */
    boundary_error=1;return 0;
}
static void c3r_src_fn_80142B24(void *p,u32 id,u32 field,u32 index,uintptr_t n)
{
    (void)id;(void)index;if(!p)return;u8 *b=p;
    switch(field){case 0x1e:put16(b,(u32)n);return;case 0x1f:put16(b+2,(u32)n);return;
    case 0x20:put32(b+4,(u32)n);return;case 0x21:put32(b+8,(u32)n);return;}
    boundary_error=1;
}
static void c3r_src_fightActionBiosSetKind(u8 *p,u32 n){if(p)put16(p,n);}
static void c3r_src_fightActionBiosSetBuff(u8 *p,u32 n){if(p)put32(p+4,n);}
static void c3r_src_fightActionBiosSetFightActionDataPtr(u8 *p,u32 n){if(p)put32(p+8,n);}
static void c3r_src_fightActionBiosSetBuffDataPtr(u8 *p,u32 n){if(p)put32(p+12,n);}
static void c3r_src_fightActionBiosSetBuffDataId(u8 *p,u32 n){if(p)put32(p+16,n);}
static void c3r_src_fightActionBiosSetActorFightTargetPtr(u8 *p,u32 n){if(p)put32(p+20,n);}
static void c3r_src_fightActionBiosSetMotoFightActionDataPtr(u8 *p,u32 n){if(p)put32(p+24,n);}
static void c3r_src_fightActionBiosSetFifoBanme(u8 *p,s32 n){if(p)put32(p+28,(u32)n);}
static void c3r_src_fightActionBiosSetDispBuff(u8 *p,u32 index,u32 n)
{if(p&&index<4)put32(p+32+index*4,n);else boundary_error=1;}
static void c3r_src_fightTrainerEnemyPokemonBiosSetFightEntryeId(void *p,uintptr_t n){if(p)put16(p,(u32)n);}
static void c3r_src_fn_801FBDF4(void *p,u8 index,u32 n){if(p&&index<=4)put16((u8*)p+2+index*2,n);else boundary_error=1;}
static void c3r_src_fightTrainerEnemyPokemonBiosSetTokuseiFlag(u8 *p,u16 n){if(p)put16(p+10,n);}
static void c3r_src_fightTrainerEnemyPokemonBiosSetStoreTokuseiData(u8 *p,u16 n){if(p)put16(p+12,n);}
static void c3r_src_fightTrainerEnemyPokemonBiosSetNowhp1banhikuiFlag(u8 *p,u8 n){if(p)p[14]=n;}
static void c3r_src_fightTrainerEnemyPokemonBiosSetLv1banhikuiFlag(u8 *p,u8 n){if(p)p[15]=n;}
static void c3r_src_fightTrainerEnemyPokemonBiosSetDefense1banhikuiFlag(u8 *p,u8 n){if(p)p[16]=n;}
static void c3r_src_fightTrainerEnemyPokemonBiosSetBadwazaHaveFlag(u8 *p,u8 n){if(p)p[17]=n;}
static void c3r_src_fightTrainerEnemyPokemonBiosSetParam1bantakaiFlag(u8 *p,u8 n){if(p)p[18]=n;}
static void c3r_src_fightOutPokemonEnemyBiosSetTargetFightOutPokemonPtr(C3OutEnemy *p,u32 n){if(p)put32(p->bytes,n);}
static void c3r_src_fightOutPokemonEnemyBiosSetOumuWazaDataId(C3OutEnemy *p,u16 n){if(p)put16(p->bytes+4,n);}
static void c3r_src_fightOutPokemonEnemyBiosSetInitHp(C3OutEnemy *p,u16 n){if(p)put16(p->bytes+6,n);}
static void c3r_src_fightOutPokemonEnemyBiosSetDamage(C3OutEnemy *p,u16 n){if(p)put16(p->bytes+8,n);}
#include "battle_runtime_trainer_original.inc"

int c3_battle_runtime_trainer_init(u8 backing[C3_BATTLE_RUNTIME_TRAINER_BYTES])
{
    if(!backing)return 1;
    if(atomic_flag_test_and_set_explicit(&in_use,memory_order_acquire))return 0;
    u8 staged[C3_BATTLE_RUNTIME_TRAINER_BYTES];memcpy(staged,backing,sizeof(staged));boundary_error=0;
    c3r_src_fightTrainerInit(staged,1);
    int ok=!boundary_error;if(ok)memcpy(backing,staged,sizeof(staged));
    boundary_error=0;atomic_flag_clear_explicit(&in_use,memory_order_release);return ok;
}
int c3_battle_runtime_trainer_create(u8 backing[C3_BATTLE_RUNTIME_TRAINER_BYTES],
    const u8 hero[C3_BATTLE_TRAINER_BYTES],u16 trainer,u32 controller,u32 sequence)
{
    if(!backing||!hero)return 1;
    uintptr_t x=(uintptr_t)backing,y=(uintptr_t)hero;
    if(x>=y?x-y<C3_BATTLE_TRAINER_BYTES:y-x<C3_BATTLE_RUNTIME_TRAINER_BYTES)return 0;
    if(atomic_flag_test_and_set_explicit(&in_use,memory_order_acquire))return 0;
    u8 staged[C3_BATTLE_RUNTIME_TRAINER_BYTES];memcpy(staged,backing,sizeof(staged));boundary_error=0;
    c3r_src_fightTrainerCreate(staged,(void*)hero,trainer,controller,sequence);
    int ok=!boundary_error;if(ok)memcpy(backing,staged,sizeof(staged));
    boundary_error=0;atomic_flag_clear_explicit(&in_use,memory_order_release);return ok;
}
int c3_battle_fight_out_pokemon_init(u8 record[C3_BATTLE_FIGHT_OUT_POKEMON_RECORD])
{
    if(!record)return 1;
    if(atomic_flag_test_and_set_explicit(&in_use,memory_order_acquire))return 0;
    u8 staged[C3_BATTLE_FIGHT_OUT_POKEMON_RECORD];memcpy(staged,record,sizeof(staged));boundary_error=0;
    c3r_src_fightOutPokemonInit(staged);
    int ok=!boundary_error;if(ok)memcpy(record,staged,sizeof(staged));
    boundary_error=0;atomic_flag_clear_explicit(&in_use,memory_order_release);return ok;
}
