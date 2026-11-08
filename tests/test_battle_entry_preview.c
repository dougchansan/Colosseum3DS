/* Entry-built FightPokemon driving the nonpersistent preview, over private
 * inputs. Needs C3_ENTRY_PACK (C3E1), C3_ENTRY_NEWGAME (newgame.c3g) and
 * C3_PREVIEW_DIR (willie.c3b, willie.c3i, willie.c3p); otherwise it only
 * checks the unfinished-session refusal. Scene services are recording test
 * doubles, not the console scene adapter. */
#include "colosseum3ds/battle_entry_pack.h"
#include "colosseum3ds/battle_entry_preview.h"
#include "colosseum3ds/game_state.h"
#include "save_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: CHECK(%s)\n",__FILE__,__LINE__,#x);exit(1);}}while(0)

static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static int32_t state(void *u,uint32_t g){(void)u;(void)g;return 0;}
static int request(void *u,uint32_t g){(void)u;(void)g;return 1;}
static uint32_t find(void *u,uint32_t g,uint32_t m){(void)u;return g&&m?(g<<8)^m:0;}
static uint32_t allocate(void *u){return ++*(uint32_t *)u;}
static int fields(void *u,uint32_t q,uint16_t i,uint16_t g,uint8_t f){(void)u;(void)q;(void)i;(void)g;return !f;}
static int load(void *u,uint32_t q,uint32_t r){(void)u;(void)q;(void)r;return 1;}
static void release(void *u,uint32_t q){(void)u;(void)q;}
static int hide(void *u,uint32_t q){(void)u;(void)q;return 1;}
static void report(void *u,uint32_t g){(void)u;(void)g;CHECK(0);}
static int grid_add(void *u,uint32_t q,uint8_t x,uint8_t y){(void)u;(void)q;(void)x;(void)y;return 1;}
static int grid_update(void *u){(void)u;return 1;}
static int visible(void *u,uint32_t q,int v){(void)u;(void)q;return v==1;}
static void abort_owned(void *u,const C3BattleEntryOwnership *o,size_t n){(void)u;(void)o;(void)n;CHECK(0);}

static uint8_t *read_file(const char *path,size_t *size)
{
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
    uint8_t *data=malloc((size_t)n);CHECK(data);CHECK(fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);*size=(size_t)n;return data;
}
static void print_mons(const char *label,const C3BattlePreview *b)
{
    for(unsigned a=0;a<4;++a){const C3BattlePreviewMon *m=&b->mons[a];
        printf("%s actor=%u %s lv%u hp=%u/%u atk=%u def=%u spa=%u spd=%u spe=%u moves=%u,%u,%u,%u\n",label,a,
            c3_battle_preview_name(a),m->level,m->hp,m->maximum_hp,m->stats[C3_STAT_ATTACK],m->stats[C3_STAT_DEFENSE],
            m->stats[C3_STAT_SP_ATTACK],m->stats[C3_STAT_SP_DEFENSE],m->stats[C3_STAT_SPEED],
            m->moves[0].id,m->moves[1].id,m->moves[2].id,m->moves[3].id);}
}
static C3BattlePreviewCommand command(const C3BattlePreview *b,const C3BattlePreviewTables *t,unsigned a)
{
    C3BattlePreviewCommand c={0,(uint8_t)(b->mons[2].hp?2:3)};
    uint8_t mask=c3_battle_preview_legal_moves(b,t,a);
    if(!mask)c.slot=4;else while(!(mask&(1u<<c.slot)))++c.slot;
    return c;
}
static void real(const char *pack_path,const char *newgame,const char *dir)
{
    char path[1024],error[C3_BATTLE_ERROR_SIZE]={0},game_error[C3_GAME_ERROR_SIZE]={0};
    size_t pack_size;uint8_t *bytes=read_file(pack_path,&pack_size);
    C3BattleEntryPack *pack=malloc(sizeof *pack);CHECK(pack);
    CHECK(c3_battle_entry_pack_decode(pack,bytes,pack_size,error));
    /* C3_ENTRY_SAVE optionally replays a real (disposable) GCI instead of a
     * fresh game, to predict a console run over the same save. */
    C3GameState game={0};const char *save=getenv("C3_ENTRY_SAVE");
    if(save?!c3_game_state_load(&game,save,newgame,game_error):!c3_game_state_new(&game,"WES",0x12345678u,newgame,game_error))
        {fprintf(stderr,"%s\n",game_error);exit(1);}
    size_t n=0;const uint8_t *body=c3_game_state_body(&game,&n);CHECK(body);
    uint8_t *before=malloc(n);CHECK(before);memcpy(before,body,n);

    C3BattleData data;C3BattleRequest r;C3BattleInitTables init;C3BattlePreviewTables t;
    snprintf(path,sizeof path,"%s/willie.c3b",dir);CHECK(c3_battle_load(&data,path,error));
    snprintf(path,sizeof path,"%s/willie.c3i",dir);CHECK(c3_battle_init_load(&init,path,error));
    snprintf(path,sizeof path,"%s/willie.c3p",dir);CHECK(c3_battle_preview_load(&t,path,error));
    const uint32_t args[]={10,1,0};CHECK(c3_battle_request(&r,&data,args,body,n,error));

    C3BattleEntrySessionConfig config;uint32_t sequences=0;
    CHECK(c3_battle_entry_pack_config(pack,10,body,n,0x12345678u,&config,error));
    config.sequence=(C3BattleSequenceServices){state,request,find,allocate,fields,load,release,hide,report};
    config.grid_add=grid_add;config.grid_update=grid_update;config.set_visible=visible;
    config.abort_owned=abort_owned;config.scene=&sequences;
    C3BattleEntrySession *s=calloc(1,sizeof *s);CHECK(s);
    C3BattlePreview *entry=calloc(1,sizeof *entry),*roster=calloc(1,sizeof *roster),*copy=malloc(sizeof *copy);
    CHECK(entry&&roster&&copy);
    CHECK(c3_battle_entry_session_begin(s,&config));
    CHECK(!c3_battle_entry_preview_begin(entry,&r,s,&t,error)); /* not finished yet */
    int step;unsigned guard=0;
    while((step=c3_battle_entry_session_step(s))==1)CHECK(++guard<64);
    CHECK(step==0);
    C3BattleRng after_entry=s->rng;
    CHECK(c3_battle_entry_preview_begin(entry,&r,s,&t,error));
    CHECK(!memcmp(&s->rng,&after_entry,sizeof after_entry)); /* session not written */
    CHECK(c3_battle_preview_begin(roster,&r,&init,&t,0x12345678u,error));
    print_mons("entry",entry);print_mons("roster",roster);
    printf("entry_rng seed=%08lx draws=%llu roster_rng draws=%llu\n",(unsigned long)entry->rng.seed,
        (unsigned long long)entry->rng.draws,(unsigned long long)roster->rng.draws);
    /* Player side is the saved party on both paths. */
    for(unsigned a=0;a<2;++a)CHECK(!memcmp(&entry->mons[a],&roster->mons[a],sizeof entry->mons[a]));
    for(unsigned a=2;a<4;++a){const uint8_t *f=c3_battle_entry_session_fighter(s,1,a-2)+4;
        CHECK(entry->mons[a].species==288&&entry->mons[a].species==be16(f));
        CHECK(entry->mons[a].level==f[0x60]&&entry->mons[a].hp==be16(f+0x8a)&&entry->mons[a].maximum_hp==be16(f+0x8c));}
    CHECK(entry->rng.draws==after_entry.draws&&entry->rng.seed==after_entry.seed);

    /* Play the entry-started battle to an outcome with a fixed policy. */
    *copy=*entry;
    for(unsigned turn=0;turn<64&&entry->outcome==C3_PREVIEW_RUNNING;++turn){
        C3BattlePreviewCommand c[2]={command(entry,&t,0),command(entry,&t,1)};
        if(!c3_battle_preview_turn(entry,&t,c,error)){fprintf(stderr,"turn: %s\n",error);CHECK(0);}
        printf("entry_turn %u draws=%llu hp=%u,%u,%u,%u\n",entry->turn,(unsigned long long)entry->rng.draws,
            entry->mons[0].hp,entry->mons[1].hp,entry->mons[2].hp,entry->mons[3].hp);
        for(unsigned e=0;e<entry->event_count;++e)printf("  event %s\n",entry->events[e].text);
        CHECK(!memcmp(body,before,n));
    }
    CHECK(entry->outcome!=C3_PREVIEW_RUNNING);
    printf("entry_outcome=%u turns=%lu\n",(unsigned)entry->outcome,(unsigned long)entry->turn);
    /* Replaying from the handoff reproduces the same battle. */
    for(unsigned turn=0;turn<64&&copy->outcome==C3_PREVIEW_RUNNING;++turn){
        C3BattlePreviewCommand c[2]={command(copy,&t,0),command(copy,&t,1)};CHECK(c3_battle_preview_turn(copy,&t,c,error));}
    CHECK(copy->outcome==entry->outcome&&copy->turn==entry->turn&&!memcmp(&copy->rng,&entry->rng,sizeof copy->rng));
    CHECK(!memcmp(body,before,n));
    free(copy);free(roster);free(entry);free(s);free(before);c3_game_state_free(&game);free(pack);free(bytes);
}
int main(void)
{
    C3BattleEntrySession *s=calloc(1,sizeof *s);C3BattlePreview b;C3BattleRequest r;C3BattlePreviewTables t;char e[C3_BATTLE_ERROR_SIZE];
    CHECK(s);memset(&r,0,sizeof r);memset(&t,0,sizeof t);
    CHECK(!c3_battle_entry_preview_begin(&b,&r,s,&t,e)&&strstr(e,"did not finish"));
    CHECK(!c3_battle_entry_preview_begin(&b,&r,NULL,&t,e));
    free(s);
    const char *pack=getenv("C3_ENTRY_PACK"),*newgame=getenv("C3_ENTRY_NEWGAME"),*dir=getenv("C3_PREVIEW_DIR");
    if(!pack||!newgame||!dir){puts("entry_preview private inputs absent; refusal checks only");return 0;}
    real(pack,newgame,dir);
    puts("entry_preview OK");
    return 0;
}
