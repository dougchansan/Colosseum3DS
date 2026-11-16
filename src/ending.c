#include "colosseum3ds/ending.h"
#include <stdio.h>

static int fail(char *error,const char *message)
{
    if(error)snprintf(error,C3_GAME_ERROR_SIZE,"%s",message);
    return -1;
}

int c3_ending_run(C3GameState *state,const char *save_path,
                  const C3EndingServices *services,int *saved,char error[C3_GAME_ERROR_SIZE])
{
    C3GameSummary before;
    uint32_t clear;
    int result;
    if(saved)*saved=0;
    if(!save_path||!services||!services->movie||!services->choice||
       !c3_game_state_summary(state,&before)||before.floor!=0x39f)
        return fail(error,"Staffroll requires its original floor and movie/choice services");
    if(!c3_game_state_get_flag(state,0x476,&clear))
        return fail(error,"Staffroll needs the original flag definitions");
    result=services->movie(services->context);
    if(result)return result<0?fail(error,"Staffroll playback failed"):1;
    if(before.previous_floor==0x76&&clear==1){
        /* These are the source's preparation writes, made before its backup.
         * No party/battle outcome or victory flag is manufactured here. */
        if(!c3_game_state_set_flag(state,0x478,1)||
           !c3_game_state_set_floor(state,2,1,1)||!c3_game_state_set_partner(state,1))
            return fail(error,"Clear-save preparation failed");
        for(;;){
            C3GameState card={0};
            C3GameSummary existing;
            char read_error[C3_GAME_ERROR_SIZE];
            int answer=services->choice(services->context,0x444c,0);
            if(answer<0)return 1;
            if(answer)break;
            /* fn_801D0748(3,2,0) reads the prior card into live memory;
             * inspect it separately instead, preserving the backed-up body.
             * Source tests same trainer ID OR unsaved current game before
             * the default-No overwrite prompt (0x3C02). */
            int confirm=c3_game_state_load(&card,save_path,NULL,read_error)&&
                c3_game_state_summary(&card,&existing)&&existing.saved_count&&
                (existing.trainer_id==before.trainer_id||!before.saved_count);
            c3_game_state_free(&card);
            if(confirm){
                answer=services->choice(services->context,0x3c02,1);
                if(answer<0)return 1;
                if(answer)continue;
            }
            if(c3_game_state_save_snapshot(state,save_path,error)){
                if(saved)*saved=1;
                break;
            }
            if(services->save_error)services->save_error(services->context,error);
        }
    }
    /* floorLink(2,1), then fade script 05960008. The saved checkpoint
     * remains (2,1,1); the live transition comes from staffroll. */
    if(!c3_game_state_set_floor(state,2,0x39f,1))return fail(error,"Staffroll return link failed");
    if(error)error[0]=0;
    return 0;
}
