#define _POSIX_C_SOURCE 200809L
#include "colosseum3ds/field_story.h"
#include "colosseum3ds/save.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Optional PRIVATE-DATA integration test. This executes the actual native
 * adapter, but it does not render screens/audio, play the opening video, or
 * claim to play a battle. The hero is positioned by this test for interaction.
 * No completed-game save is read; every run creates a fresh WES game. */
static unsigned checks;
static char error[C3_STORY_ERROR];
static FILE *trace;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr,"FAIL %s:%d: %s (%s)\n",__FILE__,__LINE__,#x,error); exit(1); } } while (0)
typedef struct Scene {
    C3FieldStory story;
    C3FieldMap map;
    C3Area area;
    C3FieldPlayer hero;
    C3Title wes, actors[C3_AREA_MAX_ACTORS];
    C3Title *models[C3_AREA_MAX_ACTORS];
    C3AreaCamera camera;
} Scene;
static void path(char out[1024],const char *root,const char *name)
{ CHECK(snprintf(out,1024,"%s/%s",root,name)>0&&strlen(out)<1023); }
static uint32_t flag(C3GameState *game,uint32_t id)
{ uint32_t value=0;CHECK(c3_game_state_get_flag(game,id,&value));return value; }
static int logged(const char *text)
{
    fpos_t position;char line[1024];int found=0;
    CHECK(fflush(trace)==0&&fgetpos(trace,&position)==0);rewind(trace);
    while(fgets(line,sizeof line,trace))if(strstr(line,text)){found=1;break;}
    CHECK(!ferror(trace)&&fsetpos(trace,&position)==0);return found;
}
static void close_scene(Scene *s)
{
    c3_field_story_close(&s->story);c3_field_close(&s->map);c3_area_close(&s->area);
    c3_title_close(&s->wes);c3_area_camera_close(&s->camera);
    for(unsigned i=0;i<C3_AREA_MAX_ACTORS;++i)c3_title_close(s->actors+i);
    memset(s,0,sizeof(*s));
}
static void load_scene(Scene *s,C3GameState *game,const char *root,uint32_t floor,uint32_t previous,uint8_t spawn)
{
    char file[1024];const char *name=floor==1?"outskirt":"shop";
    CHECK(c3_game_state_set_floor(game,floor,previous,spawn));
    char base[64];snprintf(base,sizeof base,"%s.c3f",name);path(file,root,base);
    CHECK(c3_field_load(&s->map,file,error,sizeof error));
    snprintf(base,sizeof base,"%s.c3a",name);path(file,root,base);
    CHECK(c3_area_load(&s->area,file,error,sizeof error));
    snprintf(base,sizeof base,"%s_wes.c3t",name);path(file,root,base);
    CHECK(c3_title_load(&s->wes,file,error,sizeof error));
    for(uint32_t i=0;i<s->area.actor_count;++i){
        if(!s->area.actors[i].file[0])continue;
        path(file,root,s->area.actors[i].file);
        CHECK(c3_title_load(s->actors+i,file,error,sizeof error));s->models[i]=s->actors+i;
    }
    if(floor==2){path(file,root,"shop_camera.c3c");CHECK(c3_area_camera_load(&s->camera,file,error,sizeof error));}
    CHECK(c3_field_spawn(&s->map,spawn,&s->hero));
    path(file,root,floor==1?"outskirt.c3s":"outskirt_shop.c3s");
    CHECK(c3_field_story_load(&s->story,file,game,&s->map,&s->area,&s->hero,&s->wes,s->models,&s->camera,trace,error));
    path(file,root,"willie.c3b");CHECK(c3_field_story_bind_battle(&s->story,file,error));
    CHECK(c3_field_story_prepare(&s->story,error));
    unsigned frame;
    for(frame=0;frame<3600&&!c3_field_story_view(&s->story)->prepared;++frame)
        CHECK(c3_field_story_tick(&s->story,1.0f/60.0f,0,error));
    CHECK(c3_field_story_view(&s->story)->prepared);
    fprintf(trace,"integration_prepared floor=%u frames=%u\n",floor,frame);
    CHECK(c3_field_story_enter(&s->story,error));
}
static void tick(Scene *s,unsigned frame)
{
    const C3FieldStoryView *v=c3_field_story_view(&s->story);
    /* A is supplied only to an existing original window/choice. */
    uint16_t pressed=v->dialogue&&frame%15==0?0x100:0;
    CHECK(c3_field_story_tick(&s->story,1.0f/60.0f,pressed,error));
    CHECK(c3_field_story_sample(&s->story,error));
}
static void until_control(Scene *s,uint32_t expected_message)
{
    unsigned frame;int seen=0;
    for(frame=0;frame<12000;++frame){
        const C3FieldStoryView *v=c3_field_story_view(&s->story);
        if(v->message_id==expected_message)seen=1;
        CHECK(!v->blocked[0]);
        if(v->control)break;
        tick(s,frame);
    }
    CHECK(frame<12000);if(expected_message)CHECK(seen);
    fprintf(trace,"integration_control floor=%u frames=%u expected_message=%05x seen=%d\n",s->area.floor,frame,expected_message,seen);
}
static void door(Scene *s,uint32_t source,uint32_t floor,uint32_t spawn)
{
    const C3AreaLink *link=c3_area_link(&s->area,1,source);
    CHECK(link&&link->script==0x05960004&&link->args[0]==floor&&link->args[1]==spawn);
    fprintf(trace,"integration_dispatch_source_door floor=%u source=%u target=%u spawn=%u geometry_not_walked=1\n",s->area.floor,source,floor,spawn);
    CHECK(c3_field_story_transition(&s->story,source,error));
    unsigned frame;
    for(frame=0;frame<3600&&!c3_field_story_view(&s->story)->next_floor;++frame)tick(s,frame);
    CHECK(frame<3600&&c3_field_story_view(&s->story)->next_floor==floor&&c3_field_story_view(&s->story)->next_spawn==spawn);
}
static uint16_t counting_random(void *context){unsigned *draws=context;return (uint16_t)(0x1234u+(*draws)++);}
static int accept_world_map(uint32_t floor){return floor==1||floor==2||floor==0x38e;}
static void unchanged_party(C3GameState *game,const C3GameSummary *original)
{
    C3GameSummary now;CHECK(c3_game_state_summary(game,&now));
    CHECK(now.party_count==original->party_count);
    for(unsigned i=0;i<now.party_count;++i){
        CHECK(now.party[i].species==original->party[i].species);
        CHECK(now.party[i].level==original->party[i].level);
        CHECK(now.party[i].hp==original->party[i].hp&&now.party[i].max_hp==original->party[i].max_hp);
        CHECK(now.party[i].personality==original->party[i].personality);
    }
}
/* Return ownership of the reloaded state so the caller can reconstruct the
 * scene from the saved checkpoint instead of continuing its live VM. */
static C3GameState save_new_game(C3GameState *game,const char *prepared)
{
    char directory[]="/tmp/c3-field-story-save-XXXXXX",file[1024];C3GameState restored={0};C3GameSummary summary,before;
    CHECK(c3_game_state_summary(game,&before));
    CHECK(mkdtemp(directory)!=NULL);path(file,directory,"fresh.gci");
    CHECK(c3_game_state_save(game,file,error));CHECK(c3_game_state_load(&restored,file,prepared,error));
    CHECK(c3_game_state_summary(&restored,&summary)&&summary.floor==2&&!strcmp(summary.name,"WES"));
    CHECK(summary.previous_floor==before.previous_floor&&summary.spawn==before.spawn&&summary.saved_count==before.saved_count+1);
    CHECK(summary.party_count==2&&summary.party[0].level==26&&summary.party[1].level==25);
    CHECK(flag(&restored,0x3db)==1&&flag(&restored,0x3dc)==1&&flag(&restored,0x3dd)==0&&flag(&restored,0x3de)==0);
    CHECK(unlink(file)==0);CHECK(rmdir(directory)==0);
    fprintf(trace,"integration_save fresh_new_game_roundtrip=verified floor=2 battle_started=0 battle_completed=0\n");
    return restored;
}
/* Small synthetic programs exercise the production adapter without private
 * assets. These are structural regression fixtures, never installed game data. */
typedef struct Program {uint8_t data[1024];unsigned size;} Program;
static void be16(uint8_t *p,uint32_t v){p[0]=(uint8_t)(v>>8);p[1]=(uint8_t)v;}
static void be32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(24-8*i));}
static void le32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t bits(float f){uint32_t v;memcpy(&v,&f,4);return v;}
static void rotate_reference(float v[3],const float e[3])
{
    /* Independent axis-by-axis oracle for the source XYZ convention. */
    float y=v[1],z=v[2];v[1]=cosf(e[0])*y-sinf(e[0])*z;v[2]=sinf(e[0])*y+cosf(e[0])*z;
    float x=v[0];z=v[2];v[0]=cosf(e[1])*x+sinf(e[1])*z;v[2]=-sinf(e[1])*x+cosf(e[1])*z;
    x=v[0];y=v[1];v[0]=cosf(e[2])*x-sinf(e[2])*y;v[1]=sinf(e[2])*x+cosf(e[2])*y;
}
static void raw(Program *p,uint8_t byte){CHECK(p->size<sizeof p->data);p->data[p->size++]=byte;}
static void push(Program *p,uint32_t value)
{raw(p,8);raw(p,2);for(unsigned i=0;i<4;++i)raw(p,(uint8_t)(value>>(24-8*i)));}
static void drop(Program *p){raw(p,3);raw(p,0x82);raw(p,0);raw(p,1);}
static void call_end(Program *p,unsigned argc,int discard)
{raw(p,0x25);raw(p,0);raw(p,1);raw(p,0);raw(p,(uint8_t)(argc+1));if(discard)drop(p);}
static void native(Program *p,uint32_t id,unsigned argc,const uint32_t *args,int discard)
{push(p,0);push(p,id);for(unsigned i=0;i<argc;++i)push(p,args[i]);call_end(p,argc,discard);}
static unsigned bank(uint8_t *out,unsigned id,const Program programs[8])
{
    unsigned size=56;memset(out,0,56);be16(out,id);be16(out+4,8);be32(out+16,56);
    for(unsigned i=0;i<8;++i){be32(out+24+4*i,size);memcpy(out+size,programs[i].data,programs[i].size);size+=programs[i].size;out[size++]=5;}
    return size;
}
static void write_programs(const char *file,const Program programs[8])
{
    uint8_t bytes[16384]={0},common[256]={0};
    unsigned a=bank(bytes+96,0x100,programs),b=64;
    be16(common,0x596);be16(common+4,10);be32(common+16,64);
    for(unsigned i=0;i<10;++i){be32(common+24+4*i,b);common[b++]=5;}
    /* Synthetic common fade function9, used by production departure. */
    Program fade={0};uint32_t args[2]={3,bits(.1f)};native(&fade,0x1c,2,args,1);
    args[0]=1;native(&fade,0x1d,1,args,1);b=73;memcpy(common+b,fade.data,fade.size);b+=fade.size;common[b++]=5;
    CHECK(96+a+b<=sizeof bytes);memcpy(bytes+96+a,common,b);
    memcpy(bytes,"C3S1",4);le32(bytes+4,1);le32(bytes+8,96+a+b);le32(bytes+12,1);
    le32(bytes+16,2);le32(bytes+24,64);le32(bytes+28,96);
    le32(bytes+64,0x100);le32(bytes+68,96);le32(bytes+72,a);
    le32(bytes+80,0x596);le32(bytes+84,96+a);le32(bytes+88,b);
    FILE *f=fopen(file,"wb");CHECK(f);CHECK(fwrite(bytes,1,96+a+b,f)==96+a+b);CHECK(!fclose(f));
}
static void synthetic_game(C3GameState *game)
{
    uint8_t *body=calloc(1,C3_SAVE_BODY_SIZE),*image=calloc(1,C3_SAVE_GCI_SIZE),random[20]={0};CHECK(body&&image);
    memcpy(body,"\x0b\x03\x02\x02",4);be32(body+0x1c,1);be16(body+0x70,'T');
    be16(body+0xa0,1);memset(body+0xa8,1,4);body[0x100]=1;be16(body+0x12a,10);be16(body+0x12c,10);
    memcpy(image,"GC6E01",6);memcpy(image+8,"pokemon_colosseum",18);be16(image+0x38,48);
    for(unsigned i=0;i<3;++i)CHECK(c3_save_encode_slot(body,C3_SAVE_BODY_SIZE,i+1,random,image+0x6040+i*C3_SAVE_SLOT_SIZE,C3_SAVE_SLOT_SIZE)==C3_SAVE_OK);
    CHECK(c3_game_state_decode(game,image,C3_SAVE_GCI_SIZE,NULL,error));free(image);free(body);
}
static void synthetic_model(C3Title *model)
{
    model->fps=30;model->clip_count=4;model->frame_count=31;model->bone_count=1;model->batch_count=1;model->vertex_count=1;
    model->format_version=3;model->material_frame_bytes=92;model->frame_bytes=48+92+44;
    model->file_bytes=model->frame_bytes*31;model->file_data=calloc(1,model->file_bytes);
    model->vertices=calloc(1,sizeof(*model->vertices));model->draw_vertices=calloc(1,sizeof(*model->draw_vertices));
    model->batches=calloc(1,sizeof(*model->batches));CHECK(model->file_data&&model->vertices&&model->draw_vertices&&model->batches);
    model->vertices[0].pos[0]=1;model->vertices[0].normal[2]=1;model->batches[0].count=1;
    for(unsigned i=0;i<4;++i)model->clips[i]=(C3TitleClip){0,31,1};
    for(unsigned i=0;i<31;++i){uint8_t *frame=model->file_data+i*model->frame_bytes;
        for(unsigned j=0;j<3;++j){le32(frame+(j*4+j)*4,bits(1));le32(frame+48+(20+j)*4,bits((float)(j+1)));}
        le32(frame+48+20,bits(1));le32(frame+140+4*7,bits(45));le32(frame+140+4*8,bits(.1f));
        le32(frame+140+4*9,bits(100));le32(frame+140+4*10,bits(1));
    }
}
static void synthetic_load(Scene *scene,C3GameState *game,const char *file,const Program programs[8],
                           uint32_t init,uint32_t pre,uint32_t main_script)
{
    write_programs(file,programs);scene->map.floor=scene->area.floor=1;scene->area.group=29;
    scene->map.scale=1;scene->map.radius=.4f;scene->map.walk_distance=.9f;scene->map.run_distance=1.3f;
    scene->map.walk_frames=scene->map.run_frames=30;scene->map.idle_clip=0;scene->map.walk_clip=1;scene->map.run_clip=2;
    scene->area.actor_count=1;scene->area.actors=calloc(1,sizeof(*scene->area.actors));CHECK(scene->area.actors);
    C3AreaActor *a=scene->area.actors;a->index=3;a->resource=1;a->flags=0x80000000u;a->radius=.4f;a->walk_distance=.9f;a->collision_type=1;
    a->motions[0]=0;a->motions[1]=1;a->motions[2]=2;scene->area.init_script=init;scene->area.pre_script=pre;scene->area.main_script=main_script;
    synthetic_model(&scene->wes);synthetic_model(scene->actors);scene->models[0]=scene->actors;
    scene->camera.frame_count=11;scene->camera.fps=1;scene->camera.frames=calloc(11*11,sizeof(float));CHECK(scene->camera.frames);
    for(unsigned i=0;i<11;++i){float *f=scene->camera.frames+i*11;f[0]=(float)i;f[2]=1;f[7]=45;f[8]=.1f;f[9]=100;f[10]=1;}
    CHECK(c3_field_story_load(&scene->story,file,game,&scene->map,&scene->area,&scene->hero,&scene->wes,scene->models,&scene->camera,NULL,error));
    CHECK(c3_field_story_prepare(&scene->story,error));
    for(unsigned i=0;i<10&&!c3_field_story_view(&scene->story)->prepared;++i)CHECK(c3_field_story_tick(&scene->story,0,0,error));
    CHECK(c3_field_story_view(&scene->story)->prepared);
}
static void synthetic_regressions(void)
{
    char directory[]="/tmp/c3-field-story-unit-XXXXXX",file[1024];CHECK(mkdtemp(directory));path(file,directory,"unit.c3s");
    C3GameState game={0};synthetic_game(&game);Scene *s=calloc(1,sizeof(*s));CHECK(s);
    Program code[8]={0};uint32_t args[6]={3,0};
    /* An absent init cannot suppress a present pre hook. */
    native(code+2,0x64,2,args,1);synthetic_load(s,&game,file,code,0,0x1000002,0);
    CHECK(!c3_field_story_actor(&s->story,3)->visible);close_scene(s);
    /* Menu callbacks queue work without running it inside the paused menu. */
    memset(code,0,sizeof code);native(code+3,0x1f,0,NULL,1);args[0]=3;args[1]=0;native(code+5,0x64,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));
    for(unsigned i=0;i<10&&!c3_field_story_view(&s->story)->control;++i)CHECK(c3_field_story_tick(&s->story,.1f,0,error));
    CHECK(c3_field_story_view(&s->story)->control&&c3_field_story_actor(&s->story,3)->visible);
    CHECK(!c3_field_story_menu_event(&s->story,0x10000ff,error));
    CHECK(c3_field_story_menu_event(&s->story,0x1000005,error));
    CHECK(!c3_field_story_view(&s->story)->control&&c3_field_story_actor(&s->story,3)->visible);
    c3_field_story_cancel_menu_event(&s->story);CHECK(c3_field_story_tick(&s->story,.1f,0,error));
    CHECK(c3_field_story_view(&s->story)->control&&c3_field_story_actor(&s->story,3)->visible);
    CHECK(c3_field_story_menu_event(&s->story,0x1000005,error));
    CHECK(c3_field_story_tick(&s->story,.1f,0,error));CHECK(!c3_field_story_actor(&s->story,3)->visible);close_scene(s);
    memset(code,0,sizeof code);args[0]=2;args[1]=0;native(code+3,0x5f,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));
    for(unsigned i=0;i<10&&!c3_field_story_view(&s->story)->next_floor;++i)CHECK(c3_field_story_tick(&s->story,.1f,0,error));
    CHECK(c3_field_story_view(&s->story)->next_floor==2&&c3_field_story_view(&s->story)->fade==0);close_scene(s);
    /* Source44 turns over time, and source3E remains pending until it ends. */
    memset(code,0,sizeof code);args[0]=29;args[1]=3;args[2]=bits(90);native(code+3,0x44,3,args,1);
    args[2]=1;native(code+3,0x3e,3,args,1);args[0]=3;args[1]=0;native(code+3,0x64,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));
    CHECK(c3_field_story_tick(&s->story,0,0,error));C3StoryActor *actor=c3_field_story_actor(&s->story,3);CHECK(actor->visible&&actor->pose.yaw==0);
    CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));CHECK(actor->visible&&fabsf(actor->pose.yaw-.15707964f)<1e-6f);
    CHECK(c3_field_story_tick(&s->story,8.f/60,0,error));CHECK(actor->visible&&fabsf(actor->pose.yaw-1.4137167f)<1e-5f);
    CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));CHECK(!actor->visible&&fabsf(actor->pose.yaw-1.5707964f)<1e-6f);close_scene(s);
    for(unsigned facing=0;facing<2;++facing){
        memset(code,0,sizeof code);args[0]=29;args[1]=3;args[2]=facing;args[3]=bits(.01f);args[4]=args[5]=0;
        native(code+3,0x3d,6,args,1);args[2]=1;native(code+3,0x3e,3,args,1);args[0]=3;args[1]=0;native(code+3,0x64,2,args,1);
        synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));CHECK(c3_field_story_tick(&s->story,0,0,error));
        actor=c3_field_story_actor(&s->story,3);CHECK(actor->pose.yaw==0);CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));
        CHECK(fabsf(actor->pose.position[0]-.01f)<1e-6f);
        if(facing){CHECK(actor->visible&&actor->pose.yaw>0);CHECK(c3_field_story_tick(&s->story,.2f,0,error));CHECK(!actor->visible);}
        else CHECK(!actor->visible&&actor->pose.yaw==0);
        close_scene(s);
    }
    /* Camera, animation, waits and fade share a >250ms active-frame interval. */
    memset(code,0,sizeof code);args[0]=29;args[1]=0xe51800;args[2]=args[3]=0;native(code+3,0x2a,4,args,1);
    args[0]=3;args[1]=bits(1);native(code+3,0x1c,2,args,1);args[0]=bits(.8f);native(code+3,0x9d,1,args,1);
    args[0]=3;args[1]=0;native(code+3,0x64,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));CHECK(c3_field_story_tick(&s->story,0,0,error));
    CHECK(c3_field_story_tick(&s->story,.5f,0,error));const C3FieldStoryView *v=c3_field_story_view(&s->story);actor=c3_field_story_actor(&s->story,3);
    CHECK(fabsf(v->camera_seconds-.5f)<1e-6f&&fabsf(v->camera.eye[0]-.5f)<1e-6f&&fabsf(v->fade-.5f)<1e-6f);
    CHECK(fabsf(actor->pose.animation_seconds-.5f)<1e-6f&&actor->visible);
    CHECK(c3_field_story_tick(&s->story,.31f,0,error));CHECK(!actor->visible&&fabsf(v->camera_seconds-.81f)<1e-6f);close_scene(s);
    /* A waiting parent's result survives the child slot being reused by an
     * earlier actor task before the parent gets its next scheduler turn. */
    memset(code,0,sizeof code);push(code,1);raw(code,0x24);raw(code,0);raw(code,0);raw(code,0);raw(code,1);
    args[0]=0x1000007;memset(args+1,0,5*sizeof(*args));native(code,0x03,6,args,1);
    push(code+3,0);push(code+3,0x24);args[0]=0x1000006;native(code+3,0xd1,5,args,0);call_end(code+3,1,1);
    push(code+6,0);push(code+6,3);raw(code+6,0x1f);raw(code+6,0x82);raw(code+6,0x42);drop(code+6);
    synthetic_load(s,&game,file,code,0,0,0x1000003);s->area.actors[0].move_script=0x1000000;
    CHECK(c3_field_story_enter(&s->story,error));CHECK(c3_field_story_tick(&s->story,0,0,error));CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));
    CHECK(c3_field_story_view(&s->story)->camera_active);close_scene(s);
    /* Retail floor init calls D1 with only its scriptID. Optional trailing
     * child parameters are zero-filled by the original VM convention. */
    memset(code,0,sizeof code);args[0]=0x1000007;native(code+3,0xd1,1,args,1);
    args[0]=3;args[1]=0;native(code+7,0x64,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));
    CHECK(c3_field_story_tick(&s->story,0,0,error));CHECK(!c3_field_story_actor(&s->story,3)->visible);
    CHECK(c3_field_story_tick(&s->story,0,0,error));close_scene(s);
    /* Consume one movement step across both original waypoints, while3E
     * continues waiting for the final source-rate turn. */
    memset(code,0,sizeof code);args[0]=29;args[1]=3;args[2]=2;native(code+3,0x48,3,args,1);
    args[2]=bits(.01f);args[3]=args[4]=0;native(code+3,0x49,5,args,1);
    args[2]=bits(.02f);args[4]=bits(.01f);native(code+3,0x49,5,args,1);
    args[2]=0;native(code+3,0x4a,3,args,1);args[2]=1;native(code+3,0x3e,3,args,1);
    args[0]=3;args[1]=0;native(code+3,0x64,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));
    CHECK(c3_field_story_tick(&s->story,0,0,error));CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));
    actor=c3_field_story_actor(&s->story,3);CHECK(actor->visible&&fabsf(actor->pose.position[0]-.02f)<1e-6f&&fabsf(actor->pose.position[2]-.01f)<1e-6f);
    CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));CHECK(actor->visible);
    CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));CHECK(!actor->visible&&fabsf(actor->pose.yaw-.78539816f)<1e-6f);close_scene(s);
    /* Event cleanup cannot grant controller input until the blocking return
     * blend finishes and the source main thread executes native1F again. */
    memset(code,0,sizeof code);native(code+3,0x1f,0,args,1);native(code+3,0x20,0,args,1);native(code+3,0x21,0,args,1);
    args[0]=bits(.5f);args[1]=1;native(code+3,0xb3,2,args,1);native(code+3,0x1f,0,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);s->map.camera_mode=1;s->map.camera_height=4;s->map.camera_distance=6;
    CHECK(c3_field_story_enter(&s->story,error));CHECK(c3_field_story_tick(&s->story,0,0,error));CHECK(c3_field_story_view(&s->story)->control);
    CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));CHECK(!c3_field_story_view(&s->story)->control);
    CHECK(c3_field_story_tick(&s->story,.25f,0,error));CHECK(!c3_field_story_view(&s->story)->control);
    CHECK(c3_field_story_tick(&s->story,.25f,0,error));CHECK(c3_field_story_view(&s->story)->control&&!c3_field_story_view(&s->story)->camera_active);close_scene(s);
    /* TalkWallThrough bypasses the talk ray only. The same solid wall still
     * blocks an actor with the bit clear; native76 can disable its CCD object. */
    for(unsigned mode=0;mode<3;++mode){
        memset(code,0,sizeof code);args[0]=0;args[1]=mode==2?0:1;native(code+3,0x76,2,args,1);native(code+3,0x1f,0,args,1);
        args[0]=3;args[1]=0;native(code+7,0x64,2,args,1);
        synthetic_load(s,&game,file,code,0,0,0x1000003);s->map.scale=.1f;s->map.object_count=1;s->map.wall_count=1;
        s->map.walls=calloc(1,sizeof(*s->map.walls));CHECK(s->map.walls);
        const float face[9]={-2,-2,.5f,2,-2,.5f,0,2,.5f};memcpy(s->map.walls[0].vertices,face,sizeof face);s->map.walls[0].normal[2]=1;
        s->area.actors[0].talk_script=0x1000007;if(mode==1)s->area.actors[0].flags|=0x10000000;
        c3_field_story_actor(&s->story,3)->pose.position[2]=1;
        CHECK(c3_field_story_enter(&s->story,error));CHECK(c3_field_story_tick(&s->story,0,0,error));
        CHECK(c3_field_story_view(&s->story)->control);CHECK(c3_field_story_interact(&s->story,error));
        if(!mode)CHECK(c3_field_story_view(&s->story)->control&&c3_field_story_actor(&s->story,3)->visible);
        else {CHECK(!c3_field_story_view(&s->story)->control);CHECK(c3_field_story_tick(&s->story,0,0,error));CHECK(!c3_field_story_actor(&s->story,3)->visible);}
        CHECK(c3_field_object_enabled(&s->map,0)==(mode!=2));close_scene(s);
    }
    /* Source type7 adds ancestor Eulers and rotates local translation without
     * parent scale. A generic parent*child affine product fails this fixture. */
    memset(code,0,sizeof code);args[0]=0;args[1]=100;args[2]=29;args[3]=3;args[4]=1;native(code+3,0x46,5,args,1);
    args[2]=bits(2);args[3]=bits(3);args[4]=bits(4);native(code+3,0x3b,5,args,1);
    args[2]=bits(20);args[3]=bits(90);args[4]=0;native(code+3,0x3c,5,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);s->area.actors[0].resource=0x010b0400;s->area.actors[0].part1_bone_plus_one=1;
    actor=c3_field_story_actor(&s->story,3);actor->pose.yaw=.6f;actor->pose.position[0]=5;actor->pose.position[1]=6;actor->pose.position[2]=7;
    const float joint_euler[3]={.02f,-.01f,.03f},joint_scale[3]={2,3,4};
    for(unsigned frame=0;frame<31;++frame){uint8_t *data=s->actors[0].file_data+frame*s->actors[0].frame_bytes;
        for(unsigned col=0;col<3;++col){float v[3]={0};v[col]=joint_scale[col];rotate_reference(v,joint_euler);
            for(unsigned row=0;row<3;++row)le32(data+(row*4+col)*4,bits(v[row]));}
        for(unsigned row=0;row<3;++row)le32(data+(row*4+3)*4,bits((float)(row+1)));
    }
    CHECK(c3_field_story_enter(&s->story,error));CHECK(c3_field_story_tick(&s->story,0,0,error));
    float origin[3]={1,2,3},yaw[3]={0,.6f,0},parent_euler[3]={.02f,.59f,.03f},local[3]={2,3,4};
    rotate_reference(origin,yaw);rotate_reference(local,parent_euler);
    for(unsigned row=0;row<3;++row)origin[row]+=actor->pose.position[row]+local[row];
    float vertex[3]={2,0,0},child_euler[3]={.02f+20.f*3.14159265358979323846f/180.f,.59f+1.57079632679f,.03f};
    rotate_reference(vertex,child_euler);
    for(unsigned repeat=0;repeat<2;++repeat){CHECK(c3_field_story_sample(&s->story,error));
        for(unsigned row=0;row<3;++row){CHECK(fabsf(s->hero.position[row]-origin[row])<1e-4f);
            CHECK(fabsf(s->wes.draw_vertices[0].pos[row]-origin[row]-vertex[row])<1e-4f);}}
    close_scene(s);
    /* Exact source0 mapping and material owning-joint world transform. */
    memset(code,0,sizeof code);args[0]=0;args[1]=100;args[2]=args[3]=0;args[4]=1;native(code+3,0x33,5,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));CHECK(c3_field_story_tick(&s->story,0,0,error));
    actor=c3_field_story_actor(&s->story,3);actor->pose.yaw=1.57079632679f;
    actor->pose.position[0]=10;actor->pose.position[1]=20;actor->pose.position[2]=30;
    for(unsigned i=0;i<2;++i){CHECK(c3_field_story_sample(&s->story,error));CHECK(s->wes.active_clip==3);
        const float *origin=s->actors[0].pose.materials[0].specular_origin;
        CHECK(fabsf(origin[0]-13)<1e-5f&&fabsf(origin[1]-22)<1e-5f&&fabsf(origin[2]-29)<1e-5f);}
    close_scene(s);
    synthetic_load(s,&game,file,code,0,0,0x1000003);s->wes.clip_count=3;CHECK(c3_field_story_enter(&s->story,error));
    CHECK(!c3_field_story_tick(&s->story,0,0,error)&&strstr(error,"source motion 0 is not prepared"));close_scene(s);
    static const struct {uint32_t id,argc,which;} invalid[]={
        {0x1c,2,1},{0x26,3,0},{0x27,3,1},{0x28,3,2},{0x29,3,0},{0x3a,6,2},
        {0x3b,5,4},{0x3c,5,3},{0x3d,6,4},{0x44,3,2},{0x45,3,2},{0x65,4,1},
        {0x7f,3,2},{0x8c,5,2},{0x9d,1,0},{0x30,4,3},{0x49,5,4},{0xb3,2,0}
    };
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];++i)for(unsigned inf=0;inf<2;++inf){
        memset(code,0,sizeof code);memset(args,0,sizeof args);args[0]=29;args[1]=3;
        if(invalid[i].id==0x1c)args[0]=3;
        if(invalid[i].id==0x65)args[0]=3;
        args[invalid[i].which]=inf?0x7f800000u:0x7fc00000u;native(code+3,invalid[i].id,invalid[i].argc,args,1);
        synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));
        C3FieldStoryView before=*c3_field_story_view(&s->story);C3StoryActor prior=*c3_field_story_actor(&s->story,3);
        C3FieldPlayer hero=s->hero;C3FieldMap map=s->map;
        CHECK(!c3_field_story_tick(&s->story,0,0,error)&&strstr(error,"finite range"));
        CHECK(!memcmp(&before,c3_field_story_view(&s->story),sizeof before));
        CHECK(!memcmp(&prior,c3_field_story_actor(&s->story,3),sizeof prior));
        CHECK(!memcmp(&hero,&s->hero,sizeof hero)&&!memcmp(&map,&s->map,sizeof map));close_scene(s);
    }
    memset(code,0,sizeof code);args[0]=3;args[1]=args[2]=0;native(code+3,0x65,3,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));
    CHECK(!c3_field_story_tick(&s->story,0,0,error)&&strstr(error,"expects 4 arguments"));close_scene(s);
    memset(code,0,sizeof code);args[0]=10;args[1]=1;args[2]=0;native(code+3,0x98,3,args,1);args[0]=3;args[1]=0;native(code+3,0x64,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));
    uint8_t *saved=malloc(C3_SAVE_BODY_SIZE);CHECK(saved);memcpy(saved,c3_game_state_body(&game,NULL),C3_SAVE_BODY_SIZE);
    for(unsigned i=0;i<10;++i)CHECK(c3_field_story_tick(&s->story,.1f,0x300,error));
    v=c3_field_story_view(&s->story);CHECK(v->battle_id==10&&v->blocked[0]&&!v->control&&c3_field_story_actor(&s->story,3)->visible);
    CHECK(!memcmp(saved,c3_game_state_body(&game,NULL),C3_SAVE_BODY_SIZE));free(saved);close_scene(s);
    /* Native A4 turns toward a point with the same turn machinery as 44;
     * the y word is ignored. */
    memset(code,0,sizeof code);args[0]=29;args[1]=3;args[2]=bits(1);args[3]=bits(50);args[4]=0;args[5]=bits(1);
    native(code+3,0xa4,6,args,1);args[2]=1;native(code+3,0x3e,3,args,1);args[0]=3;args[1]=0;native(code+3,0x64,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));
    CHECK(c3_field_story_tick(&s->story,0,0,error));actor=c3_field_story_actor(&s->story,3);CHECK(actor->visible&&actor->pose.yaw==0);
    CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));CHECK(actor->visible&&fabsf(actor->pose.yaw-.15707964f)<1e-6f);
    CHECK(c3_field_story_tick(&s->story,8.f/60,0,error));CHECK(actor->visible&&fabsf(actor->pose.yaw-1.4137167f)<1e-5f);
    CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));CHECK(!actor->visible&&fabsf(actor->pose.yaw-1.5707964f)<1e-6f);close_scene(s);
    /* Native A5 turns toward another person: here the hero at -x. A missing
     * target (0,101 without a partner model) leaves the person alone. */
    memset(code,0,sizeof code);args[0]=29;args[1]=3;args[2]=0;args[3]=101;args[4]=bits(1);native(code+3,0xa5,5,args,1);
    args[3]=100;native(code+3,0xa5,5,args,1);args[2]=1;native(code+3,0x3e,3,args,1);args[0]=3;args[1]=0;native(code+3,0x64,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);s->hero.position[0]=-1;CHECK(c3_field_story_enter(&s->story,error));
    CHECK(c3_field_story_tick(&s->story,0,0,error));actor=c3_field_story_actor(&s->story,3);CHECK(actor->visible&&actor->pose.yaw==0);
    CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));CHECK(actor->visible&&fabsf(actor->pose.yaw+.15707964f)<1e-6f);
    CHECK(c3_field_story_tick(&s->story,8.f/60,0,error));CHECK(actor->visible&&fabsf(actor->pose.yaw-4.8694687f)<1e-5f);
    /* The turn keeps the yaw in [0, 2pi): -90 degrees is stored as 270. */
    CHECK(c3_field_story_tick(&s->story,2.f/60,0,error));CHECK(!actor->visible&&fabsf(actor->pose.yaw-4.7123890f)<1e-5f);close_scene(s);
    /* Native B1 walks the actor to one unit (0.1 x 10) from the hero along
     * the line between them and stops there; already inside that distance
     * it does not move. E8 without a follower returns at once. */
    memset(code,0,sizeof code);args[0]=29;args[1]=3;native(code+3,0xe8,2,args,1);
    args[2]=0;args[3]=100;args[4]=bits(.1f);args[5]=1;native(code+3,0xb1,6,args,1);
    args[2]=1;native(code+3,0x3e,3,args,1);args[0]=3;args[1]=0;native(code+3,0x64,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);s->hero.position[0]=5;CHECK(c3_field_story_enter(&s->story,error));
    actor=c3_field_story_actor(&s->story,3);
    {unsigned steps;for(steps=0;steps<2000&&actor->visible;++steps)CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));
     CHECK(steps>1&&steps<2000&&fabsf(actor->pose.position[0]-4)<1e-3f&&fabsf(actor->pose.position[2])<1e-3f);}
    close_scene(s);
    synthetic_load(s,&game,file,code,0,0,0x1000003);s->hero.position[0]=.5f;CHECK(c3_field_story_enter(&s->story,error));
    actor=c3_field_story_actor(&s->story,3);CHECK(c3_field_story_tick(&s->story,1.f/60,0,error));
    CHECK(!actor->visible&&actor->pose.position[0]==0);close_scene(s);
    /* Native 23 writes the partner-present byte; 4C/4D without arguments,
     * 31, 7E, 83, 89, DA, A1 and B4 complete without waiting. */
    memset(code,0,sizeof code);native(code+3,0x4c,0,NULL,1);native(code+3,0x4d,0,NULL,1);native(code+3,0x31,0,NULL,1);
    args[0]=1;native(code+3,0x23,1,args,1);args[0]=1;args[1]=1;native(code+3,0x7e,2,args,1);
    args[0]=29;args[1]=3;args[2]=1;native(code+3,0x83,3,args,1);args[0]=3;args[1]=1000;args[2]=0;native(code+3,0x89,3,args,1);
    args[0]=0xb7;native(code+3,0xda,1,args,1);args[0]=2;args[1]=3;args[2]=1;native(code+3,0xa1,3,args,1);
    native(code+3,0xb4,0,NULL,1);args[0]=3;args[1]=0;native(code+3,0x64,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(!c3_game_state_partner(&game));CHECK(c3_field_story_enter(&s->story,error));
    CHECK(c3_field_story_tick(&s->story,0,0,error));CHECK(!c3_field_story_actor(&s->story,3)->visible&&c3_game_state_partner(&game));
    CHECK(c3_game_state_set_partner(&game,0)&&!c3_game_state_partner(&game));close_scene(s);
    /* Native 02 needs the bound game RNG and draws twice, low half first. */
    memset(code,0,sizeof code);args[0]=7;native(code+3,0x02,1,args,1);args[0]=3;args[1]=0;native(code+3,0x64,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));
    CHECK(!c3_field_story_tick(&s->story,0,0,error)&&strstr(error,"needs the game RNG"));close_scene(s);
    synthetic_load(s,&game,file,code,0,0,0x1000003);unsigned draws=0;c3_field_story_set_random(&s->story,counting_random,&draws);
    CHECK(c3_field_story_enter(&s->story,error));CHECK(c3_field_story_tick(&s->story,0,0,error));
    CHECK(draws==2&&!c3_field_story_actor(&s->story,3)->visible);close_scene(s);
    memset(code,0,sizeof code);args[0]=0;native(code+3,0x02,1,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);c3_field_story_set_random(&s->story,counting_random,&draws);
    CHECK(c3_field_story_enter(&s->story,error));CHECK(!c3_field_story_tick(&s->story,0,0,error)&&strstr(error,"zero limit")&&draws==2);close_scene(s);
    /* Native 66 starts an encounter directly and shares the 98 result path;
     * native 82 waits its substitute two seconds. */
    memset(code,0,sizeof code);args[0]=14;native(code+3,0x66,1,args,1);args[0]=0x3c9;args[1]=0;args[2]=0x7f;native(code+3,0x82,3,args,1);
    args[0]=3;args[1]=0;native(code+3,0x64,2,args,1);
    synthetic_load(s,&game,file,code,0,0,0x1000003);CHECK(c3_field_story_enter(&s->story,error));
    for(unsigned i=0;i<10;++i)CHECK(c3_field_story_tick(&s->story,.1f,0x300,error));
    {uint32_t encounter=0;v=c3_field_story_view(&s->story);
     CHECK(v->battle_id==14&&v->blocked[0]&&c3_field_story_battle_pending(&s->story,&encounter)&&encounter==14);
     CHECK(c3_field_story_battle_result(&s->story,2,error)&&!c3_field_story_battle_pending(&s->story,NULL));
     for(unsigned i=0;i<19;++i)CHECK(c3_field_story_tick(&s->story,.1f,0,error)&&c3_field_story_actor(&s->story,3)->visible);
     for(unsigned i=0;i<3;++i)CHECK(c3_field_story_tick(&s->story,.1f,0,error));
     CHECK(!c3_field_story_actor(&s->story,3)->visible);}
    close_scene(s);
    free(s);c3_game_state_free(&game);CHECK(!unlink(file));CHECK(!rmdir(directory));
    printf("field-story synthetic regressions: %u checks passed\n",checks);
}
int main(int argc,char **argv)
{
    synthetic_regressions();
    if(argc<2){puts("field-story private integration skipped: supply privately prepared boot directory");return 0;}
    trace=argc>2?fopen(argv[2],"w+"):tmpfile();CHECK(trace!=NULL);
    fprintf(trace,"integration_scope=headless_actual_runtime private_original_data=1 visual_evidence=0 battle_backend=0\n");
    char prepared[1024];path(prepared,argv[1],"newgame.c3g");
    C3GameState game={0};C3GameSummary original;Scene *scene=calloc(1,sizeof(*scene));CHECK(scene!=NULL);
    CHECK(c3_game_state_new(&game,"WES",0x12345678,prepared,error));
    CHECK(c3_game_state_summary(&game,&original)&&original.party_count==2);
    CHECK(original.party[0].level==26&&original.party[1].level==25&&original.money==10000);
    CHECK(flag(&game,0x3db)==0&&flag(&game,0x3dc)==0&&flag(&game,0x3dd)==0&&flag(&game,0x3de)==0);
    /* Explicit audit checkpoint, not a claim that this test played movies. */
    CHECK(c3_game_state_set_flag(&game,0x8d0,1));CHECK(c3_game_state_set_flag(&game,0xaf8,1));
    fprintf(trace,"integration_checkpoint=fresh_after_opening hideout_video_played_by_test=0 arrival_video_played_by_test=0\n");
    load_scene(scene,&game,argv[1],1,0,0);until_control(scene,0);
    CHECK(c3_field_story_actor(&scene->story,2)->visible&&!c3_field_story_actor(&scene->story,3)->visible);
    CHECK(flag(&game,0x3dc)==0&&flag(&game,0x3de)==0);unchanged_party(&game,&original);
    /* Enter the actual original main-script rectangle before dispatching a
     * doorway. This fixture exercises the mandatory scene8 and its concurrent
     * children4..7; it is not a claim of controller-path or visual coverage. */
    scene->hero.position[0]=-2;scene->hero.position[1]=0;scene->hero.position[2]=-1;
    fprintf(trace,"integration_fixture_hero_approach_rectangle x=-2 y=0 z=-1\n");
    unsigned approach_frame;int first_line=0,second_line=0;
    for(approach_frame=0;approach_frame<18000;++approach_frame){
        tick(scene,approach_frame);const C3FieldStoryView *v=c3_field_story_view(&scene->story);
        CHECK(!v->blocked[0]);if(v->message_id==0x515d)first_line=1;if(v->message_id==0x5146)second_line=1;
        if(flag(&game,0x3da)&&v->control)break;
    }
    CHECK(approach_frame<18000&&first_line&&second_line);
    CHECK(!c3_field_story_actor(&scene->story,0)->visible&&!c3_field_story_actor(&scene->story,1)->visible&&!c3_field_story_actor(&scene->story,2)->visible);
    CHECK(!c3_field_object_enabled(&scene->map,10));
    CHECK(logged("id=46 argc=5")&&logged("id=b3 argc=2"));
    fprintf(trace,"integration_approach_scene_complete frames=%u flag03da=1 collision10=disabled passengers_hidden=1\n",approach_frame);
    door(scene,4,2,0);close_scene(scene);
    load_scene(scene,&game,argv[1],2,1,0);until_control(scene,0x426e);
    CHECK(flag(&game,0x3db)==1&&flag(&game,0x3dc)==0&&flag(&game,0x3de)==0);
    C3StoryActor *willie=c3_field_story_actor(&scene->story,4);CHECK(willie&&willie->visible);
    memcpy(scene->hero.position,willie->pose.position,sizeof scene->hero.position);
    scene->hero.position[2]=0;scene->hero.yaw=0;
    fprintf(trace,"integration_fixture_hero_positioned_for_talk actor=4 x=%.3f y=%.3f z=%.3f\n",scene->hero.position[0],scene->hero.position[1],scene->hero.position[2]);
    CHECK(c3_field_story_interact(&scene->story,error));
    until_control(scene,0x514a);
    CHECK(flag(&game,0x3dc)==1&&flag(&game,0x3dd)==0&&flag(&game,0x3de)==0);
    unchanged_party(&game,&original);
    C3GameState restored=save_new_game(&game,prepared);
    close_scene(scene);c3_game_state_free(&game);game=restored;
    C3GameSummary continued;
    CHECK(c3_game_state_summary(&game,&continued));
    CHECK(continued.floor==2&&continued.previous_floor==1&&continued.spawn==0);
    fprintf(trace,"integration_continue_begin floor=%u previous=%u spawn=%u saved_count=%u\n",
            continued.floor,continued.previous_floor,continued.spawn,continued.saved_count);
    /* Rebuild every scene/VM/actor object using the saved floor tuple. No
     * story flags or coordinates are supplied as a replacement for Continue. */
    load_scene(scene,&game,argv[1],continued.floor,continued.previous_floor,continued.spawn);
    unsigned continue_frame;
    for(continue_frame=0;continue_frame<12000;++continue_frame){
        const C3FieldStoryView *v=c3_field_story_view(&scene->story);
        /* Original init still configures the TV texture on every visit;
         * the persisted flag must suppress the introductory news message. */
        CHECK(!v->blocked[0]&&v->message_id!=0x426e);
        if(v->control)break;
        tick(scene,continue_frame);
    }
    CHECK(continue_frame<12000);
    CHECK(flag(&game,0x3db)==1&&flag(&game,0x3dc)==1&&flag(&game,0x3dd)==0&&flag(&game,0x3de)==0);
    C3GameSummary resumed;
    CHECK(c3_game_state_summary(&game,&resumed));
    CHECK(resumed.floor==continued.floor&&resumed.previous_floor==continued.previous_floor&&resumed.spawn==continued.spawn&&resumed.saved_count==continued.saved_count);
    unchanged_party(&game,&original);
    fprintf(trace,"integration_continue_rebuilt_shop=1 control_frames=%u news_message_repeated=0 checkpoint_fields_preserved=1\n",continue_frame);
    door(scene,2,1,1);close_scene(scene);
    load_scene(scene,&game,argv[1],1,2,1);
    unsigned frame;
    for(frame=0;frame<18000&&!c3_field_story_view(&scene->story)->battle_id;++frame)tick(scene,frame);
    const C3FieldStoryView *view=c3_field_story_view(&scene->story);
    CHECK(frame<18000&&view->battle_id==10&&view->blocked[0]&&!view->control);
    CHECK(logged("story_battle_snapshot encounter=10 player_party=2 status=waiting_backend save_unchanged=1"));
    CHECK(logged("story_battle encounter=10 status=waiting_backend result_written=0"));
    CHECK(flag(&game,0x3dd)==1&&flag(&game,0x3de)==0);unchanged_party(&game,&original);
    for(frame=0;frame<120;++frame)CHECK(c3_field_story_tick(&scene->story,1.0f/60.0f,0x300,error));
    CHECK(c3_field_story_view(&scene->story)->battle_id==10&&!c3_field_story_view(&scene->story)->control);
    CHECK(flag(&game,0x3de)==0);unchanged_party(&game,&original);
    fprintf(trace,"integration_boundary encounter=10 native98_pending=1 battle_result_supplied=0 victory_flag03DE=0\n");
    /* The battle scene reports the original fight result. Only then does the
     * original script continue: it reads native 67, shows Willie's message
     * and writes its own flags, including Phenac's world-map unlock 03DF. */
    {uint32_t encounter=0;
     CHECK(c3_field_story_battle_pending(&scene->story,&encounter)&&encounter==10);
     CHECK(!c3_field_story_battle_result(&scene->story,0,error));
     CHECK(c3_field_story_battle_pending(&scene->story,NULL)&&flag(&game,0x3de)==0);
     CHECK(c3_field_story_battle_result(&scene->story,2,error));
     CHECK(!c3_field_story_battle_pending(&scene->story,NULL));
     CHECK(!c3_field_story_battle_result(&scene->story,2,error));
     CHECK(!c3_field_story_view(&scene->story)->blocked[0]&&!c3_field_story_view(&scene->story)->battle_id);
     until_control(scene,0x5145);
     CHECK(logged("story_battle_result encounter=10 result=2 native98_resumed=1"));
     CHECK(logged("id=67 argc=0"));
     CHECK(flag(&game,0x3dd)==1&&flag(&game,0x3de)==1&&flag(&game,0xa5a)==1&&flag(&game,0x3df)==1);
     unchanged_party(&game,&original);
     for(frame=0;frame<600;++frame)tick(scene,frame);
     CHECK(c3_field_story_view(&scene->story)->control&&!c3_field_story_view(&scene->story)->battle_id);
     CHECK(!c3_field_story_battle_pending(&scene->story,NULL));
     fprintf(trace,"integration_post_battle result=2 flags03DE_0A5A_03DF=1 control=1 challenge_repeated=0\n");}
    /* Leaving by the original exit surface 0 runs common script 05960004
     * with floor 0x38E (the world map). It is refused without a floor
     * predicate and requested with entrance 0 once the scene accepts it. */
    c3_field_story_set_floor_support(&scene->story,accept_world_map);
    door(scene,0,0x38e,0);
    fprintf(trace,"integration_world_map_request floor=038e spawn=0\n");
    close_scene(scene);free(scene);c3_game_state_free(&game);
    fprintf(trace,"integration_complete checks=%u\n",checks);
    if(argc<=2){char buffer[4096];size_t size;rewind(trace);
        while((size=fread(buffer,1,sizeof buffer,trace)))CHECK(fwrite(buffer,1,size,stdout)==size);
        CHECK(!ferror(trace));}
    CHECK(fclose(trace)==0);
    printf("field-story private integration: %u checks passed; ends after the reported first-battle result\n",checks);return 0;
}
