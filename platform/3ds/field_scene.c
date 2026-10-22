#include "field_scene.h"
#include "n3ds_platform.h"
#include "stage_renderer.h"
#include "colosseum3ds/field.h"
#include "colosseum3ds/title.h"

#include <3ds.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define BOOT_ROOT "sdmc:/3ds/Colosseum3DS/boot/"
#define SAVE_DIRECTORY "sdmc:/3ds/Colosseum3DS/saves"

typedef enum { FIELD_ARRIVAL, FIELD_ARRIVAL_OUT, FIELD_FADE_IN, FIELD_CONTROL } FieldPhase;
typedef enum { PAUSE_CLOSED, PAUSE_MAIN, PAUSE_PARTY, PAUSE_SAVE, PAUSE_LEAVE, PAUSE_MESSAGE } PausePage;

static int exists(const char *path)
{
    struct stat entry;
    return stat(path,&entry)==0 && S_ISREG(entry.st_mode);
}

int c3_field_scene_supports(uint32_t floor) { return floor==1u; }

int c3_field_scene_available(void)
{
    C3FieldMap map={0};char path[128],error[128];
    if(!c3_field_load(&map,BOOT_ROOT "outskirt.c3f",error,sizeof error))return 0;
    int ok=map.floor==1u && exists(BOOT_ROOT "outskirt_wes.c3t");
    for(uint32_t i=0;i<map.map_pack_count && ok;++i) {
        snprintf(path,sizeof path,BOOT_ROOT "outskirt_map%lu.c3t",(unsigned long)i);
        ok=exists(path);
    }
    for(uint32_t i=0;i<map.arrival_pack_count && ok;++i) {
        snprintf(path,sizeof path,BOOT_ROOT "outskirt_arrival%lu.c3t",(unsigned long)i);
        ok=exists(path);
    }
    c3_field_close(&map);return ok;
}

static void hide(C3Title *model)
{
    for(uint32_t i=0;i<model->batch_count;++i)model->pose.materials[i].visible=0;
}

/* The prepared Wes poses are model-local. This is the original Y-axis
 * quaternion convention, applied after the skinning/pose sample. */
static void place_actor(C3Title *model,const C3FieldPlayer *player)
{
    float sine=sinf(player->yaw),cosine=cosf(player->yaw);
    for(uint32_t i=0;i<model->vertex_count;++i) {
        C3TitleDrawVertex *v=model->draw_vertices+i;
        float x=v->pos[0],z=v->pos[2],nx=v->normal[0],nz=v->normal[2];
        v->pos[0]=cosine*x+sine*z+player->position[0];
        v->pos[1]+=player->position[1];
        v->pos[2]=-sine*x+cosine*z+player->position[2];
        v->normal[0]=cosine*nx+sine*nz;
        v->normal[2]=-sine*nx+cosine*nz;
    }
}

static const char *pokemon_name(uint16_t species)
{
    switch(species) {case 197:return "UMBREON";case 196:return "ESPEON";default:return "POKEMON";}
}

static void draw_console(const C3GameSummary *summary,FieldPhase phase,PausePage page,
                         unsigned selection,int yes,const char *message)
{
    consoleClear();
    printf("OUTSKIRT STAND\n\n%s  |  $%lu\nTime %lu:%02lu:%02lu\n\n",summary->name,
           (unsigned long)summary->money,(unsigned long)(summary->play_seconds/3600u),
           (unsigned long)(summary->play_seconds/60u%60u),(unsigned long)(summary->play_seconds%60u));
    if(phase==FIELD_ARRIVAL || phase==FIELD_ARRIVAL_OUT) {
        printf("Arriving at Outskirt Stand...\n");return;
    }
    if(page==PAUSE_CLOSED) {
        printf("Circle Pad / D-pad: move\nX / START: menu\n\n");
        printf("%s Lv.%u   HP %u/%u\n%s Lv.%u   HP %u/%u\n\n",
               pokemon_name(summary->party[0].species),summary->party[0].level,
               summary->party[0].hp,summary->party[0].max_hp,
               pokemon_name(summary->party[1].species),summary->party[1].level,
               summary->party[1].hp,summary->party[1].max_hp);
        printf("This build reaches Outskirt Stand.\nExploration and saving are available.\n");
    } else if(page==PAUSE_MAIN) {
        static const char *const choices[]={"RESUME","POKEMON","SAVE","MAIN MENU"};
        for(unsigned i=0;i<4;++i)printf("%s %s\n",i==selection?">":" ",choices[i]);
        printf("\nA: choose   B: resume\n");
    } else if(page==PAUSE_PARTY) {
        printf("POKEMON\n\n");
        for(unsigned i=0;i<6;++i)if(summary->party[i].species)
            printf("%s Lv.%u  HP %u/%u\n",pokemon_name(summary->party[i].species),
                   summary->party[i].level,summary->party[i].hp,summary->party[i].max_hp);
        printf("\nB: back\n");
    } else if(page==PAUSE_SAVE) {
        printf("Save your game here?\n\nContinue resumes at this area's\nentrance. Your party and progress\nwill be kept.\n\n");
        printf("%s YES\n%s NO\n\nA: confirm   B: cancel\n",yes?">":" ",yes?" ":">");
    } else if(page==PAUSE_LEAVE) {
        printf("Return to the main menu?\nUnsaved progress will be lost.\n\n");
        printf("%s YES\n%s NO\n\nA: confirm   B: cancel\n",yes?">":" ",yes?" ":">");
    } else {
        printf("%s\n\nA / B: back\n",message);
    }
}

static int save_game(C3GameState *state,const char *path,FILE *trace,char *message,size_t capacity)
{
    char error[C3_GAME_ERROR_SIZE]={0};C3GameSummary summary;
    if(mkdir(SAVE_DIRECTORY,0777)!=0 && errno!=EEXIST) {
        snprintf(message,capacity,"The save directory could not be created.");return 0;
    }
    int ok=c3_game_state_save(state,path,error);
    if(ok && c3_game_state_summary(state,&summary)) {
        const uint8_t *body=c3_game_state_body(state,NULL);
        snprintf(message,capacity,"%s saved the game.\nSave number %lu.\n\nContinue starts at the area entrance.",
                 summary.name,(unsigned long)summary.saved_count);
        if(trace)fprintf(trace,"game_save status=ok name=%s count=%lu floor=%lu previous=%lu spawn=%u time=%lu time_bits=%02x%02x%02x%02x\n",
                         summary.name,(unsigned long)summary.saved_count,(unsigned long)summary.floor,
                         (unsigned long)summary.previous_floor,summary.spawn,(unsigned long)summary.play_seconds,
                         body[0x20],body[0x21],body[0x22],body[0x23]);
    } else {
        snprintf(message,capacity,"The game could not be saved.\n%s",error);
        if(trace)fprintf(trace,"game_save status=failed reason=%s\n",error);
    }
    if(trace)fflush(trace);
    return ok;
}

/* This is a bounded native lowering of the original S1_out entry function
 * 0x01000010. The admitted pack contains its real attached bike animation and
 * camera. We only mark flag0AF8 after that animation and its black fade have
 * completed. General GSVM event dispatch, NPC interaction and battles are not
 * run by this scene. See docs/outskirt-field.md for that boundary. */
int c3_field_scene_run(C3GameState *state,int new_game,const char *save_path,
                       FILE *trace,char *error,size_t capacity)
{
    C3FieldMap map={0};C3FieldPlayer player={0};C3GameSummary summary;
    C3Title *storage=NULL;const C3Title *models[C3_STAGE_MAX_MODELS]={0};
    C3TitleCamera camera;C3TitleRenderStats stats={0};C3N3dsInput input={0};
    uint32_t opened=0,count=0,arrival_done=0,opening_done=0;
    unsigned wes_index=0,arrival_index=0,selection=0;
    int result=-1,yes=0,dirty=1,arrival_end_sampled=0;
    FieldPhase phase=FIELD_CONTROL;PausePage page=PAUSE_CLOSED;
    float elapsed=0,phase_seconds=0,arrival_seconds=0;
    uint64_t previous=0,next_console=0,next_trace=0;
    char path[128],message[256]={0};
    if(!state || !save_path || !c3_game_state_summary(state,&summary) ||
       (!new_game && !c3_field_scene_supports(summary.floor))) {
        snprintf(error,capacity,"The saved location is not available in this build.");return -1;
    }
    if(!c3_field_load(&map,BOOT_ROOT "outskirt.c3f",error,capacity))goto done;
    if(map.floor!=1u || map.actor_bone!=UINT32_MAX || map.actor_first_vertex!=0u) {
        snprintf(error,capacity,"The field pack has an unsupported actor layout.");goto done;
    }
    if(!c3_game_state_get_flag(state,0x08D0,&opening_done) ||
       !c3_game_state_get_flag(state,0x0AF8,&arrival_done)) {
        snprintf(error,capacity,"The original field progress flags are unavailable.");goto done;
    }
    uint32_t spawn=new_game?0u:summary.spawn;
    if(!c3_field_spawn(&map,spawn,&player)) {
        snprintf(error,capacity,"The saved Outskirt Stand entrance is unavailable.");goto done;
    }
    count=map.map_pack_count+1u+map.arrival_pack_count;wes_index=map.map_pack_count;arrival_index=wes_index+1u;
    storage=calloc(count,sizeof(*storage));
    if(!storage) {snprintf(error,capacity,"Field model allocation failed.");goto done;}
    consoleClear();printf("OUTSKIRT STAND\n\nLoading the area...\n");
    for(uint32_t i=0;i<count;++i) {
        if(i<map.map_pack_count)snprintf(path,sizeof path,BOOT_ROOT "outskirt_map%lu.c3t",(unsigned long)i);
        else if(i==wes_index)snprintf(path,sizeof path,BOOT_ROOT "outskirt_wes.c3t");
        else snprintf(path,sizeof path,BOOT_ROOT "outskirt_arrival%lu.c3t",(unsigned long)(i-arrival_index));
        if(!c3_title_load(storage+i,path,error,capacity))goto done;
        ++opened;models[i]=storage+i;
        if(!c3_title_animate(storage+i,0,0)) {snprintf(error,capacity,"Initial field animation failed.");goto done;}
    }
    C3Title *wes=storage+wes_index,*arrival=storage+arrival_index;
    if(wes->vertex_count!=map.actor_vertex_count || wes->clip_count!=3u ||
       arrival->clip_count!=1u || arrival->clips[0].loop || arrival->clips[0].frame_count<2u) {
        snprintf(error,capacity,"The prepared field motions have an unsupported layout.");goto done;
    }
    arrival_seconds=(float)(arrival->clips[0].frame_count-1u)/arrival->fps;
    if(!isfinite(arrival_seconds)||arrival_seconds<=0.0f||arrival_seconds>60.0f) {
        snprintf(error,capacity,"The original arrival duration is invalid.");goto done;
    }
    for(uint32_t i=1;i<map.arrival_pack_count;++i) {
        C3Title *part=storage+arrival_index+i;
        if(part->clip_count!=1u || part->clips[0].loop ||
           part->clips[0].frame_count!=arrival->clips[0].frame_count || part->fps!=arrival->fps) {
            snprintf(error,capacity,"The arrival model timelines disagree.");goto done;
        }
    }
    if(c3_stage_renderer_init(models,count,error,capacity))goto done;
    if(new_game && !c3_game_state_set_floor(state,1,0x03A0,0)) {
        snprintf(error,capacity,"The Outskirt Stand checkpoint could not be prepared.");goto done;
    }
    phase=opening_done && !arrival_done ? FIELD_ARRIVAL:FIELD_FADE_IN;
    if(trace) {
        fprintf(trace,"field_begin floor=1 previous=%lu spawn=%lu new=%d maps=%lu walk=%lu walls=%lu arrival=%d duration=%.3f\n",
                (unsigned long)(new_game?0x03A0:summary.previous_floor),(unsigned long)spawn,new_game,
                (unsigned long)map.map_pack_count,(unsigned long)map.walk_count,(unsigned long)map.wall_count,
                phase==FIELD_ARRIVAL,(double)arrival_seconds);fflush(trace);
    }
    previous=c3_n3ds_active_time_us();result=1;
    while(c3_n3ds_poll(&input)) {
        uint64_t now=c3_n3ds_active_time_us();float delta=(float)(now-previous)/1000000.0f;
        previous=now;elapsed+=delta;phase_seconds+=delta;
        c3_game_state_add_play_time(state,delta);
        float fade=1.0f;
        if(phase==FIELD_ARRIVAL && phase_seconds>=arrival_seconds) {
            phase=FIELD_ARRIVAL_OUT;phase_seconds=0;dirty=1;
            if(trace){fprintf(trace,"field_arrival camera_end=1 actors_closed=1\n");fflush(trace);}
        }
        if(phase==FIELD_ARRIVAL_OUT && phase_seconds>=1.0f) {
            if(!c3_game_state_set_flag(state,0x0AF8,1)) {
                snprintf(error,capacity,"The arrival completion flag could not be set.");result=-1;break;
            }
            arrival_done=1;phase=FIELD_FADE_IN;phase_seconds=0;dirty=1;
            if(trace){fprintf(trace,"field_arrival_complete flag=0af8 camera_mode=0 target=0:100\n");fflush(trace);}
        }
        if(phase==FIELD_FADE_IN && phase_seconds>=1.0f) {
            phase=FIELD_CONTROL;phase_seconds=0;dirty=1;
            if(trace){fprintf(trace,"field_control_ready name=%s floor=1\n",summary.name);fflush(trace);}
        }
        if(phase==FIELD_CONTROL) {
            if(page==PAUSE_CLOSED) {
                if(input.pressed&(C3_PAD_START|C3_PAD_X)) {page=PAUSE_MAIN;selection=0;dirty=1;}
                else if(!c3_field_move(&map,&player,input.stick_x,input.stick_y,input.held,map.camera_yaw,delta)) {
                    snprintf(error,capacity,"Field movement failed.");result=-1;break;
                }
            } else if(page==PAUSE_MAIN) {
                if(input.pressed&C3_PAD_UP){selection=(selection+3u)%4u;dirty=1;}
                if(input.pressed&C3_PAD_DOWN){selection=(selection+1u)%4u;dirty=1;}
                if(input.pressed&(C3_PAD_B|C3_PAD_START|C3_PAD_X)){page=PAUSE_CLOSED;dirty=1;}
                else if(input.pressed&C3_PAD_A) {
                    static const PausePage choices[]={PAUSE_CLOSED,PAUSE_PARTY,PAUSE_SAVE,PAUSE_LEAVE};
                    page=choices[selection];yes=0;dirty=1;
                }
            } else if(page==PAUSE_SAVE || page==PAUSE_LEAVE) {
                if(input.pressed&(C3_PAD_UP|C3_PAD_DOWN|C3_PAD_LEFT|C3_PAD_RIGHT)){yes=!yes;dirty=1;}
                if(input.pressed&C3_PAD_B){page=PAUSE_MAIN;dirty=1;}
                else if(input.pressed&C3_PAD_A) {
                    if(!yes){page=PAUSE_MAIN;dirty=1;}
                    else if(page==PAUSE_LEAVE){result=0;break;}
                    else {
                        save_game(state,save_path,trace,message,sizeof message);
                        page=PAUSE_MESSAGE;dirty=1;
                        /* The blocking write counts toward active game time on
                         * the next iteration; movement remains paused. */
                    }
                }
            } else if(input.pressed&C3_PAD_B || (page==PAUSE_MESSAGE && input.pressed&C3_PAD_A)) {
                page=PAUSE_MAIN;dirty=1;
            }
        }
        int sampled=1;
        for(uint32_t i=0;i<map.map_pack_count;++i)sampled &= c3_title_animate(storage+i,0,elapsed);
        if(phase==FIELD_ARRIVAL) {
            for(uint32_t i=0;i<map.arrival_pack_count;++i)
                sampled &= c3_title_animate(storage+arrival_index+i,0,phase_seconds);
        } else if(phase==FIELD_ARRIVAL_OUT) {
            /* The source camera ends after 300 half-frame updates (5s),
             * then clamps its cursor to authored end-0.1. Actors are closed;
             * retain that camera pose through the one-second black fade. */
            if(!arrival_end_sampled) {
                sampled &= c3_title_animate(arrival,0,fmaxf(0.0f,arrival_seconds-0.1f/30.0f));
                arrival_end_sampled=1;
            }
        } else {
            uint32_t clip=player.motion==2?map.run_clip:player.motion==1?map.walk_clip:map.idle_clip;
            sampled &= c3_title_animate(wes,clip,player.animation_seconds);
            place_actor(wes,&player);
        }
        if(!sampled){snprintf(error,capacity,"A field animation sample failed.");result=-1;break;}
        if(phase==FIELD_ARRIVAL || phase==FIELD_ARRIVAL_OUT) {
            camera=arrival->pose.camera;hide(wes);
            if(phase==FIELD_ARRIVAL_OUT) {
                for(uint32_t i=0;i<map.arrival_pack_count;++i)hide(storage+arrival_index+i);
                fade=1.0f-phase_seconds;
            }
            else fade=fminf(1.0f,phase_seconds);
        } else {
            for(uint32_t i=0;i<map.arrival_pack_count;++i)hide(storage+arrival_index+i);
            c3_field_camera(&map,&player,&camera);
            if(phase==FIELD_FADE_IN)fade=phase_seconds;
        }
        if(!c3_game_state_summary(state,&summary)){snprintf(error,capacity,"Game state became unavailable.");result=-1;break;}
        if(dirty || (page==PAUSE_CLOSED && now>=next_console)) {
            draw_console(&summary,phase,page,selection,yes,message);dirty=0;next_console=now+UINT64_C(1000000);
        }
        if(c3_stage_renderer_draw(models,count,&camera,fade,&stats,error,capacity)){result=-1;break;}
        if(trace && (now>=next_trace || input.pressed)) {
            fprintf(trace,"field_tick phase=%u menu=%u input=%04x stick=%d,%d xyz=%.4f,%.4f,%.4f yaw=%.4f motion=%lu sample=%.2f hits=%lu time=%lu\n",
                    (unsigned)phase,(unsigned)page,input.pressed,input.stick_x,input.stick_y,
                    (double)player.position[0],(double)player.position[1],(double)player.position[2],(double)player.yaw,
                    (unsigned long)player.motion,(double)wes->active_frame,(unsigned long)player.collision_count,
                    (unsigned long)summary.play_seconds);fflush(trace);next_trace=now+UINT64_C(1000000);
        }
    }
done:
    c3_stage_renderer_shutdown();
    for(uint32_t i=0;i<opened;++i)c3_title_close(storage+i);
    free(storage);c3_field_close(&map);
    if(trace){fprintf(trace,"field_end result=%d error=%s\n",result,result<0?error:"");fflush(trace);}
    return result;
}
