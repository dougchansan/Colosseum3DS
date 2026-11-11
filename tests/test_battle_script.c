/* Move-script interpreter. The synthetic suite drives every lowered opcode and
 * the fn_80211B94 loop with deliberately synthetic bytecode and recording
 * service doubles. With C3_SCRIPT_PACK set, original scripts run until their
 * first not-yet-lowered opcode, which must be reported by name. */
#include "colosseum3ds/battle_script.h"
#include "save_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do{if(!(x)){fprintf(stderr,"%s:%d: CHECK(%s)\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
#define DIRECTORY_END (64u+19u*12u)
#define PACK_BYTES (DIRECTORY_END+C3_SCRIPT_REGION_BYTES+C3_SCRIPT_EFFECTS*4u+C3_SCRIPT_WSD_BYTES+C3_SCRIPT_WDT_BYTES+4u+C3_CONDITION_TABLE_BYTES+4u+C3_ACCURACY_STAGES*2u+4u+26u+4u+8u+0x24u+4u+C3_TARGET_KINDS*8u+4u+C3_KAISUU_ROWS*8u+4u+C3_AI_MOVE_ROWS*0x14u)
#define AT(a) ((a)-C3_SCRIPT_REGION_BASE)

static void put32(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void put16(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}

typedef struct {
    char log[2048]; size_t used;
    uint32_t get_value, attacker, attacker_move;
    int fail_get, fail_pass;
} Services;
static void logf_(Services *s,const char *t){size_t n=strlen(t);CHECK(s->used+n+1<sizeof s->log);memcpy(s->log+s->used,t,n);s->used+=n;s->log[s->used++]=';';s->log[s->used]=0;}
static int sget(void *u,uint8_t k,uint32_t a1,uint32_t a2,uint32_t f,uint32_t a4,uint32_t *v)
{Services *s=u;char t[96];snprintf(t,sizeof t,"get %u %lx %lx %lx %lx",k,(unsigned long)a1,(unsigned long)a2,(unsigned long)f,(unsigned long)a4);logf_(s,t);
 if(s->fail_get)return 0;
 *v=k==5&&f==0xD9?s->attacker_move:s->get_value;return 1;}
static int sset(void *u,uint8_t k,uint32_t a1,uint32_t a2,uint32_t f,uint32_t a4,uint32_t v)
{Services *s=u;char t[96];snprintf(t,sizeof t,"set %u %lx %lx %lx %lx=%lx",k,(unsigned long)a1,(unsigned long)a2,(unsigned long)f,(unsigned long)a4,(unsigned long)v);logf_(s,t);return 1;}
static int starget(void *u,uint8_t k,uint32_t a,uint32_t *h){Services *s=u;char t[32];snprintf(t,sizeof t,"target %x %lx",k,(unsigned long)a);logf_(s,t);*h=s->attacker;return 1;}
static int spass(void *u,struct C3BattleScript *sc,C3BattleScriptPass p,uint8_t a){(void)sc;Services *s=u;char t[32];snprintf(t,sizeof t,"pass %d %u",(int)p,a);logf_(s,t);return !s->fail_pass;}

static uint8_t *synthetic_pack(void)
{
    static uint8_t b[PACK_BYTES];memset(b,0,sizeof b);
    static const char *tags[19]={"SCR ","EFF ","WSD ","WDT ","CNC ","CND ","HTC ","HIT ","ABC ","ABR ","CRC ","CRT ","ED0 ",
        "TGC ","TGT ","KAC ","KAI ","AMC ","AMT "};
    const uint32_t bases[19]={C3_SCRIPT_REGION_BASE,0x80379BFFu,C3_SCRIPT_WSD_BASE,C3_SCRIPT_WDT_BASE,0x80478B48u,0x8035BBA8u,
        0x80478D70u,0x80375DD0u,0x80478D68u,0x80375D10u,0x80478D60u,0x80478D58u,0x80279ED0u,0x80478D40u,0x80375AC8u,0x80478B20u,0x80315690u,0x80478B50u,0x8035C430u};
    const uint32_t lens[19]={C3_SCRIPT_REGION_BYTES,C3_SCRIPT_EFFECTS*4u,C3_SCRIPT_WSD_BYTES,C3_SCRIPT_WDT_BYTES,4u,
        C3_CONDITION_TABLE_BYTES,4u,C3_ACCURACY_STAGES*2u,4u,26u,4u,8u,0x24u,4u,C3_TARGET_KINDS*8u,4u,C3_KAISUU_ROWS*8u,4u,C3_AI_MOVE_ROWS*0x14u};
    memcpy(b,"C3W1",4);put32(b+4,7);put32(b+8,PACK_BYTES);put32(b+12,19);
    static const uint8_t dol[20]={0x87,0x0e,0x8b,0x96,0x93,0xca,0x78,0x07,0x82,0xd8,0x0f,0x22,0xa6,0xa4,0x57,0x2d,0x8b,0xa9,0x45,0x8f};
    memcpy(b+16,dol,20);
    for(unsigned i=0;i<19;++i){memcpy(b+64+12*i,tags[i],4);put32(b+68+12*i,bases[i]);put32(b+72+12*i,lens[i]);}
    uint8_t *eff=b+DIRECTORY_END+C3_SCRIPT_REGION_BYTES;
    {   /* counts: accuracy 13, stage ratios 13, critical 5, target kinds 30, turn counts 10, move AI 358 */
        size_t at=DIRECTORY_END;for(unsigned i=0;i<19;++i){
            if(i==6)put32(b+at,C3_ACCURACY_STAGES);
            if(i==8)put32(b+at,13);
            if(i==10)put32(b+at,5);
            if(i==13)put32(b+at,C3_TARGET_KINDS);
            if(i==15)put32(b+at,C3_KAISUU_ROWS);
            if(i==17)put32(b+at,C3_AI_MOVE_ROWS);
            at+=lens[i];}
    }
    for(unsigned i=0;i<C3_SCRIPT_EFFECTS;++i)put32(eff+4*i,C3_SCRIPT_REGION_BASE+i);
    uint8_t *wsd=eff+C3_SCRIPT_EFFECTS*4u;wsd[3]=0x5A;wsd[4]=0x77;   /* 80478D7B, 80478D7C */
    uint8_t *wdt=wsd+C3_SCRIPT_WSD_BYTES;wdt[0]=0x33;wdt[0x8038FFF9u-C3_SCRIPT_WDT_BASE]=0x44;
    return b;
}
static void seal(uint8_t *b){c3_save_codec_sha1(b+36,b+64,PACK_BYTES-64u);}
static uint8_t *script_bytes(uint8_t *b){return b+DIRECTORY_END;}

static void run(C3BattleScript *s,unsigned limit)
{
    for(unsigned i=0;i<limit;++i){
        C3BattleScriptStatus st=c3_battle_script_step(s);
        if(st==C3_SCRIPT_WAITING){CHECK(c3_battle_script_resume(s));continue;}
        if(st!=C3_SCRIPT_RUNNING)return;
    }
    CHECK(0);
}

static void pack_rejection(void)
{
    C3BattleScriptPack p;char e[C3_SCRIPT_ERROR_SIZE];uint8_t *b=synthetic_pack();seal(b);
    CHECK(c3_battle_script_pack_decode(&p,b,PACK_BYTES,e));
    CHECK(!c3_battle_script_pack_decode(&p,b,PACK_BYTES-1,e));
    b[200]^=1;CHECK(!c3_battle_script_pack_decode(&p,b,PACK_BYTES,e)&&strstr(e,"checksum"));b[200]^=1;
    put32(b+DIRECTORY_END+C3_SCRIPT_REGION_BYTES,0x80000000u);seal(b);       /* effect 0 outside the region */
    CHECK(!c3_battle_script_pack_decode(&p,b,PACK_BYTES,e)&&strstr(e,"outside"));
}

static void opcodes(void)
{
    uint8_t *b=synthetic_pack(),*sc=script_bytes(b);
    const uint32_t S=C3_SCRIPT_REGION_BASE;
    /* nested sequence at S: */
    uint8_t *p=sc;
    p[0]=0x2E;put32(p+1,0x8047B63Eu);p[5]=0x02;p+=6;                       /* setbyte 8047B63E=2 */
    p[0]=0x35;put32(p+1,0x8047B618u);put32(p+5,0x00040000u);p+=9;           /* orword hit marker */
    p[0]=0xF9;p[1]=7;put16(p+0xA,0x37);put32(p+0x10,0x8047B63Cu);p+=0x14;  /* get16 floor 0x37 */
    p[0]=0x2A;p[1]=1;put32(p+2,0x8047B63Cu);put16(p+6,0x39);put32(p+8,S+0x100);p+=12; /* != Surf -> S+0x100 */
    p[0]=0x3E;                                                             /* not reached */
    uint8_t *q=sc+0x100;
    q[0]=0x39;put16(q+1,0x20);q+=3;                                        /* pause 0x20 */
    q[0]=0x3A;q+=1;                                                        /* skip */
    q[0]=0x29;q[1]=4;put32(q+2,0x80478D7Bu);q[6]=0x0F;put32(q+7,S+0x200);q+=11; /* (5A&0F)!=0 -> S+0x200 */
    q[0]=0x3E;
    uint8_t *r=sc+0x200;
    r[0]=0xFB;r[1]=5;put32(r+2,0x8047B618u);put32(r+6,0x80478D7Bu);put16(r+0xA,0x12);put32(r+0x10,0x8047B63Eu);r+=0x14; /* set8 */
    r[0]=0x28;put32(r+1,S+0x300);
    sc[0x300]=0x3C;                                                        /* SEQRET */
    seal(b);
    C3BattleScriptPack pack;char e[C3_SCRIPT_ERROR_SIZE];CHECK(c3_battle_script_pack_decode(&pack,b,PACK_BYTES,e));
    Services sv={0};sv.get_value=33;C3BattleScriptServices svc={&sv,sget,sset,starget,spass,NULL};
    C3BattleScript *s=calloc(1,sizeof *s);CHECK(s&&c3_battle_script_init(s,&pack,&svc));
    uint32_t v;
    CHECK(c3_battle_script_read(s,0x80478D7Bu,1,&v)&&v==0x5A);            /* .sdata initial byte */
    CHECK(c3_battle_script_read(s,0x8038FF5Au,1,&v)&&v==0x33);            /* .data initial byte */
    CHECK(!c3_battle_script_read(s,0x8047B610u,4,&v));                     /* interpreter PC slot */
    CHECK(!c3_battle_script_read(s,0x8047B62Cu,1,&v));                     /* action slot */
    CHECK(!c3_battle_script_read(s,0x80470000u,1,&v));                     /* unmapped */
    CHECK(c3_battle_script_call(s,S,0x1234,0));
    unsigned waits=0;
    for(unsigned i=0;i<32;++i){C3BattleScriptStatus st=c3_battle_script_step(s);
        if(st==C3_SCRIPT_WAITING){CHECK(s->wait_frames==0x20);++waits;CHECK(c3_battle_script_resume(s));continue;}
        if(st!=C3_SCRIPT_RUNNING)break;}
    CHECK(s->status==C3_SCRIPT_DONE&&waits==1&&!s->depth);
    CHECK(c3_battle_script_read(s,0x8047B63Eu,1,&v)&&v==2);
    CHECK(c3_battle_script_read(s,0x8047B618u,4,&v)&&v==0x00040000u);
    CHECK(c3_battle_script_read(s,0x8047B63Cu,2,&v)&&v==33);
    CHECK(strstr(sv.log,"get 7 0 0 37 0;")&&strstr(sv.log,"set 5 40000 5a 12 0=2;"));
    CHECK(s->opcodes==10&&s->state==0&&s->pc==0&&s->action==0);           /* caller context restored */
    free(s);
}

static void jumpifbyte_selectors(void)
{
    /* value 0x40 against operand 0x40 / 0x30 for each selector: expected taken. */
    static const struct{uint8_t sel,operand,taken;}cases[]={
        {0,0x40,1},{0,0x30,0},{1,0x40,0},{1,0x30,1},{2,0x30,1},{2,0x40,0},{3,0x50,1},{3,0x40,0},
        {4,0x40,1},{4,0x01,0},{5,0x01,1},{5,0x40,0},{6,0x40,0}};
    for(unsigned i=0;i<sizeof cases/sizeof *cases;++i){
        uint8_t *b=synthetic_pack(),*sc=script_bytes(b);const uint32_t S=C3_SCRIPT_REGION_BASE;
        sc[0]=0x2E;put32(sc+1,0x8047B627u);sc[5]=0x40;
        sc[6]=0x29;sc[7]=cases[i].sel;put32(sc+8,0x8047B627u);sc[12]=cases[i].operand;put32(sc+13,S+0x40);
        sc[17]=0x3E;sc[0x40]=0x3C;seal(b);
        C3BattleScriptPack pack;char e[C3_SCRIPT_ERROR_SIZE];CHECK(c3_battle_script_pack_decode(&pack,b,PACK_BYTES,e));
        Services sv={0};C3BattleScriptServices svc={&sv,sget,sset,starget,spass,NULL};
        C3BattleScript *s=calloc(1,sizeof *s);CHECK(s&&c3_battle_script_init(s,&pack,&svc));
        CHECK(c3_battle_script_call(s,S,1,0));run(s,16);CHECK(s->status==C3_SCRIPT_DONE);
        /* taken -> SEQRET at +0x40 (3 opcodes), else SEQEND at +17 (3 opcodes): distinguish by state path */
        CHECK(s->opcodes==3);
        free(s);
        /* Re-run to observe the PC at exit time via a failing marker instead. */
        sc[17]=0xFF;sc[0x40]=0xFE;seal(b);CHECK(c3_battle_script_pack_decode(&pack,b,PACK_BYTES,e));
        s=calloc(1,sizeof *s);CHECK(s&&c3_battle_script_init(s,&pack,&svc));
        CHECK(c3_battle_script_call(s,S,1,0));run(s,16);
        CHECK(s->status==C3_SCRIPT_FAILED&&s->failure_pc==(cases[i].taken?S+0x40:S+17));
        free(s);
    }
}

static void top_level_and_failures(void)
{
    uint8_t *b=synthetic_pack(),*sc=script_bytes(b);const uint32_t S=C3_SCRIPT_REGION_BASE;
    sc[0]=0x3D;                                                            /* WAZAEND at top level */
    uint8_t *f=sc+AT(C3_SCRIPT_FAINT);
    f[0]=0x2E;put32(f+1,0x8047B600u);f[5]=0x99;f[6]=0x3E;                  /* faint script: setbyte, SEQEND */
    sc[0x10]=0x00;                                                         /* WS_HITCHECK: not lowered */
    sc[0x20]=0x2E;put32(sc+0x21,0x80470000u);sc[0x25]=1;                   /* write unmapped */
    sc[0x30]=0x28;put32(sc+0x31,0x80000000u);                              /* jump outside region */
    seal(b);
    C3BattleScriptPack pack;char e[C3_SCRIPT_ERROR_SIZE];CHECK(c3_battle_script_pack_decode(&pack,b,PACK_BYTES,e));
    Services sv={0};sv.attacker=0xA77;sv.attacker_move=0x21;C3BattleScriptServices svc={&sv,sget,sset,starget,spass,NULL};
    C3BattleScript *s=calloc(1,sizeof *s);CHECK(s&&c3_battle_script_init(s,&pack,&svc));
    CHECK(c3_battle_script_write(s,0x8047B618u,4,0xFFFFFFFFu));
    CHECK(c3_battle_script_call(s,S,0x55,1));run(s,32);
    CHECK(s->status==C3_SCRIPT_DONE);
    /* Order: faint mark, faint script, four post passes, cleanup (target, 0xD9,
     * move-cleanup pass, window clears, three status writes), exit flags. */
    CHECK(!strcmp(sv.log,"pass 1 0;pass 2 1;pass 3 0;pass 4 0;pass 5 0;target 11 0;get 5 a77 0 d9 0;pass 6 0;"
        "set 5 a77 0 f3 0=0;set 5 a77 0 f4 0=9;set 6 21 0 2d 0=0;"));
    uint32_t v;
    CHECK(c3_battle_script_read(s,0x8047B600u,1,&v)&&v==0x99);
    CHECK(c3_battle_script_read(s,0x8047B618u,4,&v)&&v==(0xFFFFFFFFu&0xF1E892AFu&0xFFF7FDFFu));
    CHECK(c3_battle_script_read(s,0x80478D7Bu,1,&v)&&v==0);
    CHECK(c3_battle_script_read(s,0x80478D7Cu,1,&v)&&v==0);
    CHECK(c3_battle_script_read(s,0x8038FF5Au,1,&v)&&v==0);
    CHECK(c3_battle_script_read(s,0x8038FFF9u,1,&v)&&v==0);
    CHECK(s->state==0&&s->action==0&&!s->depth);
    free(s);

    const struct{uint32_t at;const char *needle;}bad[]={{S+0x10,"WS_HITCHECK"},{S+0x20,"unmapped"},{S+0x30,"outside"}};
    for(unsigned i=0;i<3;++i){
        s=calloc(1,sizeof *s);CHECK(s&&c3_battle_script_init(s,&pack,&svc));
        CHECK(c3_battle_script_call(s,bad[i].at,1,0));run(s,8);
        CHECK(s->status==C3_SCRIPT_FAILED&&strstr(s->failure,bad[i].needle)&&s->failure_pc==bad[i].at);
        CHECK(!c3_battle_script_call(s,S,1,0));                           /* failed stays failed */
        free(s);
    }
    s=calloc(1,sizeof *s);CHECK(s&&c3_battle_script_init(s,&pack,&svc));
    CHECK(!c3_battle_script_call(s,0x80000000u,1,0));
    for(unsigned i=0;i<C3_SCRIPT_MAX_DEPTH;++i)CHECK(c3_battle_script_call(s,S+0x10,1,0));
    CHECK(!c3_battle_script_call(s,S+0x10,1,0));                          /* depth bound */
    free(s);
}

static uint8_t *read_file(const char *path,size_t *n)
{
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long len=ftell(f);CHECK(len>0);rewind(f);
    uint8_t *d=malloc((size_t)len);CHECK(d&&fread(d,1,(size_t)len,f)==(size_t)len);fclose(f);*n=(size_t)len;return d;
}
static void original(const char *path)
{
    size_t n;uint8_t *bytes=read_file(path,&n);C3BattleScriptPack pack;char e[C3_SCRIPT_ERROR_SIZE];
    CHECK(c3_battle_script_pack_decode(&pack,bytes,n,e));
    CHECK(pack.effects[0]==0x80375F40u&&pack.effects[18]==0x80376343u&&pack.effects[19]==0x8037634Eu&&
          pack.effects[76]==0x80376C69u&&pack.effects[197]==0x803782B5u);
    Services sv={0};C3BattleScriptServices svc={&sv,sget,sset,starget,spass,NULL};
    C3BattleScript *s=calloc(1,sizeof *s);uint32_t v;
    /* Ordinary hit with Tackle: the Surf guard reads floor 0x37 and skips to
     * the attack canceler, the first battle-logic opcode. */
    sv.get_value=33;CHECK(s&&c3_battle_script_init(s,&pack,&svc));
    CHECK(c3_battle_script_call(s,pack.effects[0],1,1));run(s,16);
    CHECK(s->status==C3_SCRIPT_FAILED&&s->failure_pc==0x80375F8Eu&&strstr(s->failure,"WS_HITCHECK"));
    CHECK(c3_battle_script_read(s,0x8047B63Cu,2,&v)&&v==33&&s->opcodes==2);
    printf("hit/tackle stops at %08lx: %s\n",(unsigned long)s->failure_pc,s->failure);
    /* With Surf the guard falls through to the underwater check. */
    sv.get_value=0x39;sv.used=0;CHECK(c3_battle_script_init(s,&pack,&svc));
    CHECK(c3_battle_script_call(s,pack.effects[0],1,1));run(s,16);
    CHECK(s->status==C3_SCRIPT_FAILED&&s->failure_pc==0x80375F60u&&strstr(s->failure,"WS_WAZAKOUKA_CHECK"));
    printf("hit/surf stops at %08lx: %s\n",(unsigned long)s->failure_pc,s->failure);
    /* Growl selects attack -1 and jumps to the shared stat-down script. */
    CHECK(c3_battle_script_init(s,&pack,&svc));
    CHECK(c3_battle_script_call(s,pack.effects[18],1,1));run(s,16);
    CHECK(c3_battle_script_read(s,0x8038FF76u,1,&v)&&v==0x91);
    CHECK(s->status==C3_SCRIPT_FAILED&&s->failure_pc==0x8037637Au&&strstr(s->failure,"WS_HITCHECK"));
    printf("growl stops at %08lx: %s\n",(unsigned long)s->failure_pc,s->failure);
    free(s);free(bytes);
}

int main(void)
{
    pack_rejection();opcodes();jumpifbyte_selectors();top_level_and_failures();
    const char *pack=getenv("C3_SCRIPT_PACK");
    if(!pack){puts("battle_script synthetic OK; original pack absent");return 0;}
    original(pack);
    puts("battle_script OK");
    return 0;
}
