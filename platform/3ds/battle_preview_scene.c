#include "battle_preview_scene.h"
#include "trace_writer.h"
#include "bottom_screen.h"
#include "game_ui_views.h"
#include "n3ds_platform.h"
#include "battle_stage.h"
#include "battle_entry_console.h"
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

static void trace_turn(FILE *f,const C3BattlePreview *b)
{
    if(!f)return;
    fprintf(f,"preview_turn turn=%lu seed=%08lx draws=%llu outcome=%u save_writes=0 gsvm_resumed=0\n",
        (unsigned long)b->turn,(unsigned long)b->rng.seed,(unsigned long long)b->rng.draws,(unsigned)b->outcome);
    for(unsigned i=0;i<b->event_count;++i){const C3BattlePreviewEvent *e=&b->events[i];
        fprintf(f,"preview_event index=%u hp=%u,%u,%u,%u draws=%llu text=%s\n",i,e->hp[0],e->hp[1],e->hp[2],e->hp[3],(unsigned long long)e->draws,e->text);}
    fflush(f);
}
int c3_battle_preview_scene_run(const C3BattleRequest *request,const uint8_t *entry_body,size_t entry_bytes,FILE *trace,int *outcome)
{
    if(outcome)*outcome=0;
    C3BattlePreview *battle=calloc(1,sizeof *battle);C3BattleInitTables init;C3BattlePreviewTables tables;
    C3BattlePreviewMenu menu;C3N3dsInput input={0};char error[192]={0};int result=0,dirty=1;
    C3BattleStage *stage=calloc(1,sizeof *stage);
    int failure_logged=0;
    PrintConsole scratch={0};PrintConsole *bottom=consoleSelect(&scratch);consoleSelect(bottom);
    int released=0;uint64_t previous_time=c3_n3ds_active_time_us();
    c3_battle_preview_menu_init(&menu);
    if(!battle)snprintf(error,sizeof error,"Preview allocation failed.");
    else if(c3_battle_init_load(&init,BOOT_ROOT "willie.c3i",error)&&
            c3_battle_preview_load(&tables,BOOT_ROOT "willie.c3p",error)){
        /* Original trainer entry first: when its private pack is present the
         * battle starts from the entry-built FightPokemon and session RNG.
         * Only an absent pack falls back to the preview roster initializer;
         * a failed entry is reported, never silently replaced. */
        int entry=entry_body?c3_battle_entry_console_run(10,entry_body,entry_bytes,trace,request,&tables,battle,error):0;
        const char *source=entry>0?"entry_fightpokemon":"roster_initializer";
        if(entry==0&&!c3_battle_preview_begin(battle,request,&init,&tables,0x12345678u,error))entry=-1;
        if(entry>=0){
            if(trace){fprintf(trace,"preview_begin build=%s seed=%08lx rng_draws=%llu source=%s enemy_policy=uniform_legal experimental=1\n",
                C3_BUILD_ID,(unsigned long)battle->seed,(unsigned long long)battle->rng.draws,source);fflush(trace);}
            trace_turn(trace,battle);
        }
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
    if(!error[0]&&(stage->damage_available||stage->reflect_available||stage->confusion_available||stage->tail_available||stage->growl_available||stage->stat_down_available||stage->attack_down_available||stage->tackle_available)){
        if(c3_battle_audio_player_open(BOOT_ROOT "battle",error,sizeof error)){
            stage->sound_cue=c3_battle_audio_player_cue;
            stage->sound_done=c3_battle_audio_player_done;
            stage->sound_status=c3_battle_audio_player_status;
            stage->sound_stop=c3_battle_audio_player_stop;
            c3_render_recovery_set_pause_callback(c3_battle_audio_player_pause);
        }
    }
    previous_time=c3_n3ds_active_time_us(); /* Loading time is not battle time. */
    c3_ui_perf_clock(c3_n3ds_time_us);c3_ui_perf_take(NULL);
    if(trace){fprintf(trace,"battle_load wall_us=%llu effects=%u source_camera=%u audio=%u\n",
        (unsigned long long)(c3_n3ds_time_us()-load_start),stage?stage->damage_available:0,stage?stage->camera_available:0,stage&&stage->sound_cue?1:0);fflush(trace);}
    unsigned frames=0;uint64_t metric_time=previous_time;
    uint64_t sample_cpu_us=0,full_frame_peak_us=0,sample_cpu_peak_us=0;unsigned interval_frames=0;
    /* Interval sums (all frames, not only the last one) for the bottom-screen
     * redraw, the stage step and the CPU part of the draw. */
    uint64_t ui_us_sum=0,step_us_sum=0,draw_us_sum=0,present_us_sum=0;unsigned ui_draws=0;
    C3UiPager ui_pager={0};
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
            sample_cpu_us=c3_n3ds_time_us()-sample_start;step_us_sum+=sample_cpu_us;
            if(sample_cpu_us>sample_cpu_peak_us)sample_cpu_peak_us=sample_cpu_us;
            C3BattlePresentation *p=&stage->presentation;
            int complete=p->cursor>=p->count;
            if(menu.page==C3_PREVIEW_MENU_EVENTS){
                unsigned shown=p->cursor<p->count?p->cursor:p->count?p->count-1:0;
                if(menu.event!=shown){menu.event=shown;dirty=1;}
            }
            uint16_t pressed=released?input.pressed:0;
            /* A stylus tap selects a move or target row and confirms it. */
            if(released&&input.touch_pressed&&complete)
                pressed|=c3_battle_preview_menu_touch(&menu,battle,&tables,input.touch_x,input.touch_y);
            c3_ui_pager_sync(&ui_pager,(uint32_t)menu.page*0x10000u+(uint32_t)menu.event+1u);
            if(c3_ui_pager_advance(&ui_pager,&pressed))dirty=1;
            if(p->cursor<p->count&&p->events[p->cursor].kind==C3_PREVIEW_EVENT_HP)dirty=1;
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
        if(dirty){uint64_t ui_start=c3_n3ds_time_us();
            c3_battle_ui_draw_to(c3_bottom_screen_begin(),battle,&tables,&menu,stage?&stage->presentation:NULL,error,&ui_pager);
            uint64_t present_start=c3_n3ds_time_us();c3_bottom_screen_present();dirty=0;
            present_us_sum+=c3_n3ds_time_us()-present_start;ui_us_sum+=c3_n3ds_time_us()-ui_start;++ui_draws;}
        if(stage&&stage->initialized&&!error[0]){
            C3TitleRenderStats stats;
            /* All command/message/status UI now lives on the bottom screen.
             * Keep the top view clear and avoid allocating the old debug atlas. */
            C3StageBattleHud hud={0};
            if(c3_stage_renderer_set_battle_hud(&hud,error,sizeof error)!=C3_RENDER_OK){dirty=1;continue;}
            uint64_t render_start=c3_n3ds_time_us();
            int drawn=c3_battle_stage_draw(stage,&stats,error,sizeof error);
            if(drawn==C3_RENDER_ERROR)dirty=1;
            else if(drawn==C3_RENDER_BUSY){
                C3RenderRecoveryResult recovered=c3_render_recover(trace,"battle",frames,C3_RECOVER_WAIT);
                if(recovered==C3_RECOVERY_EXIT){result=1;break;}
                if(recovered==C3_RECOVERY_RESUME_PAUSED)previous_time=c3_n3ds_active_time_us();
            }else{
                ++frames;
                /* The draw also paces presentation; keep these two fields CPU time. */
                uint64_t draw_cpu_us=c3_n3ds_time_us()-render_start-stats.wait_us;draw_us_sum+=draw_cpu_us;
                ++interval_frames;uint64_t full_frame_us=c3_n3ds_time_us()-frame_start-stats.wait_us;
                if(full_frame_us>full_frame_peak_us)full_frame_peak_us=full_frame_us;
                if(trace&&now-metric_time>=1000000u){
                    char perf[1024];c3_stage_renderer_perf_format(perf,sizeof perf);
                    fprintf(trace,"battle_render frames=%u draw_cpu_us=%llu gpu_previous_ms=%.3f draws=%lu vertices=%lu gpu_buffer_bytes=%lu vertices_prepared=%lu cached_models=%lu command_words=%lu particle_command_words=%lu command_capacity_words=%lu%s\n",frames,
                        (unsigned long long)draw_cpu_us,(double)stats.gpu_previous_ms,
                        (unsigned long)stats.draw_calls,(unsigned long)stats.vertices,(unsigned long)stats.gpu_buffer_bytes,
                        (unsigned long)stats.vertices_prepared,(unsigned long)stats.cached_models,
                        (unsigned long)stats.command_words,(unsigned long)stats.particle_command_words,(unsigned long)stats.command_capacity_words,perf);
                    c3_n3ds_log_memory(trace,"battle_render");fflush(trace);metric_time=now;
                    uint32_t particle_peak=stage->particles.peak_particles;
                    if(stage->confusion_damage_particles&&stage->confusion_damage_particles->peak_particles>particle_peak)
                        particle_peak=stage->confusion_damage_particles->peak_particles;
                    C3UiPerf ui;c3_ui_perf_take(&ui);
                    fprintf(trace,"battle_frame sample_clock_cpu_us=%llu sample_clock_peak_us=%llu full_cpu_frame_us=%llu full_cpu_peak_us=%llu interval_frames=%u particles=%lu particle_vertices=%lu particle_live_peak=%lu"
                        " draw_prepare_us=%llu prepare_us=%lu upload_us=%lu upload_bytes=%lu submit_us=%lu wait_us=%lu cached_batches=%lu vertices_static=%lu"
                        " ui_us=%llu ui_draws=%u step_us=%llu draw_us=%llu"
                        " ui_present_us=%llu ui_begin_us=%llu ui_panel_us=%llu ui_text_us=%llu ui_rect_us=%llu ui_sprite_us=%llu"
                        " ui_panels=%lu ui_glyphs=%lu ui_rects=%lu ui_sprites=%lu\n",
                        (unsigned long long)sample_cpu_us,(unsigned long long)sample_cpu_peak_us,
                        (unsigned long long)full_frame_us,(unsigned long long)full_frame_peak_us,interval_frames,
                        (unsigned long)stats.particles,(unsigned long)stats.particle_vertices,(unsigned long)particle_peak,
                        (unsigned long long)stats.draw_prepare_us,(unsigned long)stats.prepare_us,(unsigned long)stats.upload_us,
                        (unsigned long)stats.upload_bytes,(unsigned long)stats.submit_us,(unsigned long)stats.wait_us,
                        (unsigned long)stats.cached_batches,(unsigned long)stats.vertices_static,
                        (unsigned long long)ui_us_sum,ui_draws,(unsigned long long)step_us_sum,(unsigned long long)draw_us_sum,
                        (unsigned long long)present_us_sum,(unsigned long long)ui.begin_us,(unsigned long long)ui.panel_us,
                        (unsigned long long)ui.text_us,(unsigned long long)ui.rect_us,(unsigned long long)ui.sprite_us,
                        (unsigned long)ui.panels,(unsigned long)ui.glyphs,(unsigned long)ui.rects,(unsigned long)ui.sprites);
                    sample_cpu_peak_us=full_frame_peak_us=0;interval_frames=0;
                    ui_us_sum=step_us_sum=draw_us_sum=present_us_sum=0;ui_draws=0;
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

    if(outcome&&battle&&!error[0])*outcome=(int)battle->outcome;
    if(trace){fprintf(trace,"preview_exit app_exit=%d error=%s outcome=%d save_writes=0\n",result,error,battle?(int)battle->outcome:0);fflush(trace);}
    if(error[0]&&!result)result=-1;
    free(battle);return result;
}
int c3_battle_preview_from_game_state(const C3GameState *state,FILE *trace,int *outcome)
{
    if(outcome)*outcome=0;
    C3BattleData data;C3BattleRequest request;char error[192];
    size_t bytes=0;const uint8_t *body=c3_game_state_body(state,&bytes);
    const uint32_t args[]={10,1,0};
    if(c3_battle_load(&data,BOOT_ROOT "willie.c3b",error)&&
       c3_battle_request(&request,&data,args,body,bytes,error)) {
        uint8_t *before=malloc(bytes);
        if(before) {
            memcpy(before,body,bytes);
            /* The original trainer entry runs inside the scene, within this
             * state guard, and seeds the battle; nothing is authoritative. */
            int result=c3_battle_preview_scene_run(&request,body,bytes,trace,outcome);
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
    c3_bottom_screen_notice("PREVIEW UNAVAILABLE",error,"B: return");
    if(trace){fprintf(trace,"preview_admission_failed error=%s\n",error);fflush(trace);}
    C3N3dsInput input={0};
    while(c3_n3ds_poll(&input)){if(input.pressed&C3_PAD_B)return -1;gspWaitForVBlank();}
    return 1;
}

int c3_battle_preview_probe_run(void)
{
    C3GameState state={0};C3BattleData data;C3BattleRequest request;char error[192]={0};int result=1;
    FILE *trace=c3_trace_open("sdmc:/3ds/Colosseum3DS/preview-probe.log");
    if(c3_game_state_new(&state,"WES",0x12345678u,BOOT_ROOT "newgame.c3g",error)&&
       c3_battle_load(&data,BOOT_ROOT "willie.c3b",error)){
        size_t bytes;const uint8_t *body=c3_game_state_body(&state,&bytes);const uint32_t args[]={10,1,0};
        if(c3_battle_request(&request,&data,args,body,bytes,error)){
            if(trace){fprintf(trace,"preview_probe source=synthetic_entry real_tables=1 fresh_game=1 persisted_save_loaded=0 build=%s\n",C3_BUILD_ID);fflush(trace);}
            result=c3_battle_preview_scene_run(&request,NULL,0,trace,NULL)<0?1:0;
        }
    }
    if(error[0]){consoleClear();printf("BATTLE PREVIEW PROBE\n\n%s\n\nSTART + SELECT: exit\n",error);c3_n3ds_wait_for_exit();}
    if(trace){fprintf(trace,"preview_probe_end result=%d error=%s save_writes=0\n",result,error);fclose(trace);}
    c3_game_state_free(&state);return result;
}
