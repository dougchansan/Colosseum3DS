#include "colosseum3ds/battle_general.h"
#include "colosseum3ds/shadow_moves.h"
#include "save_codec.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* C3B2 layout (tools/prepare_battle_encounters.py). */
#define HEADER 128u
#define ENTRY 672u
#define MOVE_ROW 56u
#define SPECIES_ROW 284u
#define TYPE_ROW 44u
#define GROWTH_ROW 0x194u
#define TRAINER_MON 80u
#define MAGIC UINT32_C(0x43334742)
#define NO_MON 255u
#define STRUGGLE 165u
#define SHADOW_ROWS 0x61u  /* common_rel 0x1439D8: Shadow data IDs 0..0x60 */
#define SHADOW_ROW 0x38u
#define SIDE0_ROW 580u     /* id, count, trainer 52, kind 12, 6 records, 2 names */
#define AI_ROW 40u         /* FightTrainerAiData */
#define NATURE_ROW 40u     /* PokemonSeikakuData */
#define SHADOW_RUSH 0x164u

static uint16_t be16(const uint8_t *p){return (uint16_t)((unsigned)p[0]<<8|p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static void put16(uint8_t *p,unsigned v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static int fail(char *e,const char *m){if(e)snprintf(e,C3_GB_ERROR_SIZE,"%s",m);return 0;}

/* ---- pack ---------------------------------------------------------------- */

static const uint8_t *entry(const C3BattlePack *p,uint32_t id)
{return p&&p->bytes&&id<p->encounters?p->bytes+p->entries_at+id*ENTRY:NULL;}
static const uint8_t *move_row(const C3BattlePack *p,uint16_t id)
{return id&&id<p->moves?p->bytes+HEADER+id*MOVE_ROW:NULL;}
static const uint8_t *species_row(const C3BattlePack *p,uint16_t id)
{return id&&id<p->species?p->bytes+p->species_at+id*SPECIES_ROW:NULL;}
static void copy_name(char out[C3_GB_NAME],const uint8_t *in)
{memcpy(out,in,C3_GB_NAME);out[C3_GB_NAME-1]=0;if(!out[0])snprintf(out,C3_GB_NAME,"?");}
/* The side-0 CPU trainer row of an encounter (pack v3), or NULL. */
static const uint8_t *side0_row(const C3BattlePack *p,uint32_t id)
{
    for(uint32_t i=0;p&&p->bytes&&i<p->side0_count;++i){const uint8_t *r=p->bytes+p->side0_at+i*SIDE0_ROW;
        if(be16(r)==id)return r;}
    return NULL;
}

int c3_battle_pack_decode(C3BattlePack *out,const void *bytes,size_t size,char *e)
{
    const uint8_t *p=bytes;uint8_t digest[20];
    if(!out||!p||size<HEADER||size>8u*1024u*1024u)return fail(e,"Invalid story-battle pack size.");
    C3BattlePack k={0};
    k.encounters=be32(p+12);k.moves=be32(p+16);k.species=be32(p+20);k.types=be32(p+24);k.growth=be32(p+28);
    k.species_at=be32(p+112);k.types_at=be32(p+116);k.growth_at=be32(p+120);k.entries_at=be32(p+124);
    unsigned version=size>=8?be32(p+4):0;
    if(memcmp(p,"C3B2",4)||version<1||version>3||be32(p+8)!=size||!k.encounters||k.encounters>4096||
       k.moves<STRUGGLE+1||k.moves>1024||k.species<2||k.species>1024||k.types!=18||k.growth!=8)
        return fail(e,"Unsupported story-battle pack header.");
    k.names_at=k.entries_at+k.encounters*ENTRY;
    k.shadow_at=version>=2?k.names_at+(k.species+k.moves+2u*k.encounters)*C3_GB_NAME:0;
    if(version==3){uint64_t at=(uint64_t)k.shadow_at+SHADOW_ROWS*SHADOW_ROW;
        if(at+4u>size)return fail(e,"Story-battle pack sections do not match its size.");
        k.side0_at=(uint32_t)at+4u;k.side0_count=be32(p+at);
        if(k.side0_count>k.encounters)return fail(e,"Story-battle pack sections do not match its size.");
        at=(uint64_t)k.side0_at+(uint64_t)k.side0_count*SIDE0_ROW;
        if(at+4u>size)return fail(e,"Story-battle pack sections do not match its size.");
        k.ai_count=be32(p+at);k.ai_at=(uint32_t)at+4u;at+=4u+(uint64_t)k.ai_count*AI_ROW;
        if(k.ai_count>4096||at+4u>size)return fail(e,"Story-battle pack sections do not match its size.");
        k.ai_value_count=be32(p+at);k.ai_value_at=(uint32_t)at+4u;
        if(k.ai_value_count>4096)return fail(e,"Story-battle pack sections do not match its size.");
        at=(uint64_t)k.ai_value_at+4u*(uint64_t)k.ai_value_count;
        if(at+4u>size)return fail(e,"Story-battle pack sections do not match its size.");
        k.nature_count=be32(p+at);k.nature_at=(uint32_t)at+4u;at+=4u+(uint64_t)k.nature_count*NATURE_ROW;
        if(k.nature_count>64||at+4u>size)return fail(e,"Story-battle pack sections do not match its size.");
        k.dp_filter_count=be32(p+at);k.dp_filter_at=(uint32_t)at+4u;
        if(k.dp_filter_count>64)return fail(e,"Story-battle pack sections do not match its size.");}
    if(k.species_at!=HEADER+k.moves*MOVE_ROW||k.types_at!=k.species_at+k.species*SPECIES_ROW||
       k.growth_at!=k.types_at+k.types*TYPE_ROW||k.entries_at!=k.growth_at+k.growth*GROWTH_ROW||
       (uint64_t)k.names_at+(uint64_t)(k.species+k.moves+2u*k.encounters)*C3_GB_NAME+
       (version>=2?SHADOW_ROWS*SHADOW_ROW:0u)+(version==3?20u+(uint64_t)k.side0_count*SIDE0_ROW+(uint64_t)k.ai_count*AI_ROW+4u*(uint64_t)k.ai_value_count+
        (uint64_t)k.nature_count*NATURE_ROW+k.dp_filter_count+2u:0u)!=size)
        return fail(e,"Story-battle pack sections do not match its size.");
    c3_save_codec_sha1(digest,p+HEADER,size-HEADER);
    if(memcmp(digest,p+72,20))return fail(e,"Story-battle pack digest mismatch.");
    for(uint32_t i=0;i<k.encounters;++i){const uint8_t *r=p+k.entries_at+i*ENTRY;
        if(be16(r)!=i||r[2]>C3_GB_PARTY||be16(r+4)>5)return fail(e,"Damaged story-battle encounter index.");}
    for(uint32_t i=0;i<k.side0_count;++i){const uint8_t *r=p+k.side0_at+i*SIDE0_ROW;
        if(be16(r)>=k.encounters||!r[2]||r[2]>C3_GB_PARTY||be16(p+k.entries_at+be16(r)*ENTRY+4))
            return fail(e,"Damaged story-battle side-0 trainer.");}
    k.bytes=malloc(size);if(!k.bytes)return fail(e,"Story-battle pack allocation failed.");
    memcpy(k.bytes,p,size);k.size=size;*out=k;if(e)e[0]=0;return 1;
}
int c3_battle_pack_load(C3BattlePack *out,const char *path,char *e)
{
    if(!out||!path)return fail(e,"Missing story-battle pack path.");
    FILE *f=fopen(path,"rb");if(!f)return fail(e,"Prepare battles.c3x to enable story battles.");
    long n=-1;if(!fseek(f,0,SEEK_END))n=ftell(f);
    if(n<(long)HEADER||n>8L*1024L*1024L||fseek(f,0,SEEK_SET)){fclose(f);return fail(e,"Invalid story-battle pack file.");}
    uint8_t *b=malloc((size_t)n);
    if(!b){fclose(f);return fail(e,"Story-battle pack allocation failed.");}
    size_t got=fread(b,1,(size_t)n,f);int bad=ferror(f);fclose(f);
    int ok=!bad&&got==(size_t)n&&c3_battle_pack_decode(out,b,(size_t)n,e);
    if(!ok&&(bad||got!=(size_t)n))fail(e,"Truncated story-battle pack.");
    free(b);return ok;
}
void c3_battle_pack_free(C3BattlePack *p){if(p){free(p->bytes);memset(p,0,sizeof *p);}}
const char *c3_battle_pack_reason(unsigned r)
{
    static const char *const text[]={"admitted","player side is not the hero alone","more than one opponent trainer",
        "opponent has no Pokemon","unsupported battle type","encounter index out of range"};
    return r<6?text[r]:"unknown";
}
int c3_battle_pack_move_name(const C3BattlePack *p,uint16_t move,char name[C3_GB_NAME])
{
    if(!p||!p->bytes||!name||move>=p->moves)return 0;
    copy_name(name,p->bytes+p->names_at+(p->species+move)*C3_GB_NAME);return 1;
}
int c3_battle_pack_move(const C3BattlePack *p,uint16_t id,C3GeneralMoveInfo *out)
{
    const uint8_t *m=p&&p->bytes&&out?move_row(p,id):NULL;
    if(!m)return 0;
    out->priority=(int8_t)m[0];out->pp=m[1];out->type=m[2];out->range=m[3];out->accuracy=m[4];
    out->power=(int16_t)be16(m+22);out->effect=be16(m+26);return 1;
}
uint16_t c3_battle_pack_species_voice(const C3BattlePack *p,uint16_t species)
{
    const uint8_t *s=p&&p->bytes?species_row(p,species):NULL;
    return s?(uint16_t)be16(s+14):0;
}
int c3_battle_pack_shadow(const C3BattlePack *p,unsigned id,C3ShadowInfo *out)
{
    if(!p||!p->bytes||!p->shadow_at||!out||!id||id>=SHADOW_ROWS)return 0;
    const uint8_t *r=p->bytes+p->shadow_at+id*SHADOW_ROW;
    out->catch_rate=r[0];out->species=be16(r+2);out->gauge=be16(r+8);out->snag_flag=be16(r+0x12);
    return out->species!=0;
}
int c3_battle_pack_info(const C3BattlePack *p,uint32_t id,C3BattleEncounterInfo *out)
{
    const uint8_t *r=entry(p,id);
    if(!r||!out)return 0;
    C3BattleEncounterInfo i={0};
    const uint8_t *enc=r+8,*floor=r+64,*type=r+120,*trainer=r+128,*kind=r+180;
    i.id=(uint16_t)id;i.reason=be16(r+4);i.kind=enc[0];i.wipeout=enc[2];i.floor_data_id=be16(enc+4);
    i.battle_floor=be16(floor+2);i.active_per_side=type[2];i.trainer_id=be16(enc+32);
    i.trainer_kind=be16(trainer+2);i.prize_base=be16(kind);i.party_count=r[2];
    i.bgm=be32(enc+12);if(!i.bgm)i.bgm=be32(floor+12);if(!i.bgm)i.bgm=be32(kind+8);if(!i.bgm)i.bgm=1;
    i.intro_wzx=be32(enc+20);if(!i.intro_wzx)i.intro_wzx=be32(floor+8);
    i.environment_sound=be32(floor+16);
    {const uint8_t *k=r+88;i.back_save=k[0];i.use_item=k[4];i.can_run=k[6];i.get_exp=k[8];i.get_money=k[9];
     i.money_pool=k[10];i.critical=k[14];i.partner_aura=k[15];i.hyper_mode=k[22];}
    for(unsigned k=0;k<i.party_count;++k){const uint8_t *m=r+192+k*TRAINER_MON;
        i.species[k]=be16(m+10);i.levels[k]=m[4];i.shadow_count=(uint8_t)(i.shadow_count+(m[3]!=0));}
    const uint8_t *names=p->bytes+p->names_at+(p->species+p->moves+2u*id)*C3_GB_NAME;
    copy_name(i.trainer,names);copy_name(i.title,names+C3_GB_NAME);
    {const uint8_t *s0=side0_row(p,id);
     /* An older pack has no side-0 roster: the encounter stays refused. */
     if(be16(enc+24)!=1||be32(enc+28)!=1){if(!s0&&!i.reason)i.reason=1;}
     if(s0&&!i.reason){i.cpu_side0=1;i.side0_party_count=s0[2];
        copy_name(i.side0_trainer,s0+548);copy_name(i.side0_title,s0+564);}}
    *out=i;return 1;
}

/* ---- Pokemon ------------------------------------------------------------- */

/* Gen-3 nature table: n/5 raises and n%5 lowers Atk, Def, Spe, SpA, SpD. */
static const uint8_t nature_stat[5]={1,2,5,3,4};
static unsigned compute_stat(unsigned stat,unsigned base,unsigned iv,unsigned ev,unsigned level,unsigned nature)
{
    unsigned core=(2u*base+iv+ev/4u)*level/100u;
    if(stat==0)return base==1?1:core+level+10u;
    unsigned v=core+5u;
    if(nature<25&&nature/5!=nature%5){
        if(nature_stat[nature/5]==stat)v=v*110u/100u;
        if(nature_stat[nature%5]==stat)v=v*90u/100u;
    }
    return v;
}
static unsigned move_pp(const C3BattlePack *p,uint16_t id,unsigned ups)
{const uint8_t *m=move_row(p,id);return m?m[1]*(5u+(ups>3?3:ups))/5u:0;}
static void species_fields(const C3BattlePack *p,C3GeneralMon *m)
{
    const uint8_t *s=species_row(p,m->species);
    m->types[0]=s[0x30];m->types[1]=s[0x31];m->growth=s[0];m->base_exp=be16(s+6);
    copy_name(m->name,p->bytes+p->names_at+m->species*C3_GB_NAME);
}
/* pokemonGetTokuseiDataId: PokemonData tokuseiDataId[2] (+0x32); the flag
 * picks the second only when the species has one (pokemonSetTokuseiFlag). */
static uint8_t species_ability(const C3BattlePack *p,uint16_t species,unsigned flag)
{const uint8_t *s=species_row(p,species);return s?s[0x32+(flag&&s[0x33]?1:0)]:0;}
/* The moves just read are the Pokemon's own (pokemonBiosGetPokemonWazaPtr
 * mode 0); c3_general_shadow_moves then decides what each slot shows. */
static void own_moves_from_shown(C3GeneralMon *m)
{
    memcpy(m->own_moves,m->moves,sizeof m->own_moves);memcpy(m->own_pp,m->pp,sizeof m->own_pp);
    memcpy(m->own_pp_max,m->pp_max,sizeof m->own_pp_max);m->hidden=0;
}
/* A slot's own PP as the record should keep it (the live PP while shown). */
static uint8_t own_pp(const C3GeneralMon *m,unsigned j){return m->hidden>>j&1u?m->own_pp[j]:m->pp[j];}
void c3_general_shadow_moves(C3GeneralMon *m)
{
    if(!m)return;
    int shadow=m->shadow!=0;
    unsigned level=c3_shadow_level(shadow,m->dp,m->dp_max>0?(uint32_t)(m->dp_max/100):0u);
    for(unsigned i=0;i<4;++i){
        uint8_t bit=(uint8_t)(1u<<i);
        if(!(m->hidden&bit))m->own_pp[i]=m->pp[i];
        uint16_t shown=c3_shadow_shown_move(m->own_moves[i],i,shadow,m->trainer,level);
        m->moves[i]=shown;
        if(shown!=m->own_moves[i]){m->pp[i]=m->pp_max[i]=C3_SHADOW_STAND_IN_PP;m->hidden|=bit;}
        else{m->pp[i]=m->own_pp[i];m->pp_max[i]=m->own_pp_max[i];m->hidden&=(uint8_t)~bit;}
    }
}
static int mon_from_trainer(const C3BattlePack *p,C3GeneralMon *m,const uint8_t *r)
{
    memset(m,0,sizeof *m);
    m->species=be16(r+10);m->level=r[4];m->ball=4; /* trainer records carry no ball: Poke Ball */
    if(!species_row(p,m->species)||!m->level||m->level>100)return 0;
    species_fields(p,m);
    m->nature=r[2]<25?r[2]:0;m->shadow=r[3];m->friendship=be16(r+8);m->held_item=(uint16_t)be32(r+0x10);
    /* fightTrainerPokemonData +0 TokuseiFlag (s8; below 0 pokemonCreate's
     * own choice, not modelled: the first ability), +5 KeyPlayerFlag, +6
     * PartDataId. */
    m->ability=species_ability(p,m->species,(int8_t)r[0]>0?(unsigned)r[0]:0u);m->key_player=r[5];m->part=r[6];
    const uint8_t *s=species_row(p,m->species);
    for(unsigned j=0;j<6;++j){int8_t iv=(int8_t)r[0x1c+j];int16_t ev=(int16_t)be16(r+0x22+2*j);
        m->ivs[j]=(uint8_t)(iv<0?0:iv>31?31:iv);m->evs[j]=(uint16_t)(ev<0?0:ev>255?255:ev);
        m->stats[j]=(uint16_t)compute_stat(j,be16(s+0x84+2*j),m->ivs[j],m->evs[j],m->level,m->nature);}
    m->max_hp=m->hp=m->stats[0];
    unsigned count=0;
    for(unsigned j=0;j<4;++j){uint32_t id=be32(r+0x34+8*j);
        if(id&&id<p->moves){m->moves[count]=(uint16_t)id;m->pp[count]=m->pp_max[count]=(uint8_t)move_pp(p,(uint16_t)id,r[0x30+8*j]);++count;}}
    if(!count){
        /* No authored moves: the last four level-up moves at or below the
         * level (species row +0xBA, 20 x {level, pad, move}). */
        for(unsigned j=0;j<20;++j){const uint8_t *l=s+0xba+4*j;uint16_t id=be16(l+2);
            if(!l[0]||l[0]>m->level||!id||id>=p->moves)continue;
            unsigned dup=0;for(unsigned k=0;k<4;++k)dup|=m->moves[k]==id;
            if(dup)continue;
            if(count==4){memmove(m->moves,m->moves+1,3*sizeof m->moves[0]);count=3;}
            m->moves[count++]=id;
        }
        for(unsigned k=0;k<count;++k)m->pp[k]=m->pp_max[k]=(uint8_t)move_pp(p,m->moves[k],0);
    }
    /* fightTrainerPokemonDataId (status 0xC9) is the trainer row's id. */
    m->trainer=1;own_moves_from_shown(m);c3_general_shadow_moves(m);
    return 1;
}
/* PokemonBios condition record (+0x64): its first halfword is the
 * persistent condition, as the bag's cure masks read it (inventory.c):
 * 3 poison, 4 bad poison, 5 paralysis, 6 burn, 7 freeze, 8 sleep. */
static uint8_t status_from_record(unsigned id)
{
    switch(id){case 3:return C3_GB_POISON;case 4:return C3_GB_TOXIC;case 5:return C3_GB_PARALYSIS;
    case 6:return C3_GB_BURN;case 7:return C3_GB_FREEZE;case 8:return C3_GB_SLEEP;default:return C3_GB_NONE;}
}
static unsigned status_to_record(unsigned status)
{
    static const uint8_t id[]={0,8,3,6,7,5,4}; /* C3_GB_NONE..C3_GB_TOXIC */
    return status<sizeof id?id[status]:0;
}
static int mon_from_party(const C3BattlePack *p,C3GeneralMon *m,const uint8_t *r,unsigned slot)
{
    memset(m,0,sizeof *m);
    m->species=be16(r);m->level=r[0x60];m->ball=r[0x0F]>=1&&r[0x0F]<=12?r[0x0F]:4; /* PokemonBios catchBallId */
    if(!species_row(p,m->species)||!m->level||m->level>100||r[0xcb])return 0;
    species_fields(p,m);
    m->party_slot=(uint8_t)slot;m->exp=be32(r+0x5c);m->held_item=be16(r+0x88);
    /* pokemonBiosGetDarkFlag (status 0xC2): a Shadow data ID and a gauge
     * that is not negative (purification writes -100). */
    m->hp=be16(r+0x8a);m->friendship=be16(r+0xb0);m->shadow=(uint8_t)c3_shadow_record_is_shadow(r);m->nature=(uint8_t)(be32(r+4)%25u);
    m->ability=species_ability(p,m->species,r[0xcc]); /* PokemonBios tokuseiFlag */
    for(unsigned j=0;j<6;++j){m->stats[j]=be16(r+0x8c+2*j);m->ivs[j]=(uint8_t)be16(r+0xa4+2*j);m->evs[j]=be16(r+0x98+2*j);}
    m->max_hp=m->stats[0];
    if(!m->max_hp||m->hp>m->max_hp)return 0;
    m->status=status_from_record(be16(r+0x64));
    if(m->shadow){C3ShadowInfo sh;m->dp=(int32_t)be32(r+0xdc);
        m->dp_max=c3_battle_pack_shadow(p,be16(r+0xd8),&sh)?(int32_t)sh.gauge*100:0;}
    if(m->status==C3_GB_SLEEP)m->status_turns=2; /* the record's sleep counter is not identified */
    for(unsigned j=0;j<4;++j){uint16_t id=be16(r+0x78+4*j);
        if(id&&id<p->moves){m->moves[j]=id;m->pp[j]=r[0x7a+4*j];m->pp_max[j]=(uint8_t)move_pp(p,id,r[0x7b+4*j]);
            if(m->pp[j]>m->pp_max[j])m->pp[j]=m->pp_max[j];}}
    m->trainer=be16(r+0xd6)!=0;own_moves_from_shown(m);c3_general_shadow_moves(m);
    return 1;
}

/* ---- events -------------------------------------------------------------- */

static C3GeneralMon *at(C3GeneralBattle *b,unsigned pos)
{unsigned s=pos/2,i=pos%2;return pos<4&&i<b->active_count&&b->active[s][i]!=NO_MON?&b->party[s][b->active[s][i]]:NULL;}
static const C3GeneralMon *cat(const C3GeneralBattle *b,unsigned pos){return at((C3GeneralBattle *)b,pos);}
static int alive(C3GeneralBattle *b,unsigned pos){C3GeneralMon *m=at(b,pos);return m&&m->hp;}
static int say(C3GeneralBattle *b,C3GeneralEventKind kind,unsigned actor,unsigned target,uint16_t move,const char *f,...)
{
    if(b->event_count>=C3_GB_EVENT_LIMIT)return 1; /* the log saturates; the battle continues */
    C3GeneralEvent *e=&b->events[b->event_count++];
    memset(e,0,sizeof *e);e->kind=(uint8_t)kind;e->actor=(uint8_t)actor;e->target=(uint8_t)target;e->move=move;
    for(unsigned i=0;i<4;++i){const C3GeneralMon *m=cat(b,i);e->hp[i]=m?m->hp:0;e->max_hp[i]=m?m->max_hp:0;e->mon[i]=m?b->active[i/2][i%2]:NO_MON;}
    {const C3GeneralMon *m=actor<4?cat(b,actor):NULL;e->species=m?m->species:0;e->ball=m?m->ball:0;}
    e->stat=-1;
    va_list ap;va_start(ap,f);vsnprintf(e->text,sizeof e->text,f,ap);va_end(ap);
    return 1;
}
static const char *who(C3GeneralBattle *b,unsigned pos)
{
    static char text[4][40];C3GeneralMon *m=at(b,pos);
    if(!m)return "?";
    snprintf(text[pos],sizeof text[pos],"%s%s",pos/2!=b->viewer?"Foe ":"",m->name);return text[pos];
}
static const char *side_label(const C3GeneralBattle *b,unsigned s)
{return b->link?b->side_name[s]:b->info.cpu_side0&&!s?b->info.side0_trainer:b->info.trainer;}
static void announce_send(C3GeneralBattle *b,unsigned s,unsigned a,const C3GeneralMon *m)
{
    if(s==b->viewer&&!b->info.cpu_side0)say(b,C3_GB_EVENT_SWITCH,s*2+a,255,0,"Go, %s!",m->name);
    else say(b,C3_GB_EVENT_SWITCH,s*2+a,255,0,"%s sent out %s!",side_label(b,s),m->name);
}
static int roll(C3GeneralBattle *b,unsigned n,unsigned *out)
{uint16_t v;if(!n||!c3_battle_rng_next16(&b->rng,&v))return 0;*out=v%n;return 1;}

/* ---- begin --------------------------------------------------------------- */

static void mark_faced(C3GeneralBattle *b)
{
    for(unsigned f=0;f<b->active_count;++f){uint8_t foe=b->active[1][f];
        if(foe==NO_MON||!b->party[1][foe].hp)continue;
        for(unsigned h=0;h<b->active_count;++h){uint8_t hero=b->active[0][h];
            if(hero!=NO_MON&&b->party[0][hero].hp)b->party[1][foe].participated|=(uint8_t)(1u<<hero);}}
}
static int next_healthy(const C3GeneralBattle *b,unsigned side,unsigned slot)
{
    for(unsigned i=0;i<b->party_count[side];++i){
        if(b->owner_split&&b->owner[side][i]!=slot)continue;
        int used=0;for(unsigned a=0;a<b->active_count;++a)used|=b->active[side][a]==i;
        if(!used&&b->party[side][i].hp)return (int)i;
    }
    return -1;
}
static void sent_out(C3GeneralBattle *b,const C3BattlePack *p,unsigned side,unsigned index);
int c3_general_battle_begin(C3GeneralBattle *out,const C3BattlePack *p,uint32_t id,
    const void *party_bytes,uint32_t seed,char *e)
{
    C3BattleEncounterInfo info;const uint8_t *party=party_bytes;
    if(!out||!party||!c3_battle_pack_info(p,id,&info))return fail(e,"Unknown story-battle encounter.");
    if(info.reason)return fail(e,c3_battle_pack_reason(info.reason));
    C3GeneralBattle *b=calloc(1,sizeof *b);if(!b)return fail(e,"Battle allocation failed.");
    b->magic=MAGIC;b->encounter=id;b->info=info;b->outcome=C3_GB_RUNNING;
    b->active_count=info.active_per_side==1?1:2;
    c3_battle_rng_init(&b->rng,seed);
    if(info.cpu_side0){
        /* fightTrainerCreate for a CPU trainer in slot 0: its roster, not the hero's party. */
        const uint8_t *s0=side0_row(p,id);
        for(unsigned i=0;i<s0[2];++i){
            if(!mon_from_trainer(p,&b->party[0][b->party_count[0]],s0+68+i*TRAINER_MON)){free(b);return fail(e,"Damaged side-0 Pokemon record.");}
            b->player_order[b->party_count[0]]=(uint8_t)i;++b->party_count[0];
        }
    }else for(unsigned i=0;i<C3_GB_PARTY;++i){
        if(!be16(party+i*C3_GB_POKEMON_SIZE))continue;
        if(!mon_from_party(p,&b->party[0][b->party_count[0]],party+i*C3_GB_POKEMON_SIZE,i))continue;
        b->player_order[b->party_count[0]++]=(uint8_t)i;
    }
    const uint8_t *r=entry(p,id);
    for(unsigned i=0;i<info.party_count;++i)
        if(!mon_from_trainer(p,&b->party[1][b->party_count[1]],r+192+i*TRAINER_MON)){free(b);return fail(e,"Damaged opponent Pokemon record.");}
        else ++b->party_count[1];
    memset(b->active,NO_MON,sizeof b->active);
    for(unsigned s=0;s<2;++s)for(unsigned a=0;a<b->active_count;++a){int n=next_healthy(b,s,a);if(n>=0)b->active[s][a]=(uint8_t)n;}
    if(b->active[0][0]==NO_MON){free(b);return fail(e,"The hero has no Pokemon able to battle.");}
    if(b->active[1][0]==NO_MON){free(b);return fail(e,"The opponent has no Pokemon.");}
    mark_faced(b);b->okane_bai=1;
    for(unsigned a=0;a<b->active_count;++a)if(b->active[0][a]!=NO_MON)sent_out(b,p,0,b->active[0][a]);
    if(!c3_battle_rng_next16(&b->rng,&b->quick_claw_roll)){free(b);return fail(e,"Battle random-number failure.");}
    if(info.cpu_side0)say(b,C3_GB_EVENT_MESSAGE,255,255,0,"%s %s and %s %s are battling!",
        info.side0_title,info.side0_trainer,info.title,info.trainer);
    else say(b,C3_GB_EVENT_MESSAGE,255,255,0,"%s %s would like to battle!",info.title,info.trainer);
    for(unsigned a=0;a<b->active_count;++a)if(b->active[1][a]!=NO_MON)announce_send(b,1,a,&b->party[1][b->active[1][a]]);
    for(unsigned a=0;a<b->active_count;++a)if(b->active[0][a]!=NO_MON)announce_send(b,0,a,&b->party[0][b->active[0][a]]);
    *out=*b;free(b);if(e)e[0]=0;return 1;
}

int c3_general_mon_from_record(const C3BattlePack *p,const uint8_t r[C3_GB_POKEMON_SIZE],unsigned slot,C3GeneralMon *m)
{return p&&r&&m&&be16(r)&&mon_from_party(p,m,r,slot);}
int c3_general_mon_from_trainer(const C3BattlePack *p,uint32_t id,unsigned index,C3GeneralMon *m)
{
    C3BattleEncounterInfo info;
    if(!p||!m||!c3_battle_pack_info(p,id,&info)||index>=info.party_count)return 0;
    return mon_from_trainer(p,m,entry(p,id)+192+index*TRAINER_MON);
}
int c3_general_battle_begin_link(C3GeneralBattle *out,const C3BattleEncounterInfo *stage,unsigned per_side,
    const C3GeneralMon mons[2][C3_GB_PARTY],const uint8_t counts[2],const uint8_t owner[2][C3_GB_PARTY],
    const char names[2][C3_GB_NAME],uint32_t seed,unsigned viewer,char *e)
{
    if(!out||!stage||!mons||!counts||!names||viewer>1)return fail(e,"Invalid link battle.");
    for(unsigned s=0;s<2;++s)if(!counts[s]||counts[s]>C3_GB_PARTY)return fail(e,"Each side needs one to six Pokemon.");
    C3GeneralBattle *b=calloc(1,sizeof *b);if(!b)return fail(e,"Battle allocation failed.");
    b->magic=MAGIC;b->encounter=stage->id;b->info=*stage;b->outcome=C3_GB_RUNNING;
    b->active_count=per_side==1?1:2;b->link=1;b->viewer=(uint8_t)viewer;b->owner_split=owner!=NULL;
    c3_battle_rng_init(&b->rng,seed);
    for(unsigned s=0;s<2;++s){
        memcpy(b->side_name[s],names[s],C3_GB_NAME);b->side_name[s][C3_GB_NAME-1]=0;
        b->party_count[s]=counts[s];
        for(unsigned i=0;i<counts[s];++i){b->party[s][i]=mons[s][i];b->party[s][i].participated=0;
            if(owner)b->owner[s][i]=owner[s][i];
            if(!b->party[s][i].species||!b->party[s][i].max_hp){free(b);return fail(e,"Damaged link Pokemon record.");}}
    }
    for(unsigned i=0;i<C3_GB_PARTY;++i)b->player_order[i]=(uint8_t)i;
    /* The stage encounter lends its floor, music and intro only. */
    b->info.party_count=counts[1];b->info.active_per_side=b->active_count;b->info.wipeout=0;b->info.prize_base=0;
    memcpy(b->info.trainer,names[1-viewer],C3_GB_NAME);b->info.trainer[C3_GB_NAME-1]=0;b->info.title[0]=0;
    for(unsigned i=0;i<C3_GB_PARTY;++i){b->info.species[i]=i<counts[1]?b->party[1][i].species:0;b->info.levels[i]=i<counts[1]?b->party[1][i].level:0;}
    memset(b->active,NO_MON,sizeof b->active);
    for(unsigned s=0;s<2;++s)for(unsigned a=0;a<b->active_count;++a){int n=next_healthy(b,s,a);if(n>=0)b->active[s][a]=(uint8_t)n;}
    if(b->active[0][0]==NO_MON||b->active[1][0]==NO_MON){free(b);return fail(e,"A side has no Pokemon able to battle.");}
    if(!c3_battle_rng_next16(&b->rng,&b->quick_claw_roll)){free(b);return fail(e,"Battle random-number failure.");}
    say(b,C3_GB_EVENT_MESSAGE,255,255,0,"%s would like to battle!",b->side_name[1-viewer]);
    unsigned foe=1-viewer;
    for(unsigned a=0;a<b->active_count;++a)if(b->active[foe][a]!=NO_MON)announce_send(b,foe,a,&b->party[foe][b->active[foe][a]]);
    for(unsigned a=0;a<b->active_count;++a)if(b->active[viewer][a]!=NO_MON)announce_send(b,viewer,a,&b->party[viewer][b->active[viewer][a]]);
    *out=*b;free(b);if(e)e[0]=0;return 1;
}

/* ---- rules --------------------------------------------------------------- */

/* Gen-3 effect IDs (move row +0x1A) modelled below. Others: a damaging move
 * becomes a plain hit, a status move announces that nothing happened. */
enum {
    E_HIT=0,E_SLEEP=1,E_POISON_HIT=2,E_ABSORB=3,E_BURN_HIT=4,E_FREEZE_HIT=5,E_PARALYZE_HIT=6,
    E_EXPLOSION=7,E_ALWAYS_HIT=17,E_HAZE=25,E_MULTI_HIT=29,E_FLINCH_HIT=31,E_RESTORE_HP=32,
    E_TOXIC=33,E_LIGHT_SCREEN=35,E_REST=37,E_OHKO=38,E_SUPER_FANG=40,E_DRAGON_RAGE=41,
    E_HIGH_CRITICAL=43,E_DOUBLE_HIT=44,E_RECOIL=48,E_CONFUSE=49,E_REFLECT=65,E_POISON=66,
    E_PARALYZE=67,E_CONFUSE_HIT=76,E_TWINEEDLE=77,E_RECHARGE=80,E_SPLASH=85,E_LEVEL_DAMAGE=87,
    E_FALSE_SWIPE=101,E_QUICK_ATTACK=103,E_PROTECT=111,E_ENDURE=116,E_RETURN=121,
    E_FRUSTRATION=123,E_MORNING_SUN=132,E_SYNTHESIS=133,E_MOONLIGHT=134,E_SONICBOOM=130,
    E_DEFENSE_UP_HIT=138,E_ATTACK_UP_HIT=139,E_ALL_STATS_UP_HIT=140,E_SOFTBOILED=157,
    E_WILL_O_WISP=167,E_FACADE=169,E_DOUBLE_EDGE=198,E_BLAZE_KICK=200,E_POISON_FANG=202,
    E_POISON_TAIL=209,E_BULK_UP=208,E_CALM_MIND=211,E_DRAGON_DANCE=212,E_COSMIC_POWER=206,
    E_TICKLE=205,E_OVERHEAT=204,E_SUPERPOWER=182
};
static int stat_effect(uint16_t e,int *stat,int *delta,int *self,int *hit)
{
    *hit=0;
    if(e>=10&&e<=16){*stat=e-10;*delta=1;*self=1;return 1;}
    if(e>=18&&e<=24){*stat=e-18;*delta=-1;*self=0;return 1;}
    if(e>=50&&e<=56){*stat=e-50;*delta=2;*self=1;return 1;}
    if(e>=58&&e<=64){*stat=e-58;*delta=-2;*self=0;return 1;}
    if(e>=68&&e<=74){*stat=e-68;*delta=-1;*self=0;*hit=1;return 1;}
    if(e==E_DEFENSE_UP_HIT){*stat=1;*delta=1;*self=1;*hit=1;return 1;}
    if(e==E_ATTACK_UP_HIT){*stat=0;*delta=1;*self=1;*hit=1;return 1;}
    return 0;
}
/* Effect order Atk Def Spe SpA SpD Acc Eva -> stage order Atk Def SpA SpD Spe Acc Eva. */
static const uint8_t effect_stage[7]={0,1,4,2,3,5,6};
int c3_general_effect_modelled(uint16_t e)
{
    int s,d,self,hit;
    if(stat_effect(e,&s,&d,&self,&hit))return 1;
    switch(e){
    case E_HIT:case E_SLEEP:case E_POISON_HIT:case E_ABSORB:case E_BURN_HIT:case E_FREEZE_HIT:
    case E_PARALYZE_HIT:case E_EXPLOSION:case E_ALWAYS_HIT:case E_HAZE:case E_MULTI_HIT:case E_FLINCH_HIT:
    case E_RESTORE_HP:case E_TOXIC:case E_LIGHT_SCREEN:case E_REST:case E_OHKO:case E_SUPER_FANG:
    case E_DRAGON_RAGE:case E_HIGH_CRITICAL:case E_DOUBLE_HIT:case E_RECOIL:case E_CONFUSE:case E_REFLECT:
    case E_POISON:case E_PARALYZE:case E_CONFUSE_HIT:case E_TWINEEDLE:case E_RECHARGE:case E_SPLASH:
    case E_LEVEL_DAMAGE:case E_FALSE_SWIPE:case E_QUICK_ATTACK:case E_PROTECT:case E_ENDURE:case E_RETURN:
    case E_FRUSTRATION:case E_MORNING_SUN:case E_SYNTHESIS:case E_MOONLIGHT:case E_SONICBOOM:
    case E_ALL_STATS_UP_HIT:case E_SOFTBOILED:case E_WILL_O_WISP:case E_FACADE:case E_DOUBLE_EDGE:
    case E_BLAZE_KICK:case E_POISON_FANG:case E_POISON_TAIL:case E_BULK_UP:case E_CALM_MIND:
    case E_DRAGON_DANCE:case E_COSMIC_POWER:case E_TICKLE:case E_OVERHEAT:case E_SUPERPOWER:return 1;
    default:return 0;
    }
}
static unsigned staged(unsigned v,int s)
{return s>=0?v*(2u+(unsigned)s)/2u:v*2u/(2u+(unsigned)-s);}
/* Held items read by speed (Gen-III item numbers; fightOutPokemon soubi
 * data 0x18 Macho Brace, 0x1A Quick Claw with buff 20). */
#define ITEM_MACHO_BRACE 181u
#define ITEM_QUICK_CLAW 183u
/* Item 189 (Amulet Coin): soubi data 0x20, the prize doubler (fn_8022D084). */
#define ITEM_AMULET_COIN 189u
#define QUICK_CLAW_BUFF 20u
/* fightOutPokemonGetNowNimbleness (fight_out_pokemon.c:455): Speed, the
 * stage multiplier, Macho Brace >>1, paralysis >>2, and Quick Claw wins
 * outright (0xFFFFFFFF) when the turn's shared roll is under buff*0xFFFF/100.
 * Swift Swim / Chlorophyll (no weather here) and the badge bonus (floor
 * 0x1D, unused by story fights) do not apply. */
static uint32_t speed_of(const C3GeneralBattle *b,const C3GeneralMon *m)
{
    uint32_t v=staged(m->stats[5],m->stages[4]);
    if(m->held_item==ITEM_MACHO_BRACE)v>>=1;
    if(m->status==C3_GB_PARALYSIS)v>>=2;
    if(m->held_item==ITEM_QUICK_CLAW&&(int32_t)b->quick_claw_roll<(int32_t)(QUICK_CLAW_BUFF*0xFFFFu)/100)v=0xFFFFFFFFu;
    return v;
}
static unsigned effectiveness(const C3BattlePack *p,unsigned type,const C3GeneralMon *d,unsigned *num,unsigned *den)
{
    *num=*den=1;
    if(type>=18)return 1;
    for(unsigned i=0;i<2;++i){
        if(i&&d->types[1]==d->types[0])break;
        if(d->types[i]>=18)continue;
        unsigned code=be16(p->bytes+p->types_at+type*TYPE_ROW+8+d->types[i]*2);
        if(code==0x43){*num=0;return 0;}
        if(code==0x41)*num*=2;
        if(code==0x42)*den*=2;
    }
    return 1;
}
static const char *const stat_names[7]={"Attack","Defense","Sp. Atk","Sp. Def","Speed","accuracy","evasiveness"};
static int change_stage(C3GeneralBattle *b,unsigned pos,unsigned stage,int delta)
{
    C3GeneralMon *m=at(b,pos);if(!m||!m->hp)return 1;
    int next=m->stages[stage]+delta;
    if(next>6||next<-6)return say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%s's %s won't go %s!",who(b,pos),stat_names[stage],delta>0?"higher":"lower");
    m->stages[stage]=(int8_t)next;
    unsigned before=b->event_count;
    if(!say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%s's %s %s%s!",who(b,pos),stat_names[stage],
            delta>1||delta<-1?"sharply ":"",delta>0?"rose":"fell"))return 0;
    if(b->event_count>before){b->events[before].stat=(int8_t)stage;b->events[before].stat_delta=(int8_t)delta;}
    return 1;
}
static int inflict(C3GeneralBattle *b,unsigned pos,unsigned status)
{
    static const char *const text[]={"","fell asleep","was poisoned","was burned","was frozen solid","is paralyzed","was badly poisoned"};
    C3GeneralMon *m=at(b,pos);unsigned r;
    if(!m||!m->hp||m->status)return 1;
    if((status==C3_GB_POISON||status==C3_GB_TOXIC)&&(m->types[0]==3||m->types[1]==3||m->types[0]==8||m->types[1]==8))return 1;
    if(status==C3_GB_BURN&&(m->types[0]==10||m->types[1]==10))return 1;
    if(status==C3_GB_FREEZE&&(m->types[0]==15||m->types[1]==15))return 1;
    m->status=(uint8_t)status;m->toxic_count=0;
    if(status==C3_GB_SLEEP){if(!roll(b,4,&r))return 0;m->status_turns=(uint8_t)(2+r);}
    unsigned before=b->event_count;
    if(!say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%s %s!",who(b,pos),text[status]))return 0;
    if(b->event_count>before)b->events[before].status=(uint8_t)status;
    return 1;
}
static int hurt(C3GeneralBattle *b,unsigned pos,unsigned amount,int endure_ok)
{
    C3GeneralMon *m=at(b,pos);
    if(!m||!m->hp)return 1;
    if(!amount)amount=1;
    if(amount>=m->hp&&endure_ok&&m->protect==2){amount=m->hp-1u;}
    m->hp=(uint16_t)(amount>=m->hp?0:m->hp-amount);
    if(!say(b,C3_GB_EVENT_HP,pos,pos,0,"%s took %u damage.",who(b,pos),amount))return 0;
    if(!m->hp){
        if(!say(b,C3_GB_EVENT_FAINT,pos,pos,0,"%s fainted!",who(b,pos)))return 0;
        m->status=0;m->confusion=0;memset(m->stages,0,sizeof m->stages);
    }
    return 1;
}
static void heal(C3GeneralBattle *b,unsigned pos,unsigned amount)
{
    C3GeneralMon *m=at(b,pos);if(!m||!m->hp)return;
    unsigned v=m->hp+amount;m->hp=(uint16_t)(v>m->max_hp?m->max_hp:v);
    say(b,C3_GB_EVENT_HP,pos,pos,0,"%s regained health.",who(b,pos));
}
static void check_outcome(C3GeneralBattle *b)
{
    int left[2]={0,0};
    for(unsigned s=0;s<2;++s)for(unsigned i=0;i<b->party_count[s];++i)left[s]|=b->party[s][i].hp!=0;
    b->outcome=left[0]?(left[1]?C3_GB_RUNNING:C3_GB_WON):left[1]?C3_GB_LOST:C3_GB_DRAW;
}
/* Experience owed for a fainted foe, shared among the hero's Pokemon that
 * faced it (fn_802232F4 shape: giveExp * level / 7, trainer bonus x1.5). */
static void award(const C3BattlePack *p,C3GeneralBattle *b,unsigned foe_index)
{
    C3GeneralMon *foe=&b->party[1][foe_index];
    unsigned count=0;
    if(!b->info.get_exp){foe->participated=0;return;} /* fightFloorGetStatus 0x24 */
    for(unsigned i=0;i<b->party_count[0];++i)count+=(foe->participated>>i&1u)&&b->party[0][i].hp;
    if(!count)return;
    uint32_t gain=(uint32_t)foe->base_exp*foe->level/7u*3u/2u/count;
    if(!gain)gain=1;
    const uint8_t *fs=species_row(p,foe->species);
    for(unsigned i=0;i<b->party_count[0];++i){C3GeneralMon *m=&b->party[0][i];
        if(!(foe->participated>>i&1u)||!m->hp)continue;
        /* Effort: the foe species' giveEffort (+0x90) to every non-Shadow
         * Pokemon that faced it, capped at 255 a stat and 510 in all. */
        if(fs&&!m->shadow){unsigned total=0;
            for(unsigned j=0;j<6;++j)total+=m->evs[j]+m->ev_gain[j];
            for(unsigned j=0;j<6;++j){unsigned y=be16(fs+0x90+2*j),have=m->evs[j]+m->ev_gain[j];
                if(have+y>255u)y=have<255u?255u-have:0;
                if(total+y>510u)y=total<510u?510u-total:0;
                m->ev_gain[j]=(uint16_t)(m->ev_gain[j]+y);total+=y;}}
        if(m->level>=100)continue;
        if(m->shadow&&m->dp_max>0&&c3_general_dark_level(m)<3)continue; /* fn_802232F4: discarded above 60% */
        m->exp+=gain;m->participated=1;
        say(b,C3_GB_EVENT_MESSAGE,255,255,0,"%s gained %lu EXP. Points!",m->name,(unsigned long)gain);
    }
    foe->participated=0;
}
static int damage(C3GeneralBattle *b,const C3BattlePack *p,unsigned a,unsigned d,uint16_t id,const uint8_t *mv,
                  unsigned power,uint32_t *out,int *critical,unsigned *num,unsigned *den)
{
    C3GeneralMon *x=at(b,a),*y=at(b,d);unsigned r;
    unsigned type=mv[2],special=type>=10;
    if(!effectiveness(p,type,y,num,den)){*out=0;return 1;}
    unsigned crit_rate=be16(mv+26)==E_HIGH_CRITICAL?8u:16u;
    /* doCriticalAttackFlag: colosseum and Mt. Battle kinds have none. */
    if(!b->link&&!b->info.critical)*critical=0;
    else{if(!roll(b,crit_rate,&r))return 0;*critical=r==0;
        if(!*critical&&x->hyper&&id==SHADOW_RUSH){if(!roll(b,100,&r))return 0;*critical=r<90;}}
    int as=x->stages[special?2:0],ds=y->stages[special?3:1];
    if(*critical){if(as<0)as=0;if(ds>0)ds=0;}
    unsigned atk=staged(x->stats[special?3:1],as),def=staged(y->stats[special?4:2],ds);
    if(!def)def=1;
    /* fn_80232110: staged attack * power * (2L/5+2) / staged defense / 50,
     * then burn /2 (physical), the screen (/3*2 with two Pokemon on the
     * defender's side in a double battle, else /2; not on a critical hit),
     * the both-foes halving (range 4, two defenders), at least 1, +2. */
    uint32_t v=atk*power*(2u*x->level/5u+2u)/def/50u;
    if(!special&&x->status==C3_GB_BURN)v/=2u;
    unsigned side=d/2,defenders=0;
    for(unsigned i=0;i<b->active_count;++i)defenders+=alive(b,side*2+i)!=0;
    int doubles=b->active_count==2;
    if(!*critical&&b->screens[side][special]&&a/2!=d/2)v=doubles&&defenders>=2?v/3u*2u:v/2u;
    if(doubles&&mv[3]==4&&defenders>=2)v/=2u;
    if(!v)v=1;
    v+=2u;
    if(*critical)v*=2u;
    if(type==x->types[0]||type==x->types[1])v=v*3u/2u;
    v=v**num/ *den;
    if(!roll(b,16,&r))return 0;
    v=v*(85u+r)/100u;
    if(!v)v=1;
    *out=v;return 1;
}
/* fn_8022B2CC on the move's range ID (+3): 0 and 1 one chosen Pokemon,
 * 3 a random foe, 4 both foes, 6 every other Pokemon, 2/5/7 the user or
 * its side. A chosen target that is gone falls to a random live foe
 * (fightFloorGetFightOutPokemonPtrRandom, one draw) when two remain, else
 * the one left. Returns -1 on an RNG failure. */
static int targets_of(C3GeneralBattle *b,unsigned a,unsigned range,unsigned chosen,uint8_t list[3])
{
    unsigned n=0,foe=1-a/2,foes[2],nf=0,r;
    for(unsigned i=0;i<b->active_count;++i)if(alive(b,foe*2+i))foes[nf++]=foe*2+i;
    if(range==4||range==6){
        for(unsigned i=0;i<nf;++i)list[n++]=(uint8_t)foes[i];
        if(range==6)for(unsigned i=0;i<b->active_count;++i){unsigned q=(a/2)*2+i;if(q!=a&&alive(b,q))list[n++]=(uint8_t)q;}
        return (int)n;
    }
    if(range!=3&&chosen<4&&alive(b,chosen)&&chosen!=a){list[0]=(uint8_t)chosen;return 1;}
    if(!nf)return 0;
    if(nf==1){list[0]=(uint8_t)foes[0];return 1;}
    if(!roll(b,nf,&r))return -1;
    list[0]=(uint8_t)foes[r];return 1;
}
/* ---- Shadow Pokemon: Hyper Mode ---------------------------------------------
 * Only the hero's Shadow Pokemon, only when the fight kind allows it
 * (FightKindData darkpokemonHypermodeFlag; fight_floor status 0x31), as
 * fightOutPokemonIsGcHeroFightOutPokemon gates it. The nature row
 * (PokemonSeikakuData) gives the chances by dark level:
 *  - entry (fn_8022EDEC mode 0): a draw % 100 for every move such a Pokemon
 *    uses while it is not asleep and not in Hyper Mode; under +0x18[level]
 *    and the move is Shadow Rush: Hyper Mode (msg 0x771B);
 *  - each action in Hyper Mode (mode 2): a draw % 256 == 0 ends it (0x771E);
 *  - a move other than Shadow Rush in Hyper Mode (fn_8022A504): one draw
 *    % 100 against the cumulative +0x0B (another move, 0x770C), +0x0C and
 *    +0x0D (it ignores the order, 0x770D/0x770E), +0x0E (a 40-power hit on
 *    itself, 0x770F), +0x0F (no action, 0x7710), +0x10 (item misuse: no held
 *    item is used here, 0x7712), +0x11 (it returns to the trainer, here no
 *    action); above them it obeys;
 *  - Shadow Rush in Hyper Mode is a critical hit when the normal roll fails
 *    and a draw % 100 < 90 (fight_seq.c);
 *  - the end of turn reminds (mode 1, 0x771C);
 *  - Call (fn_80212D6C): in Hyper Mode a draw % 100 < +0x1F[level] snaps it
 *    out (0x7707: also wakes it, heart gauge filter 2); a failure (0x7708)
 *    wakes a sleeper on a second draw % 100 under half that chance; out of
 *    Hyper Mode Call wakes a sleeper (0x7706) or does nothing (0x771D/0x7709).
 * Heart gauge (pokemonAddDpFormPokemonDpFilterId): filter 0 once per battle
 * when the Pokemon is sent out, filter 2 on a successful Call, each the
 * filter's DP (pack) x100 into +0xDC, never below 0. The nature's rate row
 * (kake/waru) is not located in the pinned data: the drop uses 1/1. Hyper
 * Mode itself is not written back to the record. */
static const uint8_t *nature_row(const C3BattlePack *p,const C3GeneralMon *m)
{return p->nature_count&&m->nature<p->nature_count?p->bytes+p->nature_at+m->nature*NATURE_ROW:NULL;}
unsigned c3_general_dark_level(const C3GeneralMon *m)
{
    if(!m||!m->shadow||m->dp_max<=0)return 7;
    int64_t dp=m->dp<0?0:m->dp,max=m->dp_max;
    if(dp*100>=max*100)return 0;
    if(dp*100>=max*80)return 1;
    if(dp*100>=max*60)return 2;
    if(dp*100>=max*40)return 3;
    if(dp*100>=max*20)return 4;
    if(dp>0)return 5;
    return 6;
}
static int hyper_capable(const C3GeneralBattle *b,const C3BattlePack *p,unsigned pos,const C3GeneralMon *m)
{return m&&!b->link&&b->info.hyper_mode&&pos/2==0&&!b->info.cpu_side0&&m->shadow&&m->dp_max>0&&nature_row(p,m);}
static void gauge_filter(const C3BattlePack *p,C3GeneralMon *m,unsigned filter)
{
    if(filter>=p->dp_filter_count)return;
    int32_t v=(int8_t)p->bytes[p->dp_filter_at+filter]*100;
    m->dp=m->dp+v<0?0:m->dp+v;
    c3_general_shadow_moves(m); /* a move the drop opens shows at once */
}
/* fn_8022E6F0: the send-out drop, once per battle (flag 0xD1). */
static void sent_out(C3GeneralBattle *b,const C3BattlePack *p,unsigned side,unsigned index)
{
    C3GeneralMon *m=&b->party[side][index];
    /* fn_8022D084 on every appearance: soubi 0x20 sets the trainer's OkaneBai to 2. */
    if(!side&&m->held_item==ITEM_AMULET_COIN)b->okane_bai=2;
    if(m->gauge_dropped||!hyper_capable(b,p,side*2,m))return;
    m->gauge_dropped=1;gauge_filter(p,m,0);
}
/* ---- an opposing Shadow Pokemon appears ------------------------------------
 * fn_8022E6F0 mode 0 (through _fightActionFlowKaisiPostSubFightOutPokemon-
 * DarkCheckAppear at the battle's start and the send-out opcode after every
 * later send-out) for a Pokemon that is not the hero's: once per battle (flag
 * 0xD1), when the fight kind's doHizukiAiFlag (floor status 0x2B) is set and
 * the hero trainer is on the field (trainer status 0x4C), the partner's
 * cut-in (model 0x32, special 0xA0, in the hero trainer's place), then
 * fightOutPokemonDarkPokemonEffect (special 0x3A) with fight_common message
 * 0x7717 "Oh! {hero}! There's a POKEMON with a black aura!", or 0x770A "Oh!
 * There's a SHADOW POKEMON!" once flag 0x9A0 is set. Encounter 0x11 (floor
 * status 0xD) plays special 0xD9 and the snag tutorial instead: 0x7729,
 * POUND's attack sequence on the Pokemon (fightOutPokemonWazaEffect(1, 1)),
 * 0x772A, and 0x772B when the hero has no Poke Balls (fn_801F7B70). Control
 * 0x2C is the partner's name, 0x2B the hero's. A page of three lines is
 * shown as two message events (the port's box holds two). */
#define ENCOUNTER_SNAG_TUTORIAL 0x11u
static void say_page(C3GeneralBattle *b,unsigned pos,const char *page)
{
    const char *second=strchr(page,'\n');second=second?strchr(second+1,'\n'):NULL;
    unsigned first=b->event_count;
    if(!second)say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%s",page);
    else{say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%.*s",(int)(second-page),page);
         say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%s",second+1);}
    /* Each page is a fight window (fightMenuOpenMsg) that stays open until
     * fightMenuCloseMsg, after the aura: the player reads it and goes on. */
    for(unsigned i=first;i<b->event_count;++i)b->events[i].hold=1;
}
static void dark_check_appear(C3GeneralBattle *b,unsigned pos)
{
    C3GeneralMon *m=at(b,pos);
    if(!m||!m->hp||!m->shadow||m->gauge_dropped||pos/2!=1||b->link||b->info.cpu_side0||b->info.partner_aura!=1)return;
    m->gauge_dropped=1;
    const char *r=b->partner_name[0]?b->partner_name:"Rui",*w=b->side_name[0][0]?b->side_name[0]:"Player";
    char page[192];int tutorial=b->encounter==ENCOUNTER_SNAG_TUTORIAL;
    unsigned first=b->event_count;
    if(tutorial)snprintf(page,sizeof page,"%s: %s!\nIt's that one! That POK\xc3\xa9MON!",r,w);
    else if(b->shadow_named)snprintf(page,sizeof page,"%s: Oh!\nThere's a SHADOW POK\xc3\xa9MON!",r);
    else snprintf(page,sizeof page,"%s: Oh! %s!\nThere's a POK\xc3\xa9MON with a black aura!",r,w);
    say_page(b,pos,page);
    if(b->event_count>first){b->events[first].cutin=1;b->events[first].special=tutorial?0xD9:0x3A;}
    if(!tutorial)return;
    snprintf(page,sizeof page,"%s: Those jerks captured me because\nI saw that POK\xc3\xa9MON.",r);say_page(b,pos,page);
    snprintf(page,sizeof page,"%s: I can see a black aura coming from it.\nThey must have done something to that\nPOK\xc3\xa9MON!",r);say_page(b,pos,page);
    first=b->event_count;
    say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%s",""); /* POUND's attack sequence, no text */
    if(b->event_count>first)b->events[first].waza_effect=1;
    first=b->event_count;
    snprintf(page,sizeof page,"%s: %s, be careful!\nThis POK\xc3\xa9MON isn't afraid to attack\npeople!",r,w);say_page(b,pos,page);
    if(b->event_count>first)b->events[first].special=0xD9; /* the aura again (phase 1) */
    snprintf(page,sizeof page,"%s: I know, %s!\nI hate to say this, but there's no other\nchoice.",r,w);say_page(b,pos,page);
    snprintf(page,sizeof page,"%s: Get that POK\xc3\xa9MON back from the\nbad guys!\nPlease, you have to!",r);say_page(b,pos,page);
    snprintf(page,sizeof page,"%s: You can...\nActually, only you can do it!",r);say_page(b,pos,page);
    {unsigned balls=0;for(unsigned k=1;k<=12;++k)balls+=b->balls[k];
     if(!balls){snprintf(page,sizeof page,"%s: ...Huh? Whaaaaat?!\nWhy don't you have any POK\xc3\xa9 BALLS\nwith you?",r);say_page(b,pos,page);}}
}
void c3_general_battle_appear_checks(C3GeneralBattle *b)
{if(b&&b->magic==MAGIC&&!b->turn)for(unsigned a=0;a<b->active_count;++a)dark_check_appear(b,2+a);}
int c3_general_battle_can_call(const C3GeneralBattle *b,unsigned pos)
{return b&&b->magic==MAGIC&&!b->link&&!b->info.cpu_side0&&pos<2&&alive((C3GeneralBattle *)b,pos);}
static int call_pokemon(C3GeneralBattle *b,const C3BattlePack *p,unsigned a)
{
    C3GeneralMon *x=at(b,a);unsigned r;
    if(!x||!x->hp)return 1;
    if(!say(b,C3_GB_EVENT_MESSAGE,a,a,0,"You called out to %s!",x->name))return 0;
    if(x->hyper){
        const uint8_t *n=nature_row(p,x);unsigned lv=c3_general_dark_level(x),chance=n&&lv<7?n[0x1F+lv]:0;
        if(!roll(b,100,&r))return 0;
        if(r<chance){x->hyper=0;if(x->status==C3_GB_SLEEP)x->status=0;gauge_filter(p,x,2);
            return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s came to its senses!",x->name);}
        if(!say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s didn't respond!",x->name))return 0;
        if(x->status==C3_GB_SLEEP){if(!roll(b,100,&r))return 0;
            if(r<chance/2){x->status=0;return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s woke up!",x->name);}}
        return 1;
    }
    if(x->status==C3_GB_SLEEP){x->status=0;return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s woke up!",x->name);}
    return say(b,C3_GB_EVENT_MESSAGE,a,a,0,x->shadow?"%s stared back coldly.":"%s turned to look at you.",x->name);
}
/* Returns 1 to go on with *c (possibly another move), 2 when the action is
 * spent, 0 on an RNG failure. */
static int hyper_before_move(C3GeneralBattle *b,const C3BattlePack *p,unsigned a,C3GeneralCommand *c)
{
    C3GeneralMon *x=at(b,a);unsigned r;
    if(!hyper_capable(b,p,a,x))return 1;
    const uint8_t *n=nature_row(p,x);unsigned lv=c3_general_dark_level(x);
    uint16_t id=c->slot<4?x->moves[c->slot]:STRUGGLE;
    if(!x->hyper){
        if(x->status==C3_GB_SLEEP)return 1;
        if(!roll(b,100,&r))return 0;
        if(lv<7&&r<n[0x18+lv]&&id==SHADOW_RUSH){x->hyper=1;
            unsigned before=b->event_count;
            if(!say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s went into Hyper Mode!",x->name))return 0;
            if(b->event_count>before)b->events[before].hyper=1;}
        return 1;
    }
    if(!roll(b,256,&r))return 0;
    if(!r){x->hyper=0;if(x->status==C3_GB_SLEEP)x->status=0;
        if(!say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s came to its senses!",x->name))return 0;}
    if(!x->hyper||id==SHADOW_RUSH)return 1;
    if(!roll(b,100,&r))return 0;
    unsigned cum=n[0x0B];
    if(r<cum){
        uint8_t slots[4];unsigned k=0;
        for(unsigned i=0;i<4;++i){uint16_t mid=x->moves[i];if(mid&&x->pp[i]&&mid!=0x165&&mid!=0x163)slots[k++]=(uint8_t)i;}
        if(k){uint16_t v;if(!c3_battle_rng_next16(&b->rng,&v))return 0;
            c->slot=slots[v%k];
            return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s won't listen! It acted on its own!",x->name)?1:0;}
        return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s is lost in thought...",x->name)?2:0;
    }
    if(r<(cum+=n[0x0C])||r<(cum+=n[0x0D]))
        return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s is ignoring your orders!",x->name)?2:0;
    if(r<(cum+=n[0x0E])){
        unsigned atk=staged(x->stats[1],x->stages[0]),def=staged(x->stats[2],x->stages[1]);
        uint32_t v=atk*40u*(2u*x->level/5u+2u)/(def?def:1)/50u;if(!v)v=1;v+=2u;
        return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s hit itself in its rage!",x->name)&&hurt(b,a,v,0)?2:0;
    }
    if(r<(cum+=n[0x0F]))return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s is lost in thought...",x->name)?2:0;
    if(r<(cum+=n[0x10]))return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s tried to use its item, but couldn't!",x->name)?2:0;
    if(r<(cum+=n[0x11]))return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s wants to return to you!",x->name)?2:0;
    return 1;
}
static int use_move(C3GeneralBattle *b,const C3BattlePack *p,unsigned a,C3GeneralCommand c,const uint8_t acted[4])
{
    C3GeneralMon *x=at(b,a);unsigned r;
    if(!x||!x->hp)return 1;
    if(x->recharge){x->recharge=0;return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s must recharge!",who(b,a));}
    if(x->status==C3_GB_SLEEP){
        if(x->status_turns&&--x->status_turns)return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s is fast asleep.",who(b,a));
        x->status=0;if(!say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s woke up!",who(b,a)))return 0;
    }
    if(x->status==C3_GB_FREEZE){
        if(!roll(b,5,&r))return 0;
        if(r)return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s is frozen solid!",who(b,a));
        x->status=0;if(!say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s thawed out!",who(b,a)))return 0;
    }
    if(x->flinch)return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s flinched!",who(b,a));
    if(x->confusion){
        if(!--x->confusion){if(!say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s snapped out of confusion!",who(b,a)))return 0;}
        else{
            if(!say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s is confused!",who(b,a))||!roll(b,2,&r))return 0;
            if(!r){unsigned atk=staged(x->stats[1],x->stages[0]),def=staged(x->stats[2],x->stages[1]);
                uint32_t v=((2u*x->level/5u+2u)*40u*atk/(def?def:1))/50u+2u;
                return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"It hurt itself in its confusion!")&&hurt(b,a,v,0);}
        }
    }
    if(x->status==C3_GB_PARALYSIS){
        if(!roll(b,4,&r))return 0;
        if(!r)return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s is paralyzed! It can't move!",who(b,a));
    }
    {int h=hyper_before_move(b,p,a,&c);if(!h)return 0;if(h==2)return 1;}
    uint16_t id=c.slot==4?STRUGGLE:x->moves[c.slot];
    const uint8_t *mv=move_row(p,id);
    if(!mv)return 0;
    /* A stand-in slot reads its 5 PP again next time: the decrement lands
     * in the Pokemon's own slot (pokemonBiosSetPokemonWazaPp, mode 0, at
     * most that move's maximum). */
    if(c.slot<4){if(!x->pp[c.slot])id=STRUGGLE,mv=move_row(p,id);
        else if(x->hidden>>c.slot&1u){unsigned left=x->pp[c.slot]-1u;x->own_pp[c.slot]=(uint8_t)(left<x->own_pp_max[c.slot]?left:x->own_pp_max[c.slot]);}
        else --x->pp[c.slot];}
    char name[C3_GB_NAME];c3_battle_pack_move_name(p,id,name);
    if(!say(b,C3_GB_EVENT_MOVE,a,c.target,id,"%s used %s!",who(b,a),name))return 0;
    uint16_t effect=be16(mv+26);unsigned range=mv[3],power=(unsigned)(int16_t)be16(mv+22);
    if(id==STRUGGLE)effect=E_RECOIL;
    int s=0,delta=0,self=0,hit_stat=0;
    int is_stat=stat_effect(effect,&s,&delta,&self,&hit_stat);
    /* Self / side moves. */
    if(!power&&(range==2||range==5||range==7||self)){
        if(is_stat&&self)return change_stage(b,a,effect_stage[s],delta);
        switch(effect){
        case E_HAZE:for(unsigned q=0;q<4;++q){C3GeneralMon *m=at(b,q);if(m)memset(m->stages,0,sizeof m->stages);}
            return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"All stat changes were eliminated!");
        case E_REFLECT:case E_LIGHT_SCREEN:{unsigned k=effect==E_LIGHT_SCREEN;
            if(b->screens[a/2][k])return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"But it failed!");
            b->screens[a/2][k]=5;return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s raised its team's %s!",who(b,a),k?"Sp. Def":"Defense");}
        case E_RESTORE_HP:case E_SOFTBOILED:case E_MORNING_SUN:case E_SYNTHESIS:case E_MOONLIGHT:
            if(x->hp==x->max_hp)return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s's HP is full!",who(b,a));
            heal(b,a,x->max_hp/2u);return 1;
        case E_REST:if(x->hp==x->max_hp)return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"But it failed!");
            x->status=C3_GB_SLEEP;x->status_turns=3;heal(b,a,x->max_hp);
            return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s slept and became healthy!",who(b,a));
        case E_PROTECT:case E_ENDURE:x->protect=effect==E_PROTECT?1:2;
            return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s %s itself!",who(b,a),effect==E_PROTECT?"protected":"braced");
        case E_BULK_UP:return change_stage(b,a,0,1)&&change_stage(b,a,1,1);
        case E_CALM_MIND:return change_stage(b,a,2,1)&&change_stage(b,a,3,1);
        case E_DRAGON_DANCE:return change_stage(b,a,0,1)&&change_stage(b,a,4,1);
        case E_COSMIC_POWER:return change_stage(b,a,1,1)&&change_stage(b,a,3,1);
        case E_SPLASH:return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"But nothing happened!");
        default:return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"But nothing happened!");
        }
    }
    uint8_t list[3];int n=targets_of(b,a,range,c.target,list);
    if(n<0)return 0;
    if(!n)return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"But there was no target...");
    uint32_t dealt_total=0;
    for(int t=0;t<n;++t){unsigned d=list[t];C3GeneralMon *y=at(b,d);
        if(!y||!y->hp)continue;
        if(y->protect==1&&mv[7]){if(!say(b,C3_GB_EVENT_MESSAGE,d,d,0,"%s protected itself!",who(b,d)))return 0;continue;}
        /* Accuracy: Gen-3 stage table over accuracy minus evasion. */
        if(mv[4]&&effect!=E_ALWAYS_HIT){
            static const uint16_t num[13]={33,36,43,50,60,75,100,133,166,200,233,266,300};
            int st=x->stages[5]-y->stages[6];st=st<-6?-6:st>6?6:st;
            unsigned chance=mv[4]*num[st+6]/100u;
            if(effect==E_OHKO)chance=x->level<y->level?0:30u+x->level-y->level;
            if(!roll(b,100,&r))return 0;
            if(r>=chance){if(!say(b,C3_GB_EVENT_MESSAGE,a,d,0,"%s's attack missed!",who(b,a)))return 0;continue;}
        }
        if(!power){
            if(is_stat&&!self){if(!change_stage(b,d,effect_stage[s],delta))return 0;continue;}
            switch(effect){
            case E_SLEEP:if(y->status){if(!say(b,C3_GB_EVENT_MESSAGE,d,d,0,"But it failed!"))return 0;}else if(!inflict(b,d,C3_GB_SLEEP))return 0;break;
            case E_POISON:if(!inflict(b,d,C3_GB_POISON))return 0;break;
            case E_TOXIC:if(!inflict(b,d,C3_GB_TOXIC))return 0;break;
            case E_PARALYZE:if(!inflict(b,d,C3_GB_PARALYSIS))return 0;break;
            case E_WILL_O_WISP:if(!inflict(b,d,C3_GB_BURN))return 0;break;
            case E_CONFUSE:if(y->confusion){if(!say(b,C3_GB_EVENT_MESSAGE,d,d,0,"%s is already confused!",who(b,d)))return 0;}
                else{if(!roll(b,4,&r))return 0;y->confusion=(uint8_t)(2+r);if(!say(b,C3_GB_EVENT_MESSAGE,d,d,0,"%s became confused!",who(b,d)))return 0;}break;
            case E_TICKLE:if(!change_stage(b,d,0,-1)||!change_stage(b,d,1,-1))return 0;break;
            default:if(!say(b,C3_GB_EVENT_MESSAGE,d,d,0,"But nothing happened!"))return 0;
            }
            continue;
        }
        /* Fixed and special damage. */
        uint32_t v=0;int critical=0;unsigned num=1,den=1;
        if(effect==E_OHKO)v=y->hp;
        else if(effect==E_SUPER_FANG)v=y->hp/2u?y->hp/2u:1u;
        else if(effect==E_DRAGON_RAGE)v=40;
        else if(effect==E_SONICBOOM)v=20;
        else if(effect==E_LEVEL_DAMAGE)v=x->level;
        else{
            unsigned pw=power;
            if(effect==E_RETURN)pw=x->friendship*10u/25u;
            if(effect==E_FRUSTRATION)pw=(255u-(x->friendship>255?255:x->friendship))*10u/25u;
            if(effect==E_FACADE&&x->status&&x->status!=C3_GB_SLEEP&&x->status!=C3_GB_FREEZE)pw*=2u;
            if(!pw)pw=1;
            if(!damage(b,p,a,d,id,mv,pw,&v,&critical,&num,&den))return 0;
            if(!num){if(!say(b,C3_GB_EVENT_MESSAGE,a,d,0,"It doesn't affect %s...",who(b,d)))return 0;continue;}
        }
        if((effect==E_SUPER_FANG||effect==E_DRAGON_RAGE||effect==E_SONICBOOM||effect==E_LEVEL_DAMAGE||effect==E_OHKO)){
            unsigned en,ed;if(!effectiveness(p,mv[2],y,&en,&ed)){if(!say(b,C3_GB_EVENT_MESSAGE,a,d,0,"It doesn't affect %s...",who(b,d)))return 0;continue;}}
        unsigned hits=1;
        if(effect==E_MULTI_HIT){if(!roll(b,8,&r))return 0;hits=r<3?2:r<6?3:r<7?4:5;}
        if(effect==E_DOUBLE_HIT||effect==E_TWINEEDLE)hits=2;
        unsigned landed=0;
        for(unsigned h=0;h<hits&&y->hp&&x->hp;++h){
            uint32_t hv=v;if(h&&!(effect==E_OHKO)){int cr;unsigned nn,dd;if(!damage(b,p,a,d,id,mv,power,&hv,&cr,&nn,&dd))return 0;critical=cr;}
            if(effect==E_FALSE_SWIPE&&hv>=y->hp)hv=y->hp-1u;
            if(critical&&!say(b,C3_GB_EVENT_MESSAGE,a,d,0,"A critical hit!"))return 0;
            unsigned before=y->hp;
            if(!hurt(b,d,hv,1))return 0;
            dealt_total+=before-y->hp;++landed;
        }
        if(hits>1&&!say(b,C3_GB_EVENT_MESSAGE,a,d,0,"Hit %u time(s)!",landed))return 0;
        if(effect==E_OHKO&&!y->hp&&!say(b,C3_GB_EVENT_MESSAGE,a,d,0,"It's a one-hit KO!"))return 0;
        if(num>den&&!say(b,C3_GB_EVENT_MESSAGE,a,d,0,"It's super effective!"))return 0;
        if(num<den&&!say(b,C3_GB_EVENT_MESSAGE,a,d,0,"It's not very effective..."))return 0;
        if(effect==E_ABSORB&&x->hp&&landed)heal(b,a,dealt_total/2u?dealt_total/2u:1u);
        /* Secondary effects on a surviving target. */
        unsigned chance=mv[5]?mv[5]:100u;
        int secondary=0;
        switch(effect){case E_POISON_HIT:case E_BURN_HIT:case E_FREEZE_HIT:case E_PARALYZE_HIT:case E_FLINCH_HIT:
            case E_CONFUSE_HIT:case E_TWINEEDLE:case E_BLAZE_KICK:case E_POISON_FANG:case E_POISON_TAIL:case E_ALL_STATS_UP_HIT:secondary=1;break;
            default:secondary=is_stat&&hit_stat;}
        if(secondary&&(y->hp||(is_stat&&self))){
            if(!roll(b,100,&r))return 0;
            if(r<chance){
                if(is_stat&&hit_stat){if(!change_stage(b,self?a:d,effect_stage[s],delta))return 0;}
                else switch(effect){
                case E_POISON_HIT:case E_TWINEEDLE:case E_POISON_TAIL:if(!inflict(b,d,C3_GB_POISON))return 0;break;
                case E_POISON_FANG:if(!inflict(b,d,C3_GB_TOXIC))return 0;break;
                case E_BURN_HIT:case E_BLAZE_KICK:if(!inflict(b,d,C3_GB_BURN))return 0;break;
                case E_FREEZE_HIT:if(!inflict(b,d,C3_GB_FREEZE))return 0;break;
                case E_PARALYZE_HIT:if(!inflict(b,d,C3_GB_PARALYSIS))return 0;break;
                case E_FLINCH_HIT:if(!acted[d])y->flinch=1;break;
                case E_CONFUSE_HIT:if(!y->confusion){if(!roll(b,4,&r))return 0;y->confusion=(uint8_t)(2+r);
                    if(!say(b,C3_GB_EVENT_MESSAGE,d,d,0,"%s became confused!",who(b,d)))return 0;}break;
                case E_ALL_STATS_UP_HIT:for(unsigned k=0;k<5;++k)if(!change_stage(b,a,k,1))return 0;break;
                }
            }
        }
    }
    /* User-side consequences after all targets. */
    if(effect==E_RECOIL&&x->hp&&dealt_total){unsigned v=id==STRUGGLE?dealt_total/4u:dealt_total/4u;
        if(!say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s is hit with recoil!",who(b,a))||!hurt(b,a,v?v:1,0))return 0;}
    if(effect==E_DOUBLE_EDGE&&x->hp&&dealt_total){unsigned v=dealt_total/3u;
        if(!say(b,C3_GB_EVENT_MESSAGE,a,a,0,"%s is hit with recoil!",who(b,a))||!hurt(b,a,v?v:1,0))return 0;}
    if(effect==E_EXPLOSION&&x->hp){x->hp=1;if(!hurt(b,a,1,0))return 0;}
    if(effect==E_RECHARGE&&dealt_total)x->recharge=1;
    if(effect==E_OVERHEAT&&x->hp&&dealt_total&&!change_stage(b,a,2,-2))return 0;
    if(effect==E_SUPERPOWER&&x->hp&&dealt_total&&(!change_stage(b,a,0,-1)||!change_stage(b,a,1,-1)))return 0;
    return 1;
}

/* ---- turns --------------------------------------------------------------- */

uint8_t c3_general_battle_legal_moves(const C3GeneralBattle *b,const C3BattlePack *p,unsigned pos)
{
    const C3GeneralMon *m=b&&b->magic==MAGIC?cat(b,pos):NULL;uint8_t mask=0;
    if(!m||!m->hp)return 0;
    /* "????" (0x165) and 0x163 are never chosen (fightPokemonCountOutOkWaza). */
    for(unsigned i=0;i<4;++i)if(m->moves[i]&&m->pp[i]&&m->moves[i]!=C3_SHADOW_HIDDEN_MOVE&&m->moves[i]!=0x163&&move_row(p,m->moves[i]))
        mask|=(uint8_t)(1u<<i);
    return mask;
}
int c3_general_battle_choose_with(const C3GeneralBattle *cb,const C3BattlePack *p,unsigned pos,C3BattleRng *rng,C3GeneralCommand *c)
{
    C3GeneralBattle *b=(C3GeneralBattle *)cb;
    if(!b||!c||!rng||b->magic!=MAGIC||!alive(b,pos))return 0;
    uint8_t mask=c3_general_battle_legal_moves(b,p,pos);unsigned slots[4],n=0,foes[2],nf=0;uint16_t v;
    for(unsigned i=0;i<4;++i)if(mask>>i&1u)slots[n++]=i;
    c->kind=0;c->slot=4;
    if(n){if(!c3_battle_rng_next16(rng,&v))return 0;c->slot=(uint8_t)slots[v%n];}
    unsigned foe=1-pos/2;
    for(unsigned i=0;i<b->active_count;++i)if(alive(b,foe*2+i))foes[nf++]=foe*2+i;
    if(!nf)return 0;
    if(!c3_battle_rng_next16(rng,&v))return 0;
    c->target=(uint8_t)foes[v%nf];return 1;
}
/* ---- trainer AI ------------------------------------------------------------
 * fn_802342CC (fight_range_exact_80233DB0.c:395) for a trainer whose data
 * names an AI row (trainer row +6), on its side's active Pokemon:
 *  1. switch roll: AI +7 > draw % 100, then per Pokemon
 *     fightTrainerAiSelectFightActionIrekae: switch when
 *     fightTrainerAiGetFightPokemonIrekaeModosuValue > 0;
 *  2. item roll (AI +8) when the fight kind allows items: drawn, but the
 *     trainers' items are not modelled, so no item is used;
 *  3. combo roll (AI +6): drawn; fn_8024E534 is a stub returning 0;
 *  4. the Pokemon still without an action, sorted by speed
 *     (fightFloorSortFightOutPokemonPtrArySub without priority), each pick
 *     a target and a move (fn_8023A308): a random target (AI +0x11) or the
 *     scored one, a random move (AI +0x12) or fn_8023A740's scores.
 * fn_8023A740: each usable move's score is its value function's (AI +0x13;
 * the per-move fightTrainerAiWazaValue* functions are not ported: 0), then
 * the AI value codes (pack: s32 per code, added with the +-200 clamp of
 * fn_80239984): 0x3C the move can hit (the per-move hit functions and the
 * ability immunities aiWazaHitCheck reads are not modelled: always), 0x3F
 * resisted / 0x40 no effect (fn_8023793C, damaging moves), 0x42 the AI's
 * preferred types (AI +0x20 pairs, value scaled by fn_802398E4), 0x44 the
 * highest estimated damage, 0x45 more than the target's HP, 0x46 at least
 * its HP, 0x47 that and the best accuracy, 0x4A a both-foes move against
 * two; then the jitter draw % (2*AI +0x19 + 1) - AI +0x19 (retail has
 * lbl_8047882E set), and fightTrainerAiGetValueAryMaxBanme's choice (ties
 * at random, one draw). The damage estimate (AI +0xE; the per-move
 * fightTrainerAiWazaDamage* functions are not ported) is the engine's
 * damage at the full roll without a critical hit. Not ported: the
 * fightTrainerAiWazaValue/Hit/Damage per-move functions, codes 0x41, 0x43,
 * 0x48, 0x49, 0x4B, 0x4C, the scored target choice's special cases
 * (fn_8023B498: the foe whose best move scores highest is taken), and the
 * switch value's codes 1-5 and 8 and its bad-condition code. */
static const uint8_t *ai_row(const C3GeneralBattle *b,const C3BattlePack *p,unsigned side)
{
    const uint8_t *t=NULL;
    if(!p->ai_count||b->link)return NULL;
    if(side==1)t=entry(p,b->encounter)+128;
    else if(b->info.cpu_side0){const uint8_t *s0=side0_row(p,b->encounter);t=s0?s0+4:NULL;}
    if(!t)return NULL;
    unsigned id=be16(t+6);
    return id<p->ai_count?p->bytes+p->ai_at+id*AI_ROW:NULL;
}
static int32_t ai_code(const C3BattlePack *p,unsigned code)
{return code<p->ai_value_count?(int32_t)be32(p->bytes+p->ai_value_at+code*4u):0;}
/* fn_80239984 / fightTrainerAiAddValue: add, clamped to +-200 on the side moved. */
static int32_t ai_add(int32_t v,int32_t d)
{if(d>0)return v+d>200?200:v+d;if(d<0)return v+d<-200?-200:v+d;return v;}
static int32_t ai_add_scaled(int32_t v,uint8_t x,int32_t code)
{int16_t f=(int16_t)(x*100/255-50);return ai_add(v,(int16_t)(f*code/50));}
static int ai_draw(C3GeneralBattle *b,unsigned n,unsigned *out){uint16_t v;if(!c3_battle_rng_next16(&b->rng,&v))return 0;*out=n?v%n:v;return 1;}
/* fn_802395C8 with the type check flag: Struggle, Shadow Rush, Beat Up and
 * 0x161 count as typeless (9). */
static unsigned ai_move_type(const uint8_t *ai,uint16_t id,const uint8_t *mv)
{return ai[0xB]==1&&(id==0xa5||id==0x164||id==0xf8||id==0x161)?9u:mv[2];}
/* fn_8023793C's result: 0x43 no effect, 0x42 resisted, 0x41 super
 * effective, 0x3F otherwise (and for the typeless 9). */
static unsigned ai_effect_code(const C3BattlePack *p,unsigned type,const C3GeneralMon *d)
{
    unsigned num,den;
    if(type==9)return 0x3f;
    if(!effectiveness(p,type,d,&num,&den))return 0x43;
    return num>den?0x41:num<den?0x42:0x3f;
}
/* The estimate: the engine's damage formula at the full roll, no critical
 * hit; fixed-damage effects give their amount. */
static uint32_t ai_damage(C3GeneralBattle *b,const C3BattlePack *p,unsigned a,unsigned d,uint16_t id)
{
    C3GeneralMon *x=at(b,a),*y=at(b,d);const uint8_t *mv=move_row(p,id);
    if(!x||!y||!mv)return 0;
    unsigned power=(unsigned)(int16_t)be16(mv+22),effect=be16(mv+26),num,den,type=mv[2],special=type>=10;
    if(!power)return 0;
    if(!effectiveness(p,type,y,&num,&den))return 0;
    if(effect==E_DRAGON_RAGE)return 40;
    if(effect==E_SONICBOOM)return 20;
    if(effect==E_LEVEL_DAMAGE)return x->level;
    if(effect==E_SUPER_FANG)return y->hp/2u?y->hp/2u:1u;
    if(effect==E_OHKO)return x->level<y->level?0:y->hp;
    unsigned atk=staged(x->stats[special?3:1],x->stages[special?2:0]),def=staged(y->stats[special?4:2],y->stages[special?3:1]);
    if(!def)def=1;
    uint32_t v=atk*power*(2u*x->level/5u+2u)/def/50u;
    if(!special&&x->status==C3_GB_BURN)v/=2u;
    unsigned side=d/2,defenders=0;
    for(unsigned i=0;i<b->active_count;++i)defenders+=alive(b,side*2+i)!=0;
    int doubles=b->active_count==2;
    if(b->screens[side][special]&&a/2!=d/2)v=doubles&&defenders>=2?v/3u*2u:v/2u;
    if(doubles&&mv[3]==4&&defenders>=2)v/=2u;
    if(!v)v=1;
    v+=2u;
    if(type==x->types[0]||type==x->types[1])v=v*3u/2u;
    return v*num/den;
}
/* fn_802367CC: the usable moves (PP left) as slots; 0 when only Struggle. */
static unsigned ai_moves(C3GeneralBattle *b,const C3BattlePack *p,unsigned pos,uint8_t slots[4])
{
    uint8_t mask=c3_general_battle_legal_moves(b,p,pos);unsigned n=0;
    for(unsigned i=0;i<4;++i)if(mask>>i&1u)slots[n++]=(uint8_t)i;
    return n;
}
static unsigned ai_foes(C3GeneralBattle *b,unsigned pos,unsigned foes[2])
{unsigned n=0,foe=1-pos/2;for(unsigned i=0;i<b->active_count;++i)if(alive(b,foe*2+i))foes[n++]=foe*2+i;return n;}
/* fn_8023A740 without the jitter and the final choice: scores[] per slot. */
static void ai_scores(C3GeneralBattle *b,const C3BattlePack *p,const uint8_t *ai,unsigned pos,unsigned target,
                      const uint8_t *slots,unsigned n,int32_t scores[4])
{
    C3GeneralMon *x=at(b,pos),*y=at(b,target);
    int32_t dmg[4]={0},diff[4]={0},metric[4]={0},max_dmg=-0xFFFF,max_metric=0;
    unsigned hp=y?y->hp:0;
    for(unsigned k=0;k<n;++k){uint16_t id=x->moves[slots[k]];const uint8_t *mv=move_row(p,id);
        dmg[k]=ai[0xE]==1?(int32_t)ai_damage(b,p,pos,target,id):0;
        diff[k]=(int32_t)hp-dmg[k];
        if(mv&&be16(mv+22)){if(max_dmg<dmg[k])max_dmg=dmg[k];
            if(diff[k]<=0){metric[k]=mv[4]?mv[4]:100;if(max_metric<metric[k])max_metric=metric[k];}}}
    for(unsigned k=0;k<n;++k){uint16_t id=x->moves[slots[k]];const uint8_t *mv=move_row(p,id);
        int32_t sc=0;int damaging=mv&&be16(mv+22);
        unsigned type=mv?ai_move_type(ai,id,mv):9;
        sc=ai_add(sc,ai_code(p,0x3C));
        if(type!=9&&damaging&&y&&ai[0xB]==1){unsigned code=ai_effect_code(p,type,y);
            if(code==0x42)sc=ai_add(sc,ai_code(p,0x3F));
            if(code==0x43)sc=ai_add(sc,ai_code(p,0x40));}
        if(type!=9)for(unsigned j=0;j<2;++j){unsigned t=ai[0x20+2*j];
            if(t!=9&&type==t)sc=ai_add_scaled(sc,ai[0x21+2*j],ai_code(p,0x42));}
        if(damaging){
            if(max_dmg<=dmg[k])sc=ai_add(sc,ai_code(p,0x44));
            if(diff[k]<0)sc=ai_add(sc,ai_code(p,0x45));
            if(diff[k]<=0)sc=ai_add(sc,ai_code(p,0x46));
            if(diff[k]<=0&&max_metric<=metric[k])sc=ai_add(sc,ai_code(p,0x47));
        }
        if(mv&&mv[3]==4&&y){unsigned two=0;for(unsigned i=0;i<b->active_count;++i)two+=alive(b,(target/2)*2+i)!=0;
            if(two>=2)sc=ai_add(sc,ai_code(p,0x4A));}
        scores[k]=sc;}
}
/* fightTrainerAiGetValueAryMaxBanme with random ties: -1 when every value
 * is under -200. */
static int ai_argmax(C3GeneralBattle *b,const int32_t *v,unsigned n,int *out)
{
    int32_t best=-200;unsigned ties=0,r;int first=-1;
    for(unsigned i=0;i<n;++i){if(v[i]<-200)continue;if(first<0||v[i]>best){best=v[i];first=(int)i;ties=1;}else if(v[i]==best)++ties;}
    if(first<0){*out=-1;return 1;}
    if(ties==1){*out=first;return 1;}
    if(!ai_draw(b,0,&r))return 0;
    r=(r&0xFFFFu)%ties;
    for(unsigned i=0;i<n;++i)if(v[i]==best&&!r--){*out=(int)i;return 1;}
    *out=first;return 1;
}
/* fn_8023A308 for one Pokemon. */
static int ai_pick_move(C3GeneralBattle *b,const C3BattlePack *p,const uint8_t *ai,unsigned pos,C3GeneralCommand *c)
{
    uint8_t slots[4];unsigned n=ai_moves(b,p,pos,slots),foes[2],nf=ai_foes(b,pos,foes),r,target;
    c->kind=0;c->slot=4;c->target=nf?(uint8_t)foes[0]:(uint8_t)(2-2*(pos/2));
    if(!n||!nf)return 1;   /* Struggle */
    if(ai[0x11]==1){if(!ai_draw(b,0,&r))return 0;target=foes[(r&0xFFFFu)%nf];}
    else{   /* fn_8023B498's main path: the foe whose best score is highest */
        int32_t best=-0x7fffffff;target=foes[0];
        for(unsigned i=0;i<nf;++i){int32_t sc[4];ai_scores(b,p,ai,pos,foes[i],slots,n,sc);
            for(unsigned k=0;k<n;++k)if(sc[k]>best){best=sc[k];target=foes[i];}}
    }
    int k;
    if(ai[0x12]==1){if(!ai_draw(b,0,&r))return 0;k=(int)((r&0xFFFFu)%n);}
    else{
        int32_t sc[4];ai_scores(b,p,ai,pos,target,slots,n,sc);
        unsigned jit=ai[0x19];
        for(unsigned i=0;i<n;++i){if(!ai_draw(b,0,&r))return 0;
            sc[i]=ai_add(sc[i],(int32_t)((r&0xFFFFu)%(2u*jit+1u))-(int32_t)jit);}
        if(!ai_argmax(b,sc,n,&k))return 0;
        if(k<0)return 1;
    }
    c->slot=slots[k];c->target=(uint8_t)target;return 1;
}
/* fightTrainerAiSelectIrekaeDasuFightPokemon (0x8024E690): which reserve
 * Pokemon an AI trainer sends in, both after a faint and for a switch
 * (fightTrainerAiSelectFightActionIrekae). The candidates are the side's
 * healthy Pokemon that are not out (fn_801F87CC, party order). With
 * PokemonSelectRandomFlag (AI +0) one draw picks one; with
 * PokemonDataOrderOutFlag (AI +2) the lowest trainer data entry (party
 * order) goes. Otherwise each candidate is scored with the AI value codes
 * (pack, fightTrainerAiAddValue's +-200 clamp):
 *   0x1A/0x1B/0x1C its KeyPlayerFlag 1/2/3 (record +5) when AI +4 is set;
 *   0x1D (scaled by AI +0x21/+0x23) each of the AI's preferred types it has;
 *   0x1E HP at or under AI +0xD percent; 0x1F no usable move can hit an
 *   opponent out (fn_8023C530); 0x21 the highest Speed stat; 0x20 the best
 *   defensive type score (fn_802389D4: fn_8010C54C mode 0 over its types,
 *   +10 per attacking type resisted or blocked, -10 per weakness);
 *   0x22 per opponent out and per damaging move of its own that is super
 *   effective on it; 0x23 per opponent out and per damaging move of the
 *   opponent's that is super effective on it; 0x24 (PokemonSelectWeakPoint,
 *   AI +1) per type it is weak to that an ally already out is weak to too;
 *   0x25/0x26 (fightTrainerAiGetFightPokemonIrekaeDasuTokuseiAddsubDataId)
 *   Intimidate with an opponent at full HP, or Trace with an opponent whose
 *   ability is none of {1,2,0x23,0x24,0x2D,0x32,0x35,0x36,0x3B,0x46};
 *   then the jitter draw % (2*AI +0x19 + 1) - AI +0x19, and
 *   fightTrainerAiGetValueAryMaxBanme picks (ties: one draw).
 * Type checks need ZokuseiCheckFlag (AI +0xB) and ability checks
 * TokuseiCheckFlag (AI +0xC), as in the original (fn_80238E30, fn_80239058,
 * fn_80238B0C with Levitate/Wonder Guard). Not modelled: the Baton Pass
 * branch (move 0xE2 is not in the port), the weather codes 0x27-0x2C (no
 * weather: fn_80235B04 is 0), and the per-move hit functions fn_8023C530
 * calls when WazaHitFlag (AI +0x15) is set: a damaging move then hits unless
 * the type chart blocks it, a status move always. When every value is under
 * -200 the original returns -1; here the first candidate goes, so a fainted
 * slot is never left empty while the trainer has Pokemon. */
static unsigned type_code(const C3BattlePack *p,unsigned attack,unsigned defend)
{return attack<18&&defend<18?be16(p->bytes+p->types_at+attack*TYPE_ROW+8+defend*2):0x3f;}
/* fn_80238B0C: a move type against a Pokemon, as the AI sees it. */
static unsigned ai_type_against(const C3BattlePack *p,const uint8_t *ai,unsigned type,const C3GeneralMon *d,int power)
{
    unsigned weak=0,resist=0,n=0,result=0x3f;
    if(type==9)return 0x3f;
    if(ai[0xC]==1&&d->ability==0x1A&&type==4)return 0x43;
    for(unsigned i=0;ai[0xB]==1&&i<2;++i){unsigned t=d->types[i];if(t==9)continue;++n;
        unsigned c=type_code(p,type,t);if(c==0x43)return 0x43;weak+=c==0x41;resist+=c==0x42;}
    if(n&&weak!=resist)result=weak>resist?0x41:0x42;
    if(ai[0xC]==1&&d->ability==0x19&&result!=0x41&&power>0)return 0x43;
    return result;
}
static int ai_has_type(const uint8_t *ai,const C3GeneralMon *m,unsigned type)
{return ai[0xB]==1?(m->types[0]==type||m->types[1]==type):type==9;}
/* fn_802389D4 / fn_8010C54C mode 0. */
static int32_t ai_type_score(const C3BattlePack *p,const uint8_t *ai,const C3GeneralMon *m)
{
    int32_t v=0;
    for(unsigned k=0;ai[0xB]==1&&k<2;++k){unsigned t=m->types[k];if(t==9)continue;
        for(unsigned i=0;i<18;++i){if(i==9)continue;unsigned c=type_code(p,i,t);
            if(c==0x43||c==0x42)v+=10;else if(c==0x41)v-=10;}}
    return v;
}
static unsigned mon_moves(const C3BattlePack *p,const C3GeneralMon *m,uint16_t ids[4])
{unsigned n=0;for(unsigned k=0;k<4;++k)if(m->moves[k]&&m->pp[k]&&m->moves[k]!=C3_SHADOW_HIDDEN_MOVE&&m->moves[k]!=0x163&&move_row(p,m->moves[k]))ids[n++]=m->moves[k];return n;}
static int ai_can_hit(const C3BattlePack *p,const uint8_t *ai,uint16_t id,const C3GeneralMon *y)
{
    const uint8_t *mv=move_row(p,id);
    if(ai[0x15]!=1)return 1;
    if(!mv||!be16(mv+22))return 1;
    return ai_type_against(p,ai,ai_move_type(ai,id,mv),y,(int16_t)be16(mv+22))!=0x43;
}
static int ai_select_dasu(C3GeneralBattle *b,const C3BattlePack *p,const uint8_t *ai,unsigned s,const uint8_t reserved[C3_GB_PARTY])
{
    uint8_t cand[C3_GB_PARTY];unsigned count=0,r;int32_t values[C3_GB_PARTY]={0};
    for(unsigned i=0;i<b->party_count[s];++i){int used=reserved&&reserved[i];
        for(unsigned a=0;a<b->active_count;++a)used|=b->active[s][a]==i;
        if(!used&&b->party[s][i].hp)cand[count++]=(uint8_t)i;}
    if(!count)return -1;
    const C3GeneralMon *foes[2],*allies[C3_GB_PARTY];unsigned nf=0,na=0;
    for(unsigned a=0;a<b->active_count;++a){uint8_t f=b->active[1-s][a],o=b->active[s][a];
        if(f!=NO_MON&&b->party[1-s][f].hp)foes[nf++]=&b->party[1-s][f];
        if(o!=NO_MON&&b->party[s][o].hp)allies[na++]=&b->party[s][o];}
    for(unsigned i=0;reserved&&i<b->party_count[s];++i)if(reserved[i]&&b->party[s][i].hp)allies[na++]=&b->party[s][i];
    if(ai[0]==1){if(!ai_draw(b,0,&r))return -2;return cand[(r&0xFFFFu)%count];}
    uint32_t max_speed=0;int32_t max_score=-0xFFFF;
    for(unsigned i=0;i<count;++i){const C3GeneralMon *m=&b->party[s][cand[i]];int32_t t=ai_type_score(p,ai,m);
        if(max_speed<m->stats[5])max_speed=m->stats[5];
        if(max_score<t)max_score=t;}
    if(ai[2]==1)return cand[0];
    unsigned range=ai[0x19];
    for(unsigned i=0;i<count;++i){
        const C3GeneralMon *m=&b->party[s][cand[i]];uint16_t mine[4],theirs[4];unsigned n=mon_moves(p,m,mine);int hit=0;
        for(unsigned f=0;f<nf&&!hit;++f)for(unsigned k=0;k<n&&!hit;++k)if(mine[k]!=0x165&&ai_can_hit(p,ai,mine[k],foes[f]))hit=1;
        int32_t v=values[i];unsigned kind=ai[4]==1?m->key_player:0;
        if(kind>=1&&kind<=3)v=ai_add(v,ai_code(p,0x19+kind));
        for(unsigned k=0;k<2;++k){unsigned t=ai[0x20+2*k];if(t!=9&&ai_has_type(ai,m,t))v=ai_add_scaled(v,ai[0x21+2*k],ai_code(p,0x1D));}
        if(m->max_hp&&(unsigned)m->hp*100u/m->max_hp<=ai[0xD])v=ai_add(v,ai_code(p,0x1E));
        if(!hit)v=ai_add(v,ai_code(p,0x1F));
        if(max_speed<=m->stats[5])v=ai_add(v,ai_code(p,0x21));
        if(max_score<=ai_type_score(p,ai,m))v=ai_add(v,ai_code(p,0x20));
        for(unsigned f=0;f<nf;++f)for(unsigned k=0;k<n;++k){const uint8_t *mv=move_row(p,mine[k]);unsigned t=ai_move_type(ai,mine[k],mv);
            if(t==9||!be16(mv+22))continue;
            if(ai_type_against(p,ai,t,foes[f],(int16_t)be16(mv+22))==0x41)v=ai_add(v,ai_code(p,0x22));}
        for(unsigned f=0;f<nf;++f){unsigned fn=mon_moves(p,foes[f],theirs);
            for(unsigned k=0;k<fn;++k){const uint8_t *mv=move_row(p,theirs[k]);unsigned t=ai_move_type(ai,theirs[k],mv);
                if(t==9||!be16(mv+22))continue;
                if(ai_type_against(p,ai,t,m,(int16_t)be16(mv+22))==0x41)v=ai_add(v,ai_code(p,0x23));}}
        if(ai[1]==1)for(unsigned t=0;t<18;++t){if(t==9||ai_type_against(p,ai,t,m,1)!=0x41)continue;
            for(unsigned q=0;q<na;++q)if(ai_type_against(p,ai,t,allies[q],1)==0x41)v=ai_add(v,ai_code(p,0x24));}
        {   /* fightTrainerAiGetFightPokemonIrekaeDasuTokuseiAddsubDataId */
            static const uint8_t trace_ok[10]={0x01,0x02,0x23,0x24,0x2D,0x32,0x35,0x36,0x3B,0x46};
            unsigned code=0,ab=ai[0xC]==1?m->ability:0;
            if(ab==0x16)for(unsigned f=0;f<nf&&!code;++f)if(foes[f]->hp>=foes[f]->max_hp)code=0x25;
            if(!code&&ab==0x24)for(unsigned f=0;f<nf&&!code;++f){int found=0;unsigned fab=ai[0xC]==1?foes[f]->ability:0;
                for(unsigned j=0;j<10;++j)found|=fab==trace_ok[j];
                if(!found)code=0x26;}
            if(code)v=ai_add(v,ai_code(p,code));
        }
        if(!ai_draw(b,0,&r))return -2;
        v=ai_add(v,(int32_t)((r&0xFFFFu)%(2u*range+1u))-(int32_t)range);
        values[i]=v;
    }
    int best;
    if(!ai_argmax(b,values,count,&best))return -2;
    return cand[best<0?0:best];
}
/* fightTrainerAiGetFightPokemonIrekaeModosuValue: code 6 when no usable
 * move can affect any foe, code 7 when a damaging one would knock a foe
 * out; then fightTrainerAiSelectIrekaeDasuFightPokemon picks the Pokemon to
 * switch in (ai_select_dasu). Returns -1 on an RNG failure. */
static int ai_switch(C3GeneralBattle *b,const C3BattlePack *p,const uint8_t *ai,unsigned pos,
                     const uint8_t reserved[C3_GB_PARTY],C3GeneralCommand *c)
{
    uint8_t slots[4];unsigned n=ai_moves(b,p,pos,slots),foes[2],nf=ai_foes(b,pos,foes);
    C3GeneralMon *x=at(b,pos);int hittable=0,faster=0;int32_t value=0;
    for(unsigned i=0;i<nf;++i)for(unsigned k=0;k<n;++k){uint16_t id=x->moves[slots[k]];const uint8_t *mv=move_row(p,id);
        unsigned num,den;int hit=mv&&effectiveness(p,mv[2],at(b,foes[i]),&num,&den);
        if(hit)hittable=1;
        if(!mv||!be16(mv+22))continue;
        if(at(b,foes[i])->hp>=ai_damage(b,p,pos,foes[i],id))continue;
        if(hit)faster=1;}
    if(!hittable)value=ai_add(value,ai_code(p,6));
    if(faster)value=ai_add(value,ai_code(p,7));
    if(value<=0)return 0;
    int pick=ai_select_dasu(b,p,ai,pos/2,reserved);
    if(pick==-2)return -1;
    if(pick<0)return 0;
    c->kind=1;c->slot=(uint8_t)pick;c->target=0;return 1;
}
/* fn_802342CC for side s: fills cmd[s*2 + a] for its live positions.
 * Returns 0 on an RNG failure, -1 when the side has no AI row. */
static int order_actors(C3GeneralBattle *b,const C3BattlePack *p,const C3GeneralCommand *cmd,int use_pri,uint8_t ary[4]);
static int ai_side(C3GeneralBattle *b,const C3BattlePack *p,unsigned s,C3GeneralCommand cmd[4])
{
    const uint8_t *ai=ai_row(b,p,s);unsigned r;uint8_t done[4]={0};
    if(!ai)return -1;
    if(!ai_draw(b,0,&r))return 0;
    if(ai[7]>(r&0xFFFFu)%100u){
        uint8_t reserved[C3_GB_PARTY]={0}; /* fightTrainerCheckCanIrekae 3: already a switch target */
        for(unsigned a=0;a<b->active_count;++a){unsigned pos=s*2+a;int sw;
            if(!alive(b,pos))continue;
            if((sw=ai_switch(b,p,ai,pos,reserved,&cmd[pos]))<0)return 0;
            if(sw){done[pos]=1;reserved[cmd[pos].slot]=1;}}}
    if(b->info.use_item&&!ai_draw(b,0,&r))return 0;   /* item roll: no trainer items modelled */
    if(!ai_draw(b,0,&r))return 0;                     /* combo roll: fn_8024E534 returns 0 */
    uint8_t ary[4];
    if(!order_actors(b,p,NULL,0,ary))return 0;
    for(unsigned k=0;k<4;++k){unsigned pos=ary[k];
        if(pos>=4||pos/2!=s||done[pos])continue;
        if(!ai_pick_move(b,p,ai,pos,&cmd[pos]))return 0;}
    return 1;
}

/* ---- snagging ------------------------------------------------------------ */

static const char *const ball_name[13]={"","MASTER BALL","ULTRA BALL","GREAT BALL","POKE BALL",
    "SAFARI BALL","NET BALL","DIVE BALL","NEST BALL","REPEAT BALL","TIMER BALL","LUXURY BALL","PREMIER BALL"};
const char *c3_general_ball_name(unsigned ball){return ball>=1&&ball<=12?ball_name[ball]:"";}
static int snag_allowed(C3GeneralBattle *b,const C3BattlePack *p,unsigned pos,unsigned ball,unsigned target)
{
    C3ShadowInfo sh;const C3GeneralMon *y=target<4?cat(b,target):NULL;
    return !b->link&&b->snag&&pos/2==0&&target/2==1&&ball>=1&&ball<=12&&b->balls[ball]&&
           y&&y->hp&&y->shadow&&c3_battle_pack_shadow(p,y->shadow,&sh);
}
int c3_general_battle_can_snag(const C3GeneralBattle *b,const C3BattlePack *p,unsigned pos,unsigned ball,unsigned target)
{return b&&p&&b->magic==MAGIC&&pos<4&&alive((C3GeneralBattle *)b,pos)&&snag_allowed((C3GeneralBattle *)b,p,pos,ball,target);}
static uint32_t isqrt(uint32_t v){uint32_t r=0;while((r+1u)*(r+1u)<=v)++r;return r;}
/* fn_80213A78 (fightActionHokaku's catch): the Shadow table's catch rate,
 * the ball multiplier in tenths, the HP factor, x2 asleep or frozen, x1.5
 * poisoned, paralysed or burnt; 255 or more (or the Master Ball, or
 * encounter 0x11) is a catch, else four shake checks against
 * 0xFFFF0 / sqrt(sqrt(0xFF0000 / odds)) with the battle RNG. */
static int throw_ball(C3GeneralBattle *b,const C3BattlePack *p,unsigned a,C3GeneralCommand c)
{
    static const char *const broke[4]={"Oh, no! The POKEMON broke free!","Aww! It appeared to be caught!",
        "Aargh! Almost had it!","Shoot! It was so close, too!"};
    unsigned ball=c.slot,t=c.target,shakes=0;C3ShadowInfo sh;
    if(ball<1||ball>12)return 0;
    if(!b->balls[ball])return say(b,C3_GB_EVENT_MESSAGE,a,255,0,"There are no %ss left!",ball_name[ball]);
    --b->balls[ball];++b->balls_used[ball];
    say(b,C3_GB_EVENT_MESSAGE,a,t,0,"You threw a %s!",ball_name[ball]);
    C3GeneralMon *y=t<4?at(b,t):NULL;
    if(!y||!y->hp||!y->shadow||!c3_battle_pack_shadow(p,y->shadow,&sh))return say(b,C3_GB_EVENT_MESSAGE,a,t,0,"But there was no target...");
    if(ball==1||b->encounter==0x11)shakes=4;
    else{
        unsigned mult=10;
        switch(ball){
        case 2:mult=20;break;
        case 3:mult=15;break;
        case 6:mult=y->types[0]==11||y->types[1]==11||y->types[0]==6||y->types[1]==6?30:10;break;
        case 8:mult=y->level<40?(40u-y->level<10u?10u:40u-y->level):10u;break;
        case 10:mult=b->turn+9u>40u?40u:b->turn+9u;break; /* turns already taken + 10 */
        default:break; /* Dive needs the battle floor's water status; Repeat a Shadow already snagged */
        }
        uint32_t mx=y->max_hp?y->max_hp:1u,hp=y->hp;
        uint32_t odds=(uint32_t)sh.catch_rate*mult/10u*(mx*3u-hp*2u)/(mx*3u);
        if(y->status==C3_GB_SLEEP||y->status==C3_GB_FREEZE)odds*=2u;
        else if(y->status)odds=odds*15u/10u;
        if(odds>=255u)shakes=4;
        else if(odds){
            uint32_t q=isqrt(isqrt(0xff0000u/odds)),limit=q?0xffff0u/q:0xffffu;
            while(shakes<4){uint16_t v;if(!c3_battle_rng_next16(&b->rng,&v))return 0;if(v>=limit)break;++shakes;}
        }
    }
    if(shakes<4)return say(b,C3_GB_EVENT_MESSAGE,a,t,0,"%s",broke[shakes]);
    unsigned index=b->active[t/2][t%2];
    y->ball=(uint8_t)ball;y->hp=0;y->participated=0;b->caught[index]=(uint8_t)ball;
    return say(b,C3_GB_EVENT_FAINT,t,t,0,"Gotcha! %s was snagged!",y->name);
}

/* ---- the bag ------------------------------------------------------------- */

/* The item row's battleUseFunc (ItemDataBios +0x20, main.dol 0x80363CE8
 * rows): fn_80023B9C medicine on a party Pokemon, fn_80023968 PP restore
 * (a move with itemParamGetPPSelectFlag), fn_8002217C X items / Dire Hit /
 * Guard Spec on the Pokemon choosing, fn_80023DA8 message 0x44C6 only,
 * fn_80021B14 the Enigma Berry; 0 none. Balls (fn_80022050) are balls[]. */
uint8_t c3_general_bag_use_class(uint32_t f)
{
    switch(f){
    case 0x80023B9Cu:return C3_GB_USE_MEDICINE;
    case 0x80023968u:return C3_GB_USE_PP;
    case 0x8002217Cu:return C3_GB_USE_BATTLE;
    case 0:return C3_GB_USE_NONE;
    default:return C3_GB_USE_REFUSED;
    }
}
int c3_general_bag_item_selects_move(const C3GeneralBagItem *e){return e&&(e->param[4]>>5&1u);}
/* What fn_80144574 did to the Pokemon (its ItemUsePokemonLog types). */
enum { IL_SLEEP=1u<<0, IL_POISON=1u<<1, IL_BURN=1u<<2, IL_FREEZE=1u<<3, IL_PARALYSIS=1u<<4,
       IL_CONFUSION=1u<<5, IL_REVIVE=1u<<6, IL_HP=1u<<7, IL_PP=1u<<8, IL_STAGE=1u<<9 };
typedef struct { unsigned mask, hp; int8_t stage[7]; } ItemLog;
/* fn_80144574 for the effects a battle item has (itemParamGet* layout):
 * +0 bits 1-4 Attack up, bit 5 Dire Hit, bit 7 infatuation; +1 Defense
 * (high) / Speed (low) up; +2 accuracy (high) / Sp. Atk (low) up; +3 cures
 * bit 0 confusion, 1 paralysis, 2 freeze, 3 burn, 4 poison, 5 sleep, bit 7
 * Guard Spec; +4 bit 5 PP-selective, bit 6 Revive; +10 HP (255 all, 254
 * half; Revive only on a fainted Pokemon, the rest only on a living one);
 * +11 PP for the chosen move or all four (not Struggle or Shadow Rush).
 * Stages rise only on a Pokemon that is out, to +6. Returns whether
 * anything changed. Dire Hit, Guard Spec and infatuation are not modelled
 * (item_unmodelled). */
static int item_effect(C3GeneralMon *y,int out,const C3GeneralBagItem *e,unsigned move,ItemLog *log)
{
    const uint8_t *q=e->param;
    memset(log,0,sizeof *log);
    if(out&&y->hp){
        static const uint8_t stage_of[5]={0,1,4,5,2};
        unsigned boost[5]={q[0]>>1&15u,q[1]>>4,q[1]&15u,q[2]>>4,q[2]&15u};
        for(unsigned k=0;k<5;++k){int s=y->stages[stage_of[k]];
            if(!boost[k]||s>=6)continue;
            int next=s+(int)boost[k];if(next>6)next=6;
            log->stage[stage_of[k]]=(int8_t)(next-s);y->stages[stage_of[k]]=(int8_t)next;log->mask|=IL_STAGE;}
    }
    if((q[3]&0x20)&&y->status==C3_GB_SLEEP){y->status=0;y->status_turns=0;log->mask|=IL_SLEEP;}
    if((q[3]&0x10)&&(y->status==C3_GB_POISON||y->status==C3_GB_TOXIC)){y->status=0;y->toxic_count=0;log->mask|=IL_POISON;}
    if((q[3]&0x08)&&y->status==C3_GB_BURN){y->status=0;log->mask|=IL_BURN;}
    if((q[3]&0x04)&&y->status==C3_GB_FREEZE){y->status=0;log->mask|=IL_FREEZE;}
    if((q[3]&0x02)&&y->status==C3_GB_PARALYSIS){y->status=0;log->mask|=IL_PARALYSIS;}
    if((q[3]&0x01)&&y->confusion){y->confusion=0;log->mask|=IL_CONFUSION;}
    {   unsigned boost=q[10];int revive=q[4]>>6&1u;
        if(boost&&y->hp<y->max_hp&&(y->hp==0)==revive){
            unsigned gain=boost==255?y->max_hp:boost==254?(y->max_hp/2u?y->max_hp/2u:1u):boost==253?0u:boost;
            unsigned next=y->hp+gain;if(next>y->max_hp)next=y->max_hp;
            if(next>y->hp){log->hp=next-y->hp;y->hp=(uint16_t)next;log->mask|=revive?IL_REVIVE:IL_HP;}
        }
    }
    if(q[11]){
        for(unsigned i=0;i<4;++i){
            if((q[4]&0x20)&&i!=move)continue;
            if(!y->moves[i]||y->moves[i]==STRUGGLE||y->moves[i]==SHADOW_RUSH||y->pp[i]>=y->pp_max[i])continue;
            unsigned next=y->pp[i]+q[11];y->pp[i]=(uint8_t)(next>y->pp_max[i]?y->pp_max[i]:next);log->mask|=IL_PP;
        }
    }
    return log->mask!=0;
}
static int item_unmodelled(const C3GeneralBagItem *e){return (e->param[0]&0xA0u)||(e->param[3]&0x80u);}
/* The battle position the hero's party member is out at, else 255. */
static unsigned hero_position(const C3GeneralBattle *b,unsigned party)
{for(unsigned a=0;a<b->active_count;++a)if(b->active[0][a]==party)return a;return NO_MON;}
C3GeneralItemCheck c3_general_battle_item_check(const C3GeneralBattle *cb,unsigned pos,unsigned bag,unsigned party,
                                                unsigned move,unsigned reserved)
{
    C3GeneralBattle *b=(C3GeneralBattle *)cb;
    if(!b||b->magic!=MAGIC||pos>=2||!alive(b,pos)||b->link||b->info.cpu_side0||bag>=b->bag_count||bag>=C3_GB_BAG)
        return C3_GB_ITEM_INVALID;
    if(!b->info.use_item)return C3_GB_ITEM_NOT_NOW;
    const C3GeneralBagItem *e=&b->bag[bag];
    if(e->count<=reserved)return C3_GB_ITEM_NONE_LEFT;
    if(e->use!=C3_GB_USE_MEDICINE&&e->use!=C3_GB_USE_PP&&e->use!=C3_GB_USE_BATTLE)return C3_GB_ITEM_CANT_USE;
    if(e->use==C3_GB_USE_BATTLE)party=b->active[0][pos];
    if(party>=b->party_count[0]||!b->party[0][party].species)return C3_GB_ITEM_INVALID;
    const C3GeneralMon *y=&b->party[0][party];
    /* fn_80121ADC(pokemon, 0x3E): a Pokemon in Hyper Mode takes no item. */
    if(y->hyper)return C3_GB_ITEM_HYPER;
    if(c3_general_bag_item_selects_move(e)&&(move>3||!y->moves[move]))return C3_GB_ITEM_NEED_MOVE;
    if(item_unmodelled(e))return C3_GB_ITEM_CANT_USE;
    C3GeneralMon copy=*y;ItemLog log;
    return item_effect(&copy,hero_position(b,party)!=NO_MON,e,move,&log)?C3_GB_ITEM_OK:C3_GB_ITEM_NO_EFFECT;
}
/* fn_80211E18 for the hero's item: "<hero> used <item>!" (trainer name
 * control 0x13, item name 0x29), then what it did (fn_800216E8's choice,
 * table lbl_80266C7C: the first effect in the order sleep, poison, burn,
 * freeze, paralysis, confusion, Revive, HP, PP gives the line; a cure and
 * a second effect of another group give message 0x4201; X items use
 * lbl_80266D78). The item leaves the bag here, when the action runs, not
 * when it was chosen; a Pokemon that fainted first never runs it. */
static int use_bag_item(C3GeneralBattle *b,unsigned a,C3GeneralCommand c)
{
    if(c.slot>=b->bag_count)return 1;
    C3GeneralBagItem *e=&b->bag[c.slot];
    unsigned party=e->use==C3_GB_USE_BATTLE?b->active[0][a]:c.target&15u;
    if(!e->count||party>=b->party_count[0])return 1;
    C3GeneralMon *y=&b->party[0][party];
    const char *w=b->side_name[0][0]?b->side_name[0]:"Player";
    if(!say(b,C3_GB_EVENT_MESSAGE,a,255,0,"%s used %s %s!",w,strchr("AEIOU",e->name[0])?"an":"a",e->name))return 0;
    unsigned pos=hero_position(b,party);ItemLog log;C3GeneralMon before=*y;
    if(y->hyper||item_unmodelled(e)||!item_effect(y,pos!=NO_MON,e,c.target>>4,&log)){
        *y=before;return say(b,C3_GB_EVENT_MESSAGE,a,255,0,"It won't have any effect.");}
    --e->count;++e->used;
    if(log.mask&IL_STAGE){
        static const char *const rose[7]={"ATTACK","DEFENSE","SP. ATK","SP. DEF","SPEED","accuracy","evasiveness"};
        for(unsigned s=0;s<7;++s)if(log.stage[s]){unsigned first=b->event_count;
            if(!say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%s's %s %srose!",y->name,rose[s],log.stage[s]>1?"sharply ":""))return 0;
            if(b->event_count>first){b->events[first].stat=(int8_t)s;b->events[first].stat_delta=log.stage[s];}}
    }
    static const struct { unsigned bit; int group; const char *text; } line[9]={
        {IL_SLEEP,0," woke up!"},{IL_POISON,1," was cured of its poisoning."},{IL_BURN,2,"'s burn was healed."},
        {IL_FREEZE,3," was thawed out."},{IL_PARALYSIS,4," was cured of paralysis."},
        {IL_CONFUSION,5," snapped out of its confusion."},{IL_REVIVE,-1," was revived!"},
        {IL_HP,7,"'s HP was restored."},{IL_PP,-1,"'s PP was restored."}};
    unsigned k=0;
    while(k<9&&!(log.mask&line[k].bit))++k;
    if(k==9)return 1;
    const char *text=line[k].text;
    if(line[k].group>=0)for(unsigned j=k+1;j<9;++j)
        if((log.mask&line[j].bit)&&line[j].group>=0&&line[j].group!=line[k].group){text="'s HP and condition were restored.";break;}
    /* An HP change on a Pokemon that is out moves its gauge. */
    int gauge=pos!=NO_MON&&(log.mask&(IL_HP|IL_REVIVE));
    return say(b,gauge?C3_GB_EVENT_HP:C3_GB_EVENT_MESSAGE,gauge?pos:a,gauge?pos:255,0,"%s%s",y->name,text);
}

/* ---- turns --------------------------------------------------------------- */
int c3_general_battle_choose(C3GeneralBattle *b,const C3BattlePack *p,unsigned pos,C3GeneralCommand *c)
{
    if(!b||!c||b->magic!=MAGIC||!alive(b,pos))return 0;
    {   /* the original trainer AI for an AI trainer's position */
        C3GeneralCommand all[4];memset(all,0,sizeof all);
        int r=ai_side(b,p,pos/2,all);
        if(r==0)return 0;
        if(r>0){*c=all[pos];return 1;}
    }
    uint8_t mask=c3_general_battle_legal_moves(b,p,pos);unsigned slots[4],n=0,r,foes[2],nf=0;
    for(unsigned i=0;i<4;++i)if(mask>>i&1u)slots[n++]=i;
    c->kind=0;c->slot=4;
    if(n){if(!roll(b,n,&r))return 0;c->slot=(uint8_t)slots[r];}
    unsigned foe=1-pos/2;
    for(unsigned i=0;i<b->active_count;++i)if(alive(b,foe*2+i))foes[nf++]=foe*2+i;
    if(!nf)return 0;
    if(!roll(b,nf,&r))return 0;
    c->target=(uint8_t)foes[r];return 1;
}
static int command_valid(C3GeneralBattle *b,const C3BattlePack *p,unsigned pos,C3GeneralCommand c)
{
    if(!alive(b,pos))return 1;
    if(c.kind==1){
        unsigned s=pos/2;
        if(c.slot>=b->party_count[s]||!b->party[s][c.slot].hp)return 0;
        if(b->owner_split&&b->owner[s][c.slot]!=pos%2)return 0;
        for(unsigned a=0;a<b->active_count;++a)if(b->active[s][a]==c.slot)return 0;
        return 1;
    }
    if(c.kind==2)return snag_allowed(b,p,pos,c.slot,c.target);
    if(c.kind==3)return c3_general_battle_can_call(b,pos);
    if(c.kind==4)return c3_general_battle_item_check(b,pos,c.slot,c.target&15u,c.target>>4,0)==C3_GB_ITEM_OK;
    if(c.kind!=0||c.target>=4)return 0;
    uint8_t mask=c3_general_battle_legal_moves(b,p,pos);
    return c.slot==4?!mask:c.slot<4&&(mask>>c.slot&1u);
}
static int replace_fainted(const C3BattlePack *p,C3GeneralBattle *b)
{
    for(unsigned s=0;s<2;++s)for(unsigned a=0;a<b->active_count;++a){
        uint8_t i=b->active[s][a];
        if(i!=NO_MON&&b->party[s][i].hp)continue;
        if(s==1&&i!=NO_MON&&!b->link&&!b->caught[i])award(p,b,i);
        const uint8_t *ai=ai_row(b,p,s);
        int n=ai?ai_select_dasu(b,p,ai,s,NULL):next_healthy(b,s,a);
        if(n==-2)return 0;
        b->active[s][a]=n<0?NO_MON:(uint8_t)n;
        if(n>=0){announce_send(b,s,a,&b->party[s][n]);sent_out(b,p,s,(unsigned)n);dark_check_appear(b,s*2+a);}
    }
    mark_faced(b);return 1;
}
static int order_actors(C3GeneralBattle *b,const C3BattlePack *p,const C3GeneralCommand *cmd,int use_pri,uint8_t ary[4]);
/* fn_80213270: the side counters first (Reflect, Light Screen:
 * fn_802317E4), then each Pokemon in speed order (fn_80230568: poison /8,
 * bad poison max/16 x count, burn /8, each stopping at a faint), then the
 * turn's new Quick Claw roll. */
static int end_of_turn(C3GeneralBattle *b,const C3BattlePack *p)
{
    static const char *const screen[2]={"Reflect","Light Screen"};
    static const char *const team[2]={"Your team","The foe team"};
    for(unsigned s=0;s<2;++s)for(unsigned k=0;k<2;++k)if(b->screens[s][k]&&!--b->screens[s][k])
        say(b,C3_GB_EVENT_MESSAGE,255,255,0,"%s: %s wore off!",team[s!=b->viewer],screen[k]);
    for(unsigned pos=0;pos<4;++pos){C3GeneralMon *m=at(b,pos);if(m){m->flinch=0;m->protect=0;}}
    uint8_t ary[4];
    if(!order_actors(b,p,NULL,0,ary))return 0;
    for(unsigned k=0;k<4;++k){unsigned pos=ary[k];C3GeneralMon *m=pos<4?at(b,pos):NULL;
        if(!m||!m->hp)continue;
        if(m->status==C3_GB_POISON||m->status==C3_GB_BURN){
            if(!say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%s is hurt by its %s!",who(b,pos),m->status==C3_GB_BURN?"burn":"poison")||
               !hurt(b,pos,m->max_hp/8u,0))return 0;}
        else if(m->status==C3_GB_TOXIC){if(m->toxic_count<15)++m->toxic_count;
            if(!say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%s is hurt by poison!",who(b,pos))||
               !hurt(b,pos,(m->max_hp/16u?m->max_hp/16u:1u)*m->toxic_count,0))return 0;}
        if(m->hp&&m->hyper&&!say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%s is in Hyper Mode!",m->name))return 0;
    }
    return c3_battle_rng_next16(&b->rng,&b->quick_claw_roll);
}
int c3_general_battle_command_valid(const C3GeneralBattle *b,const C3BattlePack *p,unsigned pos,C3GeneralCommand c)
{return b&&p&&b->magic==MAGIC&&pos<4&&command_valid((C3GeneralBattle *)b,p,pos,c);}
/* lbl_80375BB8: the action kind's bracket (Run 0, Item 1, Call 2,
 * Switch 3, Move 4); a ball and a bag item are items. */
static int action_bracket(const C3GeneralCommand *c){return c->kind==1?3:c->kind==2||c->kind==4?1:c->kind==3?2:4;}
static int8_t command_priority(C3GeneralBattle *b,const C3BattlePack *p,unsigned pos,const C3GeneralCommand *c)
{
    const C3GeneralMon *m=at(b,pos);
    if(!m||c->kind)return 0;   /* fightOutPokemonGetCmpNimblenessWazaDataId: no move, priority 0 */
    const uint8_t *mv=move_row(p,c->slot==4?STRUGGLE:m->moves[c->slot&3]);
    return mv?(int8_t)mv[0]:0;
}
/* fightFloorCmpfightOutPokemonNimbleness (fight_floor.c:1712): 1 when x
 * acts first. Move priority counts when either is non-zero, then the
 * higher speed, then fn_800E0C54() & 1. */
static int cmp_nimbleness(C3GeneralBattle *b,const C3BattlePack *p,const C3GeneralCommand *cmd,int use_pri,unsigned x,unsigned y,int *first)
{
    uint32_t sx=speed_of(b,at(b,x)),sy=speed_of(b,at(b,y));
    int8_t px=use_pri?command_priority(b,p,x,&cmd[x]):0,py=use_pri?command_priority(b,p,y,&cmd[y]):0;
    uint16_t v;
    if(px||py){if(px>py){*first=1;return 1;}if(px<py){*first=0;return 1;}}
    if(sx!=sy){*first=sx>sy;return 1;}
    if(!c3_battle_rng_next16(&b->rng,&v))return 0;
    *first=v&1u;return 1;
}
/* fightFloorSortFightOutPokemonPtrArySub (fight_floor.c:1559) over the
 * four positions, empty or fainted ones as NULL (255): the exchange sort,
 * bracket first when use_pri, then the comparison above. */
static int order_actors(C3GeneralBattle *b,const C3BattlePack *p,const C3GeneralCommand *cmd,int use_pri,uint8_t ary[4])
{
    for(unsigned pos=0;pos<4;++pos)ary[pos]=alive(b,pos)?(uint8_t)pos:NO_MON;
    for(unsigned i=0;i<3;++i)for(unsigned j=i+1;j<4;++j){
        uint8_t t;
        if(ary[i]==NO_MON&&ary[j]==NO_MON)continue;
        if(ary[i]==NO_MON){t=ary[i];ary[i]=ary[j];ary[j]=t;continue;}
        if(ary[j]==NO_MON)continue;
        int p1=use_pri?action_bracket(&cmd[ary[i]]):0,p2=use_pri?action_bracket(&cmd[ary[j]]):0,first;
        if(p1<p2)continue;
        if(p1>p2){t=ary[i];ary[i]=ary[j];ary[j]=t;continue;}
        if(!cmp_nimbleness(b,p,cmd,use_pri,ary[i],ary[j],&first))return 0;
        if(!first){t=ary[i];ary[i]=ary[j];ary[j]=t;}
    }
    return 1;
}
/* The prize opcode 0x5D (fn_8021E754): mult x SyoukinBairitu (the
 * opponent trainer kind's prize base) x best x the hero's OkaneBai, where
 * - mult is 4 with okanePool (floor status 0x26; that amount goes to
 *   scriptAddPremium, the Premium pool, instead of money and no line is
 *   shown), else 2 when the fight type sends out more than one Pokemon a
 *   side (floor status 0x18, fightoutPokemonNum), else 1;
 * - best is the highest level among the opponent trainer's valid Pokemon
 *   (fainted ones included) and the hero's Pokemon with CatchEntryFlag
 *   (status 0xCF: snagged this battle, fn_80213A78), which are opponent
 *   Pokemon too, so every opponent party member counts once;
 * - OkaneBai is okane_bai (1, or 2 with the Amulet Coin). */
static int pays_prize(const C3GeneralBattle *b)
{return b->outcome==C3_GB_WON&&b->info.get_money&&!b->info.money_pool;}
static int pays_premium(const C3GeneralBattle *b)
{return b->outcome==C3_GB_WON&&b->info.get_money&&b->info.money_pool;}
static uint32_t prize_money(const C3GeneralBattle *b)
{
    unsigned best=0;for(unsigned i=0;i<b->party_count[1];++i)if(b->party[1][i].level>best)best=b->party[1][i].level;
    uint32_t mult=b->info.money_pool?4u:b->info.active_per_side>1?2u:1u,bai=b->okane_bai?b->okane_bai:1u;
    return mult*((uint32_t)b->info.prize_base*(best*bai));
}
/* The won trainer battle's prize line, fight_common.fsys message 0x7546
 * "{13} got ${4b} for winning!". fightActionFlowSyuuryou runs the post-win
 * script 0x80378801 after the defeat line (0x766C) and the trainer's lose
 * speech; its prize opcode pays the fight hero, sets 0x13 to the hero's name
 * and 0x4B (msgctrlMoney: thousands commas) to the amount, then prints it.
 * The opcode skips the line when getOkane is clear, with okanePool (the
 * amount goes to the Premium pool) or when side 0 has no hero (a CPU
 * trainer; link battles). Without a hero name (side_name[0], from
 * c3_battle_story_prepare) the fight text's own "Player" stands in. */
static void say_prize(C3GeneralBattle *b)
{
    char digits[16],money[24];unsigned n=0,k=0;uint32_t v=prize_money(b);
    do{digits[n++]=(char)('0'+v%10u);v/=10u;}while(v);
    while(n){money[k++]=digits[--n];if(n&&n%3==0)money[k++]=',';}
    money[k]=0;
    say(b,C3_GB_EVENT_MESSAGE,255,255,0,"%s got $%s for winning!",b->side_name[0][0]?b->side_name[0]:"Player",money);
}
/* link: player holds all four positions' commands and nothing is drawn
 * for the opponent; otherwise player[0..1] is the hero and the opponent
 * policy picks side 1's commands from the battle RNG. */
static int turn_core(C3GeneralBattle *out,const C3BattlePack *p,const C3GeneralCommand *player,int link,char *e)
{
    if(!out||!p||!player||out->magic!=MAGIC||out->outcome!=C3_GB_RUNNING||out->turn>=C3_GB_MAX_TURNS)
        return fail(e,"Invalid or finished battle.");
    for(unsigned pos=0;pos<4;++pos){
        if(!link&&(pos/2||out->info.cpu_side0))continue;
        if(pos%2<out->active_count&&!command_valid(out,p,pos,player[pos]))return fail(e,"Choose a usable move or a healthy Pokemon.");
    }
    /* The hero's two commands together (fight_menu.c SubItem): one ball a
     * turn (fightTrainerIsSelectedItemBall, message 0x426D), and the same
     * bag item twice only while two are left. */
    if(!link&&!out->info.cpu_side0&&out->active_count==2&&alive(out,0)&&alive(out,1)){
        if(player[0].kind==2&&player[1].kind==2)return fail(e,"Only one ball can be thrown a turn.");
        if(player[0].kind==4&&player[1].kind==4&&player[0].slot==player[1].slot&&
           c3_general_battle_item_check(out,1,player[1].slot,player[1].target&15u,player[1].target>>4,1)!=C3_GB_ITEM_OK)
            return fail(e,"Choose a usable move or a healthy Pokemon.");
    }
    C3GeneralBattle *b=malloc(sizeof *b);if(!b)return fail(e,"Turn allocation failed.");
    *b=*out;b->event_count=0;++b->turn;
    C3GeneralCommand cmd[4]={{0}};uint8_t order[4],count=0,acted[4]={0};int ok=0;
    for(unsigned s=0;s<2;++s){
        int human=link||(s==0&&!b->info.cpu_side0);
        int r=human?-1:ai_side(b,p,s,cmd);
        if(!r)goto failed;
        for(unsigned a=0;a<b->active_count;++a){unsigned pos=s*2+a;
            if(!alive(b,pos))continue;
            if(human)cmd[pos]=player[pos];
            else if(r<0&&!c3_general_battle_choose(b,p,pos,&cmd[pos]))goto failed;}}
    if(!order_actors(b,p,cmd,1,order))goto failed;
    count=4;
    for(unsigned i=0;i<count;++i){unsigned pos=order[i];
        if(pos>=4)continue;
        if(!alive(b,pos)){acted[pos]=1;continue;}
        if(cmd[pos].kind==1){
            say(b,C3_GB_EVENT_SWITCH,pos,255,0,"%s, come back!",who(b,pos));
            C3GeneralMon *old=at(b,pos);if(old){memset(old->stages,0,sizeof old->stages);old->confusion=0;}
            b->active[pos/2][pos%2]=cmd[pos].slot;mark_faced(b);sent_out(b,p,pos/2,cmd[pos].slot);
            announce_send(b,pos/2,pos%2,&b->party[pos/2][cmd[pos].slot]);dark_check_appear(b,pos);
        }else if(cmd[pos].kind==2){if(!throw_ball(b,p,pos,cmd[pos]))goto failed;}
        else if(cmd[pos].kind==3){if(!call_pokemon(b,p,pos))goto failed;}
        else if(cmd[pos].kind==4){if(!use_bag_item(b,pos,cmd[pos]))goto failed;}
        else if(!use_move(b,p,pos,cmd[pos],acted))goto failed;
        acted[pos]=1;
        check_outcome(b);if(b->outcome!=C3_GB_RUNNING)break;
    }
    if(b->outcome==C3_GB_RUNNING){if(!end_of_turn(b,p))goto failed;check_outcome(b);}
    if(!replace_fainted(p,b))goto failed;
    if(b->outcome==C3_GB_RUNNING&&b->turn>=C3_GB_MAX_TURNS)b->outcome=C3_GB_DRAW;
    if(b->outcome!=C3_GB_RUNNING&&b->link){
        unsigned foe=1u-b->viewer;int won=b->outcome==(b->viewer?C3_GB_LOST:C3_GB_WON);
        if(b->outcome==C3_GB_DRAW)say(b,C3_GB_EVENT_OUTCOME,255,255,0,"The battle with %s ended in a draw.",b->side_name[foe]);
        else say(b,C3_GB_EVENT_OUTCOME,255,255,0,won?"You won the battle against %s!":"You lost the battle against %s...",b->side_name[foe]);
    }else if(b->outcome!=C3_GB_RUNNING&&b->info.cpu_side0)
        say(b,C3_GB_EVENT_OUTCOME,255,255,0,b->outcome==C3_GB_DRAW?"The battle ended in a draw.":"%s %s won the battle!",
            b->outcome==C3_GB_WON?b->info.side0_title:b->info.title,b->outcome==C3_GB_WON?b->info.side0_trainer:b->info.trainer);
    else if(b->outcome!=C3_GB_RUNNING){
        say(b,C3_GB_EVENT_OUTCOME,255,255,0,b->outcome==C3_GB_WON?"You defeated %s %s!":
            b->outcome==C3_GB_LOST?"You lost to %s %s...":"The battle with %s %s ended in a draw.",b->info.title,b->info.trainer);
        if(pays_prize(b))say_prize(b);
    }
    *out=*b;ok=1;if(e)e[0]=0;
    goto done;
failed:fail(e,"Battle turn failed: random-number or table bounds.");
done:free(b);return ok;
}
int c3_general_battle_turn(C3GeneralBattle *out,const C3BattlePack *p,const C3GeneralCommand player[2],char *e)
{
    C3GeneralCommand all[4]={{0}};
    if(!player)return fail(e,"Invalid or finished battle.");
    all[0]=player[0];all[1]=player[1];
    return turn_core(out,p,all,0,e);
}
int c3_general_battle_turn_link(C3GeneralBattle *out,const C3BattlePack *p,const C3GeneralCommand commands[4],char *e)
{
    if(!out||!out->link)return fail(e,"Not a link battle.");
    return turn_core(out,p,commands,1,e);
}

/* ---- link state hash and mirror ------------------------------------------- */

uint32_t c3_general_battle_hash(const C3GeneralBattle *b)
{
    uint8_t buf[64];uint32_t h=2166136261u;size_t n;
#define MIX(len) do{for(size_t q=0;q<(len);++q){h^=buf[q];h*=16777619u;}}while(0)
    if(!b)return 0;
    n=0;put32(buf,b->turn);put32(buf+4,(uint32_t)b->outcome);put32(buf+8,b->rng.seed);
    put32(buf+12,(uint32_t)(b->rng.draws>>32));put32(buf+16,(uint32_t)b->rng.draws);
    buf[20]=b->active_count;buf[21]=b->party_count[0];buf[22]=b->party_count[1];
    memcpy(buf+23,b->active,4);memcpy(buf+27,b->screens,4);n=31;MIX(n);
    for(unsigned s=0;s<2;++s)for(unsigned i=0;i<b->party_count[s]&&i<C3_GB_PARTY;++i){
        const C3GeneralMon *m=&b->party[s][i];
        put16(buf,m->species);put16(buf+2,m->hp);put16(buf+4,m->max_hp);
        buf[6]=m->status;buf[7]=m->status_turns;buf[8]=m->toxic_count;buf[9]=m->confusion;
        buf[10]=m->flinch;buf[11]=m->protect;buf[12]=m->recharge;buf[13]=m->level;
        memcpy(buf+14,m->stages,7);memcpy(buf+21,m->pp,4);
        for(unsigned k=0;k<4;++k)put16(buf+25+2*k,m->moves[k]);
        n=33;MIX(n);
    }
#undef MIX
    return h;
}
static uint8_t flip(uint8_t pos){return pos<4?(uint8_t)(pos^2u):pos;}
void c3_general_battle_mirror(const C3GeneralBattle *in,C3GeneralBattle *out)
{
    if(!in||!out)return;
    if(out!=in)*out=*in;
    C3GeneralMon tmp[C3_GB_PARTY];
    memcpy(tmp,out->party[0],sizeof tmp);memcpy(out->party[0],out->party[1],sizeof tmp);memcpy(out->party[1],tmp,sizeof tmp);
    uint8_t t;
    t=out->party_count[0];out->party_count[0]=out->party_count[1];out->party_count[1]=t;
    for(unsigned k=0;k<2;++k){t=out->active[0][k];out->active[0][k]=out->active[1][k];out->active[1][k]=t;
        t=out->screens[0][k];out->screens[0][k]=out->screens[1][k];out->screens[1][k]=t;}
    for(unsigned i=0;i<C3_GB_PARTY;++i){t=out->owner[0][i];out->owner[0][i]=out->owner[1][i];out->owner[1][i]=t;}
    char name[C3_GB_NAME];
    memcpy(name,out->side_name[0],C3_GB_NAME);memcpy(out->side_name[0],out->side_name[1],C3_GB_NAME);memcpy(out->side_name[1],name,C3_GB_NAME);
    if(out->outcome==C3_GB_WON)out->outcome=C3_GB_LOST;else if(out->outcome==C3_GB_LOST)out->outcome=C3_GB_WON;
    out->viewer^=1u;
    for(unsigned i=0;i<out->info.party_count&&i<C3_GB_PARTY;++i){out->info.species[i]=0;out->info.levels[i]=0;}
    out->info.party_count=out->party_count[1];
    for(unsigned i=0;i<out->party_count[1];++i){out->info.species[i]=out->party[1][i].species;out->info.levels[i]=out->party[1][i].level;}
    for(uint32_t i=0;i<out->event_count;++i){C3GeneralEvent *ev=&out->events[i];
        ev->actor=flip(ev->actor);ev->target=flip(ev->target);
        for(unsigned k=0;k<2;++k){uint16_t x=ev->hp[k];ev->hp[k]=ev->hp[k+2];ev->hp[k+2]=x;
            x=ev->max_hp[k];ev->max_hp[k]=ev->max_hp[k+2];ev->max_hp[k+2]=x;
            uint8_t y=ev->mon[k];ev->mon[k]=ev->mon[k+2];ev->mon[k+2]=y;}}
}

/* ---- writeback ----------------------------------------------------------- */

static C3MoveForgetPrompt forget_prompt;static void *forget_context;
void c3_general_set_forget_prompt(C3MoveForgetPrompt prompt,void *context){forget_prompt=prompt;forget_context=context;}
int c3_battle_pack_move_is_hm(const C3BattlePack *p,uint16_t id){const uint8_t *m=p&&p->bytes?move_row(p,id):NULL;return m&&m[0x12]==1;}
int c3_battle_pack_set_move(const C3BattlePack *p,uint8_t r[C3_GB_POKEMON_SIZE],unsigned slot,uint16_t id)
{
    if(!p||!r||slot>3||!move_row(p,id))return 0;
    uint8_t *w=r+0x78+4*slot;put16(w,id);w[2]=(uint8_t)move_pp(p,id,0);w[3]=0;return 1;
}
/* A level's move for record r (four known: the prompt). Returns the slot it
 * went into (forgot = the move replaced, 0 for a free slot), or -1. */
static int learn_move(const C3BattlePack *p,uint8_t r[C3_GB_POKEMON_SIZE],uint16_t id,uint16_t *forgot)
{
    int free_slot=-1;*forgot=0;
    for(unsigned k=0;k<4;++k){uint16_t have=be16(r+0x78+4*k);if(!have&&free_slot<0)free_slot=(int)k;}
    if(free_slot<0){
        if(!forget_prompt)return -1;
        int slot=forget_prompt(forget_context,p,r,id);
        if(slot<0||slot>3||c3_battle_pack_move_is_hm(p,be16(r+0x78+4*slot)))return -1;
        *forgot=be16(r+0x78+4*slot);free_slot=slot;
    }
    c3_battle_pack_set_move(p,r,(unsigned)free_slot,id);
    return free_slot;
}
static uint32_t exp_for(const C3BattlePack *p,unsigned group,unsigned level)
{return be32(p->bytes+p->growth_at+(group<8?group:0)*GROWTH_ROW+(level>100?100:level)*4u);}
int c3_general_battle_writeback(const C3GeneralBattle *b,const C3BattlePack *p,
    uint8_t party[C3_GB_PARTY][C3_GB_POKEMON_SIZE],C3GeneralRewards *rw)
{
    if(!b||!p||!party||!rw||b->magic!=MAGIC||b->outcome==C3_GB_RUNNING)return 0;
    C3GeneralRewards out;memset(&out,0,sizeof out);
    out.result_code=b->outcome==C3_GB_WON?2u:3u;
    out.wipeout=b->outcome!=C3_GB_WON&&b->info.wipeout;
    memcpy(out.balls_used,b->balls_used,sizeof out.balls_used);
    for(unsigned i=0;i<b->party_count[1];++i)if(b->caught[i])out.caught[out.caught_count++]=(uint8_t)i;
    /* A CPU trainer fought on the hero's side: the hero copy is untouched
     * and the result says which side won (2 side 0, 3 side 1). */
    if(b->info.cpu_side0){out.wipeout=0;*rw=out;return 1;}
    /* scriptAddPremium writes the save's flag 0A9E, not the fight hero
     * copy, so backSaveDataFlag does not discard it. */
    if(pays_premium(b))out.premium=prize_money(b);
    for(unsigned i=0;i<b->party_count[0];++i)
        if(be16(party[b->party[0][i].party_slot])!=b->party[0][i].species)return 0;
    /* backSaveDataFlag clear: the hero copy (party, money) is discarded. */
    if(!b->info.back_save){*rw=out;return 1;}
    out.written=1;
    if(pays_prize(b))out.prize=prize_money(b);
    for(unsigned i=0;i<b->party_count[0];++i){
        const C3GeneralMon *m=&b->party[0][i];uint8_t *r=party[m->party_slot];unsigned slot=m->party_slot;
        uint32_t before=be32(r+0x5c),gain=m->exp>before?m->exp-before:0;
        put16(r+0x8a,m->hp);
        for(unsigned j=0;j<4;++j)if(m->own_moves[j])r[0x7a+4*j]=own_pp(m,j);
        /* Persistent condition: a fainted Pokemon keeps none. */
        {unsigned id=m->hp?status_to_record(m->status):0;
         if(id!=be16(r+0x64)){memset(r+0x64,0,7);memset(r+0x6c,0,6);put16(r+0x64,id);}}
        for(unsigned j=0;j<6;++j)if(m->ev_gain[j])put16(r+0x98+2*j,be16(r+0x98+2*j)+m->ev_gain[j]);
        if(m->shadow&&m->dp_max>0)put32(r+0xdc,(uint32_t)m->dp);
        out.exp[slot]=gain;
        if(!gain)continue;
        if(m->shadow){put32(r+0xe0,be32(r+0xe0)+gain);continue;}
        uint32_t cap=exp_for(p,m->growth,100),exp=before+gain>cap?cap:before+gain;
        unsigned level=r[0x60],from=level;
        while(level<100&&exp>=exp_for(p,m->growth,level+1))++level;
        put32(r+0x5c,exp);
        if(level!=from){
            const uint8_t *s=species_row(p,m->species);unsigned nature=be32(r+4)%25u;
            unsigned old_max=be16(r+0x8c);
            for(unsigned j=0;j<6;++j)
                put16(r+0x8c+2*j,(unsigned)compute_stat(j,be16(s+0x84+2*j),be16(r+0xa4+2*j)&31u,be16(r+0x98+2*j),level,nature));
            unsigned new_max=be16(r+0x8c),hp=be16(r+0x8a);
            if(hp)hp+=new_max>old_max?new_max-old_max:0;
            put16(r+0x8a,hp>new_max?new_max:hp);
            out.levels_gained[slot]=(uint8_t)(level-from);
            r[0x60]=(uint8_t)level;
            /* Each level reached tries its getWaza moves (as _expRecover). */
            for(unsigned lv=from+1;lv<=level;++lv)for(unsigned j=0;j<20;++j){
                const uint8_t *l=s+0xba+4*j;uint16_t id=be16(l+2);
                if(l[0]!=lv||!id||id>=p->moves)continue;
                int known=0;uint16_t forgot;
                for(unsigned k=0;k<4;++k)known|=be16(r+0x78+4*k)==id;
                if(known)continue;
                if(learn_move(p,r,id,&forgot)<0){if(out.skipped_count[slot]<4)out.skipped[slot][out.skipped_count[slot]++]=id;continue;}
                if(out.learned_count[slot]<4){out.forgot[slot][out.learned_count[slot]]=forgot;out.learned[slot][out.learned_count[slot]++]=id;}
            }
        }
    }
    *rw=out;return 1;
}

/* pokemonCreate's PokemonBios for a snagged opponent, then
 * pokemonSetDarkPokemonStatus (pokemon_range_8011F77C.c). */
int c3_general_snag_record(const C3BattlePack *p,const C3GeneralBattle *b,unsigned index,
                           const C3SnagOwner *o,uint16_t gauge,uint32_t seed,uint8_t r[C3_GB_POKEMON_SIZE])
{
    if(!p||!b||!o||!r||b->magic!=MAGIC||index>=b->party_count[1]||!b->caught[index])return 0;
    const C3GeneralMon *m=&b->party[1][index];
    if(!species_row(p,m->species)||!m->shadow)return 0;
    memset(r,0,C3_GB_POKEMON_SIZE);
    C3BattleRng rng;c3_battle_rng_init(&rng,seed);uint32_t pid=0;
    for(unsigned tries=0;tries<4096;++tries){uint16_t hi,lo;
        if(!c3_battle_rng_next16(&rng,&lo)||!c3_battle_rng_next16(&rng,&hi))return 0;
        pid=(uint32_t)hi<<16|lo;if(pid%25u==m->nature)break;}
    put16(r,m->species);put32(r+4,pid);memcpy(r+8,o->attest,4);
    put16(r+0x0c,o->floor);r[0x0e]=m->level;r[0x0f]=b->caught[index];
    put32(r+0x14,o->trainer_id);memcpy(r+0x18,o->name,0x16);
    for(unsigned i=0;i<10&&m->name[i]&&m->name[i]!='?';++i){put16(r + 0x2e + (2*i),(uint8_t)m->name[i]);put16(r + 0x44 + (2*i),(uint8_t)m->name[i]);}
    put32(r+0x5c,exp_for(p,m->growth,m->level));r[0x60]=m->level;
    {unsigned id=status_to_record(m->status);if(id)put16(r+0x64,id);}
    /* The record keeps the Pokemon's own moves (the snag only clears
     * fightTrainerPokemonDataId); Shadow Rush and "????" are what it shows. */
    for(unsigned j=0;j<4;++j)if(m->own_moves[j]){put16(r+0x78+4*j,m->own_moves[j]);r[0x7a+4*j]=own_pp(m,j);}
    put16(r+0x88,m->held_item);
    put16(r+0x8c,m->max_hp);for(unsigned j=1;j<6;++j)put16(r+0x8c+2*j,m->stats[j]);
    put16(r+0x8a,m->max_hp);
    for(unsigned j=0;j<6;++j){put16(r+0x98+2*j,m->evs[j]);put16(r+0xa4+2*j,m->ivs[j]);}
    put16(r+0xb0,m->friendship);
    put16(r+0xd8,m->shadow);put32(r+0xdc,(uint32_t)gauge*100u);put32(r+0xe0,0);
    return 1;
}

/* _expRecover (pokemon_relive.c): pay `add` experience into one party
 * record a level at a time. Each level reached sets the experience to that
 * level's threshold, recomputes the stats (the writeback's stat rule and HP
 * carry) and tries the species' level-up moves for the new level
 * (pokemonGetOboeWazaDataId): a move the Pokemon lacks goes into a free slot
 * with its full PP; with four moves known the bound forget prompt asks which
 * to forget (evolutionWazaLearn with cbWazaForget), else it is skipped. The remainder that does not reach a level is added and the
 * stored-up experience (status 0xC6, +0xE0) is cleared; at level 100 the
 * loop stops and leaves it, as the original does. */
int c3_battle_pack_recover_exp(const C3BattlePack *p,uint8_t r[C3_GB_POKEMON_SIZE],uint32_t add,C3ExpRecovery *out)
{
    C3ExpRecovery o;memset(&o,0,sizeof o);
    if(!p||!r||!out)return 0;
    uint16_t species=be16(r);const uint8_t *s=species_row(p,species);
    if(!s)return 0;
    unsigned group=s[0];uint32_t rest=add;
    for(;;){
        if(!rest){put32(r+0xe0,0);break;}
        unsigned level=r[0x60];
        if(level>=100)break;
        uint32_t exp=be32(r+0x5c),next=exp_for(p,group,level+1);
        exp+=rest;
        if(exp<next){put32(r+0x5c,exp);rest=0;continue;}
        rest=exp-next;put32(r+0x5c,next);o.level_up=1;
        ++level;
        unsigned nature=be32(r+4)%25u,old_max=be16(r+0x8c);
        for(unsigned j=0;j<6;++j)
            put16(r+0x8c+2*j,(unsigned)compute_stat(j,be16(s+0x84+2*j),be16(r+0xa4+2*j)&31u,be16(r+0x98+2*j),level,nature));
        unsigned new_max=be16(r+0x8c),hp=be16(r+0x8a);
        if(hp)hp+=new_max>old_max?new_max-old_max:0;
        put16(r+0x8a,hp>new_max?new_max:hp);
        r[0x60]=(uint8_t)level;
        if(o.level_count<C3_EXP_RECOVERY_MAX)o.levels[o.level_count++]=(uint8_t)level;
        for(unsigned j=0;j<20;++j){const uint8_t *l=s+0xba+4*j;uint16_t id=be16(l+2);
            if(l[0]!=level||!id||id>=p->moves)continue;
            int known=0;uint16_t forgot;
            for(unsigned k=0;k<4;++k)known|=be16(r+0x78+4*k)==id;
            if(known)continue;
            if(learn_move(p,r,id,&forgot)<0){if(o.skipped_count<C3_EXP_RECOVERY_MAX)o.skipped[o.skipped_count++]=id;continue;}
            if(o.learned_count<C3_EXP_RECOVERY_MAX){o.forgot[o.learned_count]=forgot;o.learned[o.learned_count]=id;o.learned_level[o.learned_count++]=(uint8_t)level;}
        }
    }
    *out=o;return 1;
}
