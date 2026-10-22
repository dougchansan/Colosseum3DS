#include "colosseum3ds/battle_preview.h"
#include "save_codec.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAGIC UINT32_C(0x43504231)
static const uint16_t move_ids[12]={33,39,44,45,93,115,165,216,269,270,289,290};
static const uint16_t species_ids[3]={197,196,288};
static const uint8_t source_pin[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
static const uint8_t common_pin[20]={0xe8,0x16,0x76,0x18,0x13,0x90,0x0a,0xa0,0xfd,0x4f,0x87,0xd0,0x12,0xb3,0xb2,0xa1,0x9d,0x2f,0x00,0x3e};
static const uint8_t dol_pin[20]={0x87,0x0e,0x8b,0x96,0x93,0xca,0x78,0x07,0x82,0xd8,0x0f,0x22,0xa6,0xa4,0x57,0x2d,0x8b,0xa9,0x45,0x8f};
static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static int fail(char *e,const char *message){if(e)snprintf(e,C3_BATTLE_ERROR_SIZE,"%s",message);return 0;}
static int overlap(const void *a,size_t as,const void *b,size_t bs)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>=y?x-y<bs:y-x<as;}

const char *c3_battle_preview_name(unsigned actor)
{
    static const char *const names[]={"UMBREON","ESPEON","ZIGZAGOON A","ZIGZAGOON B"};
    return actor<4?names[actor]:"?";
}
const char *c3_battle_preview_move_name(uint16_t id)
{
    static const char *const names[]={"TACKLE","TAIL WHIP","BITE","GROWL","CONFUSION","REFLECT",
        "STRUGGLE","RETURN","TAUNT","HELPING HAND","SNATCH","SECRET POWER"};
    for(unsigned i=0;i<12;++i)if(move_ids[i]==id)return names[i];
    return "--";
}
static int tables_valid(const C3BattlePreviewTables *t)
{
    uint8_t digest[20];const uint8_t *p=t?t->bytes:NULL;
    if(!p||memcmp(p,"C3P1",4)||be32(p+4)!=1||be32(p+8)!=C3_PREVIEW_PACK_SIZE||be32(p+12)!=10||
       memcmp(p+16,source_pin,20)||memcmp(p+36,common_pin,20)||memcmp(p+96,dol_pin,20))return 0;
    c3_save_codec_sha1(digest,p+128,C3_PREVIEW_PACK_SIZE-128);
    if(memcmp(digest,p+56,20))return 0;
    for(unsigned i=116;i<128;++i)if(p[i])return 0;
    for(unsigned i=0;i<12;++i){
        const uint8_t *r=p+128+60*i;
        if(be16(r)!=move_ids[i]||be16(r+2)||r[4]>7||!r[5]||r[5]>40||
           (r[6]!=0&&r[6]!=14&&r[6]!=17)||r[7]>5||r[8]>100||r[9]>100||r[13]>1||be16(r+26)>250)return 0;
    }
    for(unsigned i=0;i<3;++i){const uint8_t *r=p+848+12*i;
        if(be16(r)!=species_ids[i]||r[2]!=(i==0?17:i==1?14:0)||r[3]!=r[2]||
           r[4]!=(i==2?53:28)||r[5])return 0;
        for(unsigned j=6;j<12;++j)if(r[j])return 0;
    }
    for(unsigned i=0;i<13;++i)
        if(!p[884+2*i]||p[884+2*i]>40||p[885+2*i]<10||p[885+2*i]>40||
           !p[910+2*i]||!p[911+2*i])return 0;
    if(p[936]!=16||p[937]!=27||p[938]||p[939])return 0;
    for(unsigned i=974;i<C3_PREVIEW_PACK_SIZE;++i)if(p[i])return 0;
    return 1;
}
int c3_battle_preview_decode(C3BattlePreviewTables *out,const void *p,size_t n,char *e)
{
    C3BattlePreviewTables next;
    if(!out||!p||n!=sizeof next)return fail(e,"Invalid combat-preview table size.");
    memcpy(&next,p,sizeof next);
    if(!tables_valid(&next))return fail(e,"Unsupported or damaged combat-preview tables.");
    *out=next;if(e)e[0]=0;return 1;
}
int c3_battle_preview_load(C3BattlePreviewTables *out,const char *path,char *e)
{
    uint8_t p[C3_PREVIEW_PACK_SIZE];
    if(!out||!path)return fail(e,"Missing combat-preview table path.");
    FILE *f=fopen(path,"rb");if(!f)return fail(e,"Prepare willie.c3p to enable the combat preview.");
    size_t n=fread(p,1,sizeof p,f);int extra=fgetc(f),bad=ferror(f);if(fclose(f))bad=1;
    if(bad||extra!=EOF||n!=sizeof p)return fail(e,"Truncated or oversized combat-preview tables.");
    return c3_battle_preview_decode(out,p,n,e);
}
int c3_battle_preview_move(const C3BattlePreviewTables *t,uint16_t id,C3BattlePreviewMove *out)
{
    if(!t||!out||memcmp(t->bytes,"C3P1",4))return 0;
    for(unsigned i=0;i<12;++i)if(move_ids[i]==id){const uint8_t *p=t->bytes+132+60*i;
        C3BattlePreviewMove m={0};
        m.id=id;m.priority=(int8_t)p[0];m.pp=p[1];m.type=p[2];m.range=p[3];
        m.accuracy=p[4];m.secondary=p[5];m.snatchable=p[9];m.power=be16(p+22);m.effect=be16(p+26);
        *out=m;return 1;
    }
    return 0;
}
static int event(C3BattlePreview *b,const char *format,...)
{
    if(b->event_count>=C3_PREVIEW_EVENT_LIMIT)return 0;
    C3BattlePreviewEvent *e=&b->events[b->event_count++];
    va_list ap;va_start(ap,format);vsnprintf(e->text,sizeof e->text,format,ap);va_end(ap);
    for(unsigned i=0;i<4;++i)e->hp[i]=b->mons[i].hp;
    e->draws=b->rng.draws;return 1;
}
static int random_mod(C3BattlePreview *b,unsigned modulus,unsigned *out)
{
    uint16_t v;
    if(!modulus||!c3_battle_rng_next16(&b->rng,&v))return 0;
    *out=v%modulus;return 1;
}
static int state_valid(const C3BattlePreview *b,const C3BattlePreviewTables *t)
{
    if(!b||b->magic!=MAGIC||b->outcome<C3_PREVIEW_RUNNING||b->outcome>C3_PREVIEW_DRAW||
       b->turn>C3_PREVIEW_MAX_TURNS||b->event_count>C3_PREVIEW_EVENT_LIMIT||!tables_valid(t))return 0;
    for(unsigned i=0;i<4;++i){const C3BattlePreviewMon *m=&b->mons[i];
        if(m->species!=species_ids[i<2?i:2]||!m->maximum_hp||m->hp>m->maximum_hp||m->maximum_hp>999||
           !m->level||m->level>100||m->ability!=(i<2?28:53)||m->types[0]!=(i==0?17:i==1?14:0)||
           m->types[1]!=m->types[0]||m->friendship>255||m->confusion>5||m->taunt>2||
           m->flinched>1||m->helped>1||m->snatch>1)return 0;
        for(unsigned j=0;j<6;++j)if(!m->stats[j]||m->stats[j]>999)return 0;
        for(unsigned j=0;j<7;++j)if(m->stages[j]<-6||m->stages[j]>6)return 0;
        for(unsigned j=0;j<4;++j){const C3BattleInitialMove *move=&m->moves[j];C3BattlePreviewMove data;
            if(!move->id){if(move->pp||move->maximum_pp||move->pp_ups)return 0;continue;}
            if(!c3_battle_preview_move(t,move->id,&data)||move->pp_ups||move->maximum_pp!=data.pp||move->pp>data.pp)return 0;
        }
    }
    return b->reflect[0]<=5&&b->reflect[1]<=5;
}
int c3_battle_preview_begin(C3BattlePreview *out,const C3BattleRequest *r,
    const C3BattleInitTables *init,const C3BattlePreviewTables *t,uint32_t seed,char *e)
{
    uint8_t digest[20];uint16_t low,high;C3BattleRng rng;
    if(!out||!r||!init||!tables_valid(t)||overlap(out,sizeof *out,r,sizeof *r)||
       overlap(out,sizeof *out,init,sizeof *init)||overlap(out,sizeof *out,t,sizeof *t))
        return fail(e,"Invalid combat-preview arguments.");
    c3_save_codec_sha1(digest,(const uint8_t *)&r->data,sizeof r->data);
    if(memcmp(digest,t->bytes+76,20)||r->data.kind[1]||r->player_party_count!=2)
        return fail(e,"Combat preview requires the ordinary two-starter encounter 10.");
    C3BattlePreview *b=calloc(1,sizeof *b);C3BattleInitialState *initial=calloc(1,sizeof *initial);
    if(!b||!initial){free(b);free(initial);return fail(e,"Combat-preview allocation failed.");}
    int ok=0;b->magic=MAGIC;b->seed=seed;b->original_request=*r;b->outcome=C3_PREVIEW_RUNNING;
    c3_battle_rng_init(&rng,seed);
    if(!c3_battle_rng_next16(&rng,&low)||!c3_battle_rng_next16(&rng,&high))goto done;
    b->opponent_trainer_id=(uint32_t)low|((uint32_t)high<<16);
    if(!c3_battle_init_prepare(initial,r,init,b->opponent_trainer_id,&rng,e))goto done;
    b->rng=rng;
    for(unsigned i=0;i<2;++i){const uint8_t *p=r->player_party[i];C3BattlePreviewMon *m=&b->mons[i];
        if(be16(p)!=species_ids[i]||be16(p+0x88)||p[0xcc]||p[0xcd]||be16(p+0xd8)){
            fail(e,"Preview admits only ordinary item-free Umbreon and Espeon.");goto done;}
        for(unsigned j=0x64;j<0x78;++j)if(p[j]){fail(e,"Persistent status import is outside the preview profile.");goto done;}
        m->species=be16(p);m->level=p[0x60];m->hp=be16(p+0x8a);m->maximum_hp=be16(p+0x8c);
        m->friendship=be16(p+0xb0);m->ability=28;m->types[0]=m->types[1]=i?14:17;
        static const unsigned order[]={C3_STAT_HP,C3_STAT_ATTACK,C3_STAT_DEFENSE,C3_STAT_SP_ATTACK,C3_STAT_SP_DEFENSE,C3_STAT_SPEED};
        for(unsigned j=0;j<6;++j)m->stats[order[j]]=be16(p+0x8c+2*j);
        for(unsigned j=0;j<4;++j){uint16_t id=be16(p+0x78+4*j);C3BattlePreviewMove data;
            if(!id)continue;
            if(id==165||!c3_battle_preview_move(t,id,&data)){fail(e,"Player move is outside the preview profile.");goto done;}
            m->moves[j]=(C3BattleInitialMove){id,p[0x7a+4*j],p[0x7b+4*j],data.pp};
        }
    }
    if(!b->mons[0].hp&&!b->mons[1].hp){fail(e,"Preview needs a non-fainted starter.");goto done;}
    for(unsigned i=2;i<6;++i)if(be16(r->player_party[i])){fail(e,"Extra party slots need switching support.");goto done;}
    for(unsigned i=0;i<2;++i){const C3BattleInitialOpponent *p=&initial->opponents[i];C3BattlePreviewMon *m=&b->mons[i+2];
        m->species=p->pokemon.species_id;m->level=p->pokemon.level;m->hp=p->pokemon.current_hp;
        m->maximum_hp=p->pokemon.stats[C3_STAT_HP];m->friendship=p->friendship;m->ability=p->ability_id;
        memcpy(m->stats,p->pokemon.stats,sizeof m->stats);memcpy(m->moves,p->moves,sizeof m->moves);
    }
    if(!state_valid(b,t)){fail(e,"Unsupported initial combat-preview state.");goto done;}
    event(b,"Nonpersistent battle preview. No retail AI or story result.");
    event(b,"WILLIE sends out two ZIGZAGOON!");
    *out=*b;if(e)e[0]=0;ok=1;
done:free(initial);free(b);return ok;
}
uint8_t c3_battle_preview_legal_moves(const C3BattlePreview *b,const C3BattlePreviewTables *t,unsigned a)
{
    if(!b||!t||b->magic!=MAGIC||a>=4||!b->mons[a].hp)return 0;
    uint8_t mask=0;
    for(unsigned i=0;i<4;++i){const C3BattleInitialMove *m=&b->mons[a].moves[i];C3BattlePreviewMove data;
        if(m->id&&m->pp&&c3_battle_preview_move(t,m->id,&data)&&(!b->mons[a].taunt||data.power))mask|=(uint8_t)(1u<<i);
    }
    return mask;
}
static int command_valid(const C3BattlePreview *b,const C3BattlePreviewTables *t,unsigned a,C3BattlePreviewCommand c)
{
    if(c.slot>4||c.target>=4)return 0;
    if(!b->mons[a].hp)return 1;
    uint8_t mask=c3_battle_preview_legal_moves(b,t,a);
    if(c.slot==4)return !mask&&c.target/2!=a/2&&b->mons[c.target].hp;
    if(!(mask&(1u<<c.slot)))return 0;
    unsigned id=b->mons[a].moves[c.slot].id;
    if(id==115||id==270||id==289)return 1;
    return c.target/2!=a/2&&b->mons[c.target].hp;
}
static unsigned speed(const C3BattlePreviewMon *m,const C3BattlePreviewTables *t)
{unsigned i=(unsigned)(6+m->stages[4]);return m->stats[C3_STAT_SPEED]*t->bytes[884+2*i]/t->bytes[885+2*i];}
static int hit(C3BattlePreview *b,const C3BattlePreviewTables *t,unsigned a,unsigned d,const C3BattlePreviewMove *m,int *yes)
{
    int stage=b->mons[a].stages[5]-b->mons[d].stages[6];
    if(stage<-6)stage=-6;
    if(stage>6)stage=6;
    unsigned index=(unsigned)(stage+6),roll;
    unsigned chance=m->accuracy*t->bytes[910+2*index]/t->bytes[911+2*index];
    if(!m->accuracy){*yes=1;return 1;}
    if(!random_mod(b,100,&roll))return 0;
    *yes=roll<chance;return 1;
}
static int hurt(C3BattlePreview *b,unsigned d,unsigned amount)
{
    if(amount>b->mons[d].hp)amount=b->mons[d].hp;
    b->mons[d].hp=(uint16_t)(b->mons[d].hp-amount);
    if(!event(b,"%s loses %u HP.",c3_battle_preview_name(d),amount))return 0;
    return b->mons[d].hp||event(b,"%s fainted!",c3_battle_preview_name(d));
}
static int lower_stage(C3BattlePreview *b,unsigned d,unsigned stat)
{
    static const char *const names[]={"Attack","Defense","Sp.Atk","Sp.Def","Speed","accuracy","evasion"};
    if(b->mons[d].stages[stat]<=-6)return event(b,"%s's %s cannot fall further.",c3_battle_preview_name(d),names[stat]);
    --b->mons[d].stages[stat];return event(b,"%s's %s fell.",c3_battle_preview_name(d),names[stat]);
}
static void check_outcome(C3BattlePreview *b)
{
    int player=b->mons[0].hp||b->mons[1].hp,opponent=b->mons[2].hp||b->mons[3].hp;
    b->outcome=player?(opponent?C3_PREVIEW_RUNNING:C3_PREVIEW_PLAYER_WON):
        opponent?C3_PREVIEW_PLAYER_LOST:C3_PREVIEW_DRAW;
}
static int execute(C3BattlePreview *b,const C3BattlePreviewTables *t,unsigned a,
    C3BattlePreviewCommand c,const uint8_t acted[4])
{
    C3BattlePreviewMon *attacker=&b->mons[a];unsigned roll;
    if(!attacker->hp)return 1;
    if(attacker->flinched)return event(b,"%s flinched!",c3_battle_preview_name(a));
    if(attacker->confusion){
        --attacker->confusion;
        if(!attacker->confusion){if(!event(b,"%s snapped out of confusion.",c3_battle_preview_name(a)))return 0;}
        else{
            if(!random_mod(b,2,&roll))return 0;
            if(!roll){uint32_t damage;uint8_t screen=b->reflect[a/2];b->reflect[a/2]=0;
                int ok=c3_battle_preview_base_damage(b,t,a,a,0,40,0,1,&damage);b->reflect[a/2]=screen;
                if(!ok||!random_mod(b,16,&roll))return 0;
                damage=damage*(100-roll)/100;if(!damage)damage=1;
                return event(b,"%s hurt itself in confusion!",c3_battle_preview_name(a))&&hurt(b,a,damage);
            }
        }
    }
    uint16_t id=c.slot==4?165:attacker->moves[c.slot].id;C3BattlePreviewMove move;
    if(!c3_battle_preview_move(t,id,&move))return 0;
    if(attacker->taunt&&!move.power)return event(b,"%s cannot use %s after TAUNT!",c3_battle_preview_name(a),c3_battle_preview_move_name(id));
    if(c.slot<4){if(!attacker->moves[c.slot].pp)return 0;--attacker->moves[c.slot].pp;}
    if(!event(b,"%s used %s!",c3_battle_preview_name(a),c3_battle_preview_move_name(id)))return 0;
    if(id==289){attacker->snatch=1;return event(b,"%s is waiting to SNATCH a move.",c3_battle_preview_name(a));}
    if(id==270){unsigned ally=a^1u;
        if(!b->mons[ally].hp||acted[ally])return event(b,"But HELPING HAND failed.");
        b->mons[ally].helped=1;return event(b,"%s is ready to help %s.",c3_battle_preview_name(a),c3_battle_preview_name(ally));}
    if(id==115){unsigned user=a;
        if(move.snatchable)for(unsigned i=0;i<4;++i)if(i!=a&&b->mons[i].hp&&b->mons[i].snatch){
            b->mons[i].snatch=0;user=i;if(!event(b,"%s snatched REFLECT!",c3_battle_preview_name(i)))return 0;break;}
        if(b->reflect[user/2])return event(b,"But REFLECT is already active.");
        b->reflect[user/2]=5;return event(b,"REFLECT protects %s's team.",user<2?"WES":"WILLIE");}
    if(id==39||id==45){for(unsigned d=(1-a/2)*2;d<(1-a/2)*2+2;++d)if(b->mons[d].hp){int success;
            if(!hit(b,t,a,d,&move,&success))return 0;
            if(success){if(!lower_stage(b,d,id==39?1:0))return 0;}
            else if(!event(b,"It missed %s.",c3_battle_preview_name(d)))return 0;
        }return 1;}
    unsigned d=c.target;
    if(!b->mons[d].hp){d^=1u;if(!b->mons[d].hp)return event(b,"There is no target remaining.");}
    int success;if(!hit(b,t,a,d,&move,&success))return 0;
    if(!success)return event(b,"The attack missed %s.",c3_battle_preview_name(d));
    if(id==269){if(b->mons[d].taunt)return event(b,"But the target is already taunted.");
        b->mons[d].taunt=2;return event(b,"%s fell for the TAUNT!",c3_battle_preview_name(d));}
    unsigned power=id==216?attacker->friendship*10u/25u:move.power;
    if(!power)power=1;
    if(!random_mod(b,t->bytes[936],&roll))return 0;
    unsigned critical=roll==0?2:1;uint32_t damage;
    if(!c3_battle_preview_base_damage(b,t,a,d,id,(uint16_t)power,move.type,critical,&damage))return 0;
    damage*=critical;
    if(attacker->helped)damage=damage*15u/10u;
    if(id!=165&&(move.type==attacker->types[0]||move.type==attacker->types[1]))damage=damage*15u/10u;
    /* All admitted opponent-target pairs are type-neutral. Ally attacks and
     * other species need the original full type/effect system and are refused. */
    if(!random_mod(b,16,&roll))return 0;
    damage=damage*(100u-roll)/100u;if(!damage)damage=1;
    unsigned dealt=damage>b->mons[d].hp?b->mons[d].hp:damage;
    if(critical==2&&!event(b,"A critical hit!"))return 0;
    if(!hurt(b,d,damage))return 0;
    if(id==165){unsigned recoil=dealt/4;if(!recoil)recoil=1;
        return event(b,"%s is hit with recoil.",c3_battle_preview_name(a))&&hurt(b,a,recoil);}
    if(move.secondary&&b->mons[d].hp){
        if(!random_mod(b,100,&roll))return 0;
        if(roll<move.secondary){
            if(id==44&&!acted[d])b->mons[d].flinched=1;
            else if(id==93&&!b->mons[d].confusion){
                if(!random_mod(b,4,&roll))return 0;
                b->mons[d].confusion=(uint8_t)(roll+2);
                if(!event(b,"%s became confused!",c3_battle_preview_name(d)))return 0;
            }else if(id==290){if(!lower_stage(b,d,5))return 0;}
        }
    }
    return 1;
}
int c3_battle_preview_turn(C3BattlePreview *out,const C3BattlePreviewTables *t,
    const C3BattlePreviewCommand player[2],char *e)
{
    if(!out||!player||!state_valid(out,t)||out->outcome!=C3_PREVIEW_RUNNING||out->turn>=C3_PREVIEW_MAX_TURNS||
       overlap(out,sizeof *out,t,sizeof *t)||overlap(out,sizeof *out,player,2*sizeof *player))
        return fail(e,"Invalid, completed or exhausted combat preview.");
    for(unsigned a=0;a<2;++a)if(!command_valid(out,t,a,player[a]))return fail(e,"Choose a usable move and a living opponent target.");
    C3BattlePreview *b=malloc(sizeof *b);if(!b)return fail(e,"Turn allocation failed.");
    *b=*out;b->event_count=0;memset(b->events,0,sizeof b->events);++b->turn;
    C3BattlePreviewCommand commands[4]={player[0],player[1],{0,0},{0,0}};
    uint8_t order[4]={0,1,2,3},acted[4]={0};int ok=0;
    if(!event(b,"TURN %lu",(unsigned long)b->turn))goto failed;
    for(unsigned a=0;a<4;++a){b->mons[a].flinched=b->mons[a].helped=b->mons[a].snatch=0;}
    /* Deliberately explicit preview policy, NOT the original Willie AI:
     * uniform among legal move slots, then among living enemy targets. */
    for(unsigned a=2;a<4;++a)if(b->mons[a].hp){
        uint8_t mask=c3_battle_preview_legal_moves(b,t,a);unsigned count=0,slots[4],targets[2],nt=0,roll;
        for(unsigned i=0;i<4;++i)if(mask&(1u<<i))slots[count++]=i;
        commands[a].slot=4;
        if(count){if(!random_mod(b,count,&roll))goto failed;commands[a].slot=(uint8_t)slots[roll];}
        for(unsigned i=0;i<2;++i)if(b->mons[i].hp)targets[nt++]=i;
        if(!nt)goto failed;
        if(!random_mod(b,nt,&roll))goto failed;
        commands[a].target=(uint8_t)targets[roll];
    }
    /* Same pairwise traversal as fightFloorSortFightOutPokemonPtrArySub;
     * admitted commands all share one action class. Move priority and speed
     * decide order; each exact tie consumes one HSD bit, not a native sort. */
    for(unsigned i=0;i<3;++i)for(unsigned j=i+1;j<4;++j){unsigned a=order[i],d=order[j];
        if(!b->mons[d].hp)continue;
        int swap=!b->mons[a].hp;
        if(!swap){C3BattlePreviewMove ma,md;
            uint16_t ia=commands[a].slot==4?165:b->mons[a].moves[commands[a].slot].id;
            uint16_t id=commands[d].slot==4?165:b->mons[d].moves[commands[d].slot].id;
            if(!c3_battle_preview_move(t,ia,&ma)||!c3_battle_preview_move(t,id,&md))goto failed;
            unsigned sa=speed(&b->mons[a],t),sd=speed(&b->mons[d],t);
            if(ma.priority!=md.priority)swap=ma.priority<md.priority;
            else if(sa!=sd)swap=sa<sd;
            else{unsigned roll;if(!random_mod(b,2,&roll))goto failed;swap=!roll;}
        }
        if(swap){order[i]=(uint8_t)d;order[j]=(uint8_t)a;}
    }
    for(unsigned i=0;i<4;++i){unsigned a=order[i];
        if(!execute(b,t,a,commands[a],acted))goto failed;
        acted[a]=1;check_outcome(b);if(b->outcome!=C3_PREVIEW_RUNNING)break;
    }
    for(unsigned a=0;a<4;++a)if(b->mons[a].taunt)--b->mons[a].taunt;
    for(unsigned side=0;side<2;++side)if(b->reflect[side]&&!--b->reflect[side])
        if(!event(b,"%s's REFLECT wore off.",side?"WILLIE":"WES"))goto failed;
    if(b->outcome!=C3_PREVIEW_RUNNING){
        const char *result=b->outcome==C3_PREVIEW_PLAYER_WON?"WIN":b->outcome==C3_PREVIEW_PLAYER_LOST?"LOSS":"DRAW";
        if(!event(b,"Preview result: %s. No rewards, save, or story flags were written.",result))goto failed;
    }
    if(!state_valid(b,t))goto failed;
    *out=*b;if(e)e[0]=0;ok=1;goto done;
failed:fail(e,"Turn rejected: RNG/bounds/unsupported source accessor. State unchanged.");
done:free(b);return ok;
}
