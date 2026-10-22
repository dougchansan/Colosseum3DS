#include "colosseum3ds/field.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); } } while (0)
static void word(unsigned char *p, unsigned v)
{ for (unsigned i=0;i<4;++i)p[i]=(unsigned char)(v>>(i*8)); }
static void real(unsigned char *p, float v)
{ unsigned bits;memcpy(&bits,&v,4);word(p,bits); }

/* A broad floor and a one-sided wall at x=2 exercise geometry independently
 * of the private disc. These generated surfaces are never installed. */
static unsigned char *fixture(size_t *length)
{
    *length=256+24+3*56;
    unsigned char *p=calloc(1,*length);CHECK(p);
    memcpy(p,"C3F1",4);word(p+4,1);word(p+8,(unsigned)*length);word(p+12,256);
    word(p+16,1);word(p+20,0x00e20200);word(p+24,1);word(p+28,1);word(p+32,2);
    word(p+36,256);word(p+40,280);word(p+44,336);word(p+48,56);word(p+52,24);
    real(p+64,.1f);real(p+68,.4f);real(p+72,.9f);real(p+76,1.3f);
    real(p+80,31);real(p+84,18);real(p+88,31);word(p+92,1);
    real(p+100,11);real(p+104,9.1836f);real(p+112,1.4f);
    real(p+124,30);real(p+128,.01f);real(p+132,3276.8f);real(p+136,1.18f);
    real(p+140,11);real(p+144,9.1836f);real(p+152,1.4f);word(p+164,~0u);
    word(p+172,1);word(p+176,2);
    word(p+180,1);
    word(p+184,1);
    const float floor[]={-100,0,-100, 100,0,-100, 0,0,100, 0,1,0};
    const float wall1[]={2,-10,-20, 2,20,-20, 2,20,20, -1,0,0};
    const float wall2[]={2,-10,-20, 2,20,20, 2,-10,20, -1,0,0};
    for(unsigned i=0;i<12;++i) {
        real(p+280+i*4,floor[i]);real(p+336+i*4,wall1[i]);real(p+392+i*4,wall2[i]);
    }
    p[336+48]=p[392+48]=7;
    return p;
}

int main(int argc,char **argv)
{
    C3FieldMap map={0};C3FieldPlayer player;C3TitleCamera camera;
    char error[192];size_t bytes;unsigned char *p=fixture(&bytes);
    CHECK(c3_field_parse(&map,p,bytes,error,sizeof error));
    CHECK(map.floor==1 && map.spawn_count==1 && map.wall_count==2 && map.object_count==1);
    CHECK(c3_field_spawn(&map,0,&player));CHECK(!c3_field_spawn(&map,1,&player));
    CHECK(c3_field_move(&map,&player,0,127,0,0,1.0f/60));
    CHECK(player.position[2]<0 && fabsf(player.position[2]+1.3f/18)<1e-5f);
    CHECK(player.motion==2 && fabsf(player.yaw-3.1415927f)<1e-5f);
    CHECK(c3_field_spawn(&map,0,&player));
    CHECK(c3_field_move(&map,&player,0,0,8,0,1.0f/60));
    CHECK(fabsf(player.position[2]+1.3f/18)<1e-5f);
    CHECK(c3_field_spawn(&map,0,&player));
    CHECK(c3_field_move(&map,&player,64,0,0,0,1.0f/60));
    CHECK(player.motion==1 && fabsf(player.position[0]-.9f/31)<1e-5f);
    CHECK(c3_field_move(&map,&player,0,0,0,0,1.0f/60));
    CHECK(player.motion==0 && fabsf(player.animation_seconds-1.0f/60)<1e-6f);
    CHECK(c3_field_spawn(&map,0,&player));
    CHECK(c3_field_move(&map,&player,0,127,0,1.5707963f,1.0f/60));
    CHECK(player.position[0]<0 && fabsf(player.position[2])<1e-6f);
    CHECK(c3_field_spawn(&map,0,&player));
    for(unsigned i=0;i<10;++i)CHECK(c3_field_move(&map,&player,127,0,0,0,.25f));
    CHECK(player.position[0]<=1.601f && player.position[0]>1.59f && player.collision_count>0);
    CHECK(c3_field_spawn(&map,0,&player));
    CHECK(c3_field_move(&map,&player,-127,0,0,0,10));
    CHECK(fabsf(player.position[0]+1.3f/18*15)<1e-4f);
    C3FieldPlayer before=player;
    CHECK(!c3_field_move(&map,&player,127,0,0,0,NAN));
    CHECK(!memcmp(&before,&player,sizeof player));
    player.position[0]=200;player.position[2]=200;before=player;
    CHECK(c3_field_move(&map,&player,127,0,0,0,.25f));
    CHECK(!memcmp(before.position,player.position,sizeof player.position));
    float point[3]={0,.85f,0},height=123;
    CHECK(c3_field_ground(&map,point,&height) && fabsf(height)<1e-6f);
    point[0]=200;CHECK(!c3_field_ground(&map,point,&height));
    CHECK(c3_field_spawn(&map,0,&player));c3_field_camera(&map,&player,&camera);
    CHECK(fabsf(camera.eye[1]-11)<1e-6f && fabsf(camera.target[1]-1.4f)<1e-6f);
    C3FieldSpawn *old=map.spawns;
    word(p+40,281);CHECK(!c3_field_parse(&map,p,bytes,error,sizeof error));CHECK(map.spawns==old);
    word(p+40,280);real(p+336+36,2);CHECK(!c3_field_parse(&map,p,bytes,error,sizeof error));CHECK(map.spawns==old);
    real(p+336+36,-1);p[188]=1;CHECK(!c3_field_parse(&map,p,bytes,error,sizeof error));p[188]=0;
    CHECK(!c3_field_parse(&map,p,bytes-1,error,sizeof error));
    real(p+64,1);CHECK(!c3_field_parse(&map,p,bytes,error,sizeof error));real(p+64,.1f);
    CHECK(c3_field_parse(&map,p,bytes,error,sizeof error));
    free(p);c3_field_close(&map);
    if(argc>1) {
        CHECK(c3_field_load(&map,argv[1],error,sizeof error));
        CHECK(map.floor==1 && map.spawn_count==5 && map.walk_count==136 && map.wall_count==244);
        CHECK(c3_field_spawn(&map,0,&player));
        CHECK(fabsf(player.position[0]-5)<1e-5f && fabsf(player.position[2]-7)<1e-5f);
        point[0]=player.position[0];point[1]=player.position[1]+.85f;point[2]=player.position[2];
        CHECK(c3_field_ground(&map,point,&height));
        CHECK(fabsf(height-player.position[1])<.01f);
        CHECK(map.walk_frames==31 && map.run_frames==18 && map.idle_frames==31);
        c3_field_close(&map);
    }
    printf("field: %u checks passed\n",checks);return 0;
}
