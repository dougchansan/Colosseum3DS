/* Source Reflect owns one model, floor controller and shared particle system. */
static int reflect_load(C3BattleStage *s,const char *root,char *e,size_t n)
{
    char path[512];
    if(snprintf(path,sizeof path,"%s/reflect.c3w",root)>=(int)sizeof path)return 0;
    errno=0;FILE *f=fopen(path,"rb");
    if(!f)return errno==ENOENT?1:fail(e,n,"Cannot inspect Reflect descriptor.");
    if(fclose(f)||s->count>=C3_BATTLE_STAGE_MODELS)return fail(e,n,"Reflect model capacity.");
    if(!c3_wzx_reflect_load(&s->reflect_program,path,e,n))return 0;
    if(snprintf(path,sizeof path,"%s/reflect_parts.c3r",root)>=(int)sizeof path||!c3_wzx_reflect_parts_load(&s->reflect_program,path,e,n))return 0;
    if(snprintf(path,sizeof path,"%s/reflect.c3t",root)>=(int)sizeof path)return 0;
    uint32_t i=s->count++;s->reflect_model=i;s->slots[i]=UINT32_MAX-3u;
    if(!c3_title_load(&s->models[i],path,e,n))return 0;
    C3Title *m=&s->models[i];uint8_t hash[20];
    if(m->file_bytes>UINT32_MAX)return fail(e,n,"Reflect model exceeds hash bound.");
    c3_save_codec_sha1(hash,m->file_data,(uint32_t)m->file_bytes);
    if(memcmp(hash,s->reflect_program.model_sha1,20)||m->file_bytes!=176344||m->batch_count!=1||m->clip_count!=1||m->clips[0].loop)return fail(e,n,"Reflect model identity mismatch.");
    s->draw[i]=m;s->transforms[i][4]=1;
    if(snprintf(path,sizeof path,"%s/reflect.c3p",root)>=(int)sizeof path)return 0;
    f=fopen(path,"rb");if(!f)return fail(e,n,"Missing Reflect particle pack.");
    int ok=!fseek(f,0,SEEK_END);long length=ok?ftell(f):-1;
    if(length<=0||(unsigned long)length>C3_PARTICLE_PACK_MAX_BYTES||fseek(f,0,SEEK_SET))ok=0;
    if(ok){s->reflect_bytes=malloc((size_t)length);ok=s->reflect_bytes&&fread(s->reflect_bytes,1,(size_t)length,f)==(size_t)length&&!ferror(f);}
    if(fclose(f))ok=0;
    if(!ok)return fail(e,n,"Cannot read Reflect particles.");
    if(!c3_particle_pack_decode(&s->reflect_pack,s->reflect_bytes,(size_t)length,s->reflect_program.particles_sha1,e,n))return 0;
    s->reflect_available=1;return 1;
}
static int reflect_start(C3BattleStage *s,unsigned slot,char *e,size_t n)
{
    for(uint32_t i=0;i<s->count;++i)if(s->slots[i]==slot){s->reflect_owner=i;break;}
    if(!c3_wzx_reflect_begin(&s->reflect,&s->reflect_program,slot==1?196:197))return fail(e,n,"Reflect clock initialization failed.");
    s->reflect_particles=0;s->reflect_cue=0;
    memcpy(s->transforms[s->reflect_model],s->transforms[s->reflect_owner],sizeof s->transforms[0]);
    s->transforms[s->reflect_model][4]=.75f;
    s->clips[s->reflect_owner]=s->roles[s->reflect_owner][0];s->clocks[s->reflect_owner]=0;
    return 1;
}
static int reflect_event(C3BattleStage *s,const C3BattlePreviewEvent *ev,char *e,size_t n)
{
    /* Typed committed STATUS plus a newly enabled side screen is the success
     * proof. Message text, attempted moves and existing screens are insufficient. */
    const C3BattlePreviewEvent *status=NULL;
    for(uint32_t j=s->presentation.cursor+1;j<s->presentation.count&&s->presentation.events[j].group==ev->group;++j){
        const C3BattlePreviewEvent *q=&s->presentation.events[j];
        if(q->kind==C3_PREVIEW_EVENT_MISS)return 1;
        if(q->kind==C3_PREVIEW_EVENT_STATUS&&q->actor==ev->actor&&q->move==115&&q->reflect[0]&&!ev->reflect[0])status=q;
    }
    if(!status)return 1;
    if(s->bite.active||s->damage.active||s->reflect.active)return fail(e,n,"Overlapping source effect ownership.");
    s->reflect_alive=0;for(unsigned i=0;i<4;++i)if(status->hp[i])s->reflect_alive|=1u<<i;
    s->reflect_index=0;s->reflect_event=ev->sequence;s->reflect_group=ev->group;
    unsigned owner=c3_wzx_reflect_actor(ev->actor,s->reflect_alive,0);
    if(owner==C3_WZX_REFLECT_NO_ACTOR)return 1;
    if(!reflect_start(s,owner,e,n))return 0;
    c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;
    if(s->trace){fprintf(s->trace,"wzx_reflect_committed event=%lu group=%lu alive=%u\n",(unsigned long)ev->sequence,(unsigned long)ev->group,s->reflect_alive);fflush(s->trace);}
    return 1;
}
static int reflect_step(C3BattleStage *s,uint32_t ms,char *e,size_t n)
{
    if(!s->reflect.active)return 1;
    if(!c3_wzx_reflect_step(&s->reflect,ms))return fail(e,n,"Reflect clock overload.");
    float pos[3];const float *t=s->transforms[s->reflect_owner];
    if(!c3_wzx_reflect_position(&s->reflect_program,s->reflect.species,s->reflect.owner_seconds,t,t[3],t[4],pos,e,n))return 0;
    /* Model position1 and particle POSITION share the checked live source
     * part, but model scale is the independent sequence scale. */
    memcpy(s->transforms[s->reflect_model],pos,sizeof pos);
    for(unsigned i=0;i<3;++i)pos[i]*=10;
    if(!s->reflect_particles){
        if(!c3_particle_begin_position_flags(&s->particles,&s->reflect_pack.bank,0,0x5245464cu^s->reflect_event^s->reflect_index,pos,t[3],.75f,.75f,0x18A9,e,n))return 0;
        s->reflect_particles=1;
    }else if(!c3_particle_position(&s->particles,pos,e,n)||!c3_particle_step(&s->particles,ms,0,e,n))return 0;
    if(!s->reflect_cue){
        if(!s->sound_cue||!s->sound_done)return fail(e,n,"Reflect requires source audio playback and completion.");
        if(s->cue_serial==UINT32_MAX)return fail(e,n,"Audio cue identity exhausted.");
        s->reflect_cue=++s->cue_serial;
        if(!s->sound_cue(s->reflect_cue,s->reflect_program.sound_id,e,n))return 0;
    }
    int audio=s->sound_done(s->reflect_cue,s->reflect_program.sound_id,e,n);if(audio<0)return 0;
    if(c3_wzx_reflect_finish(&s->reflect,s->reflect.model_finished,c3_particle_finished(&s->particles),s->reflect.floor_finished,audio)){
        unsigned ally=c3_wzx_reflect_actor(1,s->reflect_alive,++s->reflect_index);
        if(s->trace){fprintf(s->trace,"wzx_reflect_instance_complete event=%lu owner=%lu cue=%lu\n",(unsigned long)s->reflect_event,(unsigned long)s->slots[s->reflect_owner],(unsigned long)s->reflect_cue);fflush(s->trace);}
        if(ally!=C3_WZX_REFLECT_NO_ACTOR)return reflect_start(s,ally,e,n);
    }
    return 1;
}
static int reflect_bind_particles(C3BattleStage *s,char *e,size_t n)
{
    static const float identity[12]={.1f,0,0,0,0,.1f,0,0,0,0,.1f,0};
    if(!c3_stage_renderer_particles(s->reflect_particles?&s->particles:NULL,identity))return fail(e,n,"Reflect particle binding failed.");
    return 1;
}
