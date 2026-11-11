/* Start of battle: fightActionFlowKaisiPost (kind 5) and the weather init it
 * runs (kind 6, fightActionFlowTenkouInit), lowered for Willie's battle. See
 * include/colosseum3ds/battle_start.h. */
#include "colosseum3ds/battle_start.h"
#include "colosseum3ds/battle_ai.h"
#include <string.h>

#define FLOOR_BATTLER_LIST 0xA4C4u   /* fightFloorBiosGetFightOutPokemonPtrAry (0x59) */
#define FLOOR_APPOINT_ITEM 0xA4BCu   /* 0x56 */
#define FLOOR_FIRST_ATTACK 0xA4E4u   /* fightFloorBiosGetFirstAttackRnd (0x5B) */

static int refuse(C3BattleScript *sc,const char *what)
{if(sc->status!=C3_SCRIPT_FAILED)c3_battle_script_fail(sc,"Not lowered yet: %s",what);return 0;}
static uint32_t get(const C3BattleState *s,uint32_t obj,uint32_t sel)
{uint32_t v=0;return c3_battle_fight_status_get(&s->arena,obj,sel,0,&v)?v:0;}
static uint32_t pokemon_of(const C3BattleState *s,uint32_t fo)                /* fightOutPokemonGetPokemonPtr */
{uint32_t fp=fo?get(s,fo,0xD6):0;return fp?get(s,fp,0xCC):0;}
static uint16_t condition_category(const C3BattleState *s,uint16_t id)         /* fn_80119ED0 */
{
    const C3BattleDataRows *r=&s->conditions;
    return r->rows&&id<r->count?(uint16_t)((uint16_t)r->rows[(uint32_t)id*r->stride+8]<<8|r->rows[(uint32_t)id*r->stride+9]):0;
}
static int fight_out(const C3BattleState *s,uint32_t fo,int *fault)
{return fo&&c3_battle_fight_out_check_fight_out(s,fo,fault)==1;}
static uint32_t side_at(const C3BattleState *s,unsigned side)
{return s->floor+C3_FIGHT_SIDE_OFFSET+side*C3_FIGHT_SIDE_BYTES;}
/* fightOutPokemonWriteJoutaiDataId (an erase) for the battler conditions of
 * category 0xD8, held by the battler itself; other categories are refused. */
static int erase_battler(C3BattleState *s,uint32_t fo,uint16_t id,int *fault)
{
    if(condition_category(s,id)!=0xD8)return 0;
    c3_battle_condition_erase(s,fo,id,fault);
    return 1;
}
/* pokemonIsDarkPokemon on the battler's Pokemon, as the EXP award reads it:
 * a Shadow ID (+0xD8) with a non-negative heart gauge (+0xDC). */
static int is_shadow(const C3BattleState *s,uint32_t fo,int *fault)
{
    uint32_t pokemon=pokemon_of(s,fo);
    const uint8_t *p=pokemon?c3_battle_arena_bytes(&s->arena,pokemon,0xE0):NULL;
    if(!p){*fault=1;return 0;}
    return (p[0xD8]||p[0xD9])&&!(p[0xDC]&0x80);
}

/* _fightFloorCreateFightOutPokemonPtrAry(floor, ary, 0, 0, NULL): every
 * valid battler, side by side, trainer by trainer, slot by slot. */
static int battler_list(const C3BattleState *s,uint32_t out[8])
{
    uint32_t trainers=0,pokemons=0;
    int fault=0;unsigned n=0;
    memset(out,0,8*sizeof *out);
    if(!c3_battle_floor_value(s,0x16,&trainers)||!c3_battle_floor_value(s,0x18,&pokemons))return 0;
    if((uint16_t)trainers>2||(uint16_t)pokemons>2)return 0;
    for(unsigned side=0;side<2;++side){
        uint32_t sd=side_at(s,side);
        if(!c3_battle_side_check_valid(s,sd,&fault))continue;
        for(unsigned t=0;t<(uint16_t)trainers;++t){
            uint32_t tr=sd+C3_FIGHT_SIDE_TRAINER_OFFSET+t*C3_BATTLE_RUNTIME_TRAINER_BYTES;
            if(!c3_battle_trainer_check_valid(s,tr,&fault))continue;
            for(unsigned k=0;k<(uint16_t)pokemons;++k){
                uint32_t fo=tr+C3_RUNTIME_FIGHT_OUT_OFFSET+k*C3_FIGHT_OUT_POKEMON_BYTES;
                if(!c3_battle_fight_out_check_valid(s,fo,&fault))continue;
                if(n<8)out[n++]=fo;                                /* _fightFloorSetFightOutPokemonPtrAry */
            }
        }
    }
    return !fault;
}
static int write_list(C3BattleState *s,const uint32_t list[8])                 /* fightFloorBiosSetFightOutPokemonPtrAryPtr */
{
    uint8_t *a=c3_battle_arena_bytes(&s->arena,s->floor+FLOOR_BATTLER_LIST,32);
    if(!a)return 0;
    for(unsigned i=0;i<8;++i){
        a[4*i]=(uint8_t)(list[i]>>24);a[4*i+1]=(uint8_t)(list[i]>>16);a[4*i+2]=(uint8_t)(list[i]>>8);a[4*i+3]=(uint8_t)list[i];
    }
    return 1;
}
/* fightFloorLoopValidFightOutPokemon(.., 1): the floor's list in order, valid
 * battlers only. */
static int sorted_list(const C3BattleState *s,uint32_t out[8],unsigned *count)
{
    const uint8_t *a=c3_battle_arena_bytes(&s->arena,s->floor+FLOOR_BATTLER_LIST,32);
    int fault=0;
    *count=0;
    if(!a)return 0;
    for(unsigned i=0;i<8;++i){
        uint32_t fo=(uint32_t)a[4*i]<<24|(uint32_t)a[4*i+1]<<16|(uint32_t)a[4*i+2]<<8|a[4*i+3];
        if(fo&&c3_battle_fight_out_check_valid(s,fo,&fault))out[(*count)++]=fo;
    }
    return !fault;
}

/* ---- fightSeqInit -------------------------------------------------------- */

/* fn_80213558 with flag 0: fightOutPokemonInitOneTurn (0x102..0x112), the
 * erases of 0x32 / 0x37 / 0x33, 0xED counted down; the rampage counter
 * (condition 0x12) and a Substitute (0x14) are refused. */
static int seq_init_battler(C3BattleState *s,C3BattleScript *sc,uint32_t fo)
{
    int fault=0;
    for(uint32_t sel=0x102;sel<=0x112;++sel)
        if(!c3_battle_fight_status_set(&s->arena,fo,sel,0,0))return refuse(sc,"one-turn fields (fightOutPokemonInitOneTurn)");
    if(!erase_battler(s,fo,0x32,&fault)||!erase_battler(s,fo,0x37,&fault)||!erase_battler(s,fo,0x33,&fault))
        return refuse(sc,"condition category (fn_80213558)");
    uint32_t counter=0;
    if(!c3_battle_fight_status_get(&s->arena,fo,0xED,0,&counter))return refuse(sc,"battler field 0xED");
    if((uint16_t)counter&&!c3_battle_fight_status_set(&s->arena,fo,0xED,0,(uint16_t)(counter-1u)))
        return refuse(sc,"battler field 0xED");
    if(c3_battle_condition_check(s,fo,0x12,&fault)==1)return refuse(sc,"rampage counter (condition 0x12, fn_80213558)");
    if(c3_battle_condition_check(s,fo,0x14,&fault)==1)return refuse(sc,"Substitute at battle start (condition 0x14)");
    return !fault?1:refuse(sc,"battler conditions (fn_80213558)");
}
static int seq_init(C3BattleHandlers *h,C3BattleScript *sc,const C3BattleScriptServices *sv)
{
    C3BattleState *s=h->state;
    uint32_t list[8];unsigned n=0;int fault=0;
    if(!c3_battle_state_valid_fight_outs(s,list,&n))return refuse(sc,"valid battlers");
    for(unsigned i=0;i<n;++i)if(!seq_init_battler(s,sc,list[i]))return 0;
    for(unsigned side=0;side<2;++side){                           /* fightSideInitJoutaiDataId(side, 0x4D) */
        uint32_t sd=side_at(s,side);
        if(!c3_battle_side_check_valid(s,sd,&fault))continue;
        if(condition_category(s,0x4D)==6)c3_battle_condition_erase(s,sd,0x4D,&fault);
    }
    if(fault)return refuse(sc,"side conditions (fightSeqInit)");
    if(!sv->end_of_move(sv->user,sc,C3_SCRIPT_PASS_MOVE_CLEANUP,0))return 0;   /* fn_802136A4 */
    for(uint32_t a=C3_SCRIPT_WSD_BASE;a<C3_SCRIPT_WSD_BASE+8u;++a)
        if(!c3_battle_script_write(sc,a,1,0))return refuse(sc,"script work bytes (lbl_80478D78)");
    sc->action=0;                                                  /* lbl_8047B62C */
    for(unsigned i=0;i<n;++i){                                     /* _fightSeqInitSubFightOutPokemon */
        if(!fight_out(s,list[i],&fault))continue;
        if(!erase_battler(s,list[i],0x11,&fault))return refuse(sc,"condition category (flinch)");
    }
    return !fault?1:refuse(sc,"battler records (fightSeqInit)");
}

/* ---- appear checks ------------------------------------------------------- */

/* fn_8022E6F0(fo, flag): only a Shadow Pokemon in battle does anything
 * (its first appearance, the Shadow aura): refused. */
static int shadow_check(C3BattleState *s,C3BattleScript *sc)
{
    uint32_t list[8];unsigned n=0;int fault=0;
    if(!c3_battle_state_valid_fight_outs(s,list,&n))return refuse(sc,"valid battlers");
    for(unsigned i=0;i<n;++i)
        if(fight_out(s,list[i],&fault)&&is_shadow(s,list[i],&fault))return refuse(sc,"Shadow Pokemon appearance (fn_8022E6F0)");
    return !fault?1:refuse(sc,"battler records (fn_8022E6F0)");
}
/* fn_8022E410: abilities that act on appearing. */
static int ability_check(C3BattleState *s,C3BattleScript *sc,const uint32_t list[8],unsigned n)
{
    int fault=0;
    for(unsigned i=0;i<n;++i){
        if(!fight_out(s,list[i],&fault))continue;
        switch((uint16_t)get(s,list[i],0x100)){
        case 0x02:return refuse(sc,"Drizzle on appearing (lbl_8037959E)");
        case 0x2D:return refuse(sc,"Sand Stream on appearing (lbl_803795BB)");
        case 0x46:return refuse(sc,"Drought on appearing (lbl_803795D8)");
        case 0x16:return refuse(sc,"Intimidate on appearing (condition 0x3B)");
        case 0x24:return refuse(sc,"Trace on appearing (condition 0x3C)");
        case 0x3B:return refuse(sc,"Forecast on appearing (lbl_803796D7)");
        case 0x0D:case 0x4D:return refuse(sc,"Cloud Nine / Air Lock on appearing (fn_8022EB9C)");
        default:break;
        }
    }
    return !fault?1:refuse(sc,"battler records (fn_8022E410)");
}
/* fn_8022D084: for a battler in battle, the floor's appointed item (0x56:
 * message variable 0x29 and FightFloor +0xA4BC) becomes its held item; the
 * item effects (0x20, 0x17) are refused with any held item. */
static int item_check(C3BattleState *s,C3BattleScript *sc,const uint32_t list[8],unsigned n)
{
    int fault=0;
    for(unsigned i=0;i<n;++i){
        if(!fight_out(s,list[i],&fault))continue;
        uint32_t pokemon=pokemon_of(s,list[i]);
        const uint8_t *p=pokemon?c3_battle_arena_bytes(&s->arena,pokemon+0x88u,2):NULL;
        if(!p)return refuse(sc,"held item");
        uint16_t item=0;
        if(c3_battle_condition_check(s,list[i],0x3D,&fault)!=1)item=(uint16_t)((uint16_t)p[0]<<8|p[1]);
        if(item)return refuse(sc,"held item on appearing (fn_8022D084)");
        uint8_t *a=c3_battle_arena_bytes(&s->arena,s->floor+FLOOR_APPOINT_ITEM,2);
        if(!a)return refuse(sc,"floor appointed item");
        c3_battle_msgctrl_set(s,0x29,C3_MSGREF_ITEM_NAME(item));
        a[0]=(uint8_t)(item>>8);a[1]=(uint8_t)item;
    }
    return !fault?1:refuse(sc,"battler records (fn_8022D084)");
}

/* ---- kind 6: fightActionFlowTenkouInit ------------------------------------ */

/* fightKoukaDoFightKoukaJoukenAndKouka(0, 1): field effect row 1 of
 * lbl_80375CB8 is condition 6, target 1 (the floor), effect 2. Condition 6
 * (lbl_80375A08, no linked rows): fightFloorGetStatus 0xB of condition 0x4E
 * (fn_8011B444, can it be written) equals 2. Effect 2 (lbl_80363B18, no
 * link): mode 0 sets 0x4E through fightFloorSetStatus 0xA, which writes the
 * floor condition 0x4E (category 9) with argument 0 when it can be written.
 * Condition 0x4E is clear weather; its turn-count row is fixed, so nothing is
 * drawn. Then fightFloorGetNowTenkouDataId(0, 0): a weather other than clear
 * runs its start message (lbl_80378AA0): refused. */
static int weather_init(C3BattleState *s,C3BattleScript *sc)
{
    uint32_t floor=0;int fault=0,weather=0;
    if(!c3_battle_state_target_now(s,1,0,&floor)||floor!=s->floor)return refuse(sc,"field effect target");
    if(c3_battle_condition_check_write(s,floor,0x4E,&fault)==2&&condition_category(s,0x4E)==9&&
       c3_battle_condition_check_write(s,floor,0x4E,&fault)==2)
        c3_battle_condition_write(s,floor,0x4E,0,&fault);
    if(fault)return refuse(sc,"floor condition 0x4E");
    if(!c3_battle_state_weather(s,0,&weather))return refuse(sc,"weather");
    if(weather)return refuse(sc,"weather start message (fightActionFlowTenkouInit, lbl_80378AA0)");
    return 1;
}

int c3_battle_start_post(C3BattleHandlers *h,C3BattleScript *sc)
{
    C3BattleState *s=h->state;
    uint32_t list[8];unsigned n=0;
    C3BattleScriptServices sv=c3_battle_handler_services(h);
    if(!battler_list(s,list)||!write_list(s,list))return refuse(sc,"floor battler list");
    if(!c3_battle_sort_by_speed(h,sc,list))return 0;
    if(!write_list(s,list))return refuse(sc,"floor battler list");
    if(!seq_init(h,sc,&sv))return 0;
    if(!shadow_check(s,sc))return 0;
    if(!weather_init(s,sc))return 0;
    if(!sorted_list(s,list,&n))return refuse(sc,"floor battler list");
    if(!ability_check(s,sc,list,n))return 0;
    if(!sv.end_of_move(sv.user,sc,C3_SCRIPT_PASS_POST_FAINT_A,1))return 0;      /* fn_8022E314(1): Intimidate */
    if(!sv.end_of_move(sv.user,sc,C3_SCRIPT_PASS_POST_FAINT_B,0))return 0;      /* fn_8022E1C4: Trace */
    if(!sorted_list(s,list,&n))return refuse(sc,"floor battler list");
    if(!item_check(s,sc,list,n))return 0;
    if(!shadow_check(s,sc))return 0;
    if(!c3_battle_state_set_meet_enemies(s))return refuse(sc,"met lists (fightFloorSetMeetEnemyFightPokemonEnemySideAll)");
    if(!c3_battle_state_register_enemies(s))return refuse(sc,"enemy registry (fightFloorRegistFightTrainerEnemyPokemonFightSideAll)");
    if(!c3_battle_state_create_enemy_arrays(s))return refuse(sc,"enemy arrays (fightFloorCreateFightPokemonEnemyAryEnemySideAll)");
    uint16_t tick=0;
    if(!c3_battle_rng_next16(&s->rng,&tick))return refuse(sc,"random stream");
    uint8_t *f=c3_battle_arena_bytes(&s->arena,s->floor+FLOOR_FIRST_ATTACK,2);
    if(!f)return refuse(sc,"floor field 0x5B");
    f[0]=(uint8_t)(tick>>8);f[1]=(uint8_t)tick;
    if(h->present)(void)h->present(h->present_user,C3_PRESENT_EFFECT_POOL_FREE,0,0);   /* fn_801DA7AC */
    return 1;
}

/* ---- kind 4: fightActionFlowKaisiNyuujouPokemon --------------------------- */

#define TRAINER_SEQUENCE 0x27C0u     /* fightTrainer_GetSequencePtr (0x4C) */
#define TRAINER_PARTY 0xB1Cu         /* fightTrainer_GetFightPokemonPtr (0x45): 6 x 0x154 */
#define FIGHT_POKEMON_STRIDE 0x154u
#define APPEAR_ATTACKER 0xA474u      /* floor 0x36 */

static uint32_t rd32(const C3BattleState *s,uint32_t a,int *fault)
{
    const uint8_t *p=c3_battle_arena_bytes(&s->arena,a,4);
    if(!p){*fault=1;return 0;}
    return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];
}
static uint32_t trainer_of_side(uint32_t side,unsigned t)                      /* fightSideBiosGetFightTrainerPtr */
{return side+C3_FIGHT_SIDE_TRAINER_OFFSET+t*C3_BATTLE_RUNTIME_TRAINER_BYTES;}
static uint32_t battler_of(uint32_t trainer,unsigned j)                        /* fightTrainerGetStatus 0x46 */
{return j<2?trainer+C3_RUNTIME_FIGHT_OUT_OFFSET+j*C3_FIGHT_OUT_POKEMON_BYTES:0;}
static uint32_t valid_battler(const C3BattleState *s,uint32_t trainer,unsigned j,int *fault)  /* fightTrainerGetValidFightOutPokemonPtr */
{uint32_t fo=battler_of(trainer,j);return fo&&c3_battle_fight_out_check_valid(s,fo,fault)?fo:0;}
/* fn_801F8000: the trainer class prefix (GSmsgGetGSchar of the trainer kind's
 * prefix name; the kind is data field 4 of the trainer row), 0 for an invalid
 * trainer, a kind past the table or prefix 0 (GSmsgFindMessage(0) is NULL).
 * Returns 0 when the rows are missing. */
static int trainer_prefix(const C3BattleState *s,uint32_t trainer,uint32_t *ref,int *fault)
{
    *ref=0;
    if(!s->trainer_kinds.rows||!s->trainers.rows)return 0;
    if(!c3_battle_trainer_check_valid(s,trainer,fault))return 1;
    const uint8_t *id=c3_battle_arena_bytes(&s->arena,trainer,2);   /* trainer field 0x43 */
    if(!id)return 0;
    uint16_t data_id=(uint16_t)(id[0]<<8|id[1]);
    if(data_id>=s->trainers.count)return 1;                         /* fightTrainerDataBiosGetPtr NULL */
    const uint8_t *row=s->trainers.rows+(uint32_t)data_id*s->trainers.stride;
    uint16_t kind=(uint16_t)(row[2]<<8|row[3]);                     /* fightTrainerGetStatus 4 */
    if(kind>=s->trainer_kinds.count)return 1;
    const uint8_t *k=s->trainer_kinds.rows+(uint32_t)kind*s->trainer_kinds.stride;
    uint32_t prefix=(uint32_t)k[4]<<24|(uint32_t)k[5]<<16|(uint32_t)k[6]<<8|k[7];
    if(prefix>0xFFFFu)return 0;                                     /* not in message group 0 */
    *ref=prefix?C3_MSGREF_MESSAGE(prefix):0;
    return 1;
}
static int present(C3BattleHandlers *h,C3BattlePresent r,uint32_t a,uint32_t b)
{return h->present?h->present(h->present_user,r,a,b):0;}
/* _fightActionFlowKaisiNyuujouPokemonSubAppearMsg: mode 0 the nickname
 * variables of the battler's slot (0x14 / 0x16 or 0x15 / 0x17, swapped for
 * the player's battlers outside player layouts), mode 1 the appear message
 * for the trainer (one message for both battlers of a lone trainer, else one
 * naming the battler in 0x14 / 0x16). The player flag is floor 0x1A == 0x10,
 * or a trainer without a class prefix sending out an opponent's Pokemon. */
static int appear_msg(C3BattleHandlers *h,uint32_t trainer,uint32_t fo,uint16_t trainers,uint16_t battlers,
                      uint16_t index,int mode,int *fault)
{
    C3BattleState *s=h->state;
    uint32_t kind=0,prefix=0;
    int hero=c3_battle_fight_out_is_gc_hero(s,fo);
    if(hero<0||!c3_battle_floor_value(s,0x1A,&kind)||!trainer_prefix(s,trainer,&prefix,fault))return 0;
    int player=(uint16_t)kind==0x10;
    if(!prefix&&!hero)player=1;
    uint32_t pokemon=pokemon_of(s,fo),name=pokemon?pokemon+0x2Eu:0;  /* nickname (0x77) */
    if(mode==0){
        int first=index==0;
        if(!player&&hero)first=!first;
        c3_battle_msgctrl_set(s,first?0x14:0x15,name);
        c3_battle_msgctrl_set(s,first?0x16:0x17,name);
        return 1;
    }
    uint16_t text;
    if(trainers<=1&&battlers>1)text=player?0x7674:hero?0x7679:0x7671;
    else{
        c3_battle_msgctrl_set(s,0x14,name);c3_battle_msgctrl_set(s,0x16,name);
        text=player?0x7673:hero?0x7678:0x7670;
    }
    present(h,C3_PRESENT_MESSAGE_OPEN,text,0);
    return 1;
}
/* fightOutPokemonSetOnZukanFlag / SetOnDarkPokemonFlag(fo, 0) on the
 * battler's original party Pokemon (0xD5 then 0xCB): the seen flag of its
 * species and its Strategy Memo entry are save data, recorded in state->seen;
 * a Shadow Pokemon (dark flag) is refused. */
static int record_seen(C3BattleState *s,C3BattleScript *sc,uint32_t fo)
{
    uint32_t original=get(s,fo,0xD5),party=original?get(s,original,0xCB):0;
    const uint8_t *p=party?c3_battle_arena_bytes(&s->arena,party,0xE0):NULL;
    if(!p)return refuse(sc,"send-out Pokedex record");
    if((p[0xD8]||p[0xD9])&&!(p[0xDC]&0x80))return refuse(sc,"Shadow Pokemon registration (pokemonSetOnDarkPokemonFlag)");
    if(s->seen_count>=C3_BATTLE_SEEN_MAX)return refuse(sc,"seen records");
    s->seen[s->seen_count].species=(uint16_t)(p[0]<<8|p[1]);
    s->seen[s->seen_count].personality=(uint32_t)p[4]<<24|(uint32_t)p[5]<<16|(uint32_t)p[6]<<8|p[7];
    s->seen[s->seen_count].catch_trainer=(uint32_t)p[0x14]<<24|(uint32_t)p[0x15]<<16|(uint32_t)p[0x16]<<8|p[0x17];
    ++s->seen_count;
    return 1;
}
/* fightOutPokemonDasuEffect returns at once without the battler's model
 * (0xEE); fightTrainerBallThrowEffect without the trainer's (0x4C). */
static void send_out_effect(C3BattleHandlers *h,uint32_t fo,uint32_t mode)
{if(fo&&get(h->state,fo,0xEE))present(h,C3_PRESENT_SEND_OUT_EFFECT,fo,mode);}
static void ball_throw(C3BattleHandlers *h,uint32_t trainer,uint32_t fo,uint32_t mode,int *fault)
{if(rd32(h->state,trainer+TRAINER_SEQUENCE,fault))present(h,C3_PRESENT_BALL_THROW,fo,mode);}
static uint32_t first_battler(const C3BattleState *s,uint32_t trainer,uint16_t pokemons,unsigned *index,int *fault)
{
    unsigned j=0;uint32_t fo=0;
    for(;j<pokemons;++j)if((fo=valid_battler(s,trainer,j,fault))!=0)break;
    if(index)*index=j;
    return fo;
}

int c3_battle_start_send_out(C3BattleHandlers *h,C3BattleScript *sc,uint16_t side_kind)
{
    C3BattleState *s=h->state;
    uint32_t type=0,pokemons=0,trainers=0,dex=0,side=0;
    int fault=0;
    if(!c3_battle_floor_value(s,0x14,&type)||!c3_battle_floor_value(s,0x18,&pokemons)||
       !c3_battle_floor_value(s,0x16,&trainers)||!c3_battle_floor_value(s,0x1E,&dex))return refuse(sc,"floor fields");
    if((uint16_t)trainers>2||(uint16_t)pokemons>2)return refuse(sc,"more than two trainers or active slots");
    if(!c3_battle_state_target(s,side_kind,0,(uint16_t)type,&side))return refuse(sc,"send-out side");
    if(!side||!c3_battle_side_check_valid(s,side,&fault))return refuse(sc,"send-out side (fightSideCheckValid)");
    uint16_t doing=0;                                              /* fightSideGetDoFightTrainerCount */
    for(unsigned t=0;t<2;++t){
        uint32_t tr=trainer_of_side(side,t);
        if(!c3_battle_trainer_check_valid(s,tr,&fault))continue;
        for(unsigned j=0;j<6;++j)
            if(c3_battle_fight_pokemon_check_fight_out(s,tr+TRAINER_PARTY+j*FIGHT_POKEMON_STRIDE,&fault)){++doing;break;}
    }
    /* 1. The battlers. */
    for(unsigned i=0;i<(uint16_t)trainers;++i){
        uint32_t tr=trainer_of_side(side,i);
        if(!c3_battle_trainer_check_valid(s,tr,&fault))continue;
        uint32_t grid=rd32(s,tr+TRAINER_SEQUENCE,&fault);
        if(!grid)continue;
        unsigned num=0;
        for(unsigned j=0;j<6;++j){
            if(num>=(uint16_t)pokemons||num>=2)break;
            uint32_t fp=tr+TRAINER_PARTY+j*FIGHT_POKEMON_STRIDE;
            if(!c3_battle_fight_pokemon_check_fight_out(s,fp,&fault))continue;
            if(j>=2)return refuse(sc,"send-out from party slot 2 or later");
            present(h,C3_PRESENT_FACE_LOAD,fp,0);
            uint32_t sequence=(uint32_t)present(h,C3_PRESENT_SEQUENCE_CREATE,get(s,fp,0xCC),0);
            uint32_t fo=battler_of(tr,j);
            if(!c3_battle_fight_out_pokemon_create(&s->arena,fo,fp,sequence,&s->species))
                return refuse(sc,"battler creation (fightOutPokemonCreate)");
            ++num;
            present(h,C3_PRESENT_WZX_LOAD,fo,0);
            if((uint8_t)dex==1){
                int hero=c3_battle_fight_out_is_gc_hero(s,fo);
                if(hero<0)return refuse(sc,"battler trainer");
                if(!hero&&!record_seen(s,sc,fo))return 0;
            }
            present(h,C3_PRESENT_GRID_ADD,grid,sequence);
        }
    }
    /* 2. Throws and appearances loaded. */
    for(unsigned i=0;i<(uint16_t)trainers;++i){
        uint32_t tr=trainer_of_side(side,i);
        if(!c3_battle_trainer_check_valid(s,tr,&fault))continue;
        ball_throw(h,tr,first_battler(s,tr,(uint16_t)pokemons,NULL,&fault),0,&fault);
        for(unsigned j=0;j<(uint16_t)pokemons;++j)send_out_effect(h,valid_battler(s,tr,j,&fault),0);
    }
    /* 3. Messages, throws, appearances. */
    for(unsigned i=0;i<(uint16_t)trainers;++i){
        uint32_t tr=trainer_of_side(side,i),prefix=0;
        if(!c3_battle_trainer_check_valid(s,tr,&fault))continue;
        uint16_t out=0;                                            /* fightTrainerGetDoFightOutFightOutPokemonCount */
        for(unsigned j=0;j<2;++j)if(fight_out(s,battler_of(tr,j),&fault))++out;
        for(unsigned j=0;j<(uint16_t)pokemons;++j){
            uint32_t b=valid_battler(s,tr,j,&fault);
            if(b&&!appear_msg(h,tr,b,doing,out,(uint16_t)j,0,&fault))return refuse(sc,"appear message");
        }
        unsigned first=0;
        uint32_t fo=first_battler(s,tr,(uint16_t)pokemons,&first,&fault);
        present(h,C3_PRESENT_GRID_UPDATE,0,0);
        ball_throw(h,tr,fo,1,&fault);
        if(!trainer_prefix(s,tr,&prefix,&fault))return refuse(sc,"trainer class prefix (fightTrainerKindData)");
        c3_battle_msgctrl_set(s,0x22,prefix);
        c3_battle_msgctrl_set(s,0x23,tr+4u);                       /* fightTrainerGetNamePtr */
        c3_battle_msgctrl_set(s,0x25,tr+4u);
        if(!appear_msg(h,tr,fo,doing,out,(uint16_t)first,1,&fault))return refuse(sc,"appear message");
        ball_throw(h,tr,fo,2,&fault);
        for(unsigned j=0;j<(uint16_t)pokemons;++j){
            uint32_t b=valid_battler(s,tr,j,&fault);
            if(!b)continue;
            uint32_t previous=rd32(s,s->floor+APPEAR_ATTACKER,&fault);
            if(!c3_battle_handlers_set_attacker(s,b))return refuse(sc,"floor attacker");
            send_out_effect(h,b,1);
            int hero=c3_battle_fight_out_is_gc_hero(s,b);
            if(hero<0)return refuse(sc,"battler trainer");
            present(h,hero?C3_PRESENT_ACTOR_BEGIN:C3_PRESENT_STATUS_MENU_OPEN,b,type);   /* fn_80265598 */
            send_out_effect(h,b,2);send_out_effect(h,b,3);send_out_effect(h,b,4);
            present(h,C3_PRESENT_ACTOR_END,b,type);                                   /* fn_8026532C */
            if(!c3_battle_handlers_set_attacker(s,previous))return refuse(sc,"floor attacker");
        }
    }
    /* 4. Close and free. */
    present(h,C3_PRESENT_MESSAGE_CLOSE,0,0);
    for(unsigned i=0;i<(uint16_t)trainers;++i){
        uint32_t tr=trainer_of_side(side,i);
        if(!c3_battle_trainer_check_valid(s,tr,&fault))continue;
        ball_throw(h,tr,first_battler(s,tr,(uint16_t)pokemons,NULL,&fault),3,&fault);
        for(unsigned j=0;j<(uint16_t)pokemons;++j)send_out_effect(h,valid_battler(s,tr,j,&fault),5);
    }
    return !fault?1:refuse(sc,"send-out records");
}
