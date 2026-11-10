#include "colosseum3ds/field.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_BYTES 256u
#define TRIANGLE_BYTES 56u
#define SPAWN_BYTES 24u

static uint32_t le32(const unsigned char *p)
{ return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24; }
static uint16_t le16(const unsigned char *p)
{ return (uint16_t)((unsigned)p[0] | (unsigned)p[1]<<8); }
static float flt(const unsigned char *p)
{ uint32_t bits=le32(p); float f; memcpy(&f,&bits,4); return f; }
static int finite_bound(float f, float maximum)
{ return isfinite(f) && fabsf(f)<=maximum; }
static int fail(char *error,size_t capacity,const char *message)
{ if(error && capacity)snprintf(error,capacity,"%s",message);return 0; }

void c3_field_close(C3FieldMap *map)
{
    if(!map)return;
    free(map->spawns);free(map->walk);free(map->walls);
    memset(map,0,sizeof(*map));
}

static int triangles(C3FieldTriangle *out,const unsigned char *p,uint32_t count)
{
    for(uint32_t i=0;i<count;++i,p+=TRIANGLE_BYTES) {
        for(unsigned j=0;j<9;++j) {
            out[i].vertices[j]=flt(p+j*4u);
            if(!finite_bound(out[i].vertices[j],100000.0f))return 0;
        }
        float magnitude=0.0f;
        for(unsigned j=0;j<3;++j) {
            out[i].normal[j]=flt(p+36u+j*4u);
            if(!finite_bound(out[i].normal[j],1.001f))return 0;
            magnitude+=out[i].normal[j]*out[i].normal[j];
        }
        if(magnitude<0.99f || magnitude>1.01f || le16(p+54u))return 0;
        out[i].flags=le16(p+48u);out[i].source_id=le16(p+50u);out[i].object=le16(p+52u);
        if(out[i].object>=256u)return 0;
    }
    return 1;
}

int c3_field_parse(C3FieldMap *map,const void *data,size_t bytes,char *error,size_t capacity)
{
    const unsigned char *p=data;
    C3FieldMap next={0};
    if(!map || !p || bytes<HEADER_BYTES || bytes>C3_FIELD_MAX_BYTES ||
       memcmp(p,"C3F1",4) || le32(p+4)!=1 || le32(p+8)!=bytes || le32(p+12)!=HEADER_BYTES)
        return fail(error,capacity,"Invalid C3F1 field header.");
    next.floor=le32(p+16);next.map_resource=le32(p+20);
    next.spawn_count=le32(p+24);next.walk_count=le32(p+28);next.wall_count=le32(p+32);
    if(!next.floor || !next.map_resource || !next.spawn_count || next.spawn_count>C3_FIELD_MAX_SPAWNS ||
       !next.walk_count || next.walk_count>C3_FIELD_MAX_TRIANGLES || next.wall_count>C3_FIELD_MAX_TRIANGLES ||
       le32(p+36)!=HEADER_BYTES || le32(p+48)!=TRIANGLE_BYTES || le32(p+52)!=SPAWN_BYTES ||
       le32(p+56) || le32(p+60))return fail(error,capacity,"Invalid field counts or layout.");
    size_t walk_offset=HEADER_BYTES+(size_t)next.spawn_count*SPAWN_BYTES;
    size_t wall_offset=walk_offset+(size_t)next.walk_count*TRIANGLE_BYTES;
    if(le32(p+40)!=walk_offset || le32(p+44)!=wall_offset ||
       wall_offset+(size_t)next.wall_count*TRIANGLE_BYTES!=bytes)
        return fail(error,capacity,"Field sections must be contiguous and exact.");
    next.map_pack_count=le32(p+180);
    next.arrival_pack_count=le32(p+184);
    if(!next.map_pack_count||next.map_pack_count>C3_FIELD_MAX_MODEL_PACKS||
       next.arrival_pack_count>6u||next.map_pack_count+next.arrival_pack_count>C3_FIELD_MAX_MODEL_PACKS)
        return fail(error,capacity,"Invalid field model count.");
    for(unsigned i=188;i<HEADER_BYTES;++i)if(p[i])return fail(error,capacity,"Unknown field header extension.");
    next.scale=flt(p+64);next.radius=flt(p+68);next.walk_distance=flt(p+72);next.run_distance=flt(p+76);
    next.walk_frames=flt(p+80);next.run_frames=flt(p+84);next.idle_frames=flt(p+88);
    if(next.scale!=0.1f || !finite_bound(next.radius,10.0f) || next.radius<=0.0f ||
       !finite_bound(next.walk_distance,1000.0f) || next.walk_distance<=0.0f ||
       !finite_bound(next.run_distance,1000.0f) || next.run_distance<=0.0f ||
       !finite_bound(next.walk_frames,10000.0f) || next.walk_frames<=0.0f ||
       !finite_bound(next.run_frames,10000.0f) || next.run_frames<=0.0f ||
       !finite_bound(next.idle_frames,10000.0f) || next.idle_frames<=0.0f)
        return fail(error,capacity,"Invalid source movement scale or motion data.");
    next.camera_mode=le32(p+92);
    for(unsigned j=0;j<3;++j) {
        next.camera.eye[j]=flt(p+96u+j*4u);next.camera.target[j]=flt(p+108u+j*4u);
        if(!finite_bound(next.camera.eye[j],100000.0f) || !finite_bound(next.camera.target[j],100000.0f))
            return fail(error,capacity,"Invalid source camera position.");
    }
    next.camera.roll=flt(p+120);next.camera.fov_y_degrees=flt(p+124);
    next.camera.near_plane=flt(p+128);next.camera.far_plane=flt(p+132);next.camera.aspect=flt(p+136);
    next.camera_height=flt(p+140);next.camera_distance=flt(p+144);next.camera_yaw=flt(p+148);next.camera_target_height=flt(p+152);
    if(next.camera_mode>1 || !finite_bound(next.camera.roll,100.0f) ||
       !finite_bound(next.camera.fov_y_degrees,179.0f) || next.camera.fov_y_degrees<=1.0f ||
       !finite_bound(next.camera.near_plane,100000.0f) || next.camera.near_plane<=0.0f ||
       !finite_bound(next.camera.far_plane,100000.0f) || next.camera.far_plane<=next.camera.near_plane ||
       !finite_bound(next.camera.aspect,10.0f) || next.camera.aspect<=0.0f ||
       !finite_bound(next.camera_height,100000.0f) || !finite_bound(next.camera_distance,100000.0f) ||
       !finite_bound(next.camera_yaw,100.0f) || !finite_bound(next.camera_target_height,100000.0f))
        return fail(error,capacity,"Invalid source camera parameters.");
    next.actor_first_vertex=le32(p+156);next.actor_vertex_count=le32(p+160);next.actor_bone=le32(p+164);
    next.idle_clip=le32(p+168);next.walk_clip=le32(p+172);next.run_clip=le32(p+176);
    if(next.actor_first_vertex>UINT32_MAX-next.actor_vertex_count || next.actor_vertex_count%3u ||
       next.idle_clip>=3 || next.walk_clip>=3 || next.run_clip>=3)
        return fail(error,capacity,"Invalid field actor range or clips.");
    next.spawns=calloc(next.spawn_count,sizeof(*next.spawns));
    next.walk=calloc(next.walk_count,sizeof(*next.walk));
    if(next.wall_count)next.walls=calloc(next.wall_count,sizeof(*next.walls));
    if(!next.spawns || !next.walk || (next.wall_count&&!next.walls)) {
        c3_field_close(&next);return fail(error,capacity,"Field table allocation failed.");
    }
    for(uint32_t i=0;i<next.spawn_count;++i) {
        const unsigned char *s=p+HEADER_BYTES+i*SPAWN_BYTES;
        for(unsigned j=0;j<3;++j)next.spawns[i].position[j]=flt(s+j*4u);
        next.spawns[i].yaw=flt(s+12);next.spawns[i].source_index=le32(s+16);
        for(unsigned j=0;j<3;++j)if(!finite_bound(next.spawns[i].position[j],100000.0f))goto invalid;
        if(!finite_bound(next.spawns[i].yaw,100.0f)||next.spawns[i].source_index>=256u||le32(s+20))goto invalid;
        for(uint32_t j=0;j<i;++j)if(next.spawns[j].source_index==next.spawns[i].source_index)goto invalid;
    }
    if(!triangles(next.walk,p+walk_offset,next.walk_count)||!triangles(next.walls,p+wall_offset,next.wall_count))goto invalid;
    for(uint32_t i=0;i<next.walk_count;++i)
        if(next.walk[i].object>=next.object_count)next.object_count=(uint32_t)next.walk[i].object+1u;
    for(uint32_t i=0;i<next.wall_count;++i)
        if(next.walls[i].object>=next.object_count)next.object_count=(uint32_t)next.walls[i].object+1u;
    c3_field_close(map);*map=next;
    if(error&&capacity)error[0]=0;
    return 1;
invalid:
    c3_field_close(&next);return fail(error,capacity,"Invalid field spawn or collision triangle.");
}

int c3_field_load(C3FieldMap *map,const char *path,char *error,size_t capacity)
{
    if(!map || !path)return fail(error,capacity,"Invalid field load arguments.");
    FILE *file=fopen(path,"rb");
    if(!file)return fail(error,capacity,"Prepared Outskirt Stand field data was not found.");
    long length;
    if(fseek(file,0,SEEK_END)||(length=ftell(file))<0||(unsigned long)length>C3_FIELD_MAX_BYTES||fseek(file,0,SEEK_SET)) {
        fclose(file);return fail(error,capacity,"Invalid field file size.");
    }
    unsigned char *data=malloc(length?(size_t)length:1u);
    if(!data){fclose(file);return fail(error,capacity,"Field read allocation failed.");}
    int ok=fread(data,1,(size_t)length,file)==(size_t)length && !ferror(file);
    fclose(file);
    if(ok)ok=c3_field_parse(map,data,(size_t)length,error,capacity);
    else fail(error,capacity,"Field data read failed.");
    free(data);return ok;
}

int c3_field_spawn(const C3FieldMap *map,uint32_t index,C3FieldPlayer *player)
{
    if(!map||!player||!map->spawns)return 0;
    for(uint32_t i=0;i<map->spawn_count;++i)if(map->spawns[i].source_index==index) {
        memset(player,0,sizeof(*player));memcpy(player->position,map->spawns[i].position,sizeof(player->position));
        player->yaw=map->spawns[i].yaw;return 1;
    }
    return 0;
}

static float dot(const float a[3],const float b[3])
{return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static void subtract(float out[3],const float a[3],const float b[3])
{for(unsigned i=0;i<3;++i)out[i]=a[i]-b[i];}
static float distance_squared(const float a[3],const float b[3])
{float d[3];subtract(d,a,b);return dot(d,d);}

static int in_triangle(const float point[3],const C3FieldTriangle *t)
{
    float u[3],v[3],w[3];
    subtract(u,t->vertices+3,t->vertices);subtract(v,t->vertices+6,t->vertices);subtract(w,point,t->vertices);
    double uu=dot(u,u),uv=dot(u,v),vv=dot(v,v),wu=dot(w,u),wv=dot(w,v),d=uu*vv-uv*uv;
    if(d<=1e-14)return 0;
    double a=(wu*vv-wv*uv)/d,b=(wv*uu-wu*uv)/d;
    return a>=-1e-6 && b>=-1e-6 && a+b<=1.000001;
}

int c3_field_object_enabled(const C3FieldMap *map,uint32_t object)
{
    return map&&object<map->object_count&&object<256u&&
        !(map->disabled_objects[object/8u]&(1u<<(object%8u)));
}

int c3_field_set_object_enabled(C3FieldMap *map,uint32_t object,int enabled)
{
    if(!map||object>=map->object_count||object>=256u)return 0;
    uint8_t mask=(uint8_t)(1u<<(object%8u));
    if(enabled)map->disabled_objects[object/8u]&=(uint8_t)~mask;
    else map->disabled_objects[object/8u]|=mask;
    return 1;
}

int c3_field_ground(const C3FieldMap *map,const float point[3],float *height)
{
    if(!map||!point||!height||!map->walk)return 0;
    float heights[8];unsigned count=0;
    for(uint32_t i=0;i<map->walk_count&&count<8;++i) {
        const C3FieldTriangle *t=&map->walk[i];
        if(!c3_field_object_enabled(map,t->object))continue;
        if(fabsf(t->normal[1])<1e-8f)continue;
        float p[3]={point[0],0,point[2]};
        p[1]=t->vertices[1]-(t->normal[0]*(p[0]-t->vertices[0])+t->normal[2]*(p[2]-t->vertices[2]))/t->normal[1];
        if(!in_triangle(p,t))continue;
        unsigned j;for(j=0;j<count;++j)if(heights[j]==p[1])break;
        if(j==count)heights[count++]=p[1];
    }
    if(!count)return 0;
    float best=heights[0],best_step=-INFINITY;
    for(unsigned i=0;i<count;++i) {
        if(heights[i]>best)best=heights[i];
        /* people.c fn_8018E9B4 compares to the foot sphere center and
         * accepts a height strictly less than ten source units above it. */
        if(heights[i]-point[1]<10.0f*map->scale && heights[i]>best_step)best_step=heights[i];
    }
    *height=isfinite(best_step)?best_step:best;return 1;
}

/* Three original sphere-query passes: face, flagged edges, flagged corners.
 * The prepared static map is scanned linearly by original object order;
 * object enable bits follow scriptSetCol. Moving object geometry and the
 * original spatial grid are not part of this adapter. */
static int contact(const C3FieldTriangle *t,const float point[3],float radius,unsigned pass,float hit[3])
{
    float offset[3];subtract(offset,point,t->vertices);
    float side=dot(offset,t->normal),r2=radius*radius;
    if(side<0.0f)return 0;
    if(pass==0) {
        for(unsigned i=0;i<3;++i)hit[i]=point[i]-side*t->normal[i];
        return distance_squared(hit,point)<r2 && in_triangle(hit,t);
    }
    for(unsigned i=0;i<3;++i) {
        unsigned next=(i+1)%3,prev=(i+2)%3;
        const float *a=t->vertices+i*3,*b=t->vertices+next*3;
        if(!(t->flags&(1u<<i)))continue;
        if(pass==1) {
            float direction[3],to_point[3];subtract(direction,b,a);subtract(to_point,point,a);
            float length=dot(direction,direction);
            if(length<1e-14f)continue;
            float along=dot(to_point,direction)/length;
            if(along<0.0f||along>1.0f)continue;
            for(unsigned j=0;j<3;++j)hit[j]=a[j]+along*direction[j];
        } else {
            if(!(t->flags&(1u<<prev)))continue;
            memcpy(hit,a,3*sizeof(float));
        }
        if(distance_squared(hit,point)<r2)return 1;
    }
    return 0;
}

static int collide_point(const C3FieldMap *map,float point[3])
{
    int collided=0;
    for(unsigned iteration=0;iteration<10;++iteration) {
        int hits=0;
        for(unsigned object=0;object<map->object_count;++object) {
            if(!c3_field_object_enabled(map,object))continue;
            float hit[3];const C3FieldTriangle *triangle=NULL;
            for(unsigned pass=0;pass<3&&!triangle;++pass)
                for(uint32_t i=0;i<map->wall_count;++i)
                    if(map->walls[i].object==object&&contact(&map->walls[i],point,map->radius,pass,hit)) {
                        triangle=&map->walls[i];break;
                    }
            if(!triangle)continue;
            float direction[3];subtract(direction,point,hit);
            float length=sqrtf(dot(direction,direction));
            if(length<1e-8f)memcpy(direction,triangle->normal,sizeof(direction));
            else for(unsigned i=0;i<3;++i)direction[i]/=length;
            for(unsigned i=0;i<3;++i)point[i]=hit[i]+direction[i]*(map->radius+0.0001f*map->scale);
            ++hits;collided=1;
        }
        if(!hits)break;
    }
    return collided;
}

static int collide_segment(const C3FieldMap *map,const float from[3],float to[3])
{
    float direction[3];subtract(direction,to,from);
    float distance=sqrtf(dot(direction,direction));
    unsigned steps=distance>map->radius?(unsigned)ceilf(distance/map->radius):1u;
    if(steps>512u)steps=512u;
    for(unsigned step=1;step<=steps;++step) {
        float p[3],fraction=(float)step/(float)steps;
        for(unsigned i=0;i<3;++i)p[i]=from[i]+fraction*direction[i];
        if(collide_point(map,p)){memcpy(to,p,sizeof(p));return 1;}
    }
    return 0;
}

int c3_field_resolve_position(const C3FieldMap *map,C3FieldPlayer *player,const float before[3])
{
    if(!map||!player||!before||!map->walk)return 0;
    for(unsigned i=0;i<3;++i)
        if(!finite_bound(before[i],100000.0f)||!finite_bound(player->position[i],100000.0f))return 0;
    float from[3]={before[0],before[1]+8.5f*map->scale,before[2]};
    float to[3]={player->position[0],player->position[1]+8.5f*map->scale,player->position[2]};
    if(collide_segment(map,from,to))++player->collision_count;
    float height;
    if(!c3_field_ground(map,to,&height)){
        memcpy(player->position,before,3*sizeof(float));++player->collision_count;return 1;
    }
    player->position[0]=to[0];player->position[1]=height;player->position[2]=to[2];
    return 1;
}

int c3_field_move(const C3FieldMap *map,C3FieldPlayer *player,int stick_x,int stick_y,
                   uint16_t buttons,float camera_yaw,float seconds)
{
    if(!map||!player||!map->walk||!isfinite(seconds)||seconds<0.0f||!finite_bound(camera_yaw,10000.0f))return 0;
    for(unsigned i=0;i<3;++i)if(!finite_bound(player->position[i],100000.0f))return 0;
    if(stick_x>127)stick_x=127;
    if(stick_x<-127)stick_x=-127;
    if(stick_y>127)stick_y=127;
    if(stick_y<-127)stick_y=-127;
    int x=stick_x*56/127,y=-stick_y*56/127;
    if(!x&&!y) {
        if(buttons&8)y=-56;
        if(buttons&4)y=56;
        if(buttons&1)x=-56;
        if(buttons&2)x=56;
    }
    float speed=fminf(2.0f,hypotf((float)x,(float)y)/28.0f);
    uint32_t motion=speed<0.4f?0u:speed<1.5f?1u:2u;
    if(motion!=player->motion)player->animation_seconds=0.0f;
    player->motion=motion;player->speed=speed;
    /* Bound collision travel after a stalled frame, but keep the sampled
     * motion on the same elapsed clock as scripted actors and cameras. */
    float dt=fminf(seconds,0.25f);player->animation_seconds+=seconds;
    if(!x&&!y)return 1;
    float fx=fabsf((float)x)/28.0f,fy=fabsf((float)y)/28.0f;
    float angle_part=fy<0.001f?1.5707964f:1.5707964f*sinf(fminf(fx/fy,5.0f)/3.1830988f);
    float angle=y>=0?angle_part:3.1415927f-angle_part;
    if(x<0)angle=y>=0?6.2831854f-angle_part:3.1415927f+angle_part;
    player->yaw=camera_yaw+angle;
    float walk=map->walk_distance/map->walk_frames,run=map->run_distance/map->run_frames,step;
    if(speed<=0.4f)step=speed/0.4f*(walk/2.0f);
    else if(speed<=1.0f)step=(speed-0.4f)/0.6f*(walk-walk/2.0f)+walk/2.0f;
    else step=(speed-1.0f)*(run-walk)+walk;
    step*=dt*60.0f;
    float before[3];memcpy(before,player->position,sizeof before);
    player->position[0]+=sinf(player->yaw)*step;player->position[2]+=cosf(player->yaw)*step;
    return c3_field_resolve_position(map,player,before);
}

void c3_field_camera(const C3FieldMap *map,const C3FieldPlayer *player,C3TitleCamera *camera)
{
    if(!map||!player||!camera)return;
    *camera=map->camera;
    if(map->camera_mode==1) {
        memcpy(camera->target,player->position,sizeof(camera->target));
        camera->target[1]+=map->camera_target_height;
        camera->eye[0]=player->position[0]+sinf(map->camera_yaw)*map->camera_distance;
        camera->eye[1]=player->position[1]+map->camera_height;
        camera->eye[2]=player->position[2]+cosf(map->camera_yaw)*map->camera_distance;
    }
}
