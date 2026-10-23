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
int main(int argc,char **argv)
{
    C3BattleCameraProgram p=synthetic();source_golden();exercise(&p);
    if(argc>1){char error[192];CHECK(c3_battle_camera_load(&p,argv[1],error,sizeof error));exercise(&p);
        FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);CHECK(length>=512&&length<=(long)C3_BATTLE_CAMERA_MAX_BYTES);rewind(f);
        uint8_t *bytes=malloc((size_t)length),*mut=malloc((size_t)length);CHECK(bytes&&mut);CHECK(fread(bytes,1,(size_t)length,f)==(size_t)length);fclose(f);
        for(unsigned i=0;i<512;++i){memcpy(mut,bytes,(size_t)length);mut[i]^=1;C3BattleCameraProgram before=p;CHECK(!c3_battle_camera_decode(&p,mut,(size_t)length,error,sizeof error));CHECK(!memcmp(&p,&before,sizeof p));}
        memcpy(mut,bytes,(size_t)length);store(mut+192+4,0);memset(mut+480,0,20);uint8_t digest[20];c3_save_codec_sha1(digest,mut,(size_t)length);memcpy(mut+480,digest,20);CHECK(!c3_battle_camera_decode(&p,mut,(size_t)length,error,sizeof error));
        memcpy(mut,bytes,(size_t)length);store(mut+512,0x7fc00000);memset(mut+480,0,20);c3_save_codec_sha1(digest,mut,(size_t)length);memcpy(mut+480,digest,20);CHECK(!c3_battle_camera_decode(&p,mut,(size_t)length,error,sizeof error));
        free(bytes);free(mut);c3_battle_camera_close(&p);CHECK(!p.samples&&!p.sample_count);

    }
    printf("camera checks: %u\n",checks);return 0;
}
