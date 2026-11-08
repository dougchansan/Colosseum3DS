/* Reuse the existing synthetic card/initialization fixture and codec helpers. */
#define main c3_game_state_fixture_main
#include "test_game_state.c"
#undef main
#include "ingame_menu.h"
#include "colosseum3ds/story.h"

static C3IngameEvent tap(C3IngameMenu *m,C3GameState *state,uint16_t button)
{
    C3N3dsInput input={.held=button,.pressed=button};C3IngameEvent e=c3_ingame_update(m,state,&input,1.0f/60.0f);
    input.held=input.pressed=0;c3_ingame_update(m,state,&input,1.0f/60.0f);return e;
}
static void runtime_events(const char *pack_path,const C3PdaAssets *assets)
{
    C3GameState state={0};char error[192];CHECK(c3_game_state_new(&state,"WES",7,pack_path,error));
    uint8_t *before=malloc(C3_SAVE_BODY_SIZE),*after=malloc(C3_SAVE_BODY_SIZE);CHECK(before&&after);
    memcpy(before,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE);memcpy(after,before,C3_SAVE_BODY_SIZE);
    after[0x7f3]^=1;CHECK(c3_game_state_add_play_time(&state,5));
    CHECK(c3_game_state_inventory_commit(&state,before,after,C3_SAVE_BODY_SIZE));
    C3GameSummary summary;CHECK(c3_game_state_summary(&state,&summary)&&summary.play_seconds==5);
    CHECK(!c3_game_state_inventory_commit(&state,before,after,C3_SAVE_BODY_SIZE));
    memcpy(before,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE);memcpy(after,before,C3_SAVE_BODY_SIZE);
    after[0x70]^=1;CHECK(!c3_game_state_inventory_commit(&state,before,after,C3_SAVE_BODY_SIZE));
    CHECK(!memcmp(before,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE));
    C3IngameMenu menu;c3_ingame_init(&menu,&state);c3_ingame_set_assets(&menu,assets);
    menu.page=C3_GAME_POKEMON;menu.detail=0;uint16_t species;uint32_t pid,trainer;uint8_t pokemon[312];
    CHECK(c3_game_state_pokemon(&state,0,pokemon));
    CHECK(c3_ingame_preview_identity(&menu,&state,&species,&pid,&trainer));
    CHECK(species==197&&pid==c3_pda_be32(pokemon+4)&&trainer==c3_pda_be32(pokemon+0x14));
    CHECK(tap(&menu,&state,C3_PAD_Y).kind==C3_INGAME_PREVIEW);
    menu.page=C3_GAME_MEMO_ENTRY;menu.detail=0;CHECK(!c3_ingame_preview_identity(&menu,&state,&species,&pid,&trainer));
    uint8_t *raw=(uint8_t*)c3_game_state_body(&state,NULL);w16(raw+0x82a8+4,197);
    C3GameMemo memo;CHECK(c3_game_state_memo(&state,0,&memo));
    CHECK(c3_ingame_preview_identity(&menu,&state,&species,&pid,&trainer));
    CHECK(species==memo.species&&pid==memo.personality&&trainer==memo.trainer_id);
    menu.page=C3_GAME_MAIL;
    C3IngameEvent event=tap(&menu,&state,C3_PAD_Y);CHECK(menu.page==C3_GAME_TONES&&event.kind==C3_INGAME_PLAY_TONE&&event.tone==0);
    event=tap(&menu,&state,C3_PAD_DOWN);CHECK(event.kind==C3_INGAME_PLAY_TONE&&event.tone==1);
    event=tap(&menu,&state,C3_PAD_Y);CHECK(event.kind==C3_INGAME_PLAY_TONE&&event.tone==1);
    event=tap(&menu,&state,C3_PAD_B);unsigned sort,tone;
    CHECK(event.kind==C3_INGAME_STOP_TONE&&c3_game_state_mail_settings(&state,&sort,&tone)&&tone==0);
    tap(&menu,&state,C3_PAD_Y);tap(&menu,&state,C3_PAD_DOWN);event=tap(&menu,&state,C3_PAD_A);
    CHECK(event.kind==C3_INGAME_STOP_TONE&&c3_game_state_mail_settings(&state,&sort,&tone)&&tone==1);
    c3_ingame_close(&menu);free(after);free(before);c3_game_state_free(&state);
}
static void audit_original(const char *pda_path,const char *game_path,const char *save_path,const char *story_path)
{
    size_t bytes;uint8_t *data=read_file(pda_path,&bytes);C3PdaAssets assets={0};CHECK(c3_pda_assets_open(&assets,data,bytes));
    C3GameState state={0};char error[192],text[8192];CHECK(c3_game_state_new(&state,"WES",0x12345678u,game_path,error));
    for(unsigned id=0;id<32;++id){size_t n;const uint8_t *d=c3_pda_entry(&assets,3,id,&n);CHECK(d&&n==44);
        for(unsigned k=12;k<=20;k+=4){if(!c3_pda_text(&assets,c3_pda_be32(d+k),"WES","RUI",text,sizeof text)){
                fprintf(stderr,"Original mail text decode failed: mail%u field%u message%08x\n",id,k,(unsigned)c3_pda_be32(d+k));exit(1);}}
    }
    for(unsigned i=0;i<48;++i){C3PdaSnag snag;CHECK(c3_pda_snag(&assets,&state,i,&snag));CHECK(snag.species>0&&snag.species<413);}
    C3GameMemo memo;CHECK(c3_game_state_memo(&state,0,&memo)&&memo.species==197);
    C3PdaSpecies species;CHECK(c3_pda_species(&assets,196,&species)&&species.dex==196&&species.color==7&&species.height==211&&species.weight==584);
    CHECK(c3_pda_species(&assets,197,&species)&&species.color==5);
    CHECK(c3_pda_species(&assets,335,&species)&&species.dex==296);
    /* Audit source read plans using an explicitly disposable fresh body. */
    uint8_t *fresh=(uint8_t*)c3_game_state_body(&state,NULL);
    w16(fresh+0x7d20+0x400,32);for(unsigned i=0;i<32;++i)w16(fresh+0x7d20+i*2,i);
    for(unsigned i=0;i<32;++i){C3PdaMailAction action;if(!c3_pda_mail_action(&assets,&state,i,&action)){fprintf(stderr,"Original mail read plan failed: %u\n",i);exit(1);}
        CHECK(c3_game_state_mail_read(&state,&action.save,1));}
    for(unsigned i=0;i<48;++i){C3PdaSnag snag;CHECK(c3_pda_snag(&assets,&state,i,&snag));
        if(!c3_pda_snag_text(&assets,&state,&snag,text,sizeof text)){
            fprintf(stderr,"Original Snag List text decode failed at %u message%08x\n",i,(unsigned)snag.hint_message);exit(1);}}
    if(story_path){C3Story story={0};CHECK(c3_story_load(&story,story_path,error));
        for(unsigned i=24;i<=25;++i){uint16_t key;CHECK(c3_story_start(&story,0x05960000u+i,NULL,0,&key,error));unsigned calls=0,finished=0;
            for(unsigned n=0;n<32;++n){C3StoryCall call;C3StoryStatus status=c3_story_step(&story,key,1024,&call,error);
                CHECK(status!=C3_STORY_FAILED);if(status==C3_STORY_FINISHED){finished=1;break;}
                if(status==C3_STORY_WAITING){CHECK(call.kind==C3_STORY_NATIVE&&call.native==0x4e&&call.argc==1);
                    CHECK(call.args[0]==(i==24?0xadb:0xada));CHECK(c3_game_state_set_flag(&state,call.args[0],1));CHECK(c3_story_resume(&story,key,0));++calls;}}
            CHECK(finished&&calls==1);
        }c3_story_free(&story);printf("Both original mail callbacks complete through existing native4E flag handling.\n");
    }
    if(save_path){C3GameState imported={0};CHECK(c3_game_state_load(&imported,save_path,game_path,error));
        size_t original_size;uint8_t *original_card=read_file(save_path,&original_size),*original_body=malloc(C3_SAVE_BODY_SIZE);CHECK(original_body);
        memcpy(original_body,c3_game_state_body(&imported,NULL),C3_SAVE_BODY_SIZE);unsigned mails,memos;
        CHECK(c3_game_state_mail_count(&imported,&mails)&&c3_game_state_memo_count(&imported,&memos));
        for(unsigned i=0;i<mails;++i){C3GameMail mail;CHECK(c3_game_state_mail(&imported,i,&mail));}
        for(unsigned i=0;i<memos;++i){C3GameMemo memo;CHECK(c3_game_state_memo(&imported,i,&memo));}
        for(unsigned i=0;i<48;++i){C3PdaSnag snag;CHECK(c3_pda_snag(&assets,&imported,i,&snag));}
        for(unsigned pocket=0;pocket<6;++pocket){static const unsigned counts[]={20,43,16,64,46,3};for(unsigned i=0;i<counts[pocket];++i){C3GameItem item;CHECK(c3_game_state_item(&imported,pocket,i,&item));}}
        CHECK(!memcmp(original_body,c3_game_state_body(&imported,NULL),C3_SAVE_BODY_SIZE));
        size_t after_size;uint8_t *after_card=read_file(save_path,&after_size);CHECK(after_size==original_size&&!memcmp(after_card,original_card,original_size));
        printf("Actual GCI: mail%u memo%u; all122832 live bytes and original card file unchanged by reads.\n",mails,memos);
        free(after_card);free(original_card);free(original_body);c3_game_state_free(&imported);
    }
    printf("Pinned original PDA pack:32 complete letters/read plans,48 snag records/hints,national numbers and fresh saved memo verified.\n");
    c3_game_state_free(&state);free(data);
}
int main(int argc,char **argv)
{
    char directory[]="/tmp/c3-pda-XXXXXX",pack_path[1024],save_path[1024],error[192];CHECK(mkdtemp(directory)!=NULL);
    uint8_t *pack=synthetic_pack();
    /* Synthetic level flags need more than one bit, unlike the basic game
     * fixture. Recompute the actual source-style packed positions. */
    unsigned bit=0;for(unsigned i=0;i<3593;++i){unsigned width=i>=1500&&i<=1596?8:1;pack[FLAGS+i*8]=(uint8_t)(0xc0u|width);w16(pack+FLAGS+i*8+4,bit);bit+=width;}
    w16(pack+BODY+0x7d20+0x400,6);for(unsigned i=0;i<6;++i)w16(pack+BODY+0x7d20+i*2,i);
    sign_pack(pack);snprintf(pack_path,sizeof pack_path,"%s/game.c3g",directory);write_file(pack_path,pack,PACK_SIZE);
    C3GameState state={0},reload={0};CHECK(c3_game_state_new(&state,"WES",7,pack_path,error));
    unsigned count;CHECK(c3_game_state_mail_count(&state,&count)&&count==6);CHECK(c3_game_state_memo_count(&state,&count)&&count==2);
    C3GameMail mail;CHECK(c3_game_state_mail(&state,0,&mail)&&mail.id==0&&!mail.read);
    C3GameMemo memo;CHECK(c3_game_state_memo(&state,0,&memo)&&memo.species==197&&(memo.raw&0x8000));
    size_t bytes;uint8_t *data=read_file("build/pda_fixture.c3pd",&bytes);C3PdaAssets assets={0};CHECK(c3_pda_assets_open(&assets,data,bytes));
    runtime_events(pack_path,&assets);
    C3PdaAssets before_assets=assets;data[bytes-1]^=1;CHECK(!c3_pda_assets_open(&assets,data,bytes));CHECK(!memcmp(&assets,&before_assets,sizeof assets));data[bytes-1]^=1;
    CHECK(!c3_pda_assets_open(&assets,data,63));CHECK(!c3_pda_assets_open(&assets,data,bytes-1));
    char text[8192];CHECK(c3_pda_text(&assets,400,"CUSTOM","RUI",text,sizeof text));CHECK(!strncmp(text,"CUSTOM, ",8));
    CHECK(!c3_pda_text(&assets,400,"CUSTOM","RUI",text,8));CHECK(!text[0]);
    C3PdaMailAction action;CHECK(c3_pda_mail_action(&assets,&state,0,&action));CHECK(action.save.hint_count==1);
    C3PdaMailAction bulk;CHECK(c3_pda_mail_action(&assets,&state,4,&bulk)&&bulk.save.hint_count==29);
    uint8_t *original=malloc(C3_SAVE_BODY_SIZE);CHECK(original);memcpy(original,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE);
    CHECK(c3_game_state_mail_read(&state,&action.save,0));CHECK(!memcmp(original,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE));
    C3GameMailRead broken=action.save;broken.flags[0].id=3593;CHECK(!c3_game_state_mail_read(&state,&broken,1));CHECK(!memcmp(original,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE));
    broken=action.save;broken.hint_count=C3_GAME_MAIL_HINT_MAX+1u;CHECK(!c3_game_state_mail_read(&state,&broken,1));
    CHECK(c3_game_state_mail_read(&state,&action.save,1));CHECK(c3_game_state_mail(&state,0,&mail)&&mail.read);
    uint32_t value;CHECK(c3_game_state_get_flag(&state,2000,&value)&&value==1);
    CHECK(c3_game_state_get_flag(&state,1101,&value)&&value==1);CHECK(c3_game_state_get_flag(&state,1501,&value)&&value==3);
    C3PdaSnag snag;CHECK(c3_pda_snag(&assets,&state,0,&snag)&&snag.hint==3&&!snag.snagged&&!snag.purified);
    memcpy(original,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE);CHECK(c3_game_state_mail_read(&state,&action.save,1));CHECK(!memcmp(original,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE));
    CHECK(c3_game_state_set_flag(&state,1201,1));CHECK(c3_game_state_set_flag(&state,1301,1));CHECK(c3_pda_snag(&assets,&state,0,&snag)&&snag.snagged&&snag.purified);
    C3IngameMenu menu;c3_ingame_init(&menu,&state);c3_ingame_set_assets(&menu,&assets);
    tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_A);CHECK(menu.page==C3_GAME_PDA);
    tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_A);CHECK(menu.page==C3_GAME_NOTICE);
    tap(&menu,&state,C3_PAD_B);CHECK(menu.page==C3_GAME_PDA);CHECK(c3_game_state_set_flag(&state,0x3f0,1));tap(&menu,&state,C3_PAD_A);CHECK(menu.page==C3_GAME_MEMO&&menu.count==2);
    C3GameMemo before_memo;CHECK(c3_game_state_memo(&state,0,&before_memo));tap(&menu,&state,C3_PAD_A);CHECK(menu.page==C3_GAME_MEMO_ENTRY);
    tap(&menu,&state,C3_PAD_B);CHECK(c3_game_state_memo(&state,0,&memo)&&!memcmp(&memo,&before_memo,sizeof memo));tap(&menu,&state,C3_PAD_B);
    tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_A);CHECK(menu.page==C3_GAME_MAIL&&menu.count==6);
    C3IngameEvent event=tap(&menu,&state,C3_PAD_A);CHECK(menu.page==C3_GAME_LETTER&&event.kind==C3_INGAME_READ_MAIL&&event.mail.save.id==5);
    CHECK(c3_game_state_mail(&state,5,&mail)&&!mail.read);CHECK(c3_game_state_mail_read(&state,&event.mail.save,1));
    unsigned char *pixels=calloc(1,320u*240u*3u+32u);CHECK(pixels);memset(pixels+320u*240u*3u,0xa5,32);
    C3BottomUi ui;c3_ui_begin(&ui,pixels,0,NULL,NULL);c3_ingame_draw_to(&menu,&state,&ui);CHECK(menu.pager.more);
    tap(&menu,&state,C3_PAD_A);CHECK(menu.page==C3_GAME_LETTER&&menu.pager.offset>0);
    tap(&menu,&state,C3_PAD_B);tap(&menu,&state,C3_PAD_X);CHECK(menu.page==C3_GAME_MAIL);
    unsigned sort,tone;CHECK(c3_game_state_mail_settings(&state,&sort,&tone)&&sort==1);
    tap(&menu,&state,C3_PAD_Y);CHECK(menu.page==C3_GAME_TONES);tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_A);CHECK(c3_game_state_mail_settings(&state,&sort,&tone)&&tone==1);
    CHECK(!c3_game_state_mail_preferences(&state,3,10));CHECK(c3_game_state_mail_settings(&state,&sort,&tone)&&sort==1&&tone==1);
    tap(&menu,&state,C3_PAD_B);CHECK(menu.page==C3_GAME_PDA);tap(&menu,&state,C3_PAD_UP);tap(&menu,&state,C3_PAD_A);CHECK(menu.page==C3_GAME_MEMO);
    /* Explicitly simulate the aide's update in this owned test state only. */
    uint8_t *test_body=(uint8_t*)c3_game_state_body(&state,NULL);w16(test_body+0x82a8+4,197);
    memcpy(original,test_body,C3_SAVE_BODY_SIZE);
    tap(&menu,&state,C3_PAD_Y);CHECK(menu.page==C3_GAME_MEMO_FILTER);tap(&menu,&state,C3_PAD_RIGHT);
    for(unsigned i=0;i<3;++i)tap(&menu,&state,C3_PAD_DOWN);
    tap(&menu,&state,C3_PAD_A);CHECK(menu.page==C3_GAME_MEMO&&!menu.count);
    tap(&menu,&state,C3_PAD_Y);for(unsigned i=0;i<4;++i)tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_A);CHECK(menu.count==2);
    tap(&menu,&state,C3_PAD_X);tap(&menu,&state,C3_PAD_X);CHECK(menu.sort==2&&menu.count==1);
    tap(&menu,&state,C3_PAD_X);tap(&menu,&state,C3_PAD_X);CHECK(menu.sort==0&&menu.count==2);
    CHECK(!memcmp(original,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE));
    tap(&menu,&state,C3_PAD_B);tap(&menu,&state,C3_PAD_B);CHECK(menu.page==C3_GAME_HUB);
    tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_A);CHECK(menu.page==C3_GAME_OPTIONS);tap(&menu,&state,C3_PAD_RIGHT);tap(&menu,&state,C3_PAD_B);
    C3GameSummary summary;CHECK(c3_game_state_summary(&state,&summary)&&!summary.sound_mono);
    tap(&menu,&state,C3_PAD_A);tap(&menu,&state,C3_PAD_RIGHT);tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_DOWN);
    CHECK(tap(&menu,&state,C3_PAD_A).kind==C3_INGAME_OPTIONS);CHECK(c3_game_state_summary(&state,&summary)&&summary.sound_mono==1);
    tap(&menu,&state,C3_PAD_RIGHT);CHECK(tap(&menu,&state,C3_PAD_A).kind==C3_INGAME_SAVE);
    tap(&menu,&state,C3_PAD_DOWN);tap(&menu,&state,C3_PAD_LEFT);CHECK(tap(&menu,&state,C3_PAD_A).kind==C3_INGAME_LEAVE);
    memcpy(original,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE);
    for(unsigned p=0;p<=C3_GAME_NOTICE;++p){
        menu.page=(C3IngamePage)p;menu.selection=0;menu.detail=0;menu.pocket=0;menu.sort=0;menu.count=p==C3_GAME_PARTY||p==C3_GAME_MEMO?2:0;
        for(unsigned i=0;i<512;++i)menu.order[i]=i;
        c3_ui_begin(&ui,pixels,0,NULL,NULL);c3_ingame_draw_to(&menu,&state,&ui);
        CHECK(!memcmp(original,c3_game_state_body(&state,NULL),C3_SAVE_BODY_SIZE));
        for(unsigned i=0;i<32;++i)CHECK(pixels[320u*240u*3u+i]==0xa5);
    }
    snprintf(save_path,sizeof save_path,"%s/read.gci",directory);CHECK(c3_game_state_save(&state,save_path,error));CHECK(c3_game_state_load(&reload,save_path,pack_path,error));
    CHECK(c3_game_state_mail(&reload,0,&mail)&&mail.read);CHECK(c3_game_state_get_flag(&reload,1501,&value)&&value==3);CHECK(c3_game_state_mail_settings(&reload,&sort,&tone)&&sort==1&&tone==1);
    /* Malformed list counts are rejected, never silently turned into empties. */
    uint8_t *synthetic=(uint8_t*)c3_game_state_body(&reload,NULL);w16(synthetic+0x7d20+0x400,513);CHECK(!c3_game_state_mail_count(&reload,&count));w16(synthetic+0x82a8,501);CHECK(!c3_game_state_memo_count(&reload,&count));
    c3_ingame_close(&menu);c3_game_state_free(&state);c3_game_state_free(&reload);free(original);free(pack);free(data);free(pixels);
    CHECK(unlink(save_path)==0);CHECK(unlink(pack_path)==0);CHECK(rmdir(directory)==0);
    if(argc>=3)audit_original(argv[1],argv[2],argc>=4?argv[3]:NULL,argc>=5?argv[4]:NULL);
    printf("PDA: %u checks passed; source read flags, memo registration,48-entry snag status, menus, bounds and save/reload.\n",checks);return 0;
}
