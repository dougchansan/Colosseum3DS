#include "colosseum3ds/area.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { HEADER=128, ACTOR=160, TRIANGLE=56, LINK=32,
       TRANS=HEADER+2*ACTOR, INTER=TRANS+2*TRIANGLE,
       LINKS=INTER+2*TRIANGLE, PACK=LINKS+3*LINK };
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"FAIL %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static void w16(uint8_t *p,unsigned v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void w32(uint8_t *p,uint32_t v)
{for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static void wf(uint8_t *p,float v){uint32_t bits;memcpy(&bits,&v,4);w32(p,bits);}

/* Synthetic geometry/actors only. Optional arguments load privately prepared
 * real packs; no original positions, scripts, or dialogue are embedded. */
static void fixture(uint8_t p[PACK+1])
{
    static const uint8_t pin[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,
        0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    static const float vertices[9]={0,-3,-3,0,3,-3,0,0,3};
    memset(p,0,PACK+1);memcpy(p,"C3A1",4);w32(p+4,1);w32(p+8,PACK);
    w32(p+12,1);w32(p+16,29);w32(p+20,2);w32(p+24,2);w32(p+28,2);
    w32(p+32,HEADER);w32(p+36,TRANS);w32(p+40,INTER);w32(p+44,ACTOR);w32(p+48,TRIANGLE);
    w32(p+56,3);w32(p+60,LINKS);memcpy(p+64,pin,20);
    for(unsigned i=0;i<5;++i)w32(p+84+4*i,0x1000010u+i);
    for(unsigned i=0;i<2;++i){
        uint8_t *a=p+HEADER+i*ACTOR;
        w32(a,i+2);w32(a+4,0x123400u+i);w32(a+8,100+i);w32(a+12,0x87654321);
        w32(a+16,0x1000020u+i);w32(a+20,0x1000030u+i);w32(a+24,0x10203040);w32(a+28,0x50607080);
        wf(a+32,2);wf(a+36,3);wf(a+40,4);wf(a+44,.5f);wf(a+48,.4f);wf(a+52,.9f);wf(a+56,1.3f);
        for(unsigned j=0;j<9;++j)a[60+j]=(uint8_t)(j+1);
        a[69]=(uint8_t)(i+1);
        if(i)memcpy(a+72,"actor_1.c3t",12);
        w32(a+136,500+i);w32(a+140,2000+i);w32(a+144,0x54321+i);w32(a+148,2+i);w32(a+152,1+i);
    }
    for(unsigned i=0;i<4;++i){
        uint8_t *t=p+TRANS+i*TRIANGLE;
        for(unsigned j=0;j<9;++j)wf(t+4*j,vertices[j]);
        wf(t+36,1);w16(t+48,0x1234);w16(t+50,i%2?42:7);w16(t+52,i+1);
    }
    for(unsigned i=0;i<3;++i){
        uint8_t *l=p+LINKS+i*LINK;
        w32(l,i?3:1);w32(l+4,i==2?42:7);w32(l+8,0x2000000u+i);
        for(unsigned j=0;j<4;++j)w32(l+12+4*j,100*i+j);
        w32(l+28,50+i);
    }
}

static void rejected(C3Area *area,const void *p,size_t bytes)
{
    C3Area prior=*area;C3AreaActor actor=area->actors[0];char error[128]={0};
    CHECK(!c3_area_parse(area,p,bytes,error,sizeof(error)));CHECK(error[0]);
    CHECK(!memcmp(area,&prior,sizeof(prior)));CHECK(!memcmp(area->actors,&actor,sizeof(actor)));
}

static void mutation(C3Area *area,const uint8_t *original,unsigned offset,uint32_t value)
{
    uint8_t p[PACK+1];memcpy(p,original,sizeof(p));w32(p+offset,value);rejected(area,p,PACK);
}

static void contacts(C3Area *area)
{
    float before[3]={1,0,0},after[3]={.2f,0,0};
    CHECK(c3_area_transition(area,before,after,.4f)==area->transitions);
    CHECK(!c3_area_transition(area,before,before,.4f));
    CHECK(!c3_area_transition(area,after,before,.4f));
    after[0]=.6f;CHECK(!c3_area_transition(area,before,after,.4f));
    after[0]=-2;CHECK(c3_area_transition(area,before,after,.4f)==area->transitions);
    after[0]=-1;before[1]=-5;after[1]=5; /* Crosses the face, but both projected endpoints are outside. */
    CHECK(c3_area_transition(area,before,after,.4f)==area->transitions);
    before[1]=after[1]=5;CHECK(!c3_area_transition(area,before,after,.4f));
    before[0]=-.1f;before[1]=after[1]=0;CHECK(!c3_area_transition(area,before,after,.4f));
    before[0]=1;after[0]=.2f;
    CHECK(!c3_area_transition(NULL,before,after,.4f));
    CHECK(!c3_area_transition(area,NULL,after,.4f));
    CHECK(!c3_area_transition(area,before,NULL,.4f));
    CHECK(!c3_area_transition(area,before,after,0));
    CHECK(!c3_area_transition(area,before,after,-1));
    CHECK(!c3_area_transition(area,before,after,NAN));
    before[2]=NAN;CHECK(!c3_area_transition(area,before,after,.4f));before[2]=0;
    after[2]=INFINITY;CHECK(!c3_area_transition(area,before,after,.4f));after[2]=0;
    /* Source order, not numeric ID, decides overlapping surfaces. */
    area->transitions[0].source_id=600;area->transitions[1].source_id=2;
    CHECK(c3_area_transition(area,before,after,.4f)==area->transitions);
    C3FieldMap active={.object_count=2};
    area->transitions[0].object=0;area->transitions[1].object=1;
    CHECK(c3_field_set_object_enabled(&active,0,0));
    CHECK(c3_area_transition_active(area,&active,before,after,.4f)==area->transitions+1);
    CHECK(c3_field_set_object_enabled(&active,1,0));
    CHECK(!c3_area_transition_active(area,&active,before,after,.4f));
    CHECK(c3_field_set_object_enabled(&active,0,1));
    CHECK(c3_area_transition_active(area,&active,before,after,.4f)==area->transitions);
    /* A degenerate face cannot produce contact; a later valid one still can. */
    memset(area->transitions[0].vertices,0,sizeof(area->transitions[0].vertices));
    CHECK(c3_area_transition(area,before,after,.4f)==area->transitions+1);
}

int main(int argc,char **argv)
{
    uint8_t p[PACK+1],changed[PACK+1];C3Area area={0};char error[128];
    fixture(p);CHECK(c3_area_parse(&area,p,PACK,error,sizeof(error)));CHECK(!error[0]);
    CHECK(area.floor==1&&area.group==29&&area.actor_count==2&&area.transition_count==2&&area.interaction_count==2&&area.link_count==3);
    CHECK(area.load_script==0x1000010&&area.init_script==0x1000011&&area.pre_script==0x1000012&&area.main_script==0x1000013&&area.post_script==0x1000014);
    CHECK(area.actors[0].index==2&&area.actors[0].flags==0x87654321&&area.actors[0].aux2==0x10203040&&area.actors[0].aux3==0x50607080);
    CHECK(!area.actors[0].file[0]&&!strcmp(area.actors[1].file,"actor_1.c3t"));
    CHECK(area.actors[0].motions[8]==9&&area.actors[0].position[2]==4&&area.actors[0].yaw==.5f);
    CHECK(area.actors[0].name_index==500&&area.actors[0].hear_flag==2000&&area.actors[0].name_message==0x54321&&area.actors[0].talk_start==2&&area.actors[0].talk_end==1);
    CHECK(area.actors[1].name_index==501&&area.actors[1].talk_start==3&&area.actors[1].talk_end==2);
    CHECK(area.actors[0].collision_type==1&&area.actors[1].collision_type==2);
    CHECK(c3_area_link(&area,1,7)==area.links&&c3_area_link(&area,3,7)==area.links+1&&c3_area_link(&area,3,42)==area.links+2);
    CHECK(area.links[2].args[3]==203&&area.links[2].source_index==52);
    CHECK(!c3_area_link(&area,2,7)&&!c3_area_link(NULL,1,7));
    for(size_t i=0;i<PACK;++i)rejected(&area,p,i);
    rejected(&area,p,PACK+1);rejected(&area,NULL,PACK);rejected(&area,p,256u*1024u+1u);
    CHECK(!c3_area_parse(NULL,p,PACK,error,sizeof(error)));
    static const unsigned header_offsets[]={0,4,8,12,16,32,36,40,44,48,60,64};
    for(unsigned i=0;i<sizeof(header_offsets)/sizeof(*header_offsets);++i)mutation(&area,p,header_offsets[i],0);
    mutation(&area,p,12,65536);mutation(&area,p,16,65536);
    mutation(&area,p,20,33);mutation(&area,p,24,1025);mutation(&area,p,28,1025);mutation(&area,p,56,129);mutation(&area,p,52,1);
    for(unsigned i=104;i<HEADER;++i){memcpy(changed,p,sizeof(p));changed[i]=1;rejected(&area,changed,PACK);}
    mutation(&area,p,HEADER,256);mutation(&area,p,HEADER+ACTOR,2);
    mutation(&area,p,HEADER+4,0);mutation(&area,p,HEADER+8,150);
    static const unsigned actor_offsets[]={32,36,40,44,48,52,56};
    for(unsigned i=0;i<sizeof(actor_offsets)/sizeof(*actor_offsets);++i){
        mutation(&area,p,HEADER+actor_offsets[i],0x7fc00000);mutation(&area,p,HEADER+actor_offsets[i],0x7f800000);
    }
    mutation(&area,p,HEADER+48,0);mutation(&area,p,HEADER+52,0xbf800000);mutation(&area,p,HEADER+56,0xbf800000);
    mutation(&area,p,HEADER+136,65536);mutation(&area,p,HEADER+140,65536);mutation(&area,p,HEADER+144,0x100000);
    mutation(&area,p,HEADER+148,4);mutation(&area,p,HEADER+152,4);mutation(&area,p,HEADER+156,65);
    memcpy(changed,p,sizeof(p));changed[HEADER+69]=4;rejected(&area,changed,PACK);
    for(unsigned i=70;i<72;++i){memcpy(changed,p,sizeof(p));changed[HEADER+i]=1;rejected(&area,changed,PACK);}
    static const char *bad_names[]={"../a.c3t",".hidden.c3t","a/b.c3t","a\\b.c3t","a..b.c3t","x.c3t/","x.C3T","a b.c3t",".c3t"};
    for(unsigned i=0;i<sizeof(bad_names)/sizeof(*bad_names);++i){
        memcpy(changed,p,sizeof(p));memset(changed+HEADER+72,0,64);memcpy(changed+HEADER+72,bad_names[i],strlen(bad_names[i]));rejected(&area,changed,PACK);
    }
    memcpy(changed,p,sizeof(p));memset(changed+HEADER+72,'a',64);rejected(&area,changed,PACK);
    for(unsigned i=0;i<12;++i)mutation(&area,p,TRANS+4*i,0x7fc00000);
    mutation(&area,p,TRANS+36,0);mutation(&area,p,TRANS+36,0x40000000);
    memcpy(changed,p,sizeof(p));w16(changed+TRANS+52,256);rejected(&area,changed,PACK);
    memcpy(changed,p,sizeof(p));w16(changed+INTER+54,1);rejected(&area,changed,PACK);
    mutation(&area,p,LINKS,0);mutation(&area,p,LINKS,5);mutation(&area,p,LINKS+8,0);
    mutation(&area,p,LINKS+LINK,1); /* Same source ID is valid only across different kinds. */
    C3Area prior=area;CHECK(!c3_area_load(&area,"/no-such-c3-area-pack",error,sizeof(error)));CHECK(!memcmp(&area,&prior,sizeof(area)));
    CHECK(!c3_area_load(&area,NULL,error,sizeof(error)));CHECK(!memcmp(&area,&prior,sizeof(area)));
    /* Maximum allowed basename length cannot overlap the name record at 136. */
    memcpy(changed,p,sizeof(p));memset(changed+HEADER+72,'a',59);memcpy(changed+HEADER+131,".c3t",5);
    CHECK(c3_area_parse(&area,changed,PACK,NULL,0));CHECK(strlen(area.actors[0].file)==63&&area.actors[0].name_index==500);
    contacts(&area);
    for(unsigned i=1;i<=64;i+=63){memcpy(changed,p,sizeof(p));w32(changed+HEADER+156,i);
        CHECK(c3_area_parse(&area,changed,PACK,NULL,0));CHECK(area.actors[0].part1_bone_plus_one==i);}
    /* Replacement frees the old ownership only after complete validation. */
    CHECK(c3_area_parse(&area,p,PACK,error,sizeof(error)));CHECK(area.transitions[0].source_id==7&&area.transitions[0].vertices[1]==-3);
    for(int i=1;i<argc;++i){CHECK(c3_area_load(&area,argv[i],error,sizeof(error)));CHECK(area.actor_count&&area.transition_count&&area.group);}
    c3_area_close(&area);C3Area empty={0};CHECK(!memcmp(&area,&empty,sizeof(area)));c3_area_close(&area);c3_area_close(NULL);
    printf("area: %u checks passed%s\n",checks,argc>1?" (including private prepared packs)":"");return 0;
}
