#include "colosseum3ds/battle_general.h"
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

int c3_battle_pack_decode(C3BattlePack *out,const void *bytes,size_t size,char *e)
{
    const uint8_t *p=bytes;uint8_t digest[20];
    if(!out||!p||size<HEADER||size>8u*1024u*1024u)return fail(e,"Invalid story-battle pack size.");
    C3BattlePack k={0};
    k.encounters=be32(p+12);k.moves=be32(p+16);k.species=be32(p+20);k.types=be32(p+24);k.growth=be32(p+28);
    k.species_at=be32(p+112);k.types_at=be32(p+116);k.growth_at=be32(p+120);k.entries_at=be32(p+124);
    if(memcmp(p,"C3B2",4)||be32(p+4)!=1||be32(p+8)!=size||!k.encounters||k.encounters>4096||
       k.moves<STRUGGLE+1||k.moves>1024||k.species<2||k.species>1024||k.types!=18||k.growth!=8)
        return fail(e,"Unsupported story-battle pack header.");
    k.names_at=k.entries_at+k.encounters*ENTRY;
    if(k.species_at!=HEADER+k.moves*MOVE_ROW||k.types_at!=k.species_at+k.species*SPECIES_ROW||
       k.growth_at!=k.types_at+k.types*TYPE_ROW||k.entries_at!=k.growth_at+k.growth*GROWTH_ROW||
       (uint64_t)k.names_at+(uint64_t)(k.species+k.moves+2u*k.encounters)*C3_GB_NAME!=size)
        return fail(e,"Story-battle pack sections do not match its size.");
    c3_save_codec_sha1(digest,p+HEADER,size-HEADER);
    if(memcmp(digest,p+72,20))return fail(e,"Story-battle pack digest mismatch.");
    for(uint32_t i=0;i<k.encounters;++i){const uint8_t *r=p+k.entries_at+i*ENTRY;
        if(be16(r)!=i||r[2]>C3_GB_PARTY||be16(r+4)>5)return fail(e,"Damaged story-battle encounter index.");}
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
    for(unsigned k=0;k<i.party_count;++k){const uint8_t *m=r+192+k*TRAINER_MON;
        i.species[k]=be16(m+10);i.levels[k]=m[4];i.shadow_count=(uint8_t)(i.shadow_count+(m[3]!=0));}
    const uint8_t *names=p->bytes+p->names_at+(p->species+p->moves+2u*id)*C3_GB_NAME;
    copy_name(i.trainer,names);copy_name(i.title,names+C3_GB_NAME);
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
static int mon_from_trainer(const C3BattlePack *p,C3GeneralMon *m,const uint8_t *r)
{
    memset(m,0,sizeof *m);
    m->species=be16(r+10);m->level=r[4];m->ball=4; /* trainer records carry no ball: Poke Ball */
    if(!species_row(p,m->species)||!m->level||m->level>100)return 0;
    species_fields(p,m);
    m->nature=r[2]<25?r[2]:0;m->shadow=r[3];m->friendship=be16(r+8);m->held_item=(uint16_t)be32(r+0x10);
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
    return 1;
}
static int mon_from_party(const C3BattlePack *p,C3GeneralMon *m,const uint8_t *r,unsigned slot)
{
    memset(m,0,sizeof *m);
    m->species=be16(r);m->level=r[0x60];m->ball=r[0x0F]>=1&&r[0x0F]<=12?r[0x0F]:4; /* PokemonBios catchBallId */
    if(!species_row(p,m->species)||!m->level||m->level>100||r[0xcb])return 0;
    species_fields(p,m);
    m->party_slot=(uint8_t)slot;m->exp=be32(r+0x5c);m->held_item=be16(r+0x88);
    m->hp=be16(r+0x8a);m->friendship=be16(r+0xb0);m->shadow=be16(r+0xd8)!=0;m->nature=(uint8_t)(be32(r+4)%25u);
    for(unsigned j=0;j<6;++j){m->stats[j]=be16(r+0x8c+2*j);m->ivs[j]=(uint8_t)be16(r+0xa4+2*j);m->evs[j]=be16(r+0x98+2*j);}
    m->max_hp=m->stats[0];
    if(!m->max_hp||m->hp>m->max_hp)return 0;
    for(unsigned j=0;j<4;++j){uint16_t id=be16(r+0x78+4*j);
        if(id&&id<p->moves){m->moves[j]=id;m->pp[j]=r[0x7a+4*j];m->pp_max[j]=(uint8_t)move_pp(p,id,r[0x7b+4*j]);
            if(m->pp[j]>m->pp_max[j])m->pp[j]=m->pp_max[j];}}
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
    for(unsigned i=0;i<4;++i){const C3GeneralMon *m=cat(b,i);e->hp[i]=m?m->hp:0;e->max_hp[i]=m?m->max_hp:0;}
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
static const char *side_label(const C3GeneralBattle *b,unsigned s){return b->link?b->side_name[s]:b->info.trainer;}
static void announce_send(C3GeneralBattle *b,unsigned s,unsigned a,const C3GeneralMon *m)
{
    if(s==b->viewer)say(b,C3_GB_EVENT_SWITCH,s*2+a,255,0,"Go, %s!",m->name);
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
    for(unsigned i=0;i<C3_GB_PARTY;++i){
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
    mark_faced(b);
    say(b,C3_GB_EVENT_MESSAGE,255,255,0,"%s %s would like to battle!",info.title,info.trainer);
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
static unsigned speed_of(const C3GeneralMon *m)
{unsigned v=staged(m->stats[5],m->stages[4]);return m->status==C3_GB_PARALYSIS?v/4u:v;}
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
static void award(C3GeneralBattle *b,unsigned foe_index)
{
    C3GeneralMon *foe=&b->party[1][foe_index];
    unsigned count=0;
    for(unsigned i=0;i<b->party_count[0];++i)count+=(foe->participated>>i&1u)&&b->party[0][i].hp;
    if(!count)return;
    uint32_t gain=(uint32_t)foe->base_exp*foe->level/7u*3u/2u/count;
    if(!gain)gain=1;
    for(unsigned i=0;i<b->party_count[0];++i){C3GeneralMon *m=&b->party[0][i];
        if(!(foe->participated>>i&1u)||!m->hp||m->level>=100)continue;
        m->exp+=gain;m->participated=1;
        say(b,C3_GB_EVENT_MESSAGE,255,255,0,"%s gained %lu EXP. Points!",m->name,(unsigned long)gain);
    }
    foe->participated=0;
}
static int damage(C3GeneralBattle *b,const C3BattlePack *p,unsigned a,unsigned d,uint16_t id,const uint8_t *mv,
                  unsigned power,int spread,uint32_t *out,int *critical,unsigned *num,unsigned *den)
{
    C3GeneralMon *x=at(b,a),*y=at(b,d);unsigned r;
    unsigned type=mv[2],special=type>=10;
    if(!effectiveness(p,type,y,num,den)){*out=0;return 1;}
    unsigned crit_rate=be16(mv+26)==E_HIGH_CRITICAL?8u:16u;
    if(!roll(b,crit_rate,&r))return 0;
    *critical=r==0;
    int as=x->stages[special?2:0],ds=y->stages[special?3:1];
    if(*critical){if(as<0)as=0;if(ds>0)ds=0;}
    unsigned atk=staged(x->stats[special?3:1],as),def=staged(y->stats[special?4:2],ds);
    if(!def)def=1;
    if(!special&&x->status==C3_GB_BURN)atk/=2u;
    uint32_t v=((2u*x->level/5u+2u)*power*atk/def)/50u;
    unsigned side=d/2;
    if(!*critical&&b->screens[side][special]&&a/2!=d/2)v=b->active_count==2?v*2u/3u:v/2u;
    if(spread&&b->active_count==2)v=v*3u/4u;
    v+=2u;
    if(*critical)v*=2u;
    if(type==x->types[0]||type==x->types[1])v=v*3u/2u;
    v=v**num/ *den;
    if(!roll(b,16,&r))return 0;
    v=v*(85u+r)/100u;
    if(!v)v=1;
    (void)id;*out=v;return 1;
}
static int targets_of(C3GeneralBattle *b,unsigned a,unsigned range,unsigned chosen,uint8_t list[3])
{
    unsigned n=0,foe=1-a/2;
    if(range==4||range==6){
        for(unsigned i=0;i<b->active_count;++i)if(alive(b,foe*2+i))list[n++]=(uint8_t)(foe*2+i);
        if(range==6)for(unsigned i=0;i<b->active_count;++i){unsigned q=(a/2)*2+i;if(q!=a&&alive(b,q))list[n++]=(uint8_t)q;}
        return (int)n;
    }
    if(chosen<4&&alive(b,chosen)&&chosen!=a){list[0]=(uint8_t)chosen;return 1;}
    for(unsigned i=0;i<b->active_count;++i)if(alive(b,foe*2+i)){list[0]=(uint8_t)(foe*2+i);return 1;}
    return 0;
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
    uint16_t id=c.slot==4?STRUGGLE:x->moves[c.slot];
    const uint8_t *mv=move_row(p,id);
    if(!mv)return 0;
    if(c.slot<4){if(!x->pp[c.slot])id=STRUGGLE,mv=move_row(p,id);else --x->pp[c.slot];}
    char name[C3_GB_NAME];c3_battle_pack_move_name(p,id,name);
    if(!say(b,C3_GB_EVENT_MOVE,a,c.target,id,"%s used %s!",who(b,a),name))return 0;
    uint16_t effect=be16(mv+26);unsigned range=mv[3],power=(unsigned)(int16_t)be16(mv+22);
    if(id==STRUGGLE)effect=E_RECOIL;
    int s=0,delta=0,self=0,hit_stat=0;
    int is_stat=stat_effect(effect,&s,&delta,&self,&hit_stat);
    /* Self / side moves. */
    if(!power&&(range==5||range==7||self)){
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
    if(!n)return say(b,C3_GB_EVENT_MESSAGE,a,a,0,"But there was no target...");
    int spread=n>1;uint32_t dealt_total=0;
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
            if(!damage(b,p,a,d,id,mv,pw,spread,&v,&critical,&num,&den))return 0;
            if(!num){if(!say(b,C3_GB_EVENT_MESSAGE,a,d,0,"It doesn't affect %s...",who(b,d)))return 0;continue;}
        }
        if((effect==E_SUPER_FANG||effect==E_DRAGON_RAGE||effect==E_SONICBOOM||effect==E_LEVEL_DAMAGE||effect==E_OHKO)){
            unsigned en,ed;if(!effectiveness(p,mv[2],y,&en,&ed)){if(!say(b,C3_GB_EVENT_MESSAGE,a,d,0,"It doesn't affect %s...",who(b,d)))return 0;continue;}}
        unsigned hits=1;
        if(effect==E_MULTI_HIT){if(!roll(b,8,&r))return 0;hits=r<3?2:r<6?3:r<7?4:5;}
        if(effect==E_DOUBLE_HIT||effect==E_TWINEEDLE)hits=2;
        unsigned landed=0;
        for(unsigned h=0;h<hits&&y->hp&&x->hp;++h){
            uint32_t hv=v;if(h&&!(effect==E_OHKO)){int cr;unsigned nn,dd;if(!damage(b,p,a,d,id,mv,power,spread,&hv,&cr,&nn,&dd))return 0;critical=cr;}
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
    for(unsigned i=0;i<4;++i)if(m->moves[i]&&m->pp[i]&&move_row(p,m->moves[i]))mask|=(uint8_t)(1u<<i);
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
int c3_general_battle_choose(C3GeneralBattle *b,const C3BattlePack *p,unsigned pos,C3GeneralCommand *c)
{
    if(!b||!c||b->magic!=MAGIC||!alive(b,pos))return 0;
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
    if(c.kind!=0||c.target>=4)return 0;
    uint8_t mask=c3_general_battle_legal_moves(b,p,pos);
    return c.slot==4?!mask:c.slot<4&&(mask>>c.slot&1u);
}
static int replace_fainted(C3GeneralBattle *b)
{
    for(unsigned s=0;s<2;++s)for(unsigned a=0;a<b->active_count;++a){
        uint8_t i=b->active[s][a];
        if(i!=NO_MON&&b->party[s][i].hp)continue;
        if(s==1&&i!=NO_MON&&!b->link)award(b,i);
        int n=next_healthy(b,s,a);
        b->active[s][a]=n<0?NO_MON:(uint8_t)n;
        if(n>=0)announce_send(b,s,a,&b->party[s][n]);
    }
    mark_faced(b);return 1;
}
static int end_of_turn(C3GeneralBattle *b)
{
    for(unsigned pos=0;pos<4;++pos){C3GeneralMon *m=at(b,pos);
        if(!m)continue;
        m->flinch=0;m->protect=0;
        if(!m->hp)continue;
        if(m->status==C3_GB_POISON||m->status==C3_GB_BURN){
            if(!say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%s is hurt by its %s!",who(b,pos),m->status==C3_GB_BURN?"burn":"poison")||
               !hurt(b,pos,m->max_hp/8u,0))return 0;}
        else if(m->status==C3_GB_TOXIC){if(m->toxic_count<15)++m->toxic_count;
            if(!say(b,C3_GB_EVENT_MESSAGE,pos,pos,0,"%s is hurt by poison!",who(b,pos))||
               !hurt(b,pos,m->max_hp*m->toxic_count/16u,0))return 0;}
    }
    static const char *const screen[2]={"Reflect","Light Screen"};
    static const char *const team[2]={"Your team","The foe team"};
    for(unsigned s=0;s<2;++s)for(unsigned k=0;k<2;++k)if(b->screens[s][k]&&!--b->screens[s][k])
        say(b,C3_GB_EVENT_MESSAGE,255,255,0,"%s: %s wore off!",team[s!=b->viewer],screen[k]);
    return 1;
}
int c3_general_battle_command_valid(const C3GeneralBattle *b,const C3BattlePack *p,unsigned pos,C3GeneralCommand c)
{return b&&p&&b->magic==MAGIC&&pos<4&&command_valid((C3GeneralBattle *)b,p,pos,c);}
/* link: player holds all four positions' commands and nothing is drawn
 * for the opponent; otherwise player[0..1] is the hero and the opponent
 * policy picks side 1's commands from the battle RNG. */
static int turn_core(C3GeneralBattle *out,const C3BattlePack *p,const C3GeneralCommand *player,int link,char *e)
{
    if(!out||!p||!player||out->magic!=MAGIC||out->outcome!=C3_GB_RUNNING||out->turn>=C3_GB_MAX_TURNS)
        return fail(e,"Invalid or finished battle.");
    for(unsigned pos=0;pos<4;++pos){
        if(!link&&pos/2)continue;
        if(pos%2<out->active_count&&!command_valid(out,p,pos,player[pos]))return fail(e,"Choose a usable move or a healthy Pokemon.");
    }
    C3GeneralBattle *b=malloc(sizeof *b);if(!b)return fail(e,"Turn allocation failed.");
    *b=*out;b->event_count=0;++b->turn;
    C3GeneralCommand cmd[4]={{0}};uint8_t order[4],count=0,acted[4]={0};int ok=0;unsigned r;
    for(unsigned a=0;a<b->active_count;++a){if(alive(b,a))cmd[a]=player[a];
        if(link){if(alive(b,2+a))cmd[2+a]=player[2+a];}
        else if(alive(b,2+a)&&!c3_general_battle_choose(b,p,2+a,&cmd[2+a]))goto failed;}
    for(unsigned pos=0;pos<4;++pos)if(alive(b,pos))order[count++]=(uint8_t)pos;
    /* Switches first, then move priority, speed, and a coin for exact ties. */
    for(unsigned i=0;i+1<count;++i)for(unsigned j=i+1;j<count;++j){
        unsigned x=order[i],y=order[j];int px,py;
        C3GeneralMon *mx=at(b,x),*my=at(b,y);
        const uint8_t *vx=cmd[x].kind?NULL:move_row(p,cmd[x].slot==4?STRUGGLE:mx->moves[cmd[x].slot]);
        const uint8_t *vy=cmd[y].kind?NULL:move_row(p,cmd[y].slot==4?STRUGGLE:my->moves[cmd[y].slot]);
        px=cmd[x].kind?16:vx?(int8_t)vx[0]:0;py=cmd[y].kind?16:vy?(int8_t)vy[0]:0;
        int swap=0;
        if(px!=py)swap=px<py;
        else if(speed_of(mx)!=speed_of(my))swap=speed_of(mx)<speed_of(my);
        else{if(!roll(b,2,&r))goto failed;swap=r==0;}
        if(swap){order[i]=(uint8_t)y;order[j]=(uint8_t)x;}
    }
    for(unsigned i=0;i<count;++i){unsigned pos=order[i];
        if(!alive(b,pos)){acted[pos]=1;continue;}
        if(cmd[pos].kind==1){
            say(b,C3_GB_EVENT_SWITCH,pos,255,0,"%s, come back!",who(b,pos));
            C3GeneralMon *old=at(b,pos);if(old){memset(old->stages,0,sizeof old->stages);old->confusion=0;}
            b->active[pos/2][pos%2]=cmd[pos].slot;mark_faced(b);
            announce_send(b,pos/2,pos%2,&b->party[pos/2][cmd[pos].slot]);
        }else if(!use_move(b,p,pos,cmd[pos],acted))goto failed;
        acted[pos]=1;
        check_outcome(b);if(b->outcome!=C3_GB_RUNNING)break;
    }
    if(b->outcome==C3_GB_RUNNING){if(!end_of_turn(b))goto failed;check_outcome(b);}
    if(!replace_fainted(b))goto failed;
    if(b->outcome==C3_GB_RUNNING&&b->turn>=C3_GB_MAX_TURNS)b->outcome=C3_GB_DRAW;
    if(b->outcome!=C3_GB_RUNNING&&b->link){
        unsigned foe=1u-b->viewer;int won=b->outcome==(b->viewer?C3_GB_LOST:C3_GB_WON);
        if(b->outcome==C3_GB_DRAW)say(b,C3_GB_EVENT_OUTCOME,255,255,0,"The battle with %s ended in a draw.",b->side_name[foe]);
        else say(b,C3_GB_EVENT_OUTCOME,255,255,0,won?"You won the battle against %s!":"You lost the battle against %s...",b->side_name[foe]);
    }else if(b->outcome!=C3_GB_RUNNING)
        say(b,C3_GB_EVENT_OUTCOME,255,255,0,b->outcome==C3_GB_WON?"You defeated %s %s!":
            b->outcome==C3_GB_LOST?"You lost to %s %s...":"The battle with %s %s ended in a draw.",b->info.title,b->info.trainer);
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
            x=ev->max_hp[k];ev->max_hp[k]=ev->max_hp[k+2];ev->max_hp[k+2]=x;}}
}

/* ---- writeback ----------------------------------------------------------- */

static uint32_t exp_for(const C3BattlePack *p,unsigned group,unsigned level)
{return be32(p->bytes+p->growth_at+(group<8?group:0)*GROWTH_ROW+(level>100?100:level)*4u);}
int c3_general_battle_writeback(const C3GeneralBattle *b,const C3BattlePack *p,
    uint8_t party[C3_GB_PARTY][C3_GB_POKEMON_SIZE],C3GeneralRewards *rw)
{
    if(!b||!p||!party||!rw||b->magic!=MAGIC||b->outcome==C3_GB_RUNNING)return 0;
    C3GeneralRewards out={0};
    out.result_code=b->outcome==C3_GB_WON?2u:3u;
    out.wipeout=b->outcome!=C3_GB_WON&&b->info.wipeout;
    if(b->outcome==C3_GB_WON){
        /* Prize: trainer-kind base x highest opponent level (the original
         * prize opcode's shape with its side multipliers taken as 1). */
        unsigned top=0;for(unsigned i=0;i<b->party_count[1];++i)if(b->party[1][i].level>top)top=b->party[1][i].level;
        out.prize=(uint32_t)b->info.prize_base*top;
    }
    for(unsigned i=0;i<b->party_count[0];++i)
        if(be16(party[b->party[0][i].party_slot])!=b->party[0][i].species)return 0;
    for(unsigned i=0;i<b->party_count[0];++i){
        const C3GeneralMon *m=&b->party[0][i];uint8_t *r=party[m->party_slot];
        uint32_t before=be32(r+0x5c),gain=m->exp>before?m->exp-before:0;
        put16(r+0x8a,m->hp);
        for(unsigned j=0;j<4;++j)if(m->moves[j])r[0x7a+4*j]=m->pp[j];
        out.exp[m->party_slot]=gain;
        if(!gain)continue;
        if(m->shadow){put32(r+0xe0,be32(r+0xe0)+gain);continue;}
        uint32_t cap=exp_for(p,m->growth,100),exp=before+gain>cap?cap:before+gain;
        unsigned level=r[0x60];
        while(level<100&&exp>=exp_for(p,m->growth,level+1))++level;
        put32(r+0x5c,exp);
        if(level!=r[0x60]){
            const uint8_t *s=species_row(p,m->species);unsigned nature=be32(r+4)%25u;
            unsigned old_max=be16(r+0x8c);
            for(unsigned j=0;j<6;++j)
                put16(r+0x8c+2*j,(unsigned)compute_stat(j,be16(s+0x84+2*j),be16(r+0xa4+2*j)&31u,be16(r+0x98+2*j),level,nature));
            unsigned new_max=be16(r+0x8c),hp=be16(r+0x8a);
            if(hp)hp+=new_max>old_max?new_max-old_max:0;
            put16(r+0x8a,hp>new_max?new_max:hp);
            out.levels_gained[m->party_slot]=(uint8_t)(level-r[0x60]);
            r[0x60]=(uint8_t)level;
        }
    }
    *rw=out;return 1;
}

/* _expRecover (pokemon_relive.c): pay `add` experience into one party
 * record a level at a time. Each level reached sets the experience to that
 * level's threshold, recomputes the stats (the writeback's stat rule and HP
 * carry) and tries the species' level-up moves for the new level
 * (pokemonGetOboeWazaDataId): a move the Pokemon lacks goes into a free slot
 * with its full PP; with four moves known the original asks which to forget
 * (evolutionWazaLearn), which is not ported, so the move is reported as
 * skipped. The remainder that does not reach a level is added and the
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
            int known=0,slot=-1;
            for(unsigned k=0;k<4;++k){uint16_t have=be16(r+0x78+4*k);known|=have==id;if(!have&&slot<0)slot=(int)k;}
            if(known)continue;
            if(slot<0){if(o.skipped_count<C3_EXP_RECOVERY_MAX)o.skipped[o.skipped_count++]=id;continue;}
            put16(r+0x78+4*slot,id);r[0x7a+4*slot]=(uint8_t)move_pp(p,id,0);r[0x7b+4*slot]=0;
            if(o.learned_count<C3_EXP_RECOVERY_MAX){o.learned[o.learned_count]=id;o.learned_level[o.learned_count++]=(uint8_t)level;}
        }
    }
    *out=o;return 1;
}
