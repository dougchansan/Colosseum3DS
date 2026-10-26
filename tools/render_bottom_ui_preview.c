/* Host rendering uses the exact same layout and rasterizer as the console.
 * Private original assets are provided by path, never embedded. */
#include "game_menu.h"
#include "game_ui_views.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void scene_preview(C3BottomUi *ui,const char *kind)
{
    if(!strcmp(kind,"title")){c3_ui_title(ui,1,0);return;}
    C3GameSummary summary={.name="WES",.floor=1,.money=1000,.play_seconds=3723};
    summary.party[0].species=197;summary.party[0].level=26;summary.party[0].hp=89;summary.party[0].max_hp=89;
    summary.party[1].species=196;summary.party[1].level=25;summary.party[1].hp=65;summary.party[1].max_hp=74;
    summary.party[0].experience=0x4a6fu;summary.party[1].experience=0x3ff4u;
    C3FieldStoryView story={.control=1};C3UiPager pager={0};
    if(!strcmp(kind,"dialogue")||!strcmp(kind,"choice")){
        story.dialogue=1;story.choice=!strcmp(kind,"choice");story.choice_no=1;story.message_id=1;
        snprintf(story.message,sizeof story.message,"Hey! You there! You've got some impressive POKEMON. Would you like to have a battle with me?");
        c3_field_ui_draw_to(ui,&summary,FIELD_CONTROL,PAUSE_CLOSED,0,0,"",&story,&pager);
    }else if(!strcmp(kind,"field")||!strcmp(kind,"save")||!strcmp(kind,"party")){
        c3_field_ui_draw_to(ui,&summary,FIELD_CONTROL,!strcmp(kind,"save")?PAUSE_SAVE:!strcmp(kind,"party")?PAUSE_PARTY:PAUSE_CLOSED,0,0,"",&story,&pager);
    }else{
        /* Synthetic presentation state only; this tool never executes combat,
         * loads a save, or claims source text/HP for these preview fixtures. */
        C3BattlePreview battle={.magic=0x43504231,.event_count=1,.outcome=C3_PREVIEW_PLAYER_WON};
        C3BattlePreviewTables tables={0};memcpy(tables.bytes,"C3P1",4);
        for(unsigned i=0;i<4;++i){battle.mons[i].hp=89-i*10;battle.mons[i].maximum_hp=89;}
        const uint16_t moves[]={44,289,269,270};
        for(unsigned i=0;i<4;++i){battle.mons[0].moves[i].id=moves[i];battle.mons[0].moves[i].pp=19-i; battle.mons[0].moves[i].maximum_pp=20;}
        for(unsigned i=0;i<2;++i){
            battle.mons[i].species=summary.party[i].species;battle.mons[i].level=summary.party[i].level;
            uint32_t exp=summary.party[i].experience;
            for(unsigned k=0;k<4;++k)battle.original_request.player_party[i][0x5c+k]=(unsigned char)(exp>>(24u-8u*k));
        }
        snprintf(battle.events[0].text,sizeof battle.events[0].text,"UMBREON used BITE!");
        C3BattlePreviewMenu menu={.page=C3_PREVIEW_MENU_MOVE,.target=2};
        if(!strcmp(kind,"event"))menu.page=C3_PREVIEW_MENU_EVENTS;
        if(!strcmp(kind,"target"))menu.page=C3_PREVIEW_MENU_TARGET;
        if(!strcmp(kind,"result"))menu.page=C3_PREVIEW_MENU_RESULT;
        c3_battle_ui_draw_to(ui,&battle,&tables,&menu,NULL,"",&pager);
    }
}

int main(int argc,char **argv)
{
    if(argc!=4){fprintf(stderr,"usage: render_bottom_ui_preview ui.c3ui output.ppm page\n");return 1;}
    FILE *f=fopen(argv[1],"rb");if(!f)return 1;
    unsigned char *data=malloc(1024u*1024u+1u),*pixels=calloc(320u*240u,3u);
    if(!data||!pixels){fclose(f);free(data);free(pixels);return 1;}
    size_t size=fread(data,1,1024u*1024u+1u,f);int failed=ferror(f);fclose(f);
    C3UiAssets assets={0};if(failed||!c3_ui_assets_open(&assets,data,size)){free(data);free(pixels);return 1;}
    C3BottomUi ui;C3GameMenu menu;
    C3GameMenuSave save={.present=1,.valid=1,.can_continue=1,.name="WES",.location="OUTSKIRT STAND",.money=1000,.play_seconds=3723};
    c3_game_menu_init(&menu,&save,NULL);menu.page=(C3GameMenuPage)atoi(argv[3]);
    snprintf(menu.notice,sizeof menu.notice,"This mode is not available in this native build.");
    c3_ui_begin(&ui,pixels,0,&assets,NULL);
    if(argv[3][0]>='0'&&argv[3][0]<='9')c3_game_menu_draw_to(&menu,&ui);
    else scene_preview(&ui,argv[3]);
    f=fopen(argv[2],"wb");if(!f){free(data);free(pixels);return 1;}
    fprintf(f,"P6\n320 240\n255\n");failed=fwrite(pixels,3u,320u*240u,f)!=320u*240u;
    if(fclose(f))failed=1;
    c3_game_menu_close(&menu);free(data);free(pixels);return failed;
}
