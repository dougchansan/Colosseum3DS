#include "colosseum3ds/battle_init.h"
#include "save_codec.h"

#include <stdio.h>
#include <string.h>

#define HEADER 128u
#define NATURE 32u
#define MOVE_PP 282u
#define MOVE_COUNT 358u

static const uint8_t source_pin[20] = {
    0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,
    0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55
};
static const uint8_t common_pin[20] = {
    0xe8,0x16,0x76,0x18,0x13,0x90,0x0a,0xa0,0xfd,0x4f,
    0x87,0xd0,0x12,0xb3,0xb2,0xa1,0x9d,0x2f,0x00,0x3e
};
static const unsigned stat_order[6] = {
    C3_STAT_HP,C3_STAT_ATTACK,C3_STAT_DEFENSE,
    C3_STAT_SP_ATTACK,C3_STAT_SP_DEFENSE,C3_STAT_SPEED
};

static uint16_t be16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static int signed8(uint8_t v) { return v < 128 ? v : (int)v - 256; }
static int signed16(const uint8_t *p)
{ unsigned v=be16(p); return v < 32768 ? (int)v : (int)v-65536; }
static int fail(char *error, const char *message)
{ if (error) snprintf(error,C3_BATTLE_ERROR_SIZE,"%s",message); return 0; }
static int overlap(const void *a, size_t as, const void *b, size_t bs)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x>=y ? x-y<bs : y-x<as;
}

static int valid_tables(const uint8_t *p, size_t size, char *error)
{
    uint8_t digest[20];
    if (!p || size!=C3_BATTLE_INIT_PACK_SIZE || memcmp(p,"C3I1",4) ||
        be32(p+4)!=1 || be32(p+8)!=size || be32(p+12)!=10 ||
        memcmp(p+16,source_pin,20) || memcmp(p+36,common_pin,20))
        return fail(error,"Invalid first-battle initialization source/profile.");
    for (unsigned i=96;i<HEADER;++i)
        if (p[i]) return fail(error,"Unsupported initialization header extension.");
    c3_save_codec_sha1(digest,p+HEADER,C3_BATTLE_INIT_TABLE_BYTES);
    if (memcmp(digest,p+56,20)) return fail(error,"Initialization table checksum mismatch.");
    const uint8_t *t=p+HEADER;
    if (be16(t)!=288 || !t[3] || t[3]>77 || t[4]>77 || t[5] || t[6] || t[7] ||
        be16(t+20)>255) return fail(error,"Unsupported opponent species definition.");
    for (unsigned i=22;i<NATURE;++i)
        if (t[i]) return fail(error,"Unsupported species definition extension.");
    for (unsigned i=0;i<6;++i)
        if (!be16(t+8+2*i) || be16(t+8+2*i)>255)
            return fail(error,"Invalid opponent base stat.");
    for (unsigned n=0;n<25;++n) {
        unsigned up=0,down=0;
        for (unsigned i=0;i<5;++i) {
            const uint8_t *r=t+NATURE+n*10+i*2;
            if (r[1]!=100 || (r[0]!=90 && r[0]!=100 && r[0]!=110))
                return fail(error,"Invalid original nature ratio.");
            up+=r[0]==110; down+=r[0]==90;
        }
        if (up!=down || up>1) return fail(error,"Inconsistent original nature ratios.");
    }
    if (t[MOVE_PP]) return fail(error,"Empty move must have zero PP.");
    for (unsigned i=1;i<MOVE_COUNT;++i)
        if (t[MOVE_PP+i]>40) return fail(error,"Move PP exceeds the admitted table range.");
    return 1;
}

int c3_battle_init_decode(C3BattleInitTables *out, const void *pack, size_t size, char *error)
{
    if (!out || !valid_tables(pack,size,error))
        return out ? 0 : fail(error,"Missing initialization output.");
    /* memmove also permits decoding the already owned byte buffer in place. */
    memmove(out->bytes,pack,size);
    if (error) error[0]=0;
    return 1;
}

int c3_battle_init_load(C3BattleInitTables *out, const char *path, char *error)
{
    uint8_t bytes[C3_BATTLE_INIT_PACK_SIZE];
    if (!out || !path) return fail(error,"Missing initialization table path/output.");
    FILE *f=fopen(path,"rb");
    if (!f) return fail(error,"Cannot open first-battle initialization tables.");
    size_t size=fread(bytes,1,sizeof bytes,f);
    int extra=fgetc(f),bad=ferror(f);
    if (fclose(f)) bad=1;
    if (bad || size!=sizeof bytes || extra!=EOF)
        return fail(error,"Truncated or oversized initialization table pack.");
    return c3_battle_init_decode(out,bytes,size,error);
}

static int valid_roster(const uint8_t *r, const uint8_t *t)
{
    int ability=signed8(r[0]),sex=signed8(r[1]),nature=signed8(r[2]);
    int friendship=signed16(r+8);
    unsigned ev_total=0;
    if (r[3] || !r[4] || r[4]>100 || be16(r+10)!=be16(t) || be32(r+16)!=0 ||
        ability < -1 || ability>1 || sex < -1 || sex>2 || nature < -1 || nature>24 ||
        friendship < -1 || friendship>255) return 0;
    for (unsigned i=0;i<6;++i) {
        int iv=signed8(r[28+i]),ev=signed16(r+34+2*i);
        if (iv < -1 || iv>31 || ev < -1 || ev>255) return 0;
        if (ev>=0) ev_total+=(unsigned)ev;
    }
    if (ev_total>510) return 0;
    for (unsigned i=0;i<4;++i) {
        uint32_t move=be32(r+52+8*i);
        /* All four overrides must be explicit: no unported learned-move fallback. */
        if (move>354 || r[48+8*i]!=0 || (move && !t[MOVE_PP+move])) return 0;
    }
    return 1;
}

static int valid_request(const C3BattleRequest *r, const C3BattleInitTables *t, char *error)
{
    C3BattleSummary summary; uint8_t digest[20]; unsigned count=0;
    if (r->status!=C3_BATTLE_WAITING_BACKEND || r->native98_args[0]!=10 ||
        r->native98_args[1]!=1 || r->native98_args[2]!=0 ||
        !c3_battle_summary(&r->data,&summary))
        return fail(error,"Initialization requires the reviewed pending native98(10,1,0).");
    c3_save_codec_sha1(digest,(const uint8_t *)&r->data,sizeof r->data);
    if (memcmp(digest,t->bytes+76,20))
        return fail(error,"Initialization tables belong to different encounter records.");
    for (unsigned i=0;i<C3_BATTLE_PARTY_MAX;++i) {
        const uint8_t *p=r->player_party[i]; unsigned species=be16(p);
        if (!species) continue;
        if (species>=413 || !p[0x60] || p[0x60]>100 || p[0xcd]==1 ||
            !be16(p+0x8c) || be16(p+0x8a)>be16(p+0x8c))
            return fail(error,"Invalid preserved player-party record.");
        ++count;
    }
    if (!count || count!=r->player_party_count)
        return fail(error,"Player-party count disagrees with preserved records.");
    for (unsigned i=0;i<2;++i)
        if (!valid_roster(r->data.roster[i],t->bytes+HEADER))
            return fail(error,"Opponent needs an unported Shadow/item/PP-Up/default-move initializer.");
    return 1;
}

static int high_first(C3BattleRng *rng, uint32_t *value)
{
    uint16_t high,low;
    if (!c3_battle_rng_next16(rng,&high) || !c3_battle_rng_next16(rng,&low)) return 0;
    *value=(uint32_t)high<<16 | low;
    return 1;
}
static unsigned sex_for(uint8_t ratio,uint32_t personality)
{
    if (ratio==255) return 2;
    if (!ratio) return 0;
    if (ratio==254) return 1;
    return (personality&255u)<ratio;
}

static int make_opponent(C3BattleInitialOpponent *out,const uint8_t *r,
                         const uint8_t *t,uint32_t trainer,C3BattleRng *rng,char *error)
{
    uint16_t first,second,ability_draw;
    if (!high_first(rng,&out->initial_personality) ||
        !c3_battle_rng_next16(rng,&first) || !c3_battle_rng_next16(rng,&second) ||
        !c3_battle_rng_next16(rng,&ability_draw))
        return fail(error,"Battle RNG draw counter overflow.");
    C3Pokemon *p=&out->pokemon;
    p->species_id=be16(t);
    p->iv[C3_STAT_HP]=(uint8_t)(first&31);
    p->iv[C3_STAT_ATTACK]=(uint8_t)((first>>5)&31);
    p->iv[C3_STAT_DEFENSE]=(uint8_t)((first>>10)&31);
    p->iv[C3_STAT_SPEED]=(uint8_t)(second&31);
    p->iv[C3_STAT_SP_ATTACK]=(uint8_t)((second>>5)&31);
    p->iv[C3_STAT_SP_DEFENSE]=(uint8_t)((second>>10)&31);
    for (unsigned i=0;i<6;++i) {
        unsigned stat=stat_order[i]; int iv=signed8(r[28+i]),ev=signed16(r+34+2*i);
        p->base_stats[stat]=be16(t+8+2*i);
        if (iv>=0) p->iv[stat]=(uint8_t)iv;
        p->ev[stat]=(uint16_t)(ev<0?0:ev);
    }
    out->ability_flag=t[4]?(uint8_t)(ability_draw&1u):0;
    if (signed8(r[0])>=0) out->ability_flag=t[4]?r[0]:0;
    out->ability_id=t[3+out->ability_flag];
    out->held_item=0; out->nickname_message=be32(r+20);
    out->friendship=signed16(r+8)<0?be16(t+20):be16(r+8);
    int requested_sex=signed8(r[1]),requested_nature=signed8(r[2]);
    int fixed_sex=t[2]==0 || t[2]==254 || t[2]==255;
    unsigned initial_sex=sex_for(t[2],out->initial_personality);
    for (unsigned attempt=1;;++attempt) {
        uint32_t value;
        if (!high_first(rng,&value)) return fail(error,"Battle RNG draw counter overflow.");
        unsigned sex=sex_for(t[2],value);
        int gender_matches=requested_sex<0 || fixed_sex ||
            sex==(requested_sex==2?initial_sex:(unsigned)requested_sex);
        uint32_t shiny=(trainer>>16)^(trainer&65535u)^(value>>16)^(value&65535u);
        if (gender_matches && (requested_nature<0 || value%25u==(unsigned)requested_nature) && shiny>=8) {
            out->personality=value;out->personality_attempts=attempt;
            out->sex=(uint8_t)sex;out->nature=(uint8_t)(value%25u);break;
        }
        if (attempt==C3_BATTLE_INIT_RETRY_LIMIT)
            return fail(error,"Original personality rejection loop exceeded the native bound.");
    }
    c3_pokemon_neutral_nature(p);
    for (unsigned i=1;i<6;++i) {
        const uint8_t *rate=t+NATURE+out->nature*10+(i-1)*2;
        p->nature[stat_order[i]]=(C3NatureRate){rate[0],rate[1]};
    }
    /* The final source reset leaves a newly generated ordinary Pokemon at
     * full HP. Intermediate base-stat recalculation consumes no RNG. */
    if (c3_pokemon_recalculate(p,r[4])!=C3_POKEMON_OK)
        return fail(error,"Original opponent stat recalculation failed.");
    for (unsigned i=0;i<4;++i) {
        C3BattleInitialMove *m=out->moves+i;
        m->id=(uint16_t)be32(r+52+8*i);m->pp_ups=r[48+8*i];
        /* pokemonWazaCreate fills BASE PP first; the following 0x81 write
         * changes PP-Up count, not current PP. Never refill to the new max. */
        m->pp=t[MOVE_PP+m->id];
        m->maximum_pp=m->pp; /* Admitted encounter has zero PP Ups. */
    }
    return 1;
}

int c3_battle_init_prepare(C3BattleInitialState *out,const C3BattleRequest *request,
                           const C3BattleInitTables *tables,uint32_t trainer,
                           C3BattleRng *rng,char *error)
{
    if (!out || !request || !tables || !rng)
        return fail(error,"Missing battle initialization argument.");
    if (overlap(out,sizeof *out,request,sizeof *request) ||
        overlap(out,sizeof *out,tables,sizeof *tables) || overlap(out,sizeof *out,rng,sizeof *rng) ||
        overlap(rng,sizeof *rng,request,sizeof *request) || overlap(rng,sizeof *rng,tables,sizeof *tables))
        return fail(error,"Battle initialization objects overlap.");
    if (!valid_tables(tables->bytes,sizeof tables->bytes,error) ||
        !valid_request(request,tables,error)) return 0;
    C3BattleInitialState next={0}; C3BattleRng work=*rng;
    next.request=*request;next.opponent_trainer_id=trainer;
    if (!c3_battle_rng_snapshot(&work,&next.rng_before))
        return fail(error,"Cannot snapshot battle RNG.");
    for (unsigned i=0;i<2;++i) {
        next.opponents[i].roster_id=(uint16_t)(238+i);
        if (!make_opponent(next.opponents+i,request->data.roster[i],tables->bytes+HEADER,
                           trainer,&work,error)) return 0;
    }
    if (!c3_battle_rng_snapshot(&work,&next.rng_after))
        return fail(error,"Cannot snapshot initialized battle RNG.");
    next.opponent_count=2;next.stage=C3_BATTLE_INIT_PARTY_PARAMETERS;
    *out=next;*rng=work;if (error) error[0]=0;
    return 1;
}
