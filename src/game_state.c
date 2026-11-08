#include "colosseum3ds/game_state.h"
#include "colosseum3ds/pokemon.h"
#include "colosseum3ds/save.h"
#include "save_codec.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Byte offsets are the original layouts, not native struct layouts. See
 * docs/game-state.md for the pinned source functions behind this adapter. */
#define PACK_HEADER 128u
#define PREFIX_SIZE 0x6040u
#define BODY_OFFSET PACK_HEADER
#define PREFIX_OFFSET (BODY_OFFSET + C3_SAVE_BODY_SIZE)
#define STARTER_OFFSET (PREFIX_OFFSET + PREFIX_SIZE)
#define NATURE_OFFSET (STARTER_OFFSET + 2u*64u)
#define FLAG_OFFSET (NATURE_OFFSET + 25u*10u)
#define FLAG_COUNT 3593u
#define PACK_SIZE (FLAG_OFFSET + FLAG_COUNT*8u)
#define HERO 0x70u
#define PARTY 0xa0u
#define POKEMON_SIZE 0x138u
#define MEMO 0x82a8u
#define SLOT_BASE 0x6000u

typedef struct GameStateImpl {
    uint8_t *body, *card, *pack;
    size_t card_length;
    uint32_t random;
    double play_time;
    unsigned used_backup;
} GameStateImpl;

static uint16_t be16(const uint8_t *p)
{ return (uint16_t)((uint16_t)p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p)
{ return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3]; }
static void put16(uint8_t *p, uint16_t v)
{ p[0]=(uint8_t)(v>>8); p[1]=(uint8_t)v; }
static void put32(uint8_t *p, uint32_t v)
{ p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16); p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v; }
static float read_float(const uint8_t *p)
{ uint32_t v=be32(p); float f; memcpy(&f,&v,sizeof f); return f; }
static void put_float(uint8_t *p, float f)
{ uint32_t v; memcpy(&v,&f,sizeof v); put32(p,v); }

static int fail(char *error, const char *message)
{
    if (error) (void)snprintf(error, C3_GAME_ERROR_SIZE, "%s", message);
    return 0;
}

static void release(GameStateImpl *s)
{
    if (!s) return;
    free(s->body); free(s->card); free(s->pack); free(s);
}

void c3_game_state_free(C3GameState *state)
{
    if (state) { release(state->impl); state->impl=NULL; }
}

static int terminated_name(const uint8_t *name, unsigned units, int nonempty)
{
    unsigned i;
    if (nonempty && !be16(name)) return 0;
    for (i=0; i<units; ++i) if (!be16(name+2u*i)) return 1;
    return 0;
}

static int valid_body(const uint8_t *b)
{
    unsigned i, party=0;
    float seconds;
    /* The generation byte is provenance, not a save-layout version. Retail
     * gamedataAttestCheckValid (80135530) accepts any nonzero generation;
     * imported US cards can legitimately retain 02 instead of our freshly
     * created 03. Keep the admitted Colosseum/US language and region here. */
    if (b[0]!=0x0b || !b[1] || b[2]!=2 || b[3]!=2 ||
        !terminated_name(b+HERO,11,1) || b[HERO+0xa80]>1 ||
        be32(b+HERO+0xa84)>9999999u || b[0x29]>1 || b[0x2a]>1 ||
        !be32(b+0x1c) || be32(b+0x1c)>65535u || be32(b+0x24)>65535u)
        return 0;
    seconds=read_float(b+0x20);
    if (!isfinite(seconds) || seconds<0 || (double)seconds>UINT32_MAX) return 0;
    for (i=0; i<6; ++i) {
        const uint8_t *p=b+PARTY+i*POKEMON_SIZE;
        uint16_t species=be16(p);
        if (!species) continue;
        if (species>=413 || !p[8] || !p[9] || !p[10] || !p[11] ||
            p[0x60]<1 || p[0x60]>100 || p[0xcd]==1 ||
            be16(p+0x8a)>be16(p+0x8c) || !be16(p+0x8c)) return 0;
        ++party;
    }
    return party>0;
}

static int load_pack(GameStateImpl *s, const char *path, char *error)
{
    static const uint8_t pin[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,
        0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    static const uint32_t fields[15]={1,PACK_SIZE,BODY_OFFSET,C3_SAVE_BODY_SIZE,
        PREFIX_OFFSET,PREFIX_SIZE,STARTER_OFFSET,2,64,NATURE_OFFSET,25,10,
        FLAG_OFFSET,FLAG_COUNT,8};
    static const uint32_t capacities[4]={0,256,1024,12288};
    uint32_t next[4]={0,0,0,0};
    uint8_t digest[20];
    uint8_t *pack;
    FILE *f;
    unsigned i,j;
    int extra, io_error;
    if (!path) return 1;
    f=fopen(path,"rb");
    if (!f) return fail(error,"Cannot open prepared newgame.c3g");
    pack=malloc(PACK_SIZE);
    if (!pack) { fclose(f); return fail(error,"Game-data allocation failed"); }
    i=(unsigned)fread(pack,1,PACK_SIZE,f);
    extra=fgetc(f); io_error=ferror(f);
    if (fclose(f)) io_error=1;
    if (i!=PACK_SIZE || extra!=EOF || io_error) {
        free(pack); return fail(error,"Prepared game data has an incorrect size");
    }
    if (memcmp(pack,"C3GS",4) || memcmp(pack+84,pin,sizeof pin) || be32(pack+104)!=1) {
        free(pack); return fail(error,"Prepared game data has an unsupported source/version");
    }
    for (i=0; i<15; ++i) if (be32(pack+4+i*4)!=fields[i]) {
        free(pack); return fail(error,"Prepared game-data section layout is invalid");
    }
    c3_save_codec_sha1(digest,pack+PACK_HEADER,PACK_SIZE-PACK_HEADER);
    if (memcmp(digest,pack+64,20)) {
        free(pack); return fail(error,"Prepared game-data checksum failed");
    }
    for (i=0; i<2; ++i) {
        const uint8_t *p=pack+STARTER_OFFSET+i*64;
        if (be16(p)!=(i?196:197) || p[2]!=(i?25:26) || p[3]!=31 ||
            be32(p+4)!=(i?0x3ff4u:0x4a6fu) || be16(p+8)!=220 ||
            !terminated_name(p+34,11,1)) {
            free(pack); return fail(error,"Prepared starter definition is invalid");
        }
        for (j=0; j<6; ++j) if (!be16(p+10+j*2) || be16(p+10+j*2)>255) {
            free(pack); return fail(error,"Prepared starter base stat is invalid");
        }
        for (j=0; j<4; ++j) if (!be16(p+22+j*3) || be16(p+22+j*3)>=358 ||
                                  !p[24+j*3] || p[24+j*3]>40) {
            free(pack); return fail(error,"Prepared starter move is invalid");
        }
    }
    for (i=0; i<25; ++i) {
        unsigned up=0,down=0;
        for (j=0; j<5; ++j) {
            const uint8_t *p=pack+NATURE_OFFSET+i*10+j*2;
            if (p[1]!=100 || (p[0]!=90 && p[0]!=100 && p[0]!=110)) {
                free(pack); return fail(error,"Prepared nature rate is invalid");
            }
            up+=p[0]==110; down+=p[0]==90;
        }
        if (up!=down || up>1) {
            free(pack); return fail(error,"Prepared nature rates are inconsistent");
        }
    }
    for (i=0; i<FLAG_COUNT; ++i) {
        const uint8_t *p=pack+FLAG_OFFSET+i*8;
        unsigned kind=p[0]>>6,width=p[0]&63;
        if (!width || width>32 || (kind && (be16(p+4)!=next[kind] ||
                                     width>capacities[kind]-next[kind]))) {
            free(pack); return fail(error,"Prepared flag range is invalid");
        }
        if (kind) next[kind]+=width;
    }
    s->pack=pack;
    return 1;
}

static int install(C3GameState *state, GameStateImpl *s, char *error)
{
    if (!valid_body(s->body)) {
        release(s); return fail(error,"Saved body is not a supported GC6E01 game state");
    }
    s->play_time=read_float(s->body+0x20);
    release(state->impl); state->impl=s;
    if (error) error[0]=0;
    return 1;
}

int c3_game_state_decode(C3GameState *state, const void *image, size_t length,
                         const char *prepared_path, char error[C3_GAME_ERROR_SIZE])
{
    GameStateImpl *s;
    C3SaveResult result;
    if (!state || !image || (length!=C3_SAVE_GCI_SIZE && length!=C3_SAVE_RAW_SIZE))
        return fail(error,"Invalid game-state import arguments");
    s=calloc(1,sizeof *s);
    if (!s) return fail(error,"Game-state allocation failed");
    s->body=malloc(C3_SAVE_BODY_SIZE); s->card=malloc(length); s->card_length=length;
    if (!s->body || !s->card) { release(s); return fail(error,"Game-state allocation failed"); }
    memcpy(s->card,image,length);
    result=c3_save_decode(s->card,length,s->body,C3_SAVE_BODY_SIZE,NULL);
    if (result!=C3_SAVE_OK) { release(s); return fail(error,c3_save_result_string(result)); }
    s->random=be32(s->body+0x14);
    if (!load_pack(s,prepared_path,error)) { release(s); return 0; }
    return install(state,s,error);
}

int c3_game_state_load(C3GameState *state, const char *path,
                       const char *prepared_path, char error[C3_GAME_ERROR_SIZE])
{
    uint8_t *image;
    size_t length=0;
    int used_backup=0, result;
    C3SaveResult loaded;
    if (!state || !path) return fail(error,"Invalid game-state load arguments");
    image=malloc(C3_SAVE_GCI_SIZE);
    if (!image) return fail(error,"Game-state allocation failed");
    loaded=c3_save_load_file(path,image,C3_SAVE_GCI_SIZE,&length,NULL,&used_backup);
    if (loaded!=C3_SAVE_OK) { free(image); return fail(error,c3_save_result_string(loaded)); }
    result=c3_game_state_decode(state,image,length,prepared_path,error);
    free(image);
    if (result) ((GameStateImpl*)state->impl)->used_backup=(unsigned)used_backup;
    return result;
}

/* HSD_Rand / fn_801ADCD8, with explicit per-state storage instead of the
 * original global pointer. Unsigned wrap and its full 16-bit result matter. */
static uint32_t random16(uint32_t *seed)
{ *seed=*seed*214013u+2531011u; return *seed>>16; }

static uint32_t random_low_first(uint32_t *seed)
{
    uint32_t low=random16(seed);
    return low | random16(seed)<<16;
}

static uint32_t random_high_first(uint32_t *seed)
{
    uint32_t high=random16(seed);
    return high<<16 | random16(seed);
}

static int create_starter(GameStateImpl *s, unsigned index, uint32_t trainer)
{
    static const unsigned stat_order[6]={C3_STAT_HP,C3_STAT_ATTACK,C3_STAT_DEFENSE,
                                       C3_STAT_SP_ATTACK,C3_STAT_SP_DEFENSE,C3_STAT_SPEED};
    const uint8_t *data=s->pack+STARTER_OFFSET+index*64;
    uint8_t *p=s->body+PARTY+index*POKEMON_SIZE;
    uint32_t personality,initial_personality,iv1,iv2,ability,attempt;
    C3Pokemon native={0};
    const uint8_t *nature;
    unsigned i;

    /* pokemonCreate's initial personality is superseded by RndFit below, but
     * both draws must occur. Retail PPC at80124168/80124170 draws HIGH first;
     * heroCreate and memoInit draw LOW first. Do not rely on C argument order. */
    initial_personality=random_high_first(&s->random);
    (void)initial_personality;
    iv1=random16(&s->random); iv2=random16(&s->random);
    ability=random16(&s->random)&1u;
    if (!data[57]) ability=0;
    /* pokemonCreateRndFit(sex0,nature-1,rare0,trainer): male, any nature,
     * non-shiny. A finite bound rejects corrupted execution instead of hang. */
    for (attempt=0; attempt<65536u; ++attempt) {
        uint32_t rare;
        personality=random_high_first(&s->random);
        rare=(trainer>>16)^(trainer&65535u)^(personality>>16)^(personality&65535u);
        if ((personality&255u)>=data[3] && rare>=8) break;
    }
    if (attempt==65536u) return 0;
    put16(p,be16(data)); put32(p+4,personality);
    memcpy(p+8,s->body,4);
    put16(p+0x0c,0x00fe); p[0x0e]=data[2]; p[0x0f]=4; p[0x10]=0;
    put32(p+0x14,trainer); memcpy(p+0x18,s->body+HERO,22);
    memcpy(p+0x2e,data+34,22); memcpy(p+0x44,data+34,22);
    put32(p+0x5c,be32(data+4)); p[0x60]=data[2];
    for (i=0; i<4; ++i) {
        put16(p+0x78+i*4,be16(data+22+i*3));
        p[0x7a+i*4]=data[24+i*3]; p[0x7b+i*4]=0;
    }
    put16(p+0xa4,(uint16_t)(iv1&31));
    put16(p+0xa6,(uint16_t)((iv1>>5)&31));
    put16(p+0xa8,(uint16_t)((iv1>>10)&31));
    put16(p+0xaa,(uint16_t)((iv2>>5)&31));
    put16(p+0xac,(uint16_t)((iv2>>10)&31));
    put16(p+0xae,(uint16_t)(iv2&31));
    put16(p+0xb0,be16(data+8)); p[0xcc]=(uint8_t)ability;
    native.species_id=be16(data);
    c3_pokemon_neutral_nature(&native);
    nature=s->pack+NATURE_OFFSET+(personality%25u)*10;
    for (i=0; i<6; ++i) {
        unsigned stat=stat_order[i];
        native.base_stats[stat]=be16(data+10+i*2);
        native.iv[stat]=(uint8_t)be16(p+0xa4+i*2);
        if (i) {
            native.nature[stat].numerator=nature[(i-1)*2];
            native.nature[stat].denominator=nature[(i-1)*2+1];
        }
    }
    if (c3_pokemon_recalculate(&native,data[2])!=C3_POKEMON_OK) return 0;
    put16(p+0x8a,native.current_hp);
    for (i=0; i<6; ++i) put16(p+0x8c+i*2,native.stats[stat_order[i]]);
    return 1;
}

int c3_game_state_new(C3GameState *state, const char *name, uint32_t seed,
                      const char *prepared_path, char error[C3_GAME_ERROR_SIZE])
{
    GameStateImpl *s,*old;
    unsigned i,length=0,nonspace=0;
    uint32_t trainer;
    if (!state || !prepared_path) return fail(error,"New Game requires prepared original data");
    if (!name || !*name) name="WES";
    while (name[length] && length<=C3_GAME_NAME_MAX) {
        unsigned c=(unsigned char)name[length];
        if (c<32 || c>126) return fail(error,"Name must use printable ASCII characters");
        nonspace+=c!=' '; ++length;
    }
    if (!length || length>C3_GAME_NAME_MAX || !nonspace)
        return fail(error,"Name must contain 1 to 7 characters, not only spaces");
    s=calloc(1,sizeof *s);
    if (!s) return fail(error,"Game-state allocation failed");
    if (!load_pack(s,prepared_path,error)) { release(s); return 0; }
    s->body=malloc(C3_SAVE_BODY_SIZE);
    old=state->impl;
    s->card_length=old?old->card_length:C3_SAVE_GCI_SIZE;
    s->card=calloc(1,s->card_length);
    if (!s->body || !s->card) { release(s); return fail(error,"Game-state allocation failed"); }
    memcpy(s->body,s->pack+BODY_OFFSET,C3_SAVE_BODY_SIZE);
    if (old) {
        /* Original New Game keeps the card generation/artwork while replacing
         * the live body. Disk and older slots remain untouched until Save. */
        memcpy(s->card,old->card,s->card_length);
        s->body[0x29]=old->body[0x29]; s->body[0x2a]=old->body[0x2a];
        memcpy(s->body+8,old->body+8,8); /* Preserve logical card identity. */
    } else {
        memcpy(s->card,s->pack+PREFIX_OFFSET,PREFIX_SIZE);
        for (i=0; i<3; ++i) {
            uint8_t *slot=s->card+0x40+SLOT_BASE+i*C3_SAVE_SLOT_SIZE;
            slot[0]=slot[1]=1; put32(slot+0x0c,0xfeff0000u);
        }
    }
    s->random=seed;
    /* savedataInit calls memoInit before heroCreate: 500 two-draw words. */
    for (i=0; i<500; ++i) {
        uint32_t value=random_low_first(&s->random);
        put32(s->body+MEMO+12+i*12,value==UINT32_MAX?0:value);
    }
    trainer=random_low_first(&s->random);
    memset(s->body+HERO,0,22);
    for (i=0; i<length; ++i) put16(s->body+HERO+i*2,(uint8_t)name[i]);
    put32(s->body+HERO+0x2c,trainer);
    if (!create_starter(s,0,trainer) || !create_starter(s,1,trainer)) {
        release(s); return fail(error,"Original starter initialization failed");
    }
    put16(s->body+MEMO,2);
    for (i=0; i<2; ++i) {
        const uint8_t *p=s->body+PARTY+i*POKEMON_SIZE;
        put16(s->body+MEMO+4+i*12,(uint16_t)(be16(p)|0x8000u));
        put32(s->body+MEMO+8+i*12,trainer);
        put32(s->body+MEMO+12+i*12,be32(p+4));
    }
    return install(state,s,error);
}

static void display_name(char out[32], const uint8_t *name)
{
    unsigned i;
    for (i=0; i<10; ++i) {
        uint16_t c=be16(name+2u*i);
        if (!c) break;
        out[i]=(c>=32 && c<=126)?(char)c:'?';
    }
    out[i]=0;
}

int c3_game_state_summary(const C3GameState *state, C3GameSummary *summary)
{
    const GameStateImpl *s;
    const uint8_t *b;
    unsigned i;
    if (!state || !state->impl || !summary) return 0;
    s=state->impl; b=s->body;
    memset(summary,0,sizeof *summary);
    display_name(summary->name,b+HERO);
    display_name(summary->partner,b+HERO+0xac2);
    summary->trainer_id=be32(b+HERO+0x2c); summary->money=be32(b+HERO+0xa84);
    summary->play_seconds=(uint32_t)s->play_time; summary->saved_count=be32(b+0x10);
    summary->floor=be32(b+0x1c); summary->previous_floor=be32(b+0x24);
    summary->spawn=b[0x28]; summary->rumble_disabled=b[0x29]; summary->sound_mono=b[0x2a];
    summary->used_backup=(uint8_t)s->used_backup;
    for (i=0; i<6; ++i) {
        const uint8_t *p=b+PARTY+i*POKEMON_SIZE;
        C3GamePokemon *q=&summary->party[i];
        q->species=be16(p); q->level=p[0x60]; q->personality=be32(p+4);
        q->hp=be16(p+0x8a); q->max_hp=be16(p+0x8c);
        q->experience=be32(p+0x5c); q->shadow_id=be16(p+0xd8);
        summary->party_count+=q->species!=0;
    }
    return 1;
}

int c3_game_pokemon_experience(const C3GamePokemon *p,uint32_t *progress,uint32_t *range)
{
    if(!progress||!range)return 0;
    *progress=*range=0;
    if(!p||(p->species!=196&&p->species!=197)||p->shadow_id||p->level<1||p->level>100)return 0;
    /* pokemonDataBiosGetGrowDataId: both species select bank 0. Checked against
     * original DOL 0x8035E940: all levels 1..100 use level cubed (entry 0 is
     * zero). Match pokemonToMenuPokemonStatusSubBar's interval. */
    if(p->level==100)return 1;
    uint32_t level=p->level,base=level*level*level,next=(level+1u)*(level+1u)*(level+1u);
    *range=next-base;
    *progress=p->experience>base?p->experience-base:0;
    if(*progress>*range)*progress=*range;
    return 1;
}

const uint8_t *c3_game_state_body(const C3GameState *state, size_t *length)
{
    const GameStateImpl *s=state?state->impl:NULL;
    if (length) *length=s?C3_SAVE_BODY_SIZE:0;
    return s?s->body:NULL;
}

int c3_game_state_set_floor(C3GameState *state, uint32_t floor,
                            uint32_t previous_floor, uint8_t spawn)
{
    GameStateImpl *s=state?state->impl:NULL;
    if (!s || !floor || floor>65535u || previous_floor>65535u) return 0;
    put32(s->body+0x1c,floor); put32(s->body+0x24,previous_floor); s->body[0x28]=spawn;
    return 1;
}

int c3_game_state_set_options(C3GameState *state, uint8_t sound_mono,
                              uint8_t rumble_disabled)
{
    GameStateImpl *s=state?state->impl:NULL;
    if (!s || sound_mono>1 || rumble_disabled>1) return 0;
    s->body[0x2a]=sound_mono; s->body[0x29]=rumble_disabled;
    return 1;
}

int c3_game_state_add_play_time(C3GameState *state, double seconds)
{
    GameStateImpl *s=state?state->impl:NULL;
    double next;
    float stored;
    if (!s || !isfinite(seconds) || seconds<0) return 0;
    next=s->play_time+seconds;
    stored=(float)next;
    if (!isfinite(next) || next>UINT32_MAX || (double)stored>UINT32_MAX) return 0;
    s->play_time=next; put_float(s->body+0x20,stored);
    return 1;
}

static int flag_range(const GameStateImpl *s, uint32_t id,
                       size_t *offset, unsigned *shift, unsigned *width)
{
    static const size_t bases[4]={0,0x1bdbc,0x1bddc,0x1be5c};
    const uint8_t *definition;
    unsigned type,bit;
    if (!s || !s->pack || id>=FLAG_COUNT) return 0;
    definition=s->pack+FLAG_OFFSET+id*8;
    type=definition[0]>>6;
    if (!type) return 0;
    bit=be16(definition+4);
    *offset=bases[type]+(bit/32u)*4u; *shift=bit%32u; *width=definition[0]&63u;
    return 1;
}

int c3_game_state_get_flag(const C3GameState *state, uint32_t id, uint32_t *value)
{
    const GameStateImpl *s=state?state->impl:NULL;
    size_t offset;
    unsigned shift,width;
    uint64_t bits,mask;
    if (!value || !flag_range(s,id,&offset,&shift,&width)) return 0;
    bits=be32(s->body+offset);
    if (width+shift>32u) bits|=(uint64_t)be32(s->body+offset+4)<<32;
    mask=((uint64_t)1<<width)-1;
    *value=(uint32_t)((bits>>shift)&mask);
    return 1;
}

int c3_game_state_set_flag(C3GameState *state, uint32_t id, uint32_t value)
{
    GameStateImpl *s=state?state->impl:NULL;
    size_t offset;
    unsigned shift,width;
    uint64_t bits,mask;
    if (!flag_range(s,id,&offset,&shift,&width)) return 0;
    bits=be32(s->body+offset);
    if (width+shift>32u) bits|=(uint64_t)be32(s->body+offset+4)<<32;
    mask=(((uint64_t)1<<width)-1)<<shift;
    bits=(bits&~mask)|(((uint64_t)value<<shift)&mask);
    put32(s->body+offset,(uint32_t)bits);
    if (width+shift>32u) put32(s->body+offset+4,(uint32_t)(bits>>32));
    return 1;
}

int c3_game_state_mail_count(const C3GameState *state,unsigned *count)
{
    const GameStateImpl *s=state?state->impl:NULL;
    if(!s||!count)return 0;
    unsigned n=be16(s->body+0x7d20+0x400);if(n>512)return 0;
    *count=n;return 1;
}
int c3_game_state_mail(const C3GameState *state,unsigned index,C3GameMail *mail)
{
    unsigned count;const GameStateImpl *s=state?state->impl:NULL;
    if(!mail||!c3_game_state_mail_count(state,&count)||index>=count)return 0;
    unsigned id=be16(s->body+0x7d20+index*2);if(id>=512)return 0;
    *mail=(C3GameMail){(uint16_t)id,(uint16_t)index,
        (uint8_t)!!(s->body[0x7d20+0x402+id/8]&(0x80u>>(id%8)))};return 1;
}
int c3_game_state_memo_count(const C3GameState *state,unsigned *count)
{
    const GameStateImpl *s=state?state->impl:NULL;if(!s||!count)return 0;
    unsigned n=be16(s->body+MEMO);if(n>500)return 0;*count=n;return 1;
}
int c3_game_state_memo(const C3GameState *state,unsigned index,C3GameMemo *memo)
{
    unsigned count;const GameStateImpl *s=state?state->impl:NULL;
    if(!memo||!c3_game_state_memo_count(state,&count)||index>=count)return 0;
    const uint8_t *p=s->body+MEMO+index*12;uint16_t raw=be16(p+4),species=raw&0x3fffu;
    if(!species||species>=413)return 0;
    *memo=(C3GameMemo){species,raw,be32(p+8),be32(p+12)};return 1;
}
int c3_game_state_item(const C3GameState *state,unsigned pocket,unsigned slot,C3GameItem *item)
{
    static const unsigned offsets[]={0x780,0x7d0,0x87c,0x8bc,0x9bc,0xa74};
    static const unsigned counts[]={20,43,16,64,46,3};
    const GameStateImpl *s=state?state->impl:NULL;
    if(!s||!item||pocket>=6||slot>=counts[pocket])return 0;
    const uint8_t *p=s->body+HERO+offsets[pocket]+slot*4;
    *item=(C3GameItem){be16(p),be16(p+2)};return 1;
}
int c3_game_state_pokemon(const C3GameState *state,unsigned slot,uint8_t record[0x138])
{
    const GameStateImpl *s=state?state->impl:NULL;if(!s||!record||slot>=6)return 0;
    memcpy(record,s->body+PARTY+slot*POKEMON_SIZE,POKEMON_SIZE);return 1;
}
int c3_game_state_mail_settings(const C3GameState *state,unsigned *sort,unsigned *tone)
{
    const GameStateImpl *s=state?state->impl:NULL;if(!s||!sort||!tone)return 0;
    *sort=s->body[0x7d20+0x442];*tone=s->body[0x7d20+0x443];return *sort<4&&*tone<10;
}
int c3_game_state_mail_preferences(C3GameState *state,int sort,int tone)
{
    GameStateImpl *s=state?state->impl:NULL;if(!s||sort < -1||sort>3||tone < -1||tone>9)return 0;
    if(sort>=0)s->body[0x7d20+0x442]=(uint8_t)sort;
    if(tone>=0)s->body[0x7d20+0x443]=(uint8_t)tone;
    return 1;
}
int c3_game_state_mail_read(C3GameState *state,const C3GameMailRead *a,int commit)
{
    GameStateImpl *s=state?state->impl:NULL;unsigned count,found=0;uint32_t value;
    if(!s||!a||a->id>=512||a->flag_count>C3_GAME_MAIL_FLAG_MAX||a->hint_count>C3_GAME_MAIL_HINT_MAX||!c3_game_state_mail_count(state,&count))return 0;
    for(unsigned i=0;i<count;++i){C3GameMail mail;if(!c3_game_state_mail(state,i,&mail))return 0;
        if(mail.id==a->id){found=1;if(mail.read)return 1;}}
    if(!found)return 0;
    for(unsigned i=0;i<a->flag_count;++i)
        if(!a->flags[i].id||!c3_game_state_get_flag(state,a->flags[i].id,&value))return 0;
    for(unsigned i=0;i<a->hint_count;++i){const C3GameMailHint *h=&a->hints[i];
        if(!h->id||h->id>96||!h->level||
           (h->unlock_flag&&!c3_game_state_get_flag(state,h->unlock_flag,&value))||
           (h->level_flag&&!c3_game_state_get_flag(state,h->level_flag,&value)))return 0;
    }
    if(!commit)return 1;
    /* Validate first, then apply to a private body so any failure is atomic.
     * The temporary implementation only borrows card/pack; never release it. */
    GameStateImpl next=*s;next.body=malloc(C3_SAVE_BODY_SIZE);if(!next.body)return 0;
    memcpy(next.body,s->body,C3_SAVE_BODY_SIZE);C3GameState temporary={&next};int ok=1;
    for(unsigned i=0;i<a->flag_count&&ok;++i)ok=c3_game_state_set_flag(&temporary,a->flags[i].id,a->flags[i].value);
    for(unsigned i=0;i<a->hint_count&&ok;++i){const C3GameMailHint *h=&a->hints[i];
        if(h->level_flag){c3_game_state_get_flag(&temporary,h->level_flag,&value);
            if(value>=h->level)continue;}
        uint8_t *block=next.body+0x1b014,*order=block+h->id*24u+0x4a4;
        if(be16(order)==0xffffu){uint32_t n=be32(block);if(n>=97){ok=0;break;}put16(order,(uint16_t)n);put32(block,n+1u);}
        if(h->unlock_flag)ok=c3_game_state_set_flag(&temporary,h->unlock_flag,1);
        if(ok&&h->level_flag)ok=c3_game_state_set_flag(&temporary,h->level_flag,h->level);
    }
    if(ok){next.body[0x7d20+0x402+a->id/8]|=(uint8_t)(0x80u>>(a->id%8));
        if(a->tone!=0xffffu)put16(next.body+0x7d20+0x444,a->tone);
        memcpy(s->body,next.body,C3_SAVE_BODY_SIZE);}
    free(next.body);return ok;
}

int c3_game_state_inventory_commit(C3GameState *state,const uint8_t *before,const uint8_t *after,size_t bytes)
{
    GameStateImpl *s=state?state->impl:NULL;
    static const unsigned offsets[]={0xa0,0x1d8,0x310,0x448,0x580,0x6b8,
        0x7f0,0x840,0x8ec,0x92c,0xa2c,0xae4,0x82a8,0x1bdbc};
    static const unsigned sizes[]={312,312,312,312,312,312,80,172,64,256,184,12,6004,1696};
    if(!s||!before||!after||bytes!=C3_SAVE_BODY_SIZE)return 0;
    for(unsigned i=0;i<14;++i)if(memcmp(before+offsets[i],after+offsets[i],sizes[i])&&
        memcmp(s->body+offsets[i],before+offsets[i],sizes[i]))return 0;
    for(size_t i=0;i<bytes;++i)if(before[i]!=after[i]){
        unsigned allowed=0;for(unsigned j=0;j<14;++j)if(i>=offsets[j]&&i-offsets[j]<sizes[j]){allowed=1;break;}
        if(!allowed)return 0;
    }
    uint8_t *next=malloc(bytes);if(!next)return 0;memcpy(next,s->body,bytes);
    for(unsigned i=0;i<14;++i)if(memcmp(before+offsets[i],after+offsets[i],sizes[i]))memcpy(next+offsets[i],after+offsets[i],sizes[i]);
    int ok=valid_body(next);if(ok)memcpy(s->body,next,bytes);free(next);return ok;
}

int c3_game_state_save(C3GameState *state, const char *path,
                       char error[C3_GAME_ERROR_SIZE])
{
    GameStateImpl *s=state?state->impl:NULL;
    uint8_t *card,*body,padding[20];
    uint32_t random,count;
    C3SaveResult result;
    unsigned i;
    if (!s || !path) return fail(error,"No game state is available to save");
    count=be32(s->body+0x10);
    if (count>=INT32_MAX) return fail(error,"Game save counter is exhausted");
    card=malloc(s->card_length); body=malloc(C3_SAVE_BODY_SIZE);
    if (!card || !body) { free(card); free(body); return fail(error,"Save allocation failed"); }
    memcpy(card,s->card,s->card_length); memcpy(body,s->body,C3_SAVE_BODY_SIZE);
    put32(body+0x10,count+1); memset(body+4,0,4); memset(body+0x18,0,4);
    random=s->random;
    for (i=0; i<20; ++i) padding[i]=(uint8_t)random16(&random);
    result=c3_save_update(card,s->card_length,body,C3_SAVE_BODY_SIZE,padding,NULL);
    if (result==C3_SAVE_OK) result=c3_save_store_file(path,card,s->card_length);
    if (result!=C3_SAVE_OK) {
        free(card); free(body); return fail(error,c3_save_result_string(result));
    }
    free(s->card); free(s->body); s->card=card; s->body=body;
    s->random=random; s->used_backup=0;
    if (error) error[0]=0;
    return 1;
}
