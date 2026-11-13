#include "colosseum3ds/story.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
typedef struct Fixture { unsigned char data[2048]; unsigned size; } Fixture;
static void be16(unsigned char *p, unsigned n) { p[0]=(unsigned char)(n>>8);p[1]=(unsigned char)n; }
static void be32(unsigned char *p, unsigned n) { for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(n>>(24-8*i)); }
static void le32(unsigned char *p, unsigned n) { for(unsigned i=0;i<4;++i)p[i]=(unsigned char)(n>>(8*i)); }
static unsigned bits(float f) { unsigned n;memcpy(&n,&f,4);return n; }
static void byte(Fixture *b,unsigned n) { CHECK(b->size<sizeof b->data);b->data[b->size++]=(unsigned char)n; }
static void half(Fixture *b,unsigned n) { byte(b,n>>8);byte(b,n); }
static void word(Fixture *b,unsigned n) { for(unsigned i=0;i<4;++i)byte(b,n>>(24-8*i)); }
static Fixture fixture(void) {
    Fixture b={{0},48};be16(b.data,0x123);be16(b.data+4,2);
    be32(b.data+16,32);be32(b.data+24,48);be32(b.data+28,48);return b;
}
static void immediate(Fixture *b,unsigned n) { byte(b,8);byte(b,2);word(b,n); }
static void drop(Fixture *b,unsigned n) { byte(b,3);byte(b,0x82);half(b,n); }
static void assign_return(Fixture *b) { byte(b,0x1f);byte(b,0x82);byte(b,0x42);drop(b,1);byte(b,5); }
static void extra_function(Fixture *b) { be32(b->data+28,b->size);byte(b,5); }
static unsigned evaluate(Fixture *b) {
    C3Story story={0};char error[C3_STORY_ERROR];C3StoryCall call;uint16_t key;
    CHECK(c3_story_add_bank(&story,b->data,b->size,error));
    CHECK(c3_story_start(&story,0x01230000,NULL,0,&key,error));
    CHECK(c3_story_step(&story,key,1000,&call,error)==C3_STORY_FINISHED);
    unsigned result=c3_story_result(&story,key);c3_story_free(&story);return result;
}
static void arithmetic(void) {
    static const struct { unsigned op,left,right,expected; } examples[]={
        {0x14,0xfffffffeu,3,0xfffffffau},{0x15,0xfffffff9u,2,0xfffffffdu},
        {0x16,0xfffffff9u,2,0xffffffffu},{0x17,0xffffffffu,2,1},
        {0x18,0,1,0xffffffffu},{0x19,0xffffffffu,0,1},
        {0x1a,4,4,1},{0x1b,4,5,0},{0x1c,5,5,1},{0x1d,8,8,1},
        {0x1e,8,7,1},{0x0d,0xa,6,2},{0x0e,0xa,6,0xe},
        {0x21,0,2,0},{0x22,0,2,1},{0x15,9,0,0},{0x16,9,0,0}
    };
    for(unsigned i=0;i<sizeof examples/sizeof examples[0];++i){
        Fixture b=fixture();immediate(&b,0);immediate(&b,examples[i].left);immediate(&b,examples[i].right);
        byte(&b,examples[i].op);byte(&b,0x82);byte(&b,0x82);assign_return(&b);extra_function(&b);
        CHECK(evaluate(&b)==examples[i].expected);
    }
    Fixture b=fixture();immediate(&b,0);immediate(&b,bits(0));byte(&b,0x11);byte(&b,0x83);
    /* Float logical NOT writes IEEE754 1.0, as fn_800F57F0 does. */
    byte(&b,0x1f);byte(&b,0x83);byte(&b,0x43);drop(&b,1);byte(&b,5);extra_function(&b);
    CHECK(evaluate(&b)==bits(1));
    b=fixture();immediate(&b,0);immediate(&b,3);immediate(&b,bits(0.5f));
    byte(&b,0x17);byte(&b,0x83);byte(&b,0x82);byte(&b,0x1f);byte(&b,0x83);byte(&b,0x43);drop(&b,1);byte(&b,5);extra_function(&b);
    CHECK(evaluate(&b)==bits(3.5f));
    b=fixture();immediate(&b,0);immediate(&b,bits(-3.75f));byte(&b,0x1f);byte(&b,0x83);byte(&b,0x42);drop(&b,1);byte(&b,5);extra_function(&b);
    CHECK(evaluate(&b)==0xfffffffdu);
}
static void calls_and_globals(void) {
    Fixture b=fixture();
    immediate(&b,0); /* caller return destination */
    immediate(&b,0);immediate(&b,21); /* callee return slot and argument */
    byte(&b,4);word(&b,1);half(&b,2);half(&b,0);drop(&b,1);assign_return(&b);
    be32(b.data+28,b.size);
    immediate(&b,0);immediate(&b,1);byte(&b,0x13);byte(&b,0x42);immediate(&b,2);
    byte(&b,0x14);byte(&b,0x82);byte(&b,0x82);assign_return(&b);
    CHECK(evaluate(&b)==42);

    /* An overlapping forward copy follows original _codeStructAssign,
     * which propagates word0 to words1..3 instead of using memmove. */
    b=fixture();be32(b.data+32,7);be32(b.data+36,8);be32(b.data+40,9);
    immediate(&b,1);immediate(&b,0);byte(&b,0x20);byte(&b,2);byte(&b,2);half(&b,3);drop(&b,3);
    immediate(&b,0);immediate(&b,3);byte(&b,0x13);byte(&b,2);assign_return(&b);extra_function(&b);
    CHECK(evaluate(&b)==7);

    b=fixture();immediate(&b,0);immediate(&b,0);byte(&b,0x11);byte(&b,0x82);
    byte(&b,7);unsigned target=b.size;word(&b,0);immediate(&b,99);assign_return(&b);
    be32(b.data+target,b.size);immediate(&b,42);assign_return(&b);extra_function(&b);
    CHECK(evaluate(&b)==99);
}
static void pending_and_addresses(void) {
    Fixture b=fixture();C3Story story={0};char error[C3_STORY_ERROR];C3StoryCall call,again;uint16_t key;uint32_t value;
    immediate(&b,0);immediate(&b,0);immediate(&b,0x22);immediate(&b,0);byte(&b,0x0f);byte(&b,2);
    byte(&b,0x25);half(&b,1);half(&b,2);assign_return(&b);extra_function(&b);
    CHECK(c3_story_add_bank(&story,b.data,b.size,error));CHECK(c3_story_start(&story,0x1230000,NULL,0,&key,error));
    CHECK(c3_story_step(&story,key,100,&call,error)==C3_STORY_WAITING);
    CHECK(call.kind==C3_STORY_NATIVE&&call.native==0x22&&call.argc==1&&call.script_id==0x1230000);
    CHECK(c3_story_step(&story,key,100,&again,error)==C3_STORY_WAITING&&!memcmp(&call,&again,sizeof call));
    CHECK(c3_story_read_value(&story,call.args[0],&value)&&value==0);
    CHECK(c3_story_write_value(&story,call.args[0],77));CHECK(c3_story_read_value(&story,call.args[0],&value)&&value==77);
    CHECK(!c3_story_read_value(&story,0x80000000,&value));CHECK(!c3_story_write_value(&story,0xb007ffff,1));
    CHECK(c3_story_resume(&story,key,9));CHECK(!c3_story_resume(&story,key,3));
    CHECK(c3_story_step(&story,key,100,&call,error)==C3_STORY_FINISHED&&c3_story_result(&story,key)==9);
    CHECK(!c3_story_active(&story,key));c3_story_free(&story);

    b=fixture();immediate(&b,60);byte(&b,0x24);half(&b,0);half(&b,1);
    immediate(&b,0);immediate(&b,12);assign_return(&b);extra_function(&b);
    CHECK(c3_story_add_bank(&story,b.data,b.size,error));CHECK(c3_story_start(&story,0x1230000,NULL,0,&key,error));
    CHECK(c3_story_step(&story,key,100,&call,error)==C3_STORY_WAITING&&call.kind==C3_STORY_WAIT_FRAMES&&call.args[0]==60);
    CHECK(c3_story_resume(&story,key,0xffffffff));
    CHECK(c3_story_step(&story,key,100,&call,error)==C3_STORY_FINISHED&&c3_story_result(&story,key)==12);c3_story_free(&story);
}
static void malformed_and_limits(void) {
    C3Story story={0};C3StoryCall call;Fixture b=fixture();char error[C3_STORY_ERROR];uint16_t key,keys[C3_STORY_TASKS];
    byte(&b,6);word(&b,48);extra_function(&b);
    CHECK(c3_story_add_bank(&story,b.data,b.size,error));CHECK(!c3_story_add_bank(&story,b.data,b.size,error));
    for(unsigned i=0;i<C3_STORY_TASKS;++i)CHECK(c3_story_start(&story,0x1230000,NULL,0,&keys[i],error));
    CHECK(!c3_story_start(&story,0x1230000,NULL,0,&key,error));
    CHECK(c3_story_step(&story,keys[0],100,&call,error)==C3_STORY_RUNNING);
    CHECK(c3_story_stop(&story,keys[0]));CHECK(c3_story_start(&story,0x1230000,NULL,0,&key,error)&&key!=keys[0]);
    c3_story_free(&story);
    b=fixture();byte(&b,8);byte(&b,2); /* truncated immediate */
    CHECK(c3_story_add_bank(&story,b.data,b.size,error));CHECK(c3_story_start(&story,0x1230000,NULL,0,&key,error));
    CHECK(c3_story_step(&story,key,100,&call,error)==C3_STORY_FAILED&&strstr(error,"01230000 PC 00000030"));c3_story_free(&story);
    b=fixture();immediate(&b,1000);byte(&b,0x13);byte(&b,2);extra_function(&b);
    CHECK(c3_story_add_bank(&story,b.data,b.size,error));CHECK(c3_story_start(&story,0x1230000,NULL,0,&key,error));
    CHECK(c3_story_step(&story,key,100,&call,error)==C3_STORY_FAILED&&strstr(error,"global index"));c3_story_free(&story);
    b=fixture();for(unsigned i=0;i<65;++i)byte(&b,2);extra_function(&b);
    CHECK(c3_story_add_bank(&story,b.data,b.size,error));CHECK(c3_story_start(&story,0x1230000,NULL,0,&key,error));
    CHECK(c3_story_step(&story,key,100,&call,error)==C3_STORY_FAILED&&strstr(error,"stack overflow"));c3_story_free(&story);
    b=fixture();drop(&b,65);extra_function(&b);
    CHECK(c3_story_add_bank(&story,b.data,b.size,error));CHECK(c3_story_start(&story,0x1230000,NULL,0,&key,error));
    CHECK(c3_story_step(&story,key,100,&call,error)==C3_STORY_FAILED&&strstr(error,"drop underflow"));c3_story_free(&story);
}
static void pack_and_messages(void) {
    Fixture b=fixture();byte(&b,5);extra_function(&b);
    unsigned char pack[4096]={0},msg[128]={0};char output[128],error[C3_STORY_ERROR];C3Story story={0};
    unsigned m=32;be16(msg+4,2);be32(msg+16,1);be32(msg+20,m);
    /* ... then U+3000 (full-width space padding, as after Phenac's
     * "became a partner!") and U+2026, which the ASCII field font lacks. */
    const unsigned char text[]={0,84,0,69,0,83,0,84,0,32,255,255,0x2b,255,255,0,255,255,0x59,255,255,3,0,33,0x30,0,0x20,0x26,255,255,2,0,0};
    memcpy(msg+m,text,sizeof text);m+=sizeof text;be32(msg+24,2);be32(msg+28,m);
    msg[m++]=255;msg[m++]=255;msg[m++]=0x7f;msg[m++]=0;msg[m++]=0;
    unsigned total=96+b.size+m;
    memcpy(pack,"C3S1",4);le32(pack+4,1);le32(pack+8,total);le32(pack+12,2);le32(pack+16,1);le32(pack+20,1);
    le32(pack+24,64);le32(pack+28,80);le32(pack+64,0x123);le32(pack+68,96);le32(pack+72,b.size);
    le32(pack+84,96+b.size);le32(pack+88,m);memcpy(pack+96,b.data,b.size);memcpy(pack+96+b.size,msg,m);
    CHECK(c3_story_decode(&story,pack,total,error)&&c3_story_floor(&story)==2);
    CHECK(c3_story_message(&story,1,"ALPHA",NULL,"BETA",output,sizeof output,error)&&!strcmp(output,"TEST ALPHA\nBETA\f! ..."));
    CHECK(!c3_story_message(&story,1,"ALPHA",NULL,"BETA",output,5,error));
    CHECK(!c3_story_message(&story,2,NULL,NULL,NULL,output,sizeof output,error)&&strstr(error,"control 7f"));
    CHECK(!c3_story_message(&story,999,NULL,NULL,NULL,output,sizeof output,error));
    void *old=story.impl;pack[32]=1;CHECK(!c3_story_decode(&story,pack,total,error)&&story.impl==old);pack[32]=0;
    le32(pack+64,999);CHECK(!c3_story_decode(&story,pack,total,error)&&story.impl==old&&strstr(error,"ID mismatch"));
    le32(pack+64,0x123);CHECK(!c3_story_decode(&story,pack,total-1,error)&&story.impl==old);
    CHECK(c3_story_decode(&story,pack,total,error));c3_story_free(&story);
}
int main(void) {
    arithmetic();calls_and_globals();pending_and_addresses();malformed_and_limits();pack_and_messages();
    printf("story VM: %u checks passed\n",checks);return 0;
}
