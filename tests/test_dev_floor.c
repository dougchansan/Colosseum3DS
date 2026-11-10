#include "colosseum3ds/dev_floor.h"
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

/* Synthetic Phenac-shaped pack: three entrances on one walk triangle.
 * Generated geometry only; never installed and contains no original data. */
static unsigned char *fixture(size_t *length)
{
    *length=256+3*24+56;
    unsigned char *p=calloc(1,*length);CHECK(p);
    memcpy(p,"C3F1",4);word(p+4,1);word(p+8,(unsigned)*length);word(p+12,256);
    word(p+16,3);word(p+20,0x00620200);word(p+24,3);word(p+28,1);word(p+32,0);
    word(p+36,256);word(p+40,256+72);word(p+44,256+72+56);word(p+48,56);word(p+52,24);
    real(p+64,.1f);real(p+68,.4f);real(p+72,.9f);real(p+76,1.3f);
    real(p+80,31);real(p+84,18);real(p+88,31);word(p+92,1);
    real(p+100,14);real(p+104,11.89f);real(p+112,1.4f);
    real(p+124,30);real(p+128,.01f);real(p+132,3276.8f);real(p+136,1.18f);
    real(p+140,14);real(p+144,11.89f);real(p+152,1.4f);
    word(p+160,2445);word(p+164,~0u);word(p+172,1);word(p+176,2);
    word(p+180,8);word(p+184,0);
    for(unsigned i=0;i<3;++i){
        real(p+256+i*24,(float)i);real(p+256+i*24+8,(float)i);
        real(p+256+i*24+12,i*1.5707964f);word(p+256+i*24+16,i);
    }
    const float floor[]={-100,0,-100, 100,0,-100, 0,0,100, 0,1,0};
    for(unsigned i=0;i<12;++i)real(p+256+72+i*4,floor[i]);
    return p;
}

static void real_pack(const char *path)
{
    /* Optional private check: C3_DEV_FLOOR_PACK=<private>/phenac.c3f. */
    C3FieldMap map={0};C3FieldPlayer player;char error[192];
    const C3DevFloorProfile *phenac=c3_dev_floor_profile(0);
    CHECK(c3_field_load(&map,path,error,sizeof error));
    CHECK(c3_dev_floor_validate(&map,phenac,error,sizeof error));
    CHECK(map.spawn_count==10 && map.map_pack_count>=1);
    unsigned grounded=0;
    for(uint32_t i=0;i<map.spawn_count;++i){
        float height;
        CHECK(c3_field_spawn(&map,map.spawns[i].source_index,&player));
        if(c3_field_ground(&map,player.position,&height))++grounded;
        CHECK(c3_field_move(&map,&player,0,90,0,map.camera_yaw,1.0f/60));
    }
    /* Entrance0 is where the source camera frames the town square. */
    float height;
    CHECK(c3_field_spawn(&map,0,&player));
    CHECK(c3_field_ground(&map,player.position,&height));
    printf("dev floor pack: floor=%lu maps=%lu entrances=%lu grounded=%u walk=%lu walls=%lu objects=%lu\n",
           (unsigned long)map.floor,(unsigned long)map.map_pack_count,(unsigned long)map.spawn_count,grounded,
           (unsigned long)map.walk_count,(unsigned long)map.wall_count,(unsigned long)map.object_count);
    c3_field_close(&map);
}

int main(void)
{
    C3FieldMap map={0};char error[192],path[64];size_t bytes;unsigned char *p=fixture(&bytes);
    const C3DevFloorProfile *phenac=c3_dev_floor_profile(0);
    CHECK(c3_dev_floor_profile_count()==1 && phenac && !c3_dev_floor_profile(1));
    CHECK(phenac->floor==3 && phenac->map_resource==0x00620200 && !strcmp(phenac->prefix,"phenac"));
    CHECK(strstr(C3_DEV_FLOOR_LABEL,"DEVELOPER"));

    CHECK(c3_dev_floor_path(path,sizeof path,"root",phenac,C3_DEV_FLOOR_FIELD,0) && !strcmp(path,"root/phenac.c3f"));
    CHECK(c3_dev_floor_path(path,sizeof path,"root",phenac,C3_DEV_FLOOR_MAP,7) && !strcmp(path,"root/phenac_map7.c3t"));
    CHECK(c3_dev_floor_path(path,sizeof path,"root",phenac,C3_DEV_FLOOR_WES,0) && !strcmp(path,"root/phenac_wes.c3t"));
    CHECK(!c3_dev_floor_path(path,sizeof path,"root",phenac,C3_DEV_FLOOR_MAP,C3_FIELD_MAX_MODEL_PACKS));
    CHECK(!c3_dev_floor_path(path,12,"root",phenac,C3_DEV_FLOOR_WES,0) && !path[0]);
    CHECK(!c3_dev_floor_path(path,sizeof path,NULL,phenac,C3_DEV_FLOOR_FIELD,0));

    CHECK(!c3_dev_floor_validate(&map,phenac,error,sizeof error)); /* not loaded */
    CHECK(c3_field_parse(&map,p,bytes,error,sizeof error));
    CHECK(c3_dev_floor_validate(&map,phenac,error,sizeof error) && !error[0]);
    CHECK(c3_dev_floor_next_spawn(&map,0,1)==1 && c3_dev_floor_next_spawn(&map,2,1)==0);
    CHECK(c3_dev_floor_next_spawn(&map,0,-1)==2 && c3_dev_floor_next_spawn(&map,1,0)==1);
    CHECK(c3_dev_floor_next_spawn(NULL,5,1)==0);

    word(p+16,1);CHECK(c3_field_parse(&map,p,bytes,error,sizeof error));
    CHECK(!c3_dev_floor_validate(&map,phenac,error,sizeof error) && strstr(error,"different floor"));
    word(p+16,3);word(p+20,0x00e20200);CHECK(c3_field_parse(&map,p,bytes,error,sizeof error));
    CHECK(!c3_dev_floor_validate(&map,phenac,error,sizeof error));
    word(p+20,0x00620200);word(p+184,1);CHECK(c3_field_parse(&map,p,bytes,error,sizeof error));
    CHECK(!c3_dev_floor_validate(&map,phenac,error,sizeof error) && strstr(error,"arrival"));
    word(p+184,0);word(p+164,0);CHECK(c3_field_parse(&map,p,bytes,error,sizeof error));
    CHECK(!c3_dev_floor_validate(&map,phenac,error,sizeof error) && strstr(error,"Wes"));
    word(p+164,~0u);CHECK(c3_field_parse(&map,p,bytes,error,sizeof error));
    CHECK(c3_dev_floor_validate(&map,phenac,error,sizeof error));
    c3_field_close(&map);free(p);

    /* Wes placement: a quarter turn maps local +X to world -Z, then offsets. */
    static C3Title wes;C3TitleDrawVertex vertex;memset(&vertex,0,sizeof vertex);
    vertex.pos[0]=1;vertex.pos[1]=2;vertex.normal[0]=1;
    C3FieldPlayer player={{10,1,-5},1.5707964f,0,0,0,0};
    wes.draw_vertices=&vertex;wes.vertex_count=1;wes.batch_count=1;
    wes.pose.materials[0].specular_origin[0]=1;
    c3_dev_floor_place_actor(&wes,&player);
    CHECK(fabsf(vertex.pos[0]-10)<1e-5f && fabsf(vertex.pos[1]-3)<1e-5f && fabsf(vertex.pos[2]+6)<1e-5f);
    CHECK(fabsf(vertex.normal[0])<1e-5f && fabsf(vertex.normal[2]+1)<1e-5f);
    CHECK(fabsf(wes.pose.materials[0].specular_origin[2]+6)<1e-5f);
    c3_dev_floor_place_actor(NULL,&player);

    const char *pack=getenv("C3_DEV_FLOOR_PACK");
    if(pack&&*pack)real_pack(pack);
    else printf("dev floor pack: private C3_DEV_FLOOR_PACK not set; synthetic checks only\n");
    printf("test_dev_floor: %u checks passed\n",checks);
    return 0;
}
