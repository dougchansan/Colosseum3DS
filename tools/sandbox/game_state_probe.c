/* Source-derived New Game/ARM persistence probe. Uses private original tables.
 * The fixed seed and isolated path identify development test state, not a
 * player's established progress. Never touches the application's save path. */
#include <3ds.h>
#include "colosseum3ds/game_state.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *pack_path = "sdmc:/3ds/Colosseum3DS/boot/newgame.c3g";
static const char *save_path = "sdmc:/3ds/Colosseum3DS/test-game-state-01/newgame.gci";
static const char *report_path = "sdmc:/3ds/Colosseum3DS/test-game-state-01/result.txt";

static int expected(const C3GameState *state, C3GameSummary *summary)
{
    uint32_t flag;
    return c3_game_state_summary(state, summary) &&
        !strcmp(summary->name,"WES") && summary->trainer_id==0x1c465252 &&
        summary->money==10000 && summary->floor==1 &&
        summary->previous_floor==0x3a0 && summary->spawn==0 &&
        summary->play_seconds==3661 && summary->party_count==2 &&
        summary->party[0].species==197 && summary->party[0].level==26 &&
        summary->party[0].personality==0x3f5932e4 && summary->party[0].max_hp==86 &&
        summary->party[1].species==196 && summary->party[1].level==25 &&
        summary->party[1].personality==0x0990627b && summary->party[1].max_hp==75 &&
        c3_game_state_get_flag(state,0x8d0,&flag) && flag==1;
}

int main(void)
{
    C3GameState state={0};
    C3GameSummary summary={0};
    char error[C3_GAME_ERROR_SIZE]={0};
    int loaded=0,writes=0,pass=0;
    gfxInitDefault();
    gfxSetDoubleBuffering(GFX_BOTTOM,false);
    consoleInit(GFX_BOTTOM,NULL);
    mkdir("sdmc:/3ds",0777);
    mkdir("sdmc:/3ds/Colosseum3DS",0777);
    mkdir("sdmc:/3ds/Colosseum3DS/test-game-state-01",0777);
    /* Never replace a file merely because it failed to parse. */
    struct stat existing;
    if (stat(save_path,&existing)==0) {
        loaded=c3_game_state_load(&state,save_path,pack_path,error);
        pass=loaded && expected(&state,&summary);
    } else if (c3_game_state_new(&state,NULL,0x12345678,pack_path,error)) {
        pass=c3_game_state_set_flag(&state,0x8d0,1) &&
             c3_game_state_set_floor(&state,1,0x3a0,0) &&
             c3_game_state_add_play_time(&state,3661.25) && expected(&state,&summary);
    }
    for (unsigned frame=0; aptMainLoop(); ++frame) {
        hidScanInput();
        uint32_t down=hidKeysDown();
        if (pass && (down&KEY_A)) {
            if (c3_game_state_save(&state,save_path,error)) ++writes;
            else pass=0;
            pass=pass && expected(&state,&summary);
        }
        if (frame==0 || frame%15==0 || down) {
            consoleClear();
            printf("Colosseum game-state ARM test\nSource-created test state\nFixed seed 12345678\n\n");
            printf("Known initialization: %s\n",pass?"PASS":"FAIL");
            printf("Loaded at launch: %d\nWrites: %d Count: %lu\n",loaded,writes,(unsigned long)summary.saved_count);
            printf("Name: %s Money: %lu\n",summary.name,(unsigned long)summary.money);
            printf("Floor: %lu Previous: %lu Spawn: %u\n",(unsigned long)summary.floor,
                   (unsigned long)summary.previous_floor,summary.spawn);
            printf("%s\nA saves isolated test state\nSTART exits\n",error);
            FILE *report=fopen(report_path,"w");
            if (report) {
                fprintf(report,"source_created_test_state=1\nseed=12345678\npass=%d\nloaded=%d\nwrites=%d\nsaved_count=%lu\nname=%s\nfloor=%lu\nprevious=%lu\nspawn=%u\nerror=%s\n",
                        pass,loaded,writes,(unsigned long)summary.saved_count,summary.name,
                        (unsigned long)summary.floor,(unsigned long)summary.previous_floor,
                        summary.spawn,error);
                fclose(report);
            }
        }
        if (down&KEY_START) break;
        gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();
    }
    c3_game_state_free(&state);gfxExit();return pass?0:1;
}
