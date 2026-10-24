/* Move39: original yaw controller, center-null drawable and selected sound.
 * The zero-mesh controller is motion data, never invented GPU geometry. */
static void tail_camera_restore(C3BattleStage *s)
{
    if(s->tail_camera.active){c3_battle_camera_cancel(&s->tail_camera);s->camera=s->tail_camera.camera;}
}
static int tail_pose(C3BattleStage *s,char *e,size_t n)
{
    C3WzxTailWhipPose pose;
    if(!c3_wzx_tail_whip_pose(&s->tail,&pose,e,n))return 0;
    float *owner=s->transforms[s->tail_owner],*effect=s->transforms[s->tail_model];
    memcpy(owner,pose.owner_position,12);owner[3]=pose.owner_yaw;owner[4]=pose.owner_scale;
    memcpy(effect,pose.effect_position,12);effect[3]=pose.effect_yaw;effect[4]=pose.effect_scale;
    return 1;
}
static void tail_cancel(C3BattleStage *s)
{
    if(s->tail.program.motion_data){c3_wzx_tail_whip_cancel(&s->tail);(void)tail_pose(s,NULL,0);}
    tail_camera_restore(s);
}
static int tail_load(C3BattleStage *s,const char *root,char *e,size_t n)
{
    char path[512];
    if(snprintf(path,sizeof path,"%s/tail_whip.c3w",root)>=(int)sizeof path)return fail(e,n,"Tail Whip path too long.");
    errno=0;FILE *f=fopen(path,"rb");
    if(!f)return errno==ENOENT?1:fail(e,n,"Cannot inspect Tail Whip descriptor.");
    if(fclose(f)||s->count>=C3_BATTLE_STAGE_MODELS)return fail(e,n,"Tail Whip model capacity.");
    if(!c3_wzx_tail_whip_load(&s->tail_program,path,e,n))return 0;
    if(snprintf(path,sizeof path,"%s/tail_whip_controller.c3m",root)>=(int)sizeof path||
       !c3_wzx_tail_whip_motion_load(&s->tail_program,path,e,n))return 0;
    for(uint32_t i=0;i<s->count;++i)if(s->slots[i]==2||s->slots[i]==3){
        const C3Title *m=&s->models[i];
        if(s->roles[i][0]!=0||m->fps!=120||m->clips[0].frame_count!=99||!m->clips[0].loop)
            return fail(e,n,"Tail Whip requires admitted Zigzagoon idle entry0.");
    }
    if(snprintf(path,sizeof path,"%s/tail_whip_effect.c3t",root)>=(int)sizeof path)return 0;
    uint32_t i=s->count++;s->tail_model=i;s->slots[i]=UINT32_MAX-4u;
    if(!c3_title_load(&s->models[i],path,e,n))return 0;
    C3Title *m=&s->models[i];uint8_t digest[20];
    if(m->file_bytes>UINT32_MAX)return fail(e,n,"Tail Whip model hash bound.");
    c3_save_codec_sha1(digest,m->file_data,(uint32_t)m->file_bytes);
    if(memcmp(digest,s->tail_program.effect_sha1,20)||m->clip_count!=1||m->fps!=120||
       m->clips[0].frame_count!=91||m->clips[0].loop||m->batch_count!=2||m->bone_count!=2)
        return fail(e,n,"Tail Whip drawable identity mismatch.");
    s->draw[i]=m;s->transforms[i][4]=1;s->tail_available=1;
    if(snprintf(path,sizeof path,"%s/tailwhip_camera.c3c",root)>=(int)sizeof path)return 0;
    errno=0;f=fopen(path,"rb");
    if(f){
        if(fclose(f)||!c3_battle_camera_tailwhip_load(&s->tail_camera_program,path,e,n))return 0;
        c3_battle_camera_init(&s->tail_camera,0x5441494cu);s->tail_camera_available=1;
    }else if(errno!=ENOENT)return fail(e,n,"Cannot inspect Tail Whip camera.");
    if(s->trace){fprintf(s->trace,"wzx_tail_whip_admitted controller=yaw_only drawable=original center_null=position_only camera=%s\n",s->tail_camera_available?"source":"explicitly_incomplete");fflush(s->trace);}
    return 1;
}
static int tail_event(C3BattleStage *s,const C3BattlePreviewEvent *ev,char *e,size_t n)
{
    if(s->bite.active||s->damage.active||s->reflect.active||s->confusion_active||s->tail.active||s->growl.active)
        return fail(e,n,"Overlapping source effect ownership.");
    uint32_t owner=s->count;
    for(uint32_t i=0;i<s->count;++i)if(s->slots[i]==ev->actor){owner=i;break;}
    if(owner==s->count||ev->actor<2||ev->actor>3)return fail(e,n,"Unqualified Tail Whip owner.");
    if(!s->sound_cue||!s->sound_done)return fail(e,n,"Tail Whip requires source sound and completion.");
    const float *t=s->transforms[owner];
    if(!c3_wzx_tail_whip_begin(&s->tail,&s->tail_program,t,t[3],t[4]))return fail(e,n,"Tail Whip initialization failed.");
    s->tail_owner=owner;s->tail_event=ev->sequence;s->tail_cue=0;
    s->clips[owner]=s->roles[owner][0];s->clocks[owner]=0;
    if(!tail_pose(s,e,n))return 0;
    if(s->tail_camera_available){C3BattleCameraActor actors[4];camera_actors(s,actors);
        unsigned target=ev->target<4?ev->target:0;
        if(!c3_battle_camera_begin_move(&s->tail_camera,&s->tail_camera_program,39,C3_BATTLE_CAMERA_ATTACK,ev->actor,target,actors,&s->camera,e,n))return 0;
        s->camera=s->tail_camera.camera;
    }
    c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;
    if(s->trace){fprintf(s->trace,"wzx_tail_whip_start event=%lu owner=%u dispatcher=attack_once spread_targets=coordinator\n",(unsigned long)ev->sequence,(unsigned)ev->actor);fflush(s->trace);}
    return 1;
}
static int tail_step(C3BattleStage *s,uint32_t ms,char *e,size_t n)
{
    if(!s->tail.active)return 1;
    if(!c3_wzx_tail_whip_step(&s->tail,ms))return fail(e,n,"Tail Whip clock overload.");
    if(!s->tail_cue){
        if(s->cue_serial==UINT32_MAX)return fail(e,n,"Audio cue identity exhausted.");
        s->tail_cue=++s->cue_serial;
        if(!s->sound_cue(s->tail_cue,s->tail_program.sound_id,e,n))return 0;
    }
    int audio=s->sound_done(s->tail_cue,s->tail_program.sound_id,e,n);if(audio<0)return 0;
    const C3Title *effect=&s->models[s->tail_model];
    int controller_done=s->tail.controller_seconds*120.0f>=62.0f;
    int effect_done=s->tail.effect_seconds*effect->fps>=(float)(effect->clips[0].frame_count-1u);
    if(!c3_wzx_tail_whip_signals(&s->tail,controller_done,effect_done,audio)||!tail_pose(s,e,n))return fail(e,n,"Tail Whip model completion failed.");
    if(s->tail_camera.active){C3BattleCameraActor actors[4];camera_actors(s,actors);
        if(!c3_battle_camera_step(&s->tail_camera,ms,actors,e,n))return 0;
        s->camera=s->tail_camera.camera;
    }
    if(c3_wzx_tail_whip_finish(&s->tail)){
        tail_camera_restore(s);
        if(s->trace){fprintf(s->trace,"wzx_tail_whip_complete event=%lu owner=%lu units=%llu controller_done=1 effect_done=1 audio_done=1 cue=%lu owner_restored=1\n",(unsigned long)s->tail_event,(unsigned long)s->slots[s->tail_owner],(unsigned long long)s->tail.units,(unsigned long)s->tail_cue);fflush(s->trace);}
    }
    return 1;
}
