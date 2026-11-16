/* Synthetic save fixture: no original save bytes are committed. */
#define main ending_fixture_main
#include "test_game_state.c"
#undef main
#include "colosseum3ds/ending.h"
typedef struct EndingProbe {
    int movie_result, answers[8];
    unsigned movies, choices, errors;
    uint32_t messages[8];
    int defaults[8];
} EndingProbe;
static int ending_movie(void *context)
{ EndingProbe *p=context; ++p->movies; return p->movie_result; }
static int ending_choice(void *context,uint32_t message,int initial_no)
{
    EndingProbe *p=context; unsigned i=p->choices++;
    CHECK(i<8);p->messages[i]=message;p->defaults[i]=initial_no;
    return p->answers[i];
}
static void ending_error(void *context,const char *error)
{ EndingProbe *p=context;CHECK(error&&error[0]);++p->errors; }
static void ending_checks(const char *pack,const char *directory)
{
    C3GameState game={0},loaded={0};C3GameSummary before,after;
    char error[C3_GAME_ERROR_SIZE],path[1024],extra[1100];
    unsigned char *body=malloc(C3_SAVE_BODY_SIZE);uint32_t flag;
    CHECK(body);snprintf(path,sizeof path,"%s/clear.gci",directory);
    C3EndingServices services={ending_movie,ending_choice,ending_error,NULL};
    int saved;
    /* Both guard halves, decline, exit during the movie and exit in the
     * prompt. No test obtains the ending flag through the ending adapter. */
    for(unsigned mode=0;mode<6;++mode){
        EndingProbe p={0};services.context=&p;
        CHECK(c3_game_state_new(&game,"WES",0x12345678,pack,error));
        CHECK(c3_game_state_set_floor(&game,0x39f,mode==0?0x75:0x76,0));
        CHECK(c3_game_state_set_flag(&game,0x476,mode==1?0:1));
        memcpy(body,c3_game_state_body(&game,NULL),C3_SAVE_BODY_SIZE);
        p.movie_result=mode==3?1:mode==4?-1:0;
        p.answers[0]=mode==5?-1:1;
        int result=c3_ending_run(&game,path,&services,&saved,error);
        CHECK(!saved&&p.movies==1);
        if(mode==3||mode==4){
            CHECK(result==(mode==3?1:-1)&&!p.choices);
            CHECK(!memcmp(body,c3_game_state_body(&game,NULL),C3_SAVE_BODY_SIZE));
        }else{
            CHECK(result==(mode==5?1:0));
            CHECK(c3_game_state_get_flag(&game,0x476,&flag)&&flag==(mode==1?0u:1u));
            CHECK(c3_game_state_get_flag(&game,0x478,&flag)&&flag==(mode<2?0u:1u));
            CHECK(p.choices==(mode<2?0u:1u));
            if(p.choices)CHECK(p.messages[0]==0x444c&&!p.defaults[0]);
        }
        CHECK(access(path,F_OK)!=0);
    }
    EndingProbe p={0};services.context=&p;
    CHECK(c3_game_state_new(&game,"WES",0x12345678,pack,error));
    CHECK(c3_game_state_set_flag(&game,0x476,1));
    CHECK(c3_game_state_set_floor(&game,0x39f,0x76,0));
    CHECK(c3_game_state_summary(&game,&before));
    memcpy(body,c3_game_state_body(&game,NULL),C3_SAVE_BODY_SIZE);
    CHECK(c3_ending_run(&game,path,&services,&saved,error)==0&&saved);
    CHECK(c3_game_state_summary(&game,&after)&&after.saved_count==before.saved_count);
    CHECK(after.floor==2&&after.previous_floor==0x39f&&after.spawn==1);
    CHECK(c3_game_state_load(&loaded,path,pack,error));
    CHECK(c3_game_state_summary(&loaded,&after)&&after.saved_count==before.saved_count+1);
    CHECK(after.floor==2&&after.previous_floor==1&&after.spawn==1&&c3_game_state_partner(&loaded));
    CHECK(c3_game_state_get_flag(&loaded,0x476,&flag)&&flag==1);
    CHECK(c3_game_state_get_flag(&loaded,0x478,&flag)&&flag==1);
    CHECK(!memcmp(body+PARTY,c3_game_state_body(&loaded,NULL)+PARTY,6*0x138));
    /* An existing matching trainer requires the source default-No prompt.
     * Rejecting it returns to 444C; a subsequent decline writes nothing. */
    memset(&p,0,sizeof p);p.answers[1]=1;p.answers[2]=1;
    CHECK(c3_game_state_set_floor(&game,0x39f,0x76,0));
    CHECK(c3_ending_run(&game,path,&services,&saved,error)==0&&!saved);
    CHECK(p.choices==3&&p.messages[1]==0x3c02&&p.defaults[1]==1&&p.messages[2]==0x444c);
    /* Refused atomic write: one reported error, then re-prompt; the live
     * body/counters remain exactly the prepared backup. */
    memset(&p,0,sizeof p);p.answers[2]=1;
    snprintf(extra,sizeof extra,"%s.tmp",path);write_file(extra,"pending",7);
    CHECK(c3_game_state_set_floor(&game,0x39f,0x76,0));
    CHECK(c3_ending_run(&game,path,&services,&saved,error)==0&&!saved);
    CHECK(p.errors==1&&p.choices==3);
    CHECK(c3_game_state_summary(&game,&after)&&after.saved_count==before.saved_count);
    CHECK(unlink(extra)==0);
    CHECK(c3_game_state_load(&loaded,path,pack,error));
    CHECK(c3_game_state_summary(&loaded,&after)&&after.saved_count==before.saved_count+1);
    c3_game_state_free(&game);c3_game_state_free(&loaded);free(body);
    CHECK(unlink(path)==0);
}

typedef struct {int movie_result, movies, prompts, no, confirm_no;} Service;
static int movie_cb(void *p){Service *s=p;++s->movies;return s->movie_result;}
static int choice_cb(void *p,uint32_t message,int initial_no){Service *s=p;++s->prompts;
    CHECK(message==0x444c||message==0x3c02);CHECK(initial_no==(message==0x3c02));
    if(message==0x3c02)return s->confirm_no;
    return s->no||s->prompts>2;}
int main(void)
{
    char dir[]="/tmp/c3-ending-XXXXXX",pack_path[256],save_path[256],error[C3_GAME_ERROR_SIZE];
    CHECK(mkdtemp(dir));snprintf(pack_path,sizeof pack_path,"%s/game.c3gs",dir);snprintf(save_path,sizeof save_path,"%s/copy.gci",dir);
    unsigned char *pack=synthetic_pack();write_file(pack_path,pack,PACK_SIZE);free(pack);
    C3GameState state={0},reload={0};C3GameSummary summary;int saved;uint32_t flag;
    CHECK(c3_game_state_new(&state,"WES",123,pack_path,error));
    Service service={0};C3EndingServices services={movie_cb,choice_cb,NULL,&service};
    CHECK(c3_ending_run(&state,save_path,&services,&saved,error)==-1);CHECK(!service.movies);
    CHECK(c3_game_state_set_floor(&state,0x39f,0x76,0));
    CHECK(!c3_ending_run(&state,save_path,&services,&saved,error));CHECK(service.movies==1&&!service.prompts&&!saved);
    CHECK(c3_game_state_get_flag(&state,0x478,&flag)&&!flag);
    CHECK(c3_game_state_set_flag(&state,0x476,1));
    CHECK(c3_game_state_set_floor(&state,0x39f,0x75,0));
    CHECK(!c3_ending_run(&state,save_path,&services,&saved,error));CHECK(!service.prompts);
    CHECK(c3_game_state_set_floor(&state,0x39f,0x76,0));service.movie_result=1;
    CHECK(c3_ending_run(&state,save_path,&services,&saved,error)==1);CHECK(!service.prompts);
    CHECK(c3_game_state_summary(&state,&summary)&&summary.floor==0x39f);
    service.movie_result=0;service.no=1;
    CHECK(!c3_ending_run(&state,save_path,&services,&saved,error));CHECK(!saved&&service.prompts==1);
    CHECK(c3_game_state_get_flag(&state,0x478,&flag)&&flag==1);
    CHECK(c3_game_state_set_floor(&state,0x39f,0x76,0));service.no=0;service.prompts=0;
    CHECK(!c3_ending_run(&state,save_path,&services,&saved,error));CHECK(saved);
    CHECK(c3_game_state_summary(&state,&summary)&&summary.floor==2&&summary.previous_floor==0x39f&&summary.spawn==1&&summary.saved_count==0);
    CHECK(c3_game_state_load(&reload,save_path,pack_path,error));
    CHECK(c3_game_state_summary(&reload,&summary)&&summary.floor==2&&summary.previous_floor==1&&summary.spawn==1&&summary.saved_count==1);
    CHECK(c3_game_state_get_flag(&reload,0x478,&flag)&&flag==1);c3_game_state_free(&reload);
    CHECK(c3_game_state_set_floor(&state,0x39f,0x76,0));service.prompts=0;service.confirm_no=1;
    CHECK(!c3_ending_run(&state,save_path,&services,&saved,error));CHECK(!saved&&service.prompts==3);
    c3_game_state_free(&state);unlink(save_path);ending_checks(pack_path,dir);unlink(pack_path);rmdir(dir);
    printf("ending checks: %u\n",checks);return 0;
}
