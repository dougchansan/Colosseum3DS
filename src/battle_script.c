/* Source lowering: fight_seq.c fn_80211B94 (move-script interpreter) and the
 * script-engine opcodes from fight_range_exact_80222110.c / _802136A4.c.
 * Battle-logic opcodes are added as they are lowered; until then they fail
 * with their original handler name. */
#include "colosseum3ds/battle_script.h"
#include "save_codec.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const char *const opcode_names[255] = {
#include "battle_script_names.inc"
};

const char *c3_battle_script_opcode_name(uint8_t op)
{
    return op < 255 ? opcode_names[op] : "(none)";
}

static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

/* ---- pack ---------------------------------------------------------------- */

static int pack_fail(char *e,const char *m){if(e)snprintf(e,C3_SCRIPT_ERROR_SIZE,"%s",m);return 0;}

int c3_battle_script_pack_decode(C3BattleScriptPack *out,const uint8_t *b,size_t n,char *e)
{
    enum{SECTIONS=8};
    static const struct{char tag[5];uint32_t base,len;}want[SECTIONS]={
        {"SCR ",C3_SCRIPT_REGION_BASE,C3_SCRIPT_REGION_BYTES},{"EFF ",0x80379BFFu,C3_SCRIPT_EFFECTS*4u},
        {"WSD ",C3_SCRIPT_WSD_BASE,C3_SCRIPT_WSD_BYTES},{"WDT ",C3_SCRIPT_WDT_BASE,C3_SCRIPT_WDT_BYTES},
        {"CNC ",0x80478B48u,4},{"CND ",0x8035BBA8u,C3_CONDITION_TABLE_BYTES},
        {"HTC ",0x80478D70u,4},{"HIT ",0x80375DD0u,C3_ACCURACY_STAGES*2u}};
    static const uint8_t dol_sha1[20]={0x87,0x0e,0x8b,0x96,0x93,0xca,0x78,0x07,0x82,0xd8,
        0x0f,0x22,0xa6,0xa4,0x57,0x2d,0x8b,0xa9,0x45,0x8f};
    uint8_t digest[20];size_t expect=64+SECTIONS*12;const uint8_t *data[SECTIONS];
    if(!out||!b)return pack_fail(e,"Missing move-script pack.");
    for(unsigned i=0;i<SECTIONS;++i)expect+=want[i].len;
    if(n!=expect||memcmp(b,"C3W1",4)||be32(b+4)!=3||be32(b+8)!=n||be32(b+12)!=SECTIONS||memcmp(b+16,dol_sha1,20))
        return pack_fail(e,"Unsupported or damaged move-script pack.");
    for(unsigned i=56;i<64;++i)if(b[i])return pack_fail(e,"Unsupported or damaged move-script pack.");
    c3_save_codec_sha1(digest,b+64,(uint32_t)(n-64));
    if(memcmp(digest,b+36,20))return pack_fail(e,"Move-script pack checksum mismatch.");
    size_t at=64+SECTIONS*12;
    for(unsigned i=0;i<SECTIONS;++i){const uint8_t *d=b+64+12*i;
        if(memcmp(d,want[i].tag,4)||be32(d+4)!=want[i].base||be32(d+8)!=want[i].len)
            return pack_fail(e,"Move-script pack sections are out of order.");
        data[i]=b+at;at+=want[i].len;}
    C3BattleScriptPack next;memset(&next,0,sizeof next);
    next.script=data[0];
    for(unsigned i=0;i<C3_SCRIPT_EFFECTS;++i){
        next.effects[i]=be32(data[1]+4*i);
        if(next.effects[i]<C3_SCRIPT_REGION_BASE||next.effects[i]>=C3_SCRIPT_REGION_BASE+C3_SCRIPT_REGION_BYTES)
            return pack_fail(e,"Move-script effect pointer is outside the script region.");
    }
    memcpy(next.wsd,data[2],sizeof next.wsd);memcpy(next.wdt,data[3],sizeof next.wdt);
    next.condition_count=be32(data[4]);next.conditions=data[5];
    if(next.condition_count>C3_CONDITION_TABLE_BYTES/C3_CONDITION_ROW_BYTES)
        return pack_fail(e,"Condition count exceeds the condition table.");
    next.accuracy_count=be32(data[6]);next.accuracy=data[7];
    if(next.accuracy_count!=C3_ACCURACY_STAGES)return pack_fail(e,"Unexpected accuracy-stage count.");
    *out=next;if(e)e[0]=0;return 1;
}

/* ---- failure, memory ----------------------------------------------------- */

static void vfail(C3BattleScript *s,const char *fmt,va_list ap)
{
    if(s->status!=C3_SCRIPT_FAILED){
        vsnprintf(s->failure,sizeof s->failure,fmt,ap);
        s->failure_pc=s->pc;s->status=C3_SCRIPT_FAILED;
    }
}
static int fail(C3BattleScript *s,const char *fmt,...)
{
    va_list ap;va_start(ap,fmt);vfail(s,fmt,ap);va_end(ap);
    return 0;
}
int c3_battle_script_fail(C3BattleScript *s,const char *fmt,...)
{
    if(!s)return 0;
    va_list ap;va_start(ap,fmt);vfail(s,fmt,ap);va_end(ap);
    return 0;
}

static uint8_t *window(C3BattleScript *s,uint32_t a,unsigned w)
{
    /* Interpreter-owned slots are not script variables. */
    if((a<0x8047B618u&&a+w>0x8047B610u)||(a<0x8047B630u&&a+w>0x8047B62Cu))return NULL;
    if(a>=C3_SCRIPT_WSD_BASE&&a+w<=C3_SCRIPT_WSD_BASE+C3_SCRIPT_WSD_BYTES)return s->wsd+(a-C3_SCRIPT_WSD_BASE);
    if(a>=C3_SCRIPT_SBSS_BASE&&a+w<=C3_SCRIPT_SBSS_BASE+C3_SCRIPT_SBSS_BYTES)return s->sbss+(a-C3_SCRIPT_SBSS_BASE);
    if(a>=C3_SCRIPT_WDT_BASE&&a+w<=C3_SCRIPT_WDT_BASE+C3_SCRIPT_WDT_BYTES)return s->wdt+(a-C3_SCRIPT_WDT_BASE);
    return NULL;
}
int c3_battle_script_read(const C3BattleScript *s,uint32_t a,unsigned w,uint32_t *v)
{
    if(!s||!v||(w!=1&&w!=2&&w!=4))return 0;
    const uint8_t *p=window((C3BattleScript *)s,a,w);if(!p)return 0;
    uint32_t x=0;for(unsigned i=0;i<w;++i)x=x<<8|p[i];*v=x;return 1;
}
int c3_battle_script_write(C3BattleScript *s,uint32_t a,unsigned w,uint32_t v)
{
    if(!s||(w!=1&&w!=2&&w!=4))return 0;
    uint8_t *p=window(s,a,w);if(!p)return 0;
    for(unsigned i=0;i<w;++i)p[i]=(uint8_t)(v>>8*(w-1-i));
    return 1;
}
static int var_read(C3BattleScript *s,uint32_t a,unsigned w,uint32_t *v)
{
    if(c3_battle_script_read(s,a,w,v))return 1;
    return fail(s,"Script operand reads unmapped address %08lx (width %u)",(unsigned long)a,w);
}
static int var_write(C3BattleScript *s,uint32_t a,unsigned w,uint32_t v)
{
    if(c3_battle_script_write(s,a,w,v))return 1;
    return fail(s,"Script operand writes unmapped address %08lx (width %u)",(unsigned long)a,w);
}

/* Bytecode fetch relative to the PC, bounded to the script region. */
static int fetch(C3BattleScript *s,uint32_t off,unsigned w,uint32_t *v)
{
    uint32_t a=s->pc+off;
    if(a<C3_SCRIPT_REGION_BASE||a+w>C3_SCRIPT_REGION_BASE+C3_SCRIPT_REGION_BYTES)
        return fail(s,"Script fetch outside the script region at %08lx",(unsigned long)a);
    const uint8_t *p=s->pack->script+(a-C3_SCRIPT_REGION_BASE);
    uint32_t x=0;for(unsigned i=0;i<w;++i)x=x<<8|p[i];*v=x;return 1;
}
static int jump(C3BattleScript *s,uint32_t target)
{
    if(target<C3_SCRIPT_REGION_BASE||target>=C3_SCRIPT_REGION_BASE+C3_SCRIPT_REGION_BYTES)
        return fail(s,"Script jump target %08lx is outside the script region",(unsigned long)target);
    s->pc=target;return 1;
}

int c3_battle_script_fetch(C3BattleScript *s,uint32_t off,unsigned w,uint32_t *v)
{return s&&v&&(w==1||w==2||w==4)?fetch(s,off,w,v):0;}
int c3_battle_script_jump(C3BattleScript *s,uint32_t target){return s?jump(s,target):0;}

/* ---- handlers ------------------------------------------------------------ */

enum { H_FAIL = 0, H_DONE = 1, H_WAIT = 2, H_CALL = 4 };
typedef int (*Handler)(C3BattleScript *, unsigned phase);

/* 0x28 fn_80222ACC: PC = *(u8**)(PC + 1) */
static int op_goto(C3BattleScript *s,unsigned ph){(void)ph;uint32_t t=0;return fetch(s,1,4,&t)&&jump(s,t)?H_DONE:H_FAIL;}

/* 0x29 fn_802229EC / 0x2A fn_8022290C: compare a byte/halfword variable. */
static int compare(C3BattleScript *s,unsigned w)
{
    uint32_t sel=0,pa=0,vb=0,nxt=0,v=0;unsigned len=w==1?11u:12u;
    if(!fetch(s,1,1,&sel)||!fetch(s,2,4,&pa)||!fetch(s,6,w,&vb)||!fetch(s,w==1?7u:8u,4,&nxt))return H_FAIL;
    s->pc+=len;
    if(sel>5)return H_DONE;             /* switch without a matching case */
    if(!var_read(s,pa,w,&v))return H_FAIL;
    /* The original compares against (u8)(s8)vb / (u16)(s16)vb: same bits. */
    int taken=0;
    switch(sel){
    case 0:taken=v==vb;break; case 1:taken=v!=vb;break;
    case 2:taken=v>vb;break;  case 3:taken=v<vb;break;
    case 4:taken=(v&vb)!=0;break; case 5:taken=(v&vb)==0;break;
    }
    return taken&&!jump(s,nxt)?H_FAIL:H_DONE;
}
static int op_jumpifbyte(C3BattleScript *s,unsigned ph){(void)ph;return compare(s,1);}
static int op_jumpifhalf(C3BattleScript *s,unsigned ph){(void)ph;return compare(s,2);}

/* 0x2E fn_8022273C: *(u8*)op1 = op5 */
static int op_setbyte(C3BattleScript *s,unsigned ph)
{
    (void)ph;uint32_t dst=0,v=0;
    if(!fetch(s,1,4,&dst)||!fetch(s,5,1,&v)||!var_write(s,dst,1,v))return H_FAIL;
    s->pc+=6;return H_DONE;
}
/* 0x35 fn_802225DC: *(u32*)op1 |= op5 */
static int op_orword(C3BattleScript *s,unsigned ph)
{
    (void)ph;uint32_t dst=0,v=0,old=0;
    if(!fetch(s,1,4,&dst)||!fetch(s,5,4,&v)||!var_read(s,dst,4,&old)||!var_write(s,dst,4,old|v))return H_FAIL;
    s->pc+=9;return H_DONE;
}
/* 0x39 fn_80222520: fightMainWaitFrame(op1); PC += 3 */
static int op_pause(C3BattleScript *s,unsigned ph)
{
    uint32_t frames=0;
    if(ph==0){
        if(!fetch(s,1,2,&frames))return H_FAIL;
        s->wait_frames=(uint16_t)frames;return H_WAIT;
    }
    s->pc+=3;return H_DONE;
}
/* 0x3A fn_80222510: PC++ */
static int op_skip(C3BattleScript *s,unsigned ph){(void)ph;s->pc+=1;return H_DONE;}
/* 0x3C WS_SEQRET: state = 2; 0x3D WS_WAZAEND, 0x3E WS_SEQEND, 0xF6 WS_ITEMEND,
 * 0xF7 WS_GETEND: state = 1. The PC does not move. */
static int op_return(C3BattleScript *s,unsigned ph){(void)ph;s->state=2;return H_DONE;}
static int op_end(C3BattleScript *s,unsigned ph){(void)ph;s->state=1;return H_DONE;}

/* 0xF8..0xFD WS_STATUS_GET8/16/32, SET8/16/32 (fight_range_exact_802136A4.c):
 *   id @1, p2 @2 (u32*), p6 @6 (u8*), value @0xA (u16), pC @0xC (u8*), p10 @0x10. */
static int status_op(C3BattleScript *s,unsigned w,int set)
{
    uint32_t id=0,p2=0,p6=0,field=0,pc_=0,p10=0,a2=0,a3=0,a5=0,v=0;
    if(!fetch(s,1,1,&id)||!fetch(s,2,4,&p2)||!fetch(s,6,4,&p6)||!fetch(s,0xA,2,&field)||
       !fetch(s,0xC,4,&pc_)||!fetch(s,0x10,4,&p10))return H_FAIL;
    if((p2&&!var_read(s,p2,4,&a2))||(p6&&!var_read(s,p6,1,&a3))||(pc_&&!var_read(s,pc_,1,&a5)))return H_FAIL;
    if(set){
        if(!var_read(s,p10,w,&v))return H_FAIL;
        if(!s->services.status_set(s->services.user,(uint8_t)id,a2,a3,field,a5,v))
            return fail(s,"statusSetStatus(%lu, field %#lx) failed",(unsigned long)id,(unsigned long)field),H_FAIL;
    }else{
        if(!s->services.status_get(s->services.user,(uint8_t)id,a2,a3,field,a5,&v))
            return fail(s,"statusGetStatus(%lu, field %#lx) failed",(unsigned long)id,(unsigned long)field),H_FAIL;
        if(w<4)v&=(1u<<8*w)-1u;
        if(!var_write(s,p10,w,v))return H_FAIL;
    }
    s->pc+=0x14;return H_DONE;
}
static int op_get8(C3BattleScript *s,unsigned ph){(void)ph;return status_op(s,1,0);}
static int op_get16(C3BattleScript *s,unsigned ph){(void)ph;return status_op(s,2,0);}
static int op_get32(C3BattleScript *s,unsigned ph){(void)ph;return status_op(s,4,0);}
static int op_set8(C3BattleScript *s,unsigned ph){(void)ph;return status_op(s,1,1);}
static int op_set16(C3BattleScript *s,unsigned ph){(void)ph;return status_op(s,2,1);}
static int op_set32(C3BattleScript *s,unsigned ph){(void)ph;return status_op(s,4,1);}

static Handler handler_for(uint8_t op)
{
    switch(op){
    case 0x28:return op_goto;     case 0x29:return op_jumpifbyte; case 0x2A:return op_jumpifhalf;
    case 0x2E:return op_setbyte;  case 0x35:return op_orword;     case 0x39:return op_pause;
    case 0x3A:return op_skip;     case 0x3C:return op_return;     case 0x3D:return op_end;
    case 0x3E:return op_end;      case 0xF6:return op_end;        case 0xF7:return op_end;
    case 0xF8:return op_get8;     case 0xF9:return op_get16;      case 0xFA:return op_get32;
    case 0xFB:return op_set8;     case 0xFC:return op_set16;      case 0xFD:return op_set32;
    default:return NULL;
    }
}

/* ---- interpreter --------------------------------------------------------- */

int c3_battle_script_init(C3BattleScript *s,const C3BattleScriptPack *pack,const C3BattleScriptServices *sv)
{
    if(!s||!pack||!pack->script||!sv||!sv->status_get||!sv->status_set||!sv->target||!sv->end_of_move)return 0;
    memset(s,0,sizeof *s);
    s->pack=pack;s->services=*sv;
    memcpy(s->wsd,pack->wsd,sizeof s->wsd);memcpy(s->wdt,pack->wdt,sizeof s->wdt);
    s->status=C3_SCRIPT_IDLE;return 1;
}

int c3_battle_script_call(C3BattleScript *s,uint32_t start,uint32_t action,int top)
{
    if(!s||!s->pack||s->status==C3_SCRIPT_FAILED||s->status==C3_SCRIPT_WAITING||s->depth>=C3_SCRIPT_MAX_DEPTH)return 0;
    if(start<C3_SCRIPT_REGION_BASE||start>=C3_SCRIPT_REGION_BASE+C3_SCRIPT_REGION_BYTES)return 0;
    C3BattleScriptFrame *f=&s->frames[s->depth++];
    memset(f,0,sizeof *f);
    f->saved_pc=s->pc;f->saved_action=s->action;f->saved_state=s->state;f->top_level=top?1:0;
    f->saved_handler_phase=s->handler_phase;
    f->phase=C3_SCRIPT_PHASE_RUN;
    s->pc=start;
    if(!top)s->state=0;
    s->action=action;s->handler_phase=0;s->status=C3_SCRIPT_RUNNING;
    return 1;
}

/* Execute (or resume) the opcode at PC. Returns H_DONE/H_WAIT/H_FAIL. */
static int execute(C3BattleScript *s)
{
    uint32_t op=0;
    if(!fetch(s,0,1,&op))return H_FAIL;
    Handler h=handler_for((uint8_t)op);
    int r;
    unsigned depth_before=s->depth;
    if(h)r=h(s,s->handler_phase);
    else{
        r=s->services.handler?s->services.handler(s->services.user,s,(uint8_t)op,s->handler_phase):C3_SCRIPT_HANDLER_NONE;
        if(r==C3_SCRIPT_HANDLER_NONE)
            return fail(s,"Opcode 0x%02lx %s is not lowered yet",(unsigned long)op,c3_battle_script_opcode_name((uint8_t)op)),H_FAIL;
        if(r==C3_SCRIPT_HANDLER_FAIL&&s->status!=C3_SCRIPT_FAILED)
            return fail(s,"Opcode 0x%02lx %s failed",(unsigned long)op,c3_battle_script_opcode_name((uint8_t)op)),H_FAIL;
        if(r==C3_SCRIPT_HANDLER_CALL){
            /* The nested frame resumes this opcode at the next phase. */
            if(s->depth!=depth_before+1u)return fail(s,"Nested sequence was not pushed"),H_FAIL;
            s->frames[s->depth-1].saved_handler_phase=(uint8_t)(s->frames[s->depth-1].saved_handler_phase+1u);
            return H_CALL;
        }
    }
    if(r==H_WAIT){++s->handler_phase;s->status=C3_SCRIPT_WAITING;return H_WAIT;}
    if(r==H_DONE){s->handler_phase=0;++s->opcodes;}
    return r;
}

static int pass(C3BattleScript *s,C3BattleScriptPass p,uint8_t arg)
{
    if(s->services.end_of_move(s->services.user,p,arg))return 1;
    return fail(s,"End-of-move pass %d failed",(int)p);
}

/* Final part of a top-level call, after the faint script and passes. */
static int cleanup(C3BattleScript *s)
{
    uint32_t attacker=0,move=0,flags=0;
    if(!s->services.target(s->services.user,0x11,0,&attacker))return fail(s,"Attacker target (0x11) is unavailable");
    if(!s->services.status_get(s->services.user,5,attacker,0,0xD9,0,&move))return fail(s,"Attacker field 0xD9 is unavailable");
    if(!pass(s,C3_SCRIPT_PASS_MOVE_CLEANUP,0))return 0;
    if(!var_write(s,0x80478D7Bu,1,0)||!var_read(s,0x8047B618u,4,&flags)||!var_write(s,0x8047B618u,4,flags&0xF1E892AFu)||
       !var_write(s,0x8047B625u,1,0)||!var_write(s,0x80478D7Cu,1,0)||!var_write(s,0x8038FF5Au,1,0)||
       !var_write(s,0x8038FFF9u,1,0))return 0;
    if(!s->services.status_set(s->services.user,5,attacker,0,0xF3,0,0)||
       !s->services.status_set(s->services.user,5,attacker,0,0xF4,0,9)||
       !s->services.status_set(s->services.user,6,move,0,0x2D,0,0))
        return fail(s,"Attacker end-of-move status writes failed");
    return 1;
}

static void exit_frame(C3BattleScript *s)
{
    C3BattleScriptFrame *f=&s->frames[s->depth-1];
    if(f->top_level){
        uint32_t flags=0;
        /* LAB_0020eddc: flags &= ~0x200, then flags = old & 0xFFF7FDFF. */
        if(!var_read(s,0x8047B618u,4,&flags)||!var_write(s,0x8047B618u,4,flags&0xFFF7FDFFu))return;
    }
    s->action=f->saved_action;s->state=f->saved_state;s->pc=f->saved_pc;
    s->handler_phase=f->saved_handler_phase;
    --s->depth;
    s->status=s->depth?C3_SCRIPT_RUNNING:C3_SCRIPT_DONE;
}

C3BattleScriptStatus c3_battle_script_step(C3BattleScript *s)
{
    if(!s)return C3_SCRIPT_FAILED;
    if(s->status!=C3_SCRIPT_RUNNING)return s->status;
    C3BattleScriptFrame *f=&s->frames[s->depth-1];
    int r;
    switch(f->phase){
    case C3_SCRIPT_PHASE_RUN:
        if((r=execute(s))!=H_DONE)break;
        if(!f->top_level){
            if(s->state==1||s->state==2)f->phase=C3_SCRIPT_PHASE_EXIT;
        }else if(s->state==1){
            /* Faint processing: mark, then run lbl_80378798 inline with the
             * outer action, state 0, saving PC/state (uVar7/uVar2). */
            if(!pass(s,C3_SCRIPT_PASS_FAINT_MARK,0))break;
            f->faint_action=s->action;f->faint_state=s->state;f->faint_pc=s->pc;
            s->state=0;s->pc=C3_SCRIPT_FAINT;f->phase=C3_SCRIPT_PHASE_FAINT_SCRIPT;
        }else if(s->state==2)f->phase=C3_SCRIPT_PHASE_CLEANUP;
        break;
    case C3_SCRIPT_PHASE_FAINT_SCRIPT:
        if((r=execute(s))!=H_DONE)break;
        if(s->state==1||s->state==2){
            s->action=f->faint_action;s->state=f->faint_state;s->pc=f->faint_pc;
            f->phase=C3_SCRIPT_PHASE_POST_A;
        }
        break;
    case C3_SCRIPT_PHASE_POST_A:if(pass(s,C3_SCRIPT_PASS_POST_FAINT_A,1))f->phase=C3_SCRIPT_PHASE_POST_B;break;
    case C3_SCRIPT_PHASE_POST_B:if(pass(s,C3_SCRIPT_PASS_POST_FAINT_B,0))f->phase=C3_SCRIPT_PHASE_POST_C;break;
    case C3_SCRIPT_PHASE_POST_C:if(pass(s,C3_SCRIPT_PASS_POST_FAINT_C,0))f->phase=C3_SCRIPT_PHASE_POST_D;break;
    case C3_SCRIPT_PHASE_POST_D:
        if(pass(s,C3_SCRIPT_PASS_POST_FAINT_D,0)){s->state=2;f->phase=C3_SCRIPT_PHASE_CLEANUP;}
        break;
    case C3_SCRIPT_PHASE_CLEANUP:
        if(cleanup(s))f->phase=C3_SCRIPT_PHASE_EXIT;
        break;
    case C3_SCRIPT_PHASE_EXIT:
        exit_frame(s);
        break;
    }
    return s->status;
}

int c3_battle_script_resume(C3BattleScript *s)
{
    if(!s||s->status!=C3_SCRIPT_WAITING)return 0;
    s->status=C3_SCRIPT_RUNNING;s->wait_frames=0;return 1;
}
