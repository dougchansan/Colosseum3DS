#include "colosseum3ds/wzx_confusion.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"type12 check %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static C3ConfusionType12Profile profile(unsigned phase)
{
    C3ConfusionType12Profile p={phase,phase?0:44,phase?100:90,phase?44:0,phase?402:145,phase?53:62,phase?.3f:.15f,phase?1:0,.25f};return p;
}
static void exercise(const C3ConfusionType12Profile *p)
{
    C3ConfusionType12 a,b,old;char error[192];float center[3]={1,2,3};unsigned owner=p->phase?2:1,target=p->phase?1:2;
    CHECK(c3_confusion_type12_begin(&a,p,owner,target,p->part,center,error,sizeof error));
    center[1]=99;CHECK(a.center[1]==2); /* One-shot attachment is not a following-bone alias. */
    old=a;CHECK(!c3_confusion_type12_begin(&a,p,owner,target,p->part+1,center,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));
    CHECK(!c3_confusion_type12_begin(&a,p,owner,owner,p->part,center,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));
    c3_confusion_type12_pause(&a,1);old=a;CHECK(c3_confusion_type12_step(&a,UINT32_MAX,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));c3_confusion_type12_pause(&a,0);
    old=a;CHECK(!c3_confusion_type12_step(&a,10001,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));
    for(unsigned partition=1;partition<=101;++partition){
        center[1]=2;CHECK(c3_confusion_type12_begin(&a,p,owner,target,p->part,center,error,sizeof error));CHECK(c3_confusion_type12_begin(&b,p,owner,target,p->part,center,error,sizeof error));
        unsigned elapsed=0;while(elapsed<2000){unsigned ms=partition;if(ms>2000-elapsed)ms=2000-elapsed;elapsed+=ms;
            CHECK(c3_confusion_type12_step(&a,ms,error,sizeof error));CHECK(c3_confusion_type12_begin(&b,p,owner,target,p->part,center,error,sizeof error));CHECK(c3_confusion_type12_step(&b,elapsed,error,sizeof error));CHECK(!memcmp(&a,&b,sizeof a));}
    }
    c3_confusion_type12_cancel(&a);CHECK(!a.active&&a.radius==0&&a.indirect==0);
}
static void golden(void)
{
    C3ConfusionType12 s;C3ConfusionType12Profile p=profile(1);char e[192];float center[3]={1,2,3},r[3]={1,0,0},u[3]={0,1,0};const float projected[2][2]={{.2f,.8f},{.7f,.3f}};C3ConfusionType12Frame f;
    CHECK(c3_confusion_type12_begin(&s,&p,2,1,53,center,e,sizeof e));CHECK(c3_confusion_type12_step(&s,850,e,sizeof e));CHECK(s.frame==51);
    CHECK(fabsf(s.radius-1.125f)<1e-6f&&fabsf(s.indirect-.125f)<1e-6f);
    CHECK(c3_confusion_type12_frame(&s,r,u,projected,400,240,&f,e,sizeof e));CHECK(fabsf(f.world[0][0]+.125f)<1e-6f&&fabsf(f.world[2][1]-3.125f)<1e-6f);
    CHECK(fabsf(f.captured_uv[0][0]-.5f)<1e-6f&&fabsf(f.captured_uv[1][1]+.5f)<1e-6f);
    CHECK(fabsf(f.captured_uv[0][2]-.24f)<1e-6f&&fabsf(f.captured_uv[1][2]-.8666667f)<1e-6f);
    CHECK(f.indirect_matrix[0][0]==.5f&&f.indirect_matrix[1][1]==.5f&&f.indirect_matrix[0][2]==-.4375f&&f.indirect_exponent==1);
    CHECK(f.capture_model_flags==0x3010&&!f.capture_include_particles&&f.required_capabilities==3);
    C3ConfusionType12Frame old=f;float bad[3]={2,0,0};CHECK(!c3_confusion_type12_frame(&s,bad,u,projected,400,240,&f,e,sizeof e));CHECK(!memcmp(&f,&old,sizeof f));
    CHECK(!c3_confusion_type12_require(0,e,sizeof e)&&strstr(e,"capture"));CHECK(!c3_confusion_type12_require(C3_CONFUSION_CAPTURE,e,sizeof e)&&strstr(e,"dependent"));CHECK(c3_confusion_type12_require(3,e,sizeof e));
    p=profile(0);CHECK(c3_confusion_type12_begin(&s,&p,1,2,62,center,e,sizeof e));CHECK(c3_confusion_type12_step(&s,17,e,sizeof e));CHECK(s.frame==1&&fabsf(s.radius-1.0125f)<1e-6f&&s.indirect==.25f);
    CHECK(c3_confusion_type12_step(&s,733,e,sizeof e));CHECK(s.frame==45&&s.radius<1.0125f);CHECK(c3_confusion_type12_step(&s,750,e,sizeof e));CHECK(!s.active&&s.frame==90);
    CHECK(!c3_confusion_type12_frame(&s,r,u,projected,400,240,&f,e,sizeof e));
}
int main(int argc,char **argv)
{
    C3ConfusionType12Profile a=profile(0),d=profile(1);exercise(&a);exercise(&d);golden();
    if(argc>1){C3WzxConfusionProgram *p=calloc(1,sizeof *p);char e[192];CHECK(p&&c3_wzx_confusion_load(p,argv[1],e,sizeof e));exercise(&p->phase[0]);exercise(&p->phase[1]);
        int differs=0;for(unsigned i=1;i<C3_CONFUSION_MASK_BYTES;++i)if(p->intensity[i]!=p->intensity[0])differs=1;CHECK(differs);
        FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL);size_t n=256+C3_CONFUSION_MASK_BYTES;uint8_t *b=malloc(n),*m=malloc(n);CHECK(b&&m&&fread(b,1,n,f)==n);fclose(f);
        for(unsigned i=0;i<256;++i){memcpy(m,b,n);m[i]^=1;CHECK(!c3_wzx_confusion_decode(p,m,n,e,sizeof e));}
        memcpy(m,b,n);m[256]^=1;CHECK(!c3_wzx_confusion_decode(p,m,n,e,sizeof e));free(b);free(m);free(p);
    }
    printf("Confusion type12 CPU checks: %u; scene capture/indirect renderer REQUIRED\n",checks);return 0;
}
