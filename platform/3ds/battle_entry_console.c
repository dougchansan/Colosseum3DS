/* Console owner for the isolated original trainer-entry session. */
#include "battle_entry_console.h"
#include "n3ds_platform.h"
#include "colosseum3ds/battle_entry_pack.h"
#include "colosseum3ds/battle_entry_preview.h"
#include <stdlib.h>
#include <string.h>

#define ENTRY_PACK "sdmc:/3ds/Colosseum3DS/boot/willie.c3e"
#define ENTRY_PACK_MAX (2u*1024u*1024u)
#define SESSION_SEED 0x12345678u /* labelled native seed shared with the preview */

/* Original sequence-loader resources (group, member) for the trainers the
 * stage already holds, mapped to their battle-scene slots. */
static const struct { uint32_t group, member; unsigned slot; const char *name; } stage_models[]={
    {0x9du,0x030c1e00u,4,"wes"},{0x2dbu,0x058a1e00u,5,"willie"}};

typedef struct {
    FILE *trace;
    uint32_t next, slot_of[8];
    unsigned loads, hidden, shown, grids, updates, releases, aborted, unmapped;
} ConsoleScene;

static int32_t scene_state(void *u,uint32_t group){(void)u;(void)group;return 0;} /* stage-resident */
static int scene_request(void *u,uint32_t group){(void)u;(void)group;return 1;}
static uint32_t scene_find(void *u,uint32_t group,uint32_t member)
{
    ConsoleScene *s=u;
    for(unsigned i=0;i<sizeof stage_models/sizeof *stage_models;++i)
        if(stage_models[i].group==group&&stage_models[i].member==member)return i+1u;
    /* An unmapped original resource is not silently treated as a source NULL. */
    ++s->unmapped;
    if(s->trace)fprintf(s->trace,"entry_scene_unmapped group=%lx member=%08lx\n",(unsigned long)group,(unsigned long)member);
    return 0;
}
static uint32_t scene_allocate(void *u){ConsoleScene *s=u;return s->next<7?++s->next:0;}
static int scene_fields(void *u,uint32_t q,uint16_t index,uint16_t group,uint8_t flags)
{
    ConsoleScene *s=u;
    if(s->trace)fprintf(s->trace,"entry_sequence seq=%lu index=%u group=%x flags=%u\n",(unsigned long)q,index,group,flags);
    return q&&q<8;
}
static int scene_load(void *u,uint32_t q,uint32_t resource)
{
    ConsoleScene *s=u;
    if(!q||q>=8||!resource||resource>sizeof stage_models/sizeof *stage_models)return -1;
    s->slot_of[q]=stage_models[resource-1u].slot;++s->loads;
    if(s->trace)fprintf(s->trace,"entry_sequence_load seq=%lu model=%s stage_slot=%u\n",
        (unsigned long)q,stage_models[resource-1u].name,stage_models[resource-1u].slot);
    return 1;
}
static void scene_release(void *u,uint32_t q){(void)q;++((ConsoleScene *)u)->releases;}
static int scene_hide(void *u,uint32_t q){(void)q;++((ConsoleScene *)u)->hidden;return 1;}
static void scene_report(void *u,uint32_t group)
{ConsoleScene *s=u;if(s->trace)fprintf(s->trace,"entry_scene_negative_state group=%lx\n",(unsigned long)group);}
static int scene_grid(void *u,uint32_t q,uint8_t x,uint8_t y)
{
    ConsoleScene *s=u;if(!q||q>=8)return 0;++s->grids;
    if(s->trace)fprintf(s->trace,"entry_grid seq=%lu stage_slot=%u x=%u y=%d\n",(unsigned long)q,(unsigned)s->slot_of[q],x,(int)(int8_t)y);
    return 1;
}
static int scene_grid_update(void *u){++((ConsoleScene *)u)->updates;return 1;}
static int scene_visible(void *u,uint32_t q,int visible)
{
    ConsoleScene *s=u;if(!q||q>=8)return 0;s->shown+=visible?1u:0u;
    if(s->trace)fprintf(s->trace,"entry_visible seq=%lu stage_slot=%u visible=%d\n",(unsigned long)q,(unsigned)s->slot_of[q],visible);
    return 1;
}
static void scene_abort(void *u,const C3BattleEntryOwnership *own,size_t n){(void)own;((ConsoleScene *)u)->aborted+=(unsigned)n;}

static uint16_t be16(const uint8_t *p){return (uint16_t)((uint16_t)p[0]<<8|p[1]);}
static uint32_t be32(const uint8_t *p){return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}

static uint8_t *read_pack(size_t *size)
{
    FILE *f=fopen(ENTRY_PACK,"rb");if(!f)return NULL;
    uint8_t *data=NULL;long n=-1;
    if(!fseek(f,0,SEEK_END))n=ftell(f);
    if(n>0&&(unsigned long)n<=ENTRY_PACK_MAX&&!fseek(f,0,SEEK_SET)&&(data=malloc((size_t)n))&&
       fread(data,1,(size_t)n,f)==(size_t)n){*size=(size_t)n;}
    else{free(data);data=NULL;*size=0;}
    fclose(f);return data;
}
int c3_battle_entry_console_run(uint16_t encounter_id,const uint8_t *body,size_t body_size,FILE *trace,
    const C3BattleRequest *request,const C3BattlePreviewTables *tables,C3BattlePreview *battle,char *error_out)
{
    size_t size=0;uint64_t start=c3_n3ds_time_us();
    uint8_t *bytes=read_pack(&size);
    if(!bytes){if(trace){fprintf(trace,"entry_session skipped reason=pack_unavailable\n");fflush(trace);}return 0;}
    C3BattleEntryPack *pack=malloc(sizeof *pack);C3BattleEntrySession *session=calloc(1,sizeof *session);
    C3BattleEntrySessionConfig config;ConsoleScene scene={0};char error[C3_BATTLE_ERROR_SIZE]={0};int result=-1,entered=0;
    scene.trace=trace;
    if(!pack||!session)snprintf(error,sizeof error,"Entry session allocation failed");
    else if(c3_battle_entry_pack_decode(pack,bytes,size,error)&&
            c3_battle_entry_pack_config(pack,encounter_id,body,body_size,SESSION_SEED,&config,error)){
        config.sequence=(C3BattleSequenceServices){scene_state,scene_request,scene_find,scene_allocate,
            scene_fields,scene_load,scene_release,scene_hide,scene_report};
        config.grid_add=scene_grid;config.grid_update=scene_grid_update;config.set_visible=scene_visible;
        config.abort_owned=scene_abort;config.scene=&scene;
        if(trace){fprintf(trace,"entry_session_begin encounter=%u seed=%08lx seed_source=native_preview_seed preset_mode=%u trainers=%u/%u,%u/%u\n",
            encounter_id,(unsigned long)SESSION_SEED,config.preset_mode,config.requests[0].trainer_id,
            (unsigned)config.requests[0].input_device,config.requests[1].trainer_id,(unsigned)config.requests[1].input_device);fflush(trace);}
        if(!c3_battle_entry_session_begin(session,&config))snprintf(error,sizeof error,"Entry session admission failed");
        else{
            int r;unsigned guard=0;
            while((r=c3_battle_entry_session_step(session))==1&&++guard<64){}
            if(r==0&&!scene.unmapped)result=entered=1;
            else snprintf(error,sizeof error,"Entry stopped at %s",r==0?"unmapped_resource":
                session->failure?session->failure:guard>=64?"step_limit":"action_frame");
            if(result<0)c3_battle_entry_session_abort(session);
            /* Hand the active FightPokemon and the session RNG to combat while
             * the session (and the pack it borrows) is still alive. */
            else if(!c3_battle_entry_preview_begin(battle,request,session,tables,error))result=-1;
        }
    }
    if(trace){
        if(entered){
            fprintf(trace,"entry_session result=%lu,%lu steps=%lu waits=%lu rng_seed=%08lx rng_draws=%llu entry_counter=%u loads=%u grids=%u shown=%u hidden=%u\n",
                (unsigned long)session->source_result[0],(unsigned long)session->source_result[1],
                (unsigned long)session->steps,(unsigned long)session->waits,(unsigned long)session->rng.seed,
                (unsigned long long)session->rng.draws,session->entry_counter,scene.loads,scene.grids,scene.shown,scene.hidden);
            for(unsigned side=0;side<2;++side){
                const uint8_t *rt=c3_battle_entry_session_runtime(session,side);
                fprintf(trace,"entry_trainer side=%u trainer=%u sequence=%lu\n",side,be16(rt),(unsigned long)be32(rt+0x27c0));
                for(unsigned j=0;j<6;++j){const uint8_t *f=c3_battle_entry_session_fighter(session,side,j);
                    if(!be32(f))continue;
                    const uint8_t *p=f+4;
                    fprintf(trace,"entry_fighter side=%u slot=%u species=%u level=%u hp=%u/%u personality=%08lx entry_id=%d\n",
                        side,j,be16(p),p[0x60],be16(p+0x8a),be16(p+0x8c),(unsigned long)be32(p+4),(int)(int16_t)be16(f+0x14c));}
            }
            if(result==1)fprintf(trace,"entry_preview_begin source=entry_fightpokemon rng_seed=%08lx rng_draws=%llu hp=%u,%u,%u,%u\n",
                (unsigned long)battle->rng.seed,(unsigned long long)battle->rng.draws,
                battle->mons[0].hp,battle->mons[1].hp,battle->mons[2].hp,battle->mons[3].hp);
            else fprintf(trace,"entry_preview_failed error=%s\n",error);
        }else fprintf(trace,"entry_session failed error=%s aborted=%u\n",error,scene.aborted);
        fprintf(trace,"entry_session_end wall_us=%llu save_writes=0 gsvm_resumed=0 authoritative=0\n",
            (unsigned long long)(c3_n3ds_time_us()-start));
        c3_n3ds_log_memory(trace,"entry_session");fflush(trace);
    }
    if(result<0&&error_out)snprintf(error_out,C3_BATTLE_ERROR_SIZE,"%s",error[0]?error:"Trainer entry failed.");
    free(session);free(pack);free(bytes);
    return result;
}
