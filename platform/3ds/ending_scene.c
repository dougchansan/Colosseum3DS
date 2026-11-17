#include "ending_scene.h"
#include "colosseum3ds/ending.h"
#include "colosseum3ds/story.h"
#include "bottom_screen.h"
#include "movie_player.h"
#include "music_player.h"
#include "n3ds_platform.h"
#include <3ds.h>
#include <string.h>
#define ROOT "sdmc:/3ds/Colosseum3DS/boot/"
typedef struct { C3Story story; C3GameSummary summary; FILE *trace; char error[C3_GAME_ERROR_SIZE]; int failed; } Ending;
static int exists(const char *path){FILE *f=fopen(path,"rb");if(!f)return 0;fclose(f);return 1;}
int c3_ending_scene_available(void){return exists(ROOT "staffroll.c3v")&&exists(ROOT "staffroll.c3s");}
static int movie(void *context)
{
    Ending *e=context;
    c3_music_play(C3_MUSIC_SLOT_406,0x4c9,0x7f,0);
    int result=c3_movie_player_run(ROOT "staffroll.c3v","staffroll",e->trace,e->error,sizeof e->error);
    c3_music_play(C3_MUSIC_SLOT_406,1,0x7f,0);
    return result;
}
static int choice(void *context,uint32_t id,int initial_no)
{
    Ending *e=context;char text[C3_STORY_MESSAGE];int yes=!initial_no;
    if(!c3_story_message(&e->story,id,e->summary.name,"RUI",NULL,text,sizeof text,e->error)){e->failed=1;return -1;}
    if(e->trace){fprintf(e->trace,"ending_prompt message=%04lx initial_no=%d\n",(unsigned long)id,initial_no);fflush(e->trace);}
#if defined(C3_ENDING_PROBE) || defined(C3_FINAL_EVENT_PROBE) || defined(C3_ROUNDTRIP_PROBE)
    c3_n3ds_capture_top("sdmc:/3ds/Colosseum3DS/ending-last-frame.bmp");
    c3_bottom_screen_notice("Original clear-save prompt",text,"Probe: selecting Yes");
    c3_n3ds_capture_bottom("sdmc:/3ds/Colosseum3DS/ending-prompt.bmp");
    return 0;
#endif
    C3UiPager pager={0};c3_ui_pager_sync(&pager,id);int dirty=0;
    /* Draw before the first poll: dialogue populates pager.more. */
    C3BottomUi *initial=c3_bottom_screen_begin();
    c3_ui_dialogue(initial,&pager,text,1,yes);c3_bottom_screen_present();
    while(aptMainLoop()){
        C3N3dsInput input;if(!c3_n3ds_poll(&input)||input.exit_requested)return -1;
        if(c3_ui_pager_advance(&pager,&input.pressed))dirty=1;
        if(input.pressed&(C3_PAD_UP|C3_PAD_DOWN|C3_PAD_LEFT|C3_PAD_RIGHT)){yes=!yes;dirty=1;}
        if(input.pressed&C3_PAD_B)return 1;
        if(input.pressed&C3_PAD_A)return yes?0:1;
        if(dirty){C3BottomUi *ui=c3_bottom_screen_begin();c3_ui_dialogue(ui,&pager,text,1,yes);c3_bottom_screen_present();dirty=0;}
        gspWaitForVBlank();
    }
    return -1;
}
static void save_error(void *context,const char *error)
{
    Ending *e=context;c3_bottom_screen_notice("Save failed",error,"Please try again.");
    if(e->trace){fprintf(e->trace,"ending_save_error reason=%s\n",error);fflush(e->trace);}
}
int c3_ending_scene_run(C3GameState *state,const char *save_path,FILE *trace,char *error,size_t capacity)
{
    Ending e={0};e.trace=trace;int saved=0;
    if(!c3_game_state_summary(state,&e.summary)||!c3_story_load(&e.story,ROOT "staffroll.c3s",e.error)){snprintf(error,capacity,"%s",e.error);return -1;}
    C3EndingServices services={movie,choice,save_error,&e};
    if(trace){fprintf(trace,"ending_start floor=%03lx previous=%03lx\n",(unsigned long)e.summary.floor,(unsigned long)e.summary.previous_floor);fflush(trace);}
    int result=c3_ending_run(state,save_path,&services,&saved,e.error);
    c3_story_free(&e.story);
    if(e.failed)result=-1;
    if(trace){fprintf(trace,"ending_return result=%d saved=%d floor=002 entrance=1 fade_script=05960008 fade_handoff=destination_entry_script\n",result,saved);fflush(trace);}
    if(result<0)snprintf(error,capacity,"%s",e.error);
    return result?result:2;
}
