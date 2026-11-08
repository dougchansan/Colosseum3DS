/* FightPokemon/FightOutPokemon status accessors over a synthetic arena. */
#include "colosseum3ds/battle_status.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: CHECK(%s)\n",__FILE__,__LINE__,#x);exit(1);}}while(0)

static void waza_and_floor(void)
{
    static uint8_t big[2*0x28E4+C3_FIGHT_FLOOR_BYTES],moves[3*56],floors[2*24];
    C3BattleArena a={big,sizeof big};
    C3BattleDataRows mv={moves,3,56},fl={floors,2,24};
    const uint32_t fo=C3_ARENA_BASE+0x1314,floor=C3_ARENA_BASE+2*0x28E4,fw=fo+0x49C;
    uint32_t v;
    /* Move row 2: priority -1, PP 30, type 14, accuracy 100, power 50 (s16). */
    uint8_t *r=moves+2*56;r[0]=0xFF;r[1]=30;r[2]=14;r[4]=100;r[0x16]=0;r[0x17]=50;
    CHECK(c3_battle_waza_status_get(&a,&mv,0,2,4,0,&v)&&v==0xFFFFFFFFu);   /* (s8) priority */
    CHECK(c3_battle_waza_status_get(&a,&mv,0,2,2,0,&v)&&v==30);
    CHECK(c3_battle_waza_status_get(&a,&mv,0,2,3,0,&v)&&v==14);
    CHECK(c3_battle_waza_status_get(&a,&mv,0,2,7,0,&v)&&v==50);
    CHECK(c3_battle_waza_status_get(&a,&mv,0,9,2,0,&v)&&v==0);            /* beyond count: NULL row */
    CHECK(c3_battle_waza_status_get(&a,&mv,0,2,0,0,&v)&&v==0);            /* status 0 */
    CHECK(c3_battle_waza_status_get(&a,&mv,0,2,0x35,0,&v)&&v==0);         /* >= 0x35 */
    CHECK(!c3_battle_waza_status_set(&a,fw,2,0,1));                       /* move rows read-only */
    CHECK(!c3_battle_waza_status_get(&a,&mv,0,2,0x1C,0,&v));              /* AI func ptr: computed */
    /* FightWaza record: original move @+2, used move @+4, joutai @+8 (x9). */
    CHECK(c3_battle_waza_status_set(&a,fw,0x27,0,1)&&c3_battle_waza_status_set(&a,fw,0x28,0,2));
    CHECK(big[0x1314+0x49C+3]==1&&big[0x1314+0x49C+5]==2);
    CHECK(c3_battle_waza_status_get(&a,&mv,fw,0,0x2A,8,&v)&&v==fw+8+8*16);
    CHECK(c3_battle_waza_status_get(&a,&mv,fw,0,0x2A,9,&v)&&v==0);
    CHECK(c3_battle_waza_status_get(&a,&mv,fw,0,0x34,0,&v)&&v==14);       /* used move's type */
    CHECK(c3_battle_waza_status_get(&a,&mv,fw,0,0x33,0,&v)&&v==0);        /* move 1 type 0 */
    /* pokemonGetStatus 0xDA/0xDB through the FightWaza at FightOutPokemon +0x49C. */
    CHECK(c3_battle_fight_status_get(&a,fo,0xDA,0,&v)&&v==2);
    CHECK(c3_battle_fight_status_get(&a,fo,0xDB,0,&v)&&v==1);
    CHECK(c3_battle_fight_status_get(&a,0,0xDA,0,&v)&&v==0);
    /* FightFloor: attacker @+0xA474, defender @+0xA478, turn count @+0x10. */
    uint8_t *fr=big+2*0x28E4;
    fr[0xA474]=(uint8_t)(fo>>24);fr[0xA475]=(uint8_t)(fo>>16);fr[0xA476]=(uint8_t)(fo>>8);fr[0xA477]=(uint8_t)fo;
    const uint32_t def=C3_ARENA_BASE+0x28E4+0x1314;
    fr[0xA478]=(uint8_t)(def>>24);fr[0xA479]=(uint8_t)(def>>16);fr[0xA47A]=(uint8_t)(def>>8);fr[0xA47B]=(uint8_t)def;
    CHECK(c3_battle_floor_status_set(&a,floor,0,0x0C,0,7)&&fr[0x11]==7);
    CHECK(c3_battle_floor_status_get(&a,&fl,floor,0,0,0x0C,0,&v)&&v==7);
    CHECK(c3_battle_floor_status_get(&a,&fl,floor,0,0,0,0,&v)&&v==floor);
    CHECK(c3_battle_floor_status_get(&a,&fl,floor,0,0,0x60,0,&v)&&v==0);
    CHECK(c3_battle_floor_status_get(&a,&fl,floor,0,0,0x37,0,&v)&&v==2);  /* attacker used move */
    CHECK(c3_battle_floor_status_get(&a,&fl,floor,0,0,0x38,0,&v)&&v==1);
    CHECK(!c3_battle_floor_status_set(&a,floor,0,0x36,0,fo));             /* drives presentation */
    /* Floor data rows by slot: side data IDs @+0x14 (x2). */
    floors[24+0x14]=0;floors[24+0x15]=2;floors[24+0x16]=0;floors[24+0x17]=1;
    CHECK(c3_battle_floor_status_get(&a,&fl,floor,0,1,3,0,&v)&&v==2);
    CHECK(c3_battle_floor_status_get(&a,&fl,floor,0,1,3,1,&v)&&v==1);
    CHECK(c3_battle_floor_status_get(&a,&fl,floor,0,1,3,2,&v)&&v==0);
    CHECK(c3_battle_floor_status_get(&a,&fl,floor,0,5,3,0,&v)&&v==0);     /* NULL row */
    /* Targets: attacker / defender / floor; side-relative kinds refused. */
    CHECK(c3_battle_target(&a,floor,0x11,0,&v)&&v==fo);
    CHECK(c3_battle_target(&a,floor,0x12,0,&v)&&v==def);
    CHECK(c3_battle_target(&a,floor,1,0,&v)&&v==floor);
    CHECK(c3_battle_target(&a,floor,0,0,&v)&&v==0);
    CHECK(!c3_battle_target(&a,floor,2,0,&v));
}

int main(void)
{
    static uint8_t bytes[0x4000];
    C3BattleArena arena={bytes,sizeof bytes};
    const uint32_t obj=C3_ARENA_BASE+0x100;
    uint32_t v;

    /* Arena address translation and bounds. */
    CHECK(c3_battle_arena_bytes(&arena,obj,4)==bytes+0x100);
    CHECK(!c3_battle_arena_bytes(&arena,C3_ARENA_BASE+sizeof bytes-1,2));
    CHECK(!c3_battle_arena_bytes(&arena,0x80000000u,1));
    CHECK(c3_battle_arena_address(&arena,bytes+0x10)==C3_ARENA_BASE+0x10);
    CHECK(!c3_battle_arena_address(&arena,bytes+sizeof bytes));

    /* Plain value fields: big-endian at the generated offsets, widths from
     * the accessor types (cross-checked against the earlier hand mapping in
     * src/battle_runtime_trainer.c). */
    static const struct{uint32_t sel,off,width;}known[]={
        {0xD5,0x000,4},{0xD6,0x004,4},{0xCE,0x14C,2},{0xCF,0x14E,1},{0xD2,0x151,1},
        {0xE6,0x604,1},{0xEC,0x60A,1},{0xED,0x610,2},{0xEE,0x600,4},{0xEF,0x668,2},{0xF7,0x678,2},
        {0xF9,0x67A,1},{0xFB,0x680,2},{0xFC,0x67E,1},{0x100,0x660,2},{0x102,0x682,2},{0x105,0x688,2},
        {0x106,0x68A,1},{0x11A,0x69E,1},{0x11C,0x6A4,2},{0x11F,0x6AA,2}};
    for(unsigned i=0;i<sizeof known/sizeof *known;++i){
        memset(bytes,0,sizeof bytes);
        uint32_t value=known[i].width==4?0x89ABCDEFu:known[i].width==2?0x1234u:0x5Au;
        CHECK(c3_battle_fight_status_set(&arena,obj,known[i].sel,0,value));
        const uint8_t *p=bytes+0x100+known[i].off;
        if(known[i].width==4)CHECK(p[0]==0x89&&p[3]==0xEF);
        else if(known[i].width==2)CHECK(p[0]==0x12&&p[1]==0x34);
        else CHECK(p[0]==0x5A);
        CHECK(c3_battle_fight_status_get(&arena,obj,known[i].sel,0,&v)&&v==value);
        /* Setters truncate to the field width. */
        CHECK(c3_battle_fight_status_set(&arena,obj,known[i].sel,0,0xFFFFFF00u|value));
        CHECK(c3_battle_fight_status_get(&arena,obj,known[i].sel,0,&v));
        if(known[i].width<4){
            uint32_t t=(0xFFFFFF00u|value)&((1u<<8*known[i].width)-1u);
            /* Signed s16 accessors (extsh): entry ID, damage values. */
            int is_signed=known[i].sel==0xCE||known[i].sel==0x102||known[i].sel==0x104||
                known[i].sel==0x11C||known[i].sel==0x11E;
            if(is_signed&&(t&0x8000u))t|=0xFFFF0000u;
            CHECK(v==t);
        }
    }

    /* Signed 16-bit fields sign-extend (entry ID, meet-enemy IDs, irekae target). */
    memset(bytes,0,sizeof bytes);
    CHECK(c3_battle_fight_status_set(&arena,obj,0xCE,0,0xFFFF));
    CHECK(c3_battle_fight_status_get(&arena,obj,0xCE,0,&v)&&v==0xFFFFFFFFu);
    CHECK(c3_battle_fight_status_set(&arena,obj,0x121,0,0x8000));
    CHECK(c3_battle_fight_status_get(&arena,obj,0x121,0,&v)&&v==0xFFFF8000u);

    /* Indexed fields: stride, bound, out-of-range value; index is a u8. */
    CHECK(c3_battle_fight_status_set(&arena,obj,0xFD,11,0x0007));
    CHECK(bytes[0x100+0x612+22]==0&&bytes[0x100+0x612+23]==7);
    CHECK(c3_battle_fight_status_get(&arena,obj,0xFD,11,&v)&&v==7);
    CHECK(c3_battle_fight_status_get(&arena,obj,0xFD,12,&v)&&v==0xFFFFFFFFu);
    CHECK(c3_battle_fight_status_set(&arena,obj,0xFD,12,1));          /* ignored */
    CHECK(c3_battle_fight_status_set(&arena,obj,0xFF,1,0x0E));
    CHECK(c3_battle_fight_status_get(&arena,obj,0xFF,1,&v)&&v==0x0E&&bytes[0x100+0x65F]==0x0E);
    CHECK(c3_battle_fight_status_get(&arena,obj,0xFF,2,&v)&&v==0);
    CHECK(c3_battle_fight_status_get(&arena,obj,0xFF,0x101,&v)&&v==0x0E); /* (u8)0x101 == 1 */

    /* Address-of fields return guest addresses inside the record. */
    CHECK(c3_battle_fight_status_get(&arena,obj,0xD9,0,&v)&&v==obj+0x49C);
    CHECK(c3_battle_fight_status_get(&arena,obj,0xFE,0,&v)&&v==obj+0x62C);
    CHECK(c3_battle_fight_status_get(&arena,obj,0xD8,51,&v)&&v==obj+0x15C+51*16);
    CHECK(c3_battle_fight_status_get(&arena,obj,0xD8,52,&v)&&v==0);
    CHECK(c3_battle_fight_status_get(&arena,obj,0x122,3,&v)&&v==obj+0x6B0+36);
    CHECK(!c3_battle_fight_status_set(&arena,obj,0xD9,0,1));          /* no setter */

    /* NULL object: reads 0, writes ignored; selector truncated to u16. */
    v=5;CHECK(c3_battle_fight_status_get(&arena,0,0xE6,0,&v)&&v==0);
    CHECK(c3_battle_fight_status_set(&arena,0,0xE6,0,3));
    CHECK(c3_battle_fight_status_get(&arena,obj,0x100D9,0,&v)&&v==obj+0x49C);

    /* Not handled here: computed, absent, outside the range, outside the arena. */
    v=0x77;
    CHECK(!c3_battle_fight_status_get(&arena,obj,0xDC,0,&v)&&v==0x77);  /* still computed */
    CHECK(!c3_battle_fight_status_get(&arena,obj,0x123,0,&v));          /* checkFightOut: computed */
    CHECK(!c3_battle_fight_status_get(&arena,obj,0xD3,0,&v));           /* absent */
    CHECK(!c3_battle_fight_status_get(&arena,obj,0x6E,0,&v));           /* Pokemon field: elsewhere */
    CHECK(!c3_battle_fight_status_get(&arena,C3_ARENA_BASE+sizeof bytes-4,0xE6,0,&v));
    CHECK(!c3_battle_fight_status_set(&arena,0x80001000u,0xE6,0,1));
    waza_and_floor();
    puts("battle_status OK");
    return 0;
}
