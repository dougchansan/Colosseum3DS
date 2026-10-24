#include "battle_preview_scene.h"
#include "n3ds_platform.h"
#include "battle_stage.h"
#include "battle_audio_player.h"
#include "render_recovery.h"
#include "colosseum3ds/battle_preview_menu.h"
#include "colosseum3ds/game_state.h"
#include "save_codec.h"
#include <3ds.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BOOT_ROOT "sdmc:/3ds/Colosseum3DS/boot/"
#ifndef C3_BUILD_ID
#define C3_BUILD_ID "unknown"
#endif

static void draw_bottom(PrintConsole *bottom,const C3BattlePreview *b,const C3BattlePreviewTables *t,
    const C3BattlePreviewMenu *m,const char *error)
{
    consoleSelect(bottom);consoleClear();
    if(error[0]){printf("PREVIEW STOPPED\n\n%s\n\nBattle and saved game unchanged.\n\nB / X: return\n",error);return;}
    printf("WILLIE | 2 vs 2\nItems / Switch / Call unavailable\n\n");
    switch(m->page){
    case C3_PREVIEW_MENU_EVENTS:
        if(m->event<b->event_count)printf("%s\n\n",b->events[m->event].text);
        printf("A / B: next message\nX: leave preview\n");break;
    case C3_PREVIEW_MENU_MOVE:{
        const C3BattlePreviewMon *p=&b->mons[m->actor];uint8_t mask=c3_battle_preview_legal_moves(b,t,m->actor);
        printf("Choose %s's move\n\n",c3_battle_preview_name(m->actor));
        if(!mask)printf("> STRUGGLE (no usable moves)\n");
        else for(unsigned i=0;i<4;++i)printf("%s %-13s %2u/%2u PP%s\n",m->selection==i?">":" ",
            c3_battle_preview_move_name(p->moves[i].id),p->moves[i].pp,p->moves[i].maximum_pp,mask&(1u<<i)?"":" -");
        printf("\nD-pad: select   A: confirm\nB: back         X: leave\n");break;
    }
    case C3_PREVIEW_MENU_TARGET:
        printf("Choose a target for\n%s\n\n",m->selection==4?"STRUGGLE":c3_battle_preview_move_name(b->mons[m->actor].moves[m->selection].id));
        for(unsigned a=2;a<4;++a)printf("%s %-12s %u HP%s\n",m->target==a?">":" ",c3_battle_preview_name(a),b->mons[a].hp,b->mons[a].hp?"":" FAINTED");
        printf("\nD-pad: target   A: confirm\nB: change move  X: leave\n");break;
    case C3_PREVIEW_MENU_CONFIRM:
        printf("Execute these commands?\n\n");
        for(unsigned a=0;a<2;++a)if(b->mons[a].hp){const C3BattlePreviewCommand *c=&m->commands[a];
            uint16_t id=c->slot==4?165:b->mons[a].moves[c->slot].id;
            printf("%s: %s\n",c3_battle_preview_name(a),c3_battle_preview_move_name(id));
            if(id!=115&&id!=270&&id!=289)printf("  -> %s\n",c3_battle_preview_name(c->target));}
        printf("\nA: execute turn\nB: revise command\nX: leave preview\n");break;
    case C3_PREVIEW_MENU_RESULT:
        printf("PREVIEW %s\n\n",b->outcome==C3_PREVIEW_PLAYER_WON?"WIN":b->outcome==C3_PREVIEW_PLAYER_LOST?"LOSS":"DRAW");
        printf("Your commands completed this preview.\nThe original story battle is pending.\n\nNo HP, PP, money, experience or story\nflags were written to the game state.\n\nA / B: return\n");break;
    case C3_PREVIEW_MENU_LEAVE:
        printf("Leave the nonpersistent preview?\n\nAll preview changes will be discarded.\n\nA: leave\nB: continue playing\n");break;
    default:break;
    }
}
static int draw_hud(C3BattleStage *s,const C3BattlePreview *b,const C3BattlePreviewMenu *m,char *error,size_t cap)
{
    C3StageBattleHud hud={0};hud.enabled=1;
    snprintf(hud.lines[0],sizeof hud.lines[0],"%s",s->damage_available?"DEV: Bite hit FX; other FX/cameras incomplete":s->bite_available?"DEV: Bite attack FX; hit FX incomplete":"DEV: ball/move FX, cameras, audio missing");
    for(unsigned i=0;i<4;++i){hud.hp[i]=s->presentation.hp[i];hud.maximum_hp[i]=b->mons[i].maximum_hp;}
    const C3BattlePresentation *p=&s->presentation;
    if(p->cursor<p->count&&p->events[p->cursor].kind==C3_PREVIEW_EVENT_HP&&p->duration_ms){
        const C3BattlePreviewEvent *e=&p->events[p->cursor];
        for(unsigned i=0;i<4;++i){int32_t before=e->hp_before[i],after=e->hp[i];
            hud.hp[i]=(uint16_t)(before+(after-before)*(int32_t)p->elapsed_ms/(int32_t)p->duration_ms);}
    }
    snprintf(hud.lines[1],sizeof hud.lines[1],"Umbreon %u/%u    Espeon %u/%u",hud.hp[0],hud.maximum_hp[0],hud.hp[1],hud.maximum_hp[1]);
    snprintf(hud.lines[2],sizeof hud.lines[2],"Zigzagoon %u/%u  Zigzagoon %u/%u",hud.hp[2],hud.maximum_hp[2],hud.hp[3],hud.maximum_hp[3]);
    if(m->page==C3_PREVIEW_MENU_EVENTS){
        const char *text=m->event<b->event_count?b->events[m->event].text:"";
        snprintf(hud.lines[3],sizeof hud.lines[3],"%.48s",text);
        if(strlen(text)>48)snprintf(hud.lines[4],sizeof hud.lines[4],"%.48s",text+48);
        snprintf(hud.lines[6],sizeof hud.lines[6],"%s",p->cursor<p->count?"Presenting committed event...":"A/B: continue   X: leave");
    }else if(m->page==C3_PREVIEW_MENU_MOVE){
        snprintf(hud.lines[3],sizeof hud.lines[3],"%s: choose move",c3_battle_preview_name(m->actor));
        if(m->selection==4)snprintf(hud.lines[4],sizeof hud.lines[4],"> STRUGGLE");
        else for(unsigned i=0;i<4;++i){const C3BattleInitialMove *move=&b->mons[m->actor].moves[i];
            snprintf(hud.lines[4+i],sizeof hud.lines[4+i],"%c %s %u/%u PP",i==m->selection?'>':' ',c3_battle_preview_move_name(move->id),move->pp,move->maximum_pp);}
    }else if(m->page==C3_PREVIEW_MENU_TARGET){
        snprintf(hud.lines[3],sizeof hud.lines[3],"Target: %s %u",c3_battle_preview_name(m->target),m->target-1);
        snprintf(hud.lines[5],sizeof hud.lines[5],"D-pad: target  A: confirm  B: back");
    }else if(m->page==C3_PREVIEW_MENU_CONFIRM){
        snprintf(hud.lines[3],sizeof hud.lines[3],"Execute commands? A: yes B: revise");
    }else if(m->page==C3_PREVIEW_MENU_RESULT){
        snprintf(hud.lines[3],sizeof hud.lines[3],"Preview %s",p->outcome==C3_PREVIEW_PLAYER_WON?"WIN":p->outcome==C3_PREVIEW_PLAYER_LOST?"LOSS":"DRAW");
        snprintf(hud.lines[5],sizeof hud.lines[5],"A/B: return to pending encounter");
    }else if(m->page==C3_PREVIEW_MENU_LEAVE)snprintf(hud.lines[3],sizeof hud.lines[3],"Leave preview? A: leave B: continue");
    return c3_stage_renderer_set_battle_hud(&hud,error,cap)==C3_RENDER_OK;
}
static void trace_turn(FILE *f,const C3BattlePreview *b)
{
    if(!f)return;
    fprintf(f,"preview_turn turn=%lu seed=%08lx draws=%llu outcome=%u save_writes=0 gsvm_resumed=0\n",
        (unsigned long)b->turn,(unsigned long)b->rng.seed,(unsigned long long)b->rng.draws,(unsigned)b->outcome);
    for(unsigned i=0;i<b->event_count;++i){const C3BattlePreviewEvent *e=&b->events[i];
        fprintf(f,"preview_event index=%u hp=%u,%u,%u,%u draws=%llu text=%s\n",i,e->hp[0],e->hp[1],e->hp[2],e->hp[3],(unsigned long long)e->draws,e->text);}
    fflush(f);
}
int c3_battle_preview_scene_run(const C3BattleRequest *request,FILE *trace)
{
    C3BattlePreview *battle=calloc(1,sizeof *battle);C3BattleInitTables init;C3BattlePreviewTables tables;
    C3BattlePreviewMenu menu;C3N3dsInput input={0};char error[192]={0};int result=0,dirty=1;
    C3BattleStage *stage=calloc(1,sizeof *stage);
    int failure_logged=0;
    PrintConsole scratch={0};PrintConsole *bottom=consoleSelect(&scratch);consoleSelect(bottom);
    int released=0;uint64_t previous_time=c3_n3ds_active_time_us();
    c3_battle_preview_menu_init(&menu);
    if(!battle)snprintf(error,sizeof error,"Preview allocation failed.");
    else if(c3_battle_init_load(&init,BOOT_ROOT "willie.c3i",error)&&
            c3_battle_preview_load(&tables,BOOT_ROOT "willie.c3p",error)&&
            c3_battle_preview_begin(battle,request,&init,&tables,0x12345678u,error)){
        if(trace){fprintf(trace,"preview_begin build=%s seed=12345678 enemy_policy=uniform_legal experimental=1\n",C3_BUILD_ID);fflush(trace);}
        trace_turn(trace,battle);
    }
    if(!stage&&!error[0])snprintf(error,sizeof error,"Battle stage allocation failed.");
    uint64_t load_start=c3_n3ds_time_us();
    if(!error[0]){
        uint16_t hp[4];for(unsigned i=0;i<4;++i)hp[i]=battle->events[0].hp_before[i];
        if(c3_battle_stage_load(stage,BOOT_ROOT "battle",hp,trace,error,sizeof error)&&
           !c3_battle_stage_submit(stage,battle->events,battle->event_count))
            snprintf(error,sizeof error,"Invalid introduction event batch.");
        c3_n3ds_log_memory(trace,"battle_stage_loaded");
    }
    if(!error[0]&&(stage->damage_available||stage->reflect_available||stage->confusion_available||stage->tail_available||stage->growl_available||stage->stat_down_available)){
        if(c3_battle_audio_player_open(BOOT_ROOT "battle",error,sizeof error)){
            stage->sound_cue=c3_battle_audio_player_cue;
            stage->sound_done=c3_battle_audio_player_done;
            stage->sound_status=c3_battle_audio_player_status;
            stage->sound_stop=c3_battle_audio_player_stop;
            c3_render_recovery_set_pause_callback(c3_battle_audio_player_pause);
        }
    }
    previous_time=c3_n3ds_active_time_us(); /* Loading time is not battle time. */
    if(trace){fprintf(trace,"battle_load wall_us=%llu effects=%u source_camera=%u audio=%u\n",
        (unsigned long long)(c3_n3ds_time_us()-load_start),stage?stage->damage_available:0,stage?stage->camera_available:0,stage&&stage->sound_cue?1:0);fflush(trace);}
    unsigned frames=0;uint64_t metric_time=previous_time;
    uint64_t sample_cpu_us=0,full_frame_peak_us=0,sample_cpu_peak_us=0;unsigned interval_frames=0;
    unsigned last_turn=0;C3BattlePreviewMenuPage previous=menu.page;unsigned previous_event=0;
    while(c3_n3ds_poll(&input)){
        uint64_t frame_start=c3_n3ds_time_us();
        uint64_t now=c3_n3ds_active_time_us();uint64_t delta=(now-previous_time)/1000u;previous_time+=delta*1000u;
        uint32_t ms=delta>UINT32_MAX?UINT32_MAX:(uint32_t)delta;
        if(!input.held)released=1;
        if(error[0]){
            if(trace&&!failure_logged){fprintf(trace,"preview_runtime_failed error=%s\n",error);fflush(trace);failure_logged=1;}
            if(released&&(input.pressed&(C3_PAD_B|C3_PAD_X)))break;
        }
        else{
            uint64_t sample_start=c3_n3ds_time_us();
            if(!c3_battle_stage_step(stage,ms,error,sizeof error))dirty=1;
            sample_cpu_us=c3_n3ds_time_us()-sample_start;
            if(sample_cpu_us>sample_cpu_peak_us)sample_cpu_peak_us=sample_cpu_us;
            C3BattlePresentation *p=&stage->presentation;
            int complete=p->cursor>=p->count;
            if(menu.page==C3_PREVIEW_MENU_EVENTS){
                unsigned shown=p->cursor<p->count?p->cursor:p->count?p->count-1:0;
                if(menu.event!=shown){menu.event=shown;dirty=1;}
            }
            uint16_t pressed=released?input.pressed:0;
            if(menu.page==C3_PREVIEW_MENU_EVENTS&&!complete)pressed&=C3_PAD_X;
            if(pressed){
                if(!c3_battle_preview_menu_step(&menu,battle,&tables,pressed,error))dirty=1;
                if(menu.page==C3_PREVIEW_MENU_EXIT)break;
                if(battle->turn!=last_turn){
                    last_turn=battle->turn;trace_turn(trace,battle);
                    if(!c3_battle_stage_submit(stage,battle->events,battle->event_count))snprintf(error,sizeof error,"Invalid committed battle event batch.");
                }
                if(trace&&(menu.page!=previous||menu.event!=previous_event)){
                    fprintf(trace,"preview_ui page=%u actor=%u slot=%u target=%u event=%u input=%04x\n",(unsigned)menu.page,menu.actor,menu.selection,menu.target,menu.event,pressed);fflush(trace);}
                previous=menu.page;previous_event=menu.event;dirty=1;
            }
        }
        if(dirty){draw_bottom(bottom,battle,&tables,&menu,error);dirty=0;}
        if(stage&&stage->initialized&&!error[0]){
            C3TitleRenderStats stats;
            if(!draw_hud(stage,battle,&menu,error,sizeof error)){dirty=1;continue;}
            uint64_t render_start=c3_n3ds_time_us();
            int drawn=c3_battle_stage_draw(stage,&stats,error,sizeof error);
            if(drawn==C3_RENDER_ERROR)dirty=1;
            else if(drawn==C3_RENDER_BUSY){
                C3RenderRecoveryResult recovered=c3_render_recover(trace,"battle",frames,C3_RECOVER_WAIT);
                if(recovered==C3_RECOVERY_EXIT){result=1;break;}
                if(recovered==C3_RECOVERY_RESUME_PAUSED)previous_time=c3_n3ds_active_time_us();
            }else{
                ++frames;
                ++interval_frames;uint64_t full_frame_us=c3_n3ds_time_us()-frame_start;
                if(full_frame_us>full_frame_peak_us)full_frame_peak_us=full_frame_us;
                if(trace&&now-metric_time>=1000000u){
                    fprintf(trace,"battle_render frames=%u draw_cpu_us=%llu gpu_previous_ms=%.3f draws=%lu vertices=%lu gpu_buffer_bytes=%lu vertices_prepared=%lu cached_models=%lu command_words=%lu particle_command_words=%lu command_capacity_words=%lu\n",frames,
                        (unsigned long long)(c3_n3ds_time_us()-render_start),(double)stats.gpu_previous_ms,
                        (unsigned long)stats.draw_calls,(unsigned long)stats.vertices,(unsigned long)stats.gpu_buffer_bytes,
                        (unsigned long)stats.vertices_prepared,(unsigned long)stats.cached_models,
                        (unsigned long)stats.command_words,(unsigned long)stats.particle_command_words,(unsigned long)stats.command_capacity_words);
                    c3_n3ds_log_memory(trace,"battle_render");fflush(trace);metric_time=now;
                    uint32_t particle_peak=stage->particles.peak_particles;
                    if(stage->confusion_damage_particles&&stage->confusion_damage_particles->peak_particles>particle_peak)
                        particle_peak=stage->confusion_damage_particles->peak_particles;
                    fprintf(trace,"battle_frame sample_clock_cpu_us=%llu sample_clock_peak_us=%llu full_cpu_frame_us=%llu full_cpu_peak_us=%llu interval_frames=%u particles=%lu particle_vertices=%lu particle_live_peak=%lu\n",
                        (unsigned long long)sample_cpu_us,(unsigned long long)sample_cpu_peak_us,
                        (unsigned long long)full_frame_us,(unsigned long long)full_frame_peak_us,interval_frames,
                        (unsigned long)stats.particles,(unsigned long)stats.particle_vertices,(unsigned long)particle_peak);
                    sample_cpu_peak_us=full_frame_peak_us=0;interval_frames=0;
                    c3_battle_audio_player_trace(trace,"battle_render");
                }
            }
        }else{gfxFlushBuffers();gspWaitForVBlank();}
    }
    if(stage){
        if(stage->initialized&&c3_render_recover(trace,"battle",frames,result?C3_RECOVER_EXIT:C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT)result=1;
        while(!c3_battle_stage_close(stage)){
            if(c3_render_recover(trace,"battle_teardown",frames,C3_RECOVER_DRAIN)==C3_RECOVERY_EXIT)result=1;
        }
        c3_render_recovery_set_pause_callback(NULL);c3_battle_audio_player_close();
        c3_n3ds_log_memory(trace,"battle_released");
        free(stage);
    }
    if(input.exit_requested)result=1;
    /* aptMainLoop cancellation may not fill input.exit_requested. Check it
     * without issuing additional controller actions before returning. */
    if(!aptMainLoop())result=1;
    consoleSelect(bottom);

    if(trace){fprintf(trace,"preview_exit app_exit=%d error=%s save_writes=0 gsvm_resumed=0\n",result,error);fflush(trace);}
    if(error[0]&&!result)result=-1;
    free(battle);return result;
}
int c3_battle_preview_from_game_state(const C3GameState *state,FILE *trace)
{
    C3BattleData data;C3BattleRequest request;char error[192];
    size_t bytes=0;const uint8_t *body=c3_game_state_body(state,&bytes);
    const uint32_t args[]={10,1,0};
    if(c3_battle_load(&data,BOOT_ROOT "willie.c3b",error)&&
       c3_battle_request(&request,&data,args,body,bytes,error)) {
        uint8_t *before=malloc(bytes);
        if(before) {
            memcpy(before,body,bytes);
            int result=c3_battle_preview_scene_run(&request,trace);
            size_t after_bytes=0;const uint8_t *after=c3_game_state_body(state,&after_bytes);
            int unchanged=after&&after_bytes==bytes&&!memcmp(before,after,bytes);
            if(trace){
                uint8_t old_hash[20],new_hash[20];
                c3_save_codec_sha1(old_hash,before,(uint32_t)bytes);
                if(after&&after_bytes==bytes)c3_save_codec_sha1(new_hash,after,(uint32_t)bytes);
                else memset(new_hash,0,sizeof new_hash);
                fprintf(trace,"preview_state_guard bytes=%lu unchanged=%d before_sha1=",(unsigned long)bytes,unchanged);
                for(unsigned i=0;i<20;++i)fprintf(trace,"%02x",old_hash[i]);
                fprintf(trace," after_sha1=");
                for(unsigned i=0;i<20;++i)fprintf(trace,"%02x",new_hash[i]);
                fprintf(trace," play_time_advanced=0\n");fflush(trace);
            }
            free(before);
            return unchanged?result:-1;
        }
        snprintf(error,sizeof error,"Unable to allocate the live-state preservation guard.");
    }
    consoleClear();printf("PREVIEW UNAVAILABLE\n\n%s\n\nB: return\n",error);
    if(trace){fprintf(trace,"preview_admission_failed error=%s\n",error);fflush(trace);}
    C3N3dsInput input={0};
    while(c3_n3ds_poll(&input)){if(input.pressed&C3_PAD_B)return -1;gspWaitForVBlank();}
    return 1;
}

int c3_battle_preview_probe_run(void)
{
    C3GameState state={0};C3BattleData data;C3BattleRequest request;char error[192]={0};int result=1;
    FILE *trace=fopen("sdmc:/3ds/Colosseum3DS/preview-probe.log","w");
    if(c3_game_state_new(&state,"WES",0x12345678u,BOOT_ROOT "newgame.c3g",error)&&
       c3_battle_load(&data,BOOT_ROOT "willie.c3b",error)){
        size_t bytes;const uint8_t *body=c3_game_state_body(&state,&bytes);const uint32_t args[]={10,1,0};
        if(c3_battle_request(&request,&data,args,body,bytes,error)){
            if(trace){fprintf(trace,"preview_probe source=synthetic_entry real_tables=1 fresh_game=1 persisted_save_loaded=0 build=%s\n",C3_BUILD_ID);fflush(trace);}
            result=c3_battle_preview_scene_run(&request,trace)<0?1:0;
        }
    }
    if(error[0]){consoleClear();printf("BATTLE PREVIEW PROBE\n\n%s\n\nSTART + SELECT: exit\n",error);c3_n3ds_wait_for_exit();}
    if(trace){fprintf(trace,"preview_probe_end result=%d error=%s save_writes=0\n",result,error);fclose(trace);}
    c3_game_state_free(&state);return result;
}
