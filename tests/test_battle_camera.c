#include "colosseum3ds/battle_camera.h"
#include "save_codec.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"camera check %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static C3BattleCameraProgram synthetic(void)
{
    static float tracks[12][3];for(unsigned i=0;i<12;++i){tracks[i][0]=tracks[i][2]=0;tracks[i][1]=(float)(i&1u);}
    C3BattleCameraProgram p={0};p.samples=tracks;p.sample_count=12;p.flags[0]=0x201048;p.flags[1]=0x200848;p.pattern[0]=6;p.pattern[1]=5;p.fov_mask[0]=4;p.fov_mask[1]=2;p.entry[0]=2;p.entry[1]=8;p.owner_start[1]=22;
    for(unsigned i=0;i<3;++i){C3BattleCameraBinding *a=&p.actors[i];a->species=(uint32_t[]){197,196,288}[i];a->scale_selector=-1;a->minimum[0]=a->minimum[2]=-3;a->maximum[0]=a->maximum[2]=3;a->maximum[1]=10;
        a->timing[0][0]=72;a->timing[0][1]=100;a->timing[0][2]=160;a->timing[1][0]=190;a->timing[1][1]=358;a->animation_kind[0]=4;a->animation_kind[1]=3;a->part[0]=a->part[1]=(uint32_t[]){42,62,53}[i];
        for(unsigned phase=0;phase<2;++phase){a->track_offset[phase]=i*4+phase*2;a->track_count[phase]=2;}}
    return p;
}
static C3BattleCameraActor actors[4]={
    {0,197,1,{-3.5f,0,-2.625f},1.5707963268f},
    {1,196,1,{-3.5f,0,2.625f},1.5707963268f},
    {2,288,-1,{3.5f,0,2.625f},-1.5707963268f},
    {3,288,-1,{3.5f,0,-2.625f},-1.5707963268f}
};
static const C3TitleCamera base={{0,11,9},{0,0,0},0,30,.01f,3276.8f,1.18f};
static void exercise(const C3BattleCameraProgram *p)
{
    char error[192];C3BattleCamera a,b,old;c3_battle_camera_init(&a,12345);
    CHECK(c3_battle_camera_begin(&a,p,C3_BATTLE_CAMERA_ATTACK,0,2,actors,&base,error,sizeof error));
    CHECK(a.duration==p->actors[0].timing[0][1]&&a.cuts==1&&a.active);
    CHECK(a.motion==2||a.motion==3);CHECK(a.distance>=4.375f&&a.distance<=5.25f);CHECK(a.height>=.6f&&a.height<=2);
    CHECK(a.camera.near_plane==base.near_plane&&a.camera.far_plane==base.far_plane&&a.camera.aspect==base.aspect);
    CHECK(a.fov>=15&&a.fov<=85);CHECK(isfinite(a.camera.target[0]));
    old=a;CHECK(!c3_battle_camera_begin(&a,p,C3_BATTLE_CAMERA_ATTACK,4,2,actors,&base,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));
    CHECK(!c3_battle_camera_begin(&a,p,(C3BattleCameraPhase)-1,0,2,actors,&base,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));
    CHECK(!c3_battle_camera_begin(&a,p,C3_BATTLE_CAMERA_ATTACK,0,0,actors,&base,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));
    unsigned previous=a.motion;CHECK(c3_battle_camera_begin(&a,p,C3_BATTLE_CAMERA_DAMAGE,2,0,actors,&base,error,sizeof error));CHECK(a.motion!=previous&&a.duration==p->actors[2].timing[1][0]&&a.cuts==2);
    c3_battle_camera_pause(&a,1);old=a;CHECK(c3_battle_camera_step(&a,UINT32_MAX,actors,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));c3_battle_camera_pause(&a,0);
    old=a;CHECK(!c3_battle_camera_step(&a,60001,actors,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));
    c3_battle_camera_init(&b,12345);CHECK(c3_battle_camera_begin(&b,p,C3_BATTLE_CAMERA_DAMAGE,3,0,actors,&base,error,sizeof error));
    CHECK(b.owner==3&&isfinite(b.camera.target[2]));CHECK(a.owner==2&&isfinite(a.camera.target[2]));
    C3BattleCameraActor invalid[4];memcpy(invalid,actors,sizeof actors);invalid[3].slot=2;old=a;CHECK(!c3_battle_camera_step(&a,1,invalid,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));

    for(unsigned seed=1;seed<=64;++seed){
        for(unsigned phase=0;phase<2;++phase){
            c3_battle_camera_init(&a,seed);c3_battle_camera_init(&b,seed);
            CHECK(c3_battle_camera_begin(&a,p,(C3BattleCameraPhase)phase,phase?2:0,phase?0:2,actors,&base,error,sizeof error));
            CHECK(c3_battle_camera_begin(&b,p,(C3BattleCameraPhase)phase,phase?2:0,phase?0:2,actors,&base,error,sizeof error));
            CHECK(!memcmp(&a,&b,sizeof a));
            C3BattleCameraActor tracked[4];memcpy(tracked,actors,sizeof actors);
            for(unsigned i=0;i<3000;++i)CHECK(c3_battle_camera_step(&a,1,tracked,error,sizeof error));
            CHECK(c3_battle_camera_step(&b,3000,tracked,error,sizeof error));CHECK(!memcmp(&a,&b,sizeof a));CHECK(a.ticks==180&&a.units==180000);
            if(!p->owned)CHECK(a.camera.target[1]>.99f&&a.camera.target[1]<=1);
            c3_battle_camera_cancel(&a);CHECK(!a.active&&!memcmp(&a.camera,&base,sizeof base));
        }
    }
}
static void source_golden(void)
{
    /* Independent scalar evaluation of the original CalculateParams,
     * DoPosition case3 and short-table5/6 FOV expressions. Cosmetic seed1
     * supplies r=[.0000629425,.015747428,.616404057,.071618617,.558488309]. */
    C3BattleCameraProgram p=synthetic();C3BattleCamera c;char e[192];
    c3_battle_camera_init(&c,1);CHECK(c3_battle_camera_begin(&c,&p,C3_BATTLE_CAMERA_ATTACK,0,2,actors,&base,e,sizeof e));
    CHECK(c.motion==3&&c.random_draws==5&&c.duration==100);
    CHECK(fabsf(c.distance-4.388779f)<1e-5f&&fabsf(c.height-.8465616f)<1e-5f);
    CHECK(fabsf(c.camera.eye[0]-.6424088f)<1e-5f&&fabsf(c.camera.eye[2]+4.074769f)<1e-5f);
    CHECK(fabsf(c.fov-48.283696f)<1e-4f);
    c3_battle_camera_init(&c,1);CHECK(c3_battle_camera_begin(&c,&p,C3_BATTLE_CAMERA_DAMAGE,2,0,actors,&base,e,sizeof e));
    CHECK(c.motion==3&&c.random_draws==5&&c.duration==190);
    CHECK(fabsf(c.distance-3.0831685f)<1e-5f&&fabsf(c.camera.eye[0]-.5899095f)<1e-5f&&fabsf(c.camera.eye[2]-1.6065202f)<1e-5f);
    CHECK(fabsf(c.fov-43.777706f)<1e-4f);
    CHECK(c.track_delay==22);CHECK(c3_battle_camera_step(&c,366,actors,e,sizeof e));CHECK(c.ticks==21&&c.view[1]==0);
    CHECK(c3_battle_camera_step(&c,1,actors,e,sizeof e));CHECK(c.ticks==22&&c.view[1]==0);
    CHECK(c3_battle_camera_step(&c,17,actors,e,sizeof e));CHECK(c.ticks==23&&fabsf(c.view[1]-1/12.f)<1e-7f);

}
static void store(uint8_t *b,uint32_t v){b[0]=(uint8_t)v;b[1]=(uint8_t)(v>>8);b[2]=(uint8_t)(v>>16);b[3]=(uint8_t)(v>>24);}
static void confusion_synthetic(void)
{
    C3BattleCameraProgram p=synthetic();p.move_id=93;p.flags[0]=0x200848;p.flags[1]=0x48;p.pattern[0]=5;p.pattern[1]=UINT32_MAX;p.fov_mask[0]=2;p.fov_mask[1]=0;p.entry[0]=1;p.owner_start[1]=44;
    float one=1,threshold=.7f;memcpy(&p.patterns[0][0],&one,4);p.patterns[0][1]=3;p.patterns[0][2]=5;memcpy(&p.patterns[0][3],&threshold,4);p.patterns[0][7]=4;p.patterns[0][8]=2;p.patterns[0][9]=1;
    C3BattleCamera c,before;char error[192];c3_battle_camera_init(&c,1);
    CHECK(c3_battle_camera_begin_move(&c,&p,93,C3_BATTLE_CAMERA_DAMAGE,2,1,actors,&base,error,sizeof error));
    CHECK(c.dynamic_fov&&c.random_draws==8&&c.fov_pattern==0&&c.fov_begin[0]==0&&c.fov_finish[0]==12);
    float start=c.fov_start[0],end=c.fov_end[0];CHECK(c3_battle_camera_step(&c,100,actors,error,sizeof error));CHECK(fabsf(c.fov-(start+end)*.5f)<1e-5f);
    before=c;p.patterns[0][2]=99;CHECK(!c3_battle_camera_begin_move(&c,&p,93,C3_BATTLE_CAMERA_DAMAGE,2,1,actors,&base,error,sizeof error));CHECK(!memcmp(&c,&before,sizeof c));
    CHECK(!c3_battle_camera_begin_move(&c,&p,94,C3_BATTLE_CAMERA_DAMAGE,2,1,actors,&base,error,sizeof error));CHECK(!memcmp(&c,&before,sizeof c));
}
static void confusion_exercise(const char *path)
{
    char error[192];C3BattleCameraProgram p={0};C3BattleCamera a,b,old;
    CHECK(c3_battle_camera_confusion_load(&p,path,error,sizeof error));CHECK(p.move_id==93);
    CHECK(p.entry[0]==1&&p.entry[1]==8&&p.owner_start[1]==44);
    unsigned patterns=0;
    for(unsigned seed=1;seed<=512;++seed)for(unsigned phase=0;phase<2;++phase){
        c3_battle_camera_init(&a,seed);c3_battle_camera_init(&b,seed);
        CHECK(c3_battle_camera_begin_move(&a,&p,93,(C3BattleCameraPhase)phase,phase?2:1,phase?1:2,actors,&base,error,sizeof error));
        CHECK(c3_battle_camera_begin_move(&b,&p,93,(C3BattleCameraPhase)phase,phase?2:1,phase?1:2,actors,&base,error,sizeof error));
        CHECK(a.duration==p.actors[phase?2:1].timing[phase][phase?0:1]);
        CHECK(a.distance>=(phase?2:3.0625f)&&a.distance<=(phase?5.25f:4.375f));
        CHECK(a.track_delay==(phase?44u:0u));CHECK(a.dynamic_fov==(phase?1:0));
        if(phase){CHECK(a.fov_pattern<13);patterns|=1u<<a.fov_pattern;
            CHECK(a.fov_begin[0]<=a.fov_finish[0]&&a.fov_begin[1]<=a.fov_finish[1]);}
        for(unsigned i=0;i<500;++i)CHECK(c3_battle_camera_step(&a,17,actors,error,sizeof error));
        CHECK(c3_battle_camera_step(&b,8500,actors,error,sizeof error));CHECK(!memcmp(&a,&b,sizeof a));
        CHECK(a.fov>=15&&a.fov<=85);old=a;
        CHECK(!c3_battle_camera_begin(&a,&p,C3_BATTLE_CAMERA_ATTACK,1,2,actors,&base,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));
        CHECK(!c3_battle_camera_begin_move(&a,&p,44,C3_BATTLE_CAMERA_ATTACK,1,2,actors,&base,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));
        c3_battle_camera_cancel(&a);CHECK(!memcmp(&a.camera,&base,sizeof base));
    }
    CHECK((patterns&0x3ffu)==0x3ffu);
    /* Source row5: mode3, duration mode5, threshold.7, initialmask4,
     * targetmask2, start-anchored. Seed1 draws select this exact row. */
    c3_battle_camera_init(&a,1);CHECK(c3_battle_camera_begin_move(&a,&p,93,C3_BATTLE_CAMERA_DAMAGE,2,1,actors,&base,error,sizeof error));
    CHECK(a.motion==3&&a.fov_pattern==5&&a.random_draws==8);
    CHECK(a.fov_begin[0]==0&&a.fov_finish[0]==12&&a.fov_begin[1]==190&&a.fov_finish[1]==358);
    float initial=a.fov_start[0],target=a.fov_end[0];CHECK(initial>target);
    CHECK(c3_battle_camera_step(&a,100,actors,error,sizeof error));CHECK(a.ticks==6&&fabsf(a.fov-(initial+target)*.5f)<1e-5f);
    c3_battle_camera_pause(&a,1);old=a;CHECK(c3_battle_camera_step(&a,UINT32_MAX,actors,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);CHECK(size>1500);rewind(f);
    uint8_t *raw=malloc((size_t)size);CHECK(raw);CHECK(fread(raw,1,(size_t)size,f)==(size_t)size);fclose(f);
    C3BattleCameraProgram previous=p;
    CHECK(!c3_battle_camera_decode(&p,raw,(size_t)size,error,sizeof error));CHECK(!memcmp(&p,&previous,sizeof p));
    raw[512]^=1;memset(raw+480,0,20);uint8_t digest[20];c3_save_codec_sha1(digest,raw,(size_t)size);memcpy(raw+480,digest,20);
    CHECK(!c3_battle_camera_confusion_decode(&p,raw,(size_t)size,error,sizeof error));CHECK(!memcmp(&p,&previous,sizeof p));free(raw);
    c3_battle_camera_close(&p);
}
static uint32_t tail_word(uint32_t *s){uint32_t x=*s;x^=x<<13;x^=x>>17;x^=x<<5;return *s=x;}
static float tail_unit(uint32_t *s){return (float)(tail_word(s)>>8)*(1.f/16777216.f);}
static C3BattleCameraProgram tail_synthetic(void)
{
    static float tracks[6][3]={{.2f,.3f,.1f},{.1f,.4f,.2f},{.2f,.3f,.1f},{.1f,.4f,.2f},{.2f,.3f,.1f},{.1f,.4f,.2f}};
    C3BattleCameraProgram p=synthetic();p.move_id=39;p.samples=tracks;p.sample_count=6;p.flags[0]=0x78;p.pattern[0]=UINT32_MAX;p.fov_mask[0]=p.entry[0]=p.owner_start[0]=0;p.flags[1]=p.pattern[1]=p.fov_mask[1]=p.entry[1]=p.owner_start[1]=0;
    for(unsigned i=0;i<3;++i){C3BattleCameraBinding *a=&p.actors[i];memset(a->timing,0,sizeof a->timing);a->timing[0][0]=98;a->animation_kind[0]=2;a->animation_kind[1]=a->part[1]=a->track_count[1]=0;a->track_offset[0]=i*2;a->track_count[0]=2;a->track_offset[1]=i*2+2;}
    for(unsigned i=0;i<8;++i){float weight=(float[]){.2f,.25f,.2f,.1f,.05f,.05f,.05f,.1f}[i];memcpy(&p.patterns[i][0],&weight,4);p.patterns[i][7]=(unsigned[]){6,4,1,1,1,2,4,1}[i];}
    return p;
}
static void tail_exercise(const C3BattleCameraProgram *p)
{
    unsigned motions=0,patterns=0,shortened=0;char error[192];C3BattleCamera a,b,old;
    for(uint32_t seed=1;seed<=20000;seed+=13){c3_battle_camera_init(&a,seed);CHECK(c3_battle_camera_begin_move(&a,p,39,C3_BATTLE_CAMERA_ATTACK,2,0,actors,&base,error,sizeof error));
        CHECK(a.motion<4&&a.duration==p->actors[2].timing[0][0]&&!a.dynamic_fov&&a.track_delay==0);motions|=1u<<a.motion;CHECK(a.fov>=15&&a.fov<=85);CHECK(a.camera.near_plane==base.near_plane&&a.camera.far_plane==base.far_plane&&a.camera.aspect==base.aspect);
        if(a.motion==0){CHECK(a.fov_pattern==UINT32_MAX&&a.motion_duration>=a.duration/2&&a.motion_duration<=a.duration);shortened+=a.motion_duration<a.duration;}
        else{CHECK(a.fov_pattern<8);patterns|=1u<<a.fov_pattern;}
        b=a;CHECK(c3_battle_camera_step(&a,1700,actors,error,sizeof error));for(unsigned i=0;i<100;++i)CHECK(c3_battle_camera_step(&b,17,actors,error,sizeof error));CHECK(!memcmp(&a,&b,sizeof a));
        c3_battle_camera_pause(&a,1);old=a;CHECK(c3_battle_camera_step(&a,UINT32_MAX,actors,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));c3_battle_camera_cancel(&a);CHECK(!memcmp(&a.camera,&base,sizeof base));
    }
    CHECK(motions==15&&patterns==255&&shortened);
    for(unsigned bin=0;bin<4;++bin){uint32_t seed=1;float draw;for(;;++seed){uint32_t rng=seed;draw=tail_unit(&rng);if(draw>bin*.25f&&draw<(bin+1)*.25f)break;}
        c3_battle_camera_init(&a,seed);a.previous_motion=(uint32_t[]){3,0,1,2}[bin];CHECK(c3_battle_camera_begin_move(&a,p,39,C3_BATTLE_CAMERA_ATTACK,2,0,actors,&base,error,sizeof error));CHECK((a.motion!=(uint32_t[]){3,0,1,2}[bin]));if(bin<3)CHECK((a.motion==(uint32_t[]){0,1,2}[bin]));
    }
    uint32_t seed=1;for(;;++seed){uint32_t rng=seed;float draw=tail_unit(&rng);if(draw>.5f&&draw<.75f)break;}
    c3_battle_camera_init(&a,seed);CHECK(c3_battle_camera_begin_move(&a,p,39,C3_BATTLE_CAMERA_ATTACK,2,0,actors,&base,error,sizeof error));CHECK(a.motion==1);uint32_t rng=seed;(void)tail_unit(&rng);float offset=25+10*tail_unit(&rng),height=1+9*tail_unit(&rng),distance=20+30*tail_unit(&rng);
    CHECK(fabsf(a.camera.eye[0]-actors[2].position[0]-1)<1e-6f&&fabsf(a.camera.eye[1]-height*.1f)<1e-6f&&fabsf(a.camera.eye[2]-actors[2].position[2]-distance*.1f)<1e-6f);
    CHECK(fabsf(a.eye_to[0]-(10+offset)*.1f)<1e-6f);
    C3BattleCameraActor changed[4];memcpy(changed,actors,sizeof changed);changed[2].yaw=1.1f;c3_battle_camera_init(&b,seed);CHECK(c3_battle_camera_begin_move(&b,p,39,C3_BATTLE_CAMERA_ATTACK,2,0,changed,&base,error,sizeof error));CHECK(!memcmp(a.camera.eye,b.camera.eye,sizeof a.camera.eye));CHECK(memcmp(a.camera.target,b.camera.target,sizeof a.camera.target));
    /* Independent source call-order oracle for a real Dolly seed. Integer
     * helper consumes two16bit draws, and randomDelay shortens motion now. */
    uint32_t dolly_seed=1;for(;;++dolly_seed){uint32_t state=dolly_seed;float r=tail_unit(&state);if(r>.25f&&r<.5f)break;}
    c3_battle_camera_init(&b,dolly_seed);CHECK(c3_battle_camera_begin_move(&b,p,39,C3_BATTLE_CAMERA_ATTACK,2,0,actors,&base,error,sizeof error));CHECK(b.motion==0);rng=dolly_seed;(void)tail_unit(&rng);int alternate=tail_unit(&rng)<=.7f;float chance=tail_unit(&rng),threshold0=alternate?.3f:.5f,threshold1=alternate?.65f:.75f;uint32_t effective=p->actors[2].timing[0][0];unsigned expected_draws=8;
    if(chance>=threshold0){uint32_t first=tail_word(&rng)&65535,second=tail_word(&rng)&65535,draw=((second<<16)|first)%effective;expected_draws+=2;if(chance<threshold1)effective=draw<effective/2?effective/2:draw;else effective-=draw>effective/2?effective/2:draw;}
    float near=20+32.5f*tail_unit(&rng),far=20+32.5f*tail_unit(&rng);if((alternate&&near<far)||(!alternate&&near>far)){float swap=near;near=far;far=swap;}
    float lo=p->actors[2].minimum[1],hi=p->actors[2].maximum[1];if(lo<6){lo=6;if(hi<6)hi=9;}if(hi>20){hi=20;if(lo>20)lo=16;}height=lo+(hi-lo)*tail_unit(&rng);float r=tail_unit(&rng),rotation=actors[2].yaw-.31415927f-(1.0995574f-.31415927f)*r;float mix=tail_unit(&rng);
    CHECK(b.motion_duration==effective&&b.random_draws==expected_draws&&b.rng==rng);CHECK(fabsf(b.eye_from[0]-sinf(rotation)*near*.1f)<1e-6f&&fabsf(b.eye_to[2]-cosf(rotation)*far*.1f)<1e-6f);
    float length=0;for(unsigned k=0;k<3;++k){float d=p->actors[2].maximum[k]-p->actors[2].minimum[k];length+=d*d;}float size=.57735026f*sqrtf(length)*1.1f,span=fmaxf(.75f*size,(p->actors[2].maximum[1]-p->actors[2].minimum[1])*1.1f),radius=sqrtf(near*near+height*height);float lower=fminf(85,fmaxf(15,57.29578f*2*atan2f(.5f*span,radius))),upper=fminf(85,fmaxf(15,57.29578f*2*atan2f(2*span,radius)));CHECK(fabsf(b.fov-(lower+(upper-lower)*(.35f+.25f*mix)))<1e-5f);
    old=a;C3BattleCameraProgram invalid=*p;invalid.flags[0]^=1;CHECK(!c3_battle_camera_begin_move(&a,&invalid,39,C3_BATTLE_CAMERA_ATTACK,2,0,actors,&base,error,sizeof error));CHECK(!memcmp(&old,&a,sizeof a));CHECK(!c3_battle_camera_begin_move(&a,p,39,C3_BATTLE_CAMERA_DAMAGE,2,0,actors,&base,error,sizeof error));CHECK(!memcmp(&old,&a,sizeof a));
}
static void tail_actual(const char *path)
{
    C3BattleCameraProgram p={0};char error[192];CHECK(c3_battle_camera_tailwhip_load(&p,path,error,sizeof error));tail_exercise(&p);
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);rewind(f);uint8_t *raw=malloc((size_t)size);CHECK(raw&&fread(raw,1,(size_t)size,f)==(size_t)size);fclose(f);C3BattleCameraProgram previous=p;
    CHECK(!c3_battle_camera_decode(&p,raw,(size_t)size,error,sizeof error));CHECK(!memcmp(&p,&previous,sizeof p));CHECK(!c3_battle_camera_confusion_decode(&p,raw,(size_t)size,error,sizeof error));CHECK(!memcmp(&p,&previous,sizeof p));
    raw[64]^=1;memset(raw+480,0,20);uint8_t hash[20];c3_save_codec_sha1(hash,raw,(uint32_t)size);memcpy(raw+480,hash,20);CHECK(!c3_battle_camera_tailwhip_decode(&p,raw,(size_t)size,error,sizeof error));CHECK(!memcmp(&p,&previous,sizeof p));free(raw);c3_battle_camera_close(&p);
}
static C3BattleCameraProgram growl_synthetic(void)
{
    C3BattleCameraProgram p=tail_synthetic();p.move_id=45;p.flags[0]=0x200858;p.pattern[0]=5;p.fov_mask[0]=2;p.entry[0]=1;
    for(unsigned i=0;i<3;++i){p.actors[i].animation_kind[0]=4;p.actors[i].timing[0][0]=130;p.actors[i].timing[0][1]=244;p.actors[i].timing[0][2]=366;}
    return p;
}
static void growl_exercise(const C3BattleCameraProgram *p)
{
    C3BattleCamera a,b,old;char error[192];unsigned modes=0,shortened=0;
    for(unsigned seed=1;seed<20000;seed+=17){c3_battle_camera_init(&a,seed);CHECK(c3_battle_camera_begin_move(&a,p,45,C3_BATTLE_CAMERA_ATTACK,2,0,actors,&base,error,sizeof error));CHECK(a.move_id==45&&a.motion!=1&&a.duration==244&&!a.dynamic_fov&&a.track_delay==0);modes|=1u<<a.motion;
        CHECK(a.distance>=3.0625f&&a.distance<=4.375f&&a.height>=.6f&&a.height<=1.1f);CHECK(a.fov>=15&&a.fov<=85);CHECK(a.camera.near_plane==base.near_plane&&a.camera.far_plane==base.far_plane&&a.camera.aspect==base.aspect);
        if(a.motion==0){CHECK(a.fov_pattern==UINT32_MAX&&a.motion_duration>=122&&a.motion_duration<=244);shortened+=a.motion_duration<244;CHECK(a.random_draws==8||a.random_draws==10);}
        else{CHECK(a.fov_pattern==5);CHECK(a.random_draws==(a.motion==3?6u:7u));}
        b=a;CHECK(c3_battle_camera_step(&a,5100,actors,error,sizeof error));for(unsigned i=0;i<300;++i)CHECK(c3_battle_camera_step(&b,17,actors,error,sizeof error));CHECK(!memcmp(&a,&b,sizeof a));
        c3_battle_camera_pause(&a,1);old=a;CHECK(c3_battle_camera_step(&a,UINT32_MAX,actors,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));c3_battle_camera_pause(&a,0);old=a;CHECK(!c3_battle_camera_step(&a,60001,actors,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));c3_battle_camera_cancel(&a);CHECK(!memcmp(&a.camera,&base,sizeof base));
    }
    CHECK(modes==13&&shortened);
    for(unsigned bin=0;bin<3;++bin){unsigned seed=1;for(;;++seed){uint32_t state=seed;float r=tail_unit(&state);if(r>(float)bin/3&&r<(float)(bin+1)/3)break;}
        c3_battle_camera_init(&a,seed);unsigned previous=(unsigned[]){3,0,2}[bin];a.previous_motion=previous;CHECK(c3_battle_camera_begin_move(&a,p,45,C3_BATTLE_CAMERA_ATTACK,2,0,actors,&base,error,sizeof error));CHECK(a.motion!=previous);if(bin<2)CHECK(a.motion==(bin==0?0u:2u));
    }
    /* Growl source parameter/RNG oracle uses the action duration244. */
    uint32_t dolly_seed=1;for(;;++dolly_seed){uint32_t state=dolly_seed;float r=tail_unit(&state);if(r>1.f/3&&r<2.f/3)break;}
    c3_battle_camera_init(&b,dolly_seed);CHECK(c3_battle_camera_begin_move(&b,p,45,C3_BATTLE_CAMERA_ATTACK,2,0,actors,&base,error,sizeof error));CHECK(b.motion==0);uint32_t rng=dolly_seed;(void)tail_unit(&rng);int alternate=tail_unit(&rng)<=.7f;float chance=tail_unit(&rng),threshold0=alternate?.3f:.5f,threshold1=alternate?.65f:.75f;uint32_t effective=p->actors[2].timing[0][1];unsigned expected_draws=8;
    if(chance>=threshold0){uint32_t first=tail_word(&rng)&65535,second=tail_word(&rng)&65535,draw=((second<<16)|first)%effective;expected_draws+=2;if(chance<threshold1)effective=draw<effective/2?effective/2:draw;else effective-=draw>effective/2?effective/2:draw;}
    float near=30.625f+13.125f*tail_unit(&rng),far=30.625f+13.125f*tail_unit(&rng);if((alternate&&near<far)||(!alternate&&near>far)){float swap=near;near=far;far=swap;}
    float lo=p->actors[2].minimum[1],hi=p->actors[2].maximum[1];if(lo<6){lo=6;if(hi<6)hi=9;}if(hi>11){hi=11;if(lo>11)lo=.8f*11;}float height=lo+(hi-lo)*tail_unit(&rng);float r=tail_unit(&rng),rotation=actors[2].yaw-.31415927f-(.62831855f-.31415927f)*r;float mix=tail_unit(&rng);
    CHECK(b.motion_duration==effective&&b.random_draws==expected_draws&&b.rng==rng);CHECK(fabsf(b.eye_from[0]-sinf(rotation)*near*.1f)<1e-6f&&fabsf(b.eye_to[2]-cosf(rotation)*far*.1f)<1e-6f);
    float length=0;for(unsigned k=0;k<3;++k){float d=p->actors[2].maximum[k]-p->actors[2].minimum[k];length+=d*d;}float size=.57735026f*sqrtf(length)*1.1f,span=fmaxf(.75f*size,(p->actors[2].maximum[1]-p->actors[2].minimum[1])*1.1f),radius=sqrtf(near*near+height*height);float lower=fminf(85,fmaxf(15,57.29578f*2*atan2f(.5f*span,radius))),upper=fminf(85,fmaxf(15,57.29578f*2*atan2f(2*span,radius)));CHECK(fabsf(b.fov-(lower+(upper-lower)*(.35f+.25f*mix)))<1e-5f);
    old=a;C3BattleCameraProgram bad=*p;bad.flags[0]|=0x20;CHECK(!c3_battle_camera_begin_move(&a,&bad,45,C3_BATTLE_CAMERA_ATTACK,2,0,actors,&base,error,sizeof error));CHECK(!memcmp(&old,&a,sizeof a));CHECK(!c3_battle_camera_begin_move(&a,p,45,C3_BATTLE_CAMERA_DAMAGE,2,0,actors,&base,error,sizeof error));CHECK(!memcmp(&old,&a,sizeof a));CHECK(!c3_battle_camera_begin_move(&a,p,39,C3_BATTLE_CAMERA_ATTACK,2,0,actors,&base,error,sizeof error));CHECK(!memcmp(&old,&a,sizeof a));
}
static void growl_actual(const char *path)
{
    C3BattleCameraProgram p={0};char error[192];CHECK(c3_battle_camera_growl_load(&p,path,error,sizeof error));CHECK(p.actors[2].timing[0][0]==130&&p.actors[2].timing[0][1]==244&&p.actors[2].timing[0][2]==366);growl_exercise(&p);
    FILE *f=fopen(path,"rb");CHECK(f&&!fseek(f,0,SEEK_END));long length=ftell(f);rewind(f);uint8_t *raw=malloc((size_t)length);CHECK(raw&&fread(raw,1,(size_t)length,f)==(size_t)length);fclose(f);C3BattleCameraProgram before=p;CHECK(!c3_battle_camera_tailwhip_decode(&p,raw,(size_t)length,error,sizeof error));CHECK(!memcmp(&before,&p,sizeof p));
    raw[64]^=0x20;memset(raw+480,0,20);uint8_t hash[20];c3_save_codec_sha1(hash,raw,(uint32_t)length);memcpy(raw+480,hash,20);CHECK(!c3_battle_camera_growl_decode(&p,raw,(size_t)length,error,sizeof error));CHECK(!memcmp(&before,&p,sizeof p));free(raw);c3_battle_camera_close(&p);
}
static C3BattleCameraProgram stat_synthetic(void)
{
    static float tracks[300][3];for(unsigned i=0;i<300;++i){tracks[i][0]=.2f;tracks[i][1]=.3f;tracks[i][2]=.1f;}
    C3BattleCameraProgram p=tail_synthetic();p.move_id=C3_BATTLE_CAMERA_GREEN_DOWN_PROFILE;p.flags[0]=0;p.samples=tracks;p.sample_count=300;memset(&p.actors[2],0,sizeof p.actors[2]);
    for(unsigned i=0;i<2;++i){p.actors[i].timing[0][0]=i?180:118;p.actors[i].track_offset[0]=i?119:0;p.actors[i].track_count[0]=i?181:119;p.actors[i].track_offset[1]=i?300:119;}return p;
}
static void stat_exercise(const C3BattleCameraProgram *p)
{
    char error[192];C3BattleCamera a,b,old;unsigned motions=0,patterns=0;uint32_t special=p->move_id==C3_BATTLE_CAMERA_RED_DOWN_PROFILE?0x27u:0x29u;
    for(unsigned affected=0;affected<2;++affected)for(unsigned seed=1;seed<20000;seed+=29){c3_battle_camera_init(&a,seed);CHECK(c3_battle_camera_begin_special(&a,p,special,affected,2,actors,&base,error,sizeof error));CHECK(a.move_id==p->move_id&&a.duration==(affected?180u:118u)&&a.track_delay==0&&!a.dynamic_fov);motions|=1u<<a.motion;
        CHECK(a.camera.near_plane==base.near_plane&&a.camera.far_plane==base.far_plane&&a.camera.aspect==base.aspect&&a.fov>=15&&a.fov<=85);CHECK(!memcmp(a.origin,actors[affected].position,3*sizeof(float)));
        if(a.motion!=0){CHECK(a.fov_pattern<8);patterns|=1u<<a.fov_pattern;}else CHECK(a.fov_pattern==UINT32_MAX);
        if(a.motion==1)CHECK(fabsf(a.eye_from[0]+1.f)<1e-6f&&a.eye_to[0]<-3.5f);
        b=a;CHECK(c3_battle_camera_step(&a,3400,actors,error,sizeof error));for(unsigned i=0;i<200;++i)CHECK(c3_battle_camera_step(&b,17,actors,error,sizeof error));CHECK(!memcmp(&a,&b,sizeof a));c3_battle_camera_pause(&a,1);old=a;CHECK(c3_battle_camera_step(&a,UINT32_MAX,actors,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));c3_battle_camera_cancel(&a);CHECK(!memcmp(&a.camera,&base,sizeof base));
    }
    CHECK(motions==15&&patterns==255);
    c3_battle_camera_init(&a,8128);CHECK(c3_battle_camera_begin_special(&a,p,special,0,3,actors,&base,error,sizeof error));unsigned first_motion=a.motion,first_draws=a.random_draws;c3_battle_camera_cancel(&a);CHECK(c3_battle_camera_begin_special(&a,p,special,1,3,actors,&base,error,sizeof error));CHECK(a.cuts==2&&a.motion!=first_motion&&a.random_draws>first_draws);
    c3_battle_camera_init(&a,123);old=a;CHECK(!c3_battle_camera_begin_special(&a,p,special==0x29?0x27:0x29,0,2,actors,&base,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));CHECK(!c3_battle_camera_begin_special(&a,p,29,0,2,actors,&base,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));CHECK(!c3_battle_camera_begin_special(&a,p,special,2,0,actors,&base,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));CHECK(!c3_battle_camera_begin_move(&a,p,39,C3_BATTLE_CAMERA_ATTACK,0,2,actors,&base,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));
    C3BattleCameraProgram bad=*p;bad.flags[0]=special==0x29?0x78:0;CHECK(!c3_battle_camera_begin_special(&a,&bad,0x29,0,2,actors,&base,error,sizeof error));CHECK(!memcmp(&a,&old,sizeof a));
}
static void stat_actual(const char *path)
{
    char error[192];C3BattleCameraProgram p={0};CHECK(c3_battle_camera_stat_down_load(&p,path,error,sizeof error));CHECK(p.actors[0].timing[0][0]==118&&p.actors[1].timing[0][0]==180&&!p.actors[2].species);stat_exercise(&p);
    FILE *f=fopen(path,"rb");CHECK(f&&!fseek(f,0,SEEK_END));long length=ftell(f);rewind(f);uint8_t *raw=malloc((size_t)length);CHECK(raw&&fread(raw,1,(size_t)length,f)==(size_t)length);fclose(f);C3BattleCameraProgram before=p;CHECK(!c3_battle_camera_attack_down_decode(&p,raw,(size_t)length,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));CHECK(!c3_battle_camera_tailwhip_decode(&p,raw,(size_t)length,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));raw[64]=0x78;memset(raw+480,0,20);uint8_t hash[20];c3_save_codec_sha1(hash,raw,(uint32_t)length);memcpy(raw+480,hash,20);CHECK(!c3_battle_camera_stat_down_decode(&p,raw,(size_t)length,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));free(raw);c3_battle_camera_close(&p);
}
static void red_actual(const char *path)
{
    char error[192];C3BattleCameraProgram p={0};CHECK(c3_battle_camera_attack_down_load(&p,path,error,sizeof error));stat_exercise(&p);
    FILE *f=fopen(path,"rb");CHECK(f&&!fseek(f,0,SEEK_END));long length=ftell(f);rewind(f);uint8_t *raw=malloc((size_t)length);CHECK(raw&&fread(raw,1,(size_t)length,f)==(size_t)length);fclose(f);C3BattleCameraProgram before=p;
    CHECK(!c3_battle_camera_stat_down_decode(&p,raw,(size_t)length,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));
    CHECK(!c3_battle_camera_tailwhip_decode(&p,raw,(size_t)length,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));
    raw[64]=0;memset(raw+480,0,20);uint8_t hash[20];c3_save_codec_sha1(hash,raw,(uint32_t)length);memcpy(raw+480,hash,20);CHECK(!c3_battle_camera_attack_down_decode(&p,raw,(size_t)length,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));free(raw);c3_battle_camera_close(&p);
}
int main(int argc,char **argv)
{
    C3BattleCameraProgram p=synthetic();source_golden();exercise(&p);confusion_synthetic();C3BattleCameraProgram tail=tail_synthetic();tail_exercise(&tail);C3BattleCameraProgram growl=growl_synthetic();growl_exercise(&growl);C3BattleCameraProgram stat=stat_synthetic();stat_exercise(&stat);C3BattleCameraProgram red=stat;red.move_id=C3_BATTLE_CAMERA_RED_DOWN_PROFILE;red.flags[0]=0x78;stat_exercise(&red);
    if(argc>1){char error[192];CHECK(c3_battle_camera_load(&p,argv[1],error,sizeof error));exercise(&p);
        FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);CHECK(length>=512&&length<=(long)C3_BATTLE_CAMERA_MAX_BYTES);rewind(f);
        uint8_t *bytes=malloc((size_t)length),*mut=malloc((size_t)length);CHECK(bytes&&mut);CHECK(fread(bytes,1,(size_t)length,f)==(size_t)length);fclose(f);
        for(unsigned i=0;i<512;++i){memcpy(mut,bytes,(size_t)length);mut[i]^=1;C3BattleCameraProgram before=p;CHECK(!c3_battle_camera_decode(&p,mut,(size_t)length,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));}
        memcpy(mut,bytes,(size_t)length);store(mut+192+4,0);memset(mut+480,0,20);uint8_t digest[20];c3_save_codec_sha1(digest,mut,(size_t)length);memcpy(mut+480,digest,20);CHECK(!c3_battle_camera_decode(&p,mut,(size_t)length,error,sizeof error));
        memcpy(mut,bytes,(size_t)length);store(mut+512,0x7fc00000);memset(mut+480,0,20);c3_save_codec_sha1(digest,mut,(size_t)length);memcpy(mut+480,digest,20);CHECK(!c3_battle_camera_decode(&p,mut,(size_t)length,error,sizeof error));
        free(bytes);free(mut);c3_battle_camera_close(&p);CHECK(!p.samples&&!p.sample_count);

    }
    if(argc>2)confusion_exercise(argv[2]);
    if(argc>3)tail_actual(argv[3]);
    if(argc>4)growl_actual(argv[4]);
    if(argc>5)stat_actual(argv[5]);
    if(argc>6)red_actual(argv[6]);
    printf("camera checks: %u\n",checks);return 0;
}
