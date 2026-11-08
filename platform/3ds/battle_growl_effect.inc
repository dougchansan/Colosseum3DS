/* Original Growl: one caster sequence, source-family teardown at control0. */
static void growl_camera_restore(C3BattleStage *s)
{
    if(s->growl_camera.active){c3_battle_camera_cancel(&s->growl_camera);s->camera=s->growl_camera.camera;}
}
static void growl_release_owner(C3BattleStage *s)
{
    /* Pokemon owner[75]==1 bypasses ApplyStop's forced idle. Its original
     * motion callback owns the remaining clip, independently of WZX status. */
    if(s->growl_owner<s->count){unsigned i=s->growl_owner;s->growl_motion[i]=1;
        s->growl_motion_units[i]=s->growl.units;s->clocks[i]=s->growl.owner_seconds;s->fresh[i]=1;}
}
static void growl_cancel(C3BattleStage *s)
{
    if(s->growl.active){
        if(s->growl_cue&&!s->growl_keyoff_sent&&s->sound_stop){char error[160]={0};
            if(!s->sound_stop(s->growl_cue,0x210,error,sizeof error)&&s->trace)
                fprintf(s->trace,"wzx_growl_cancel_audio_error error=%s\n",error);
        }
        if(s->particles.bank.profile==C3_PARTICLE_PROFILE_GROWL_V1)c3_particle_cancel(&s->particles);
        if(!s->growl_teardown_started)growl_release_owner(s);
        c3_wzx_growl_cancel(&s->growl);
    }
    growl_camera_restore(s);
}
static int growl_load(C3BattleStage *s,const char *root,char *e,size_t n)
{
    char path[512];
    if(snprintf(path,sizeof path,"%s/growl.c3w",root)>=(int)sizeof path)return fail(e,n,"Growl path too long.");
    errno=0;FILE *f=fopen(path,"rb");
    if(!f)return errno==ENOENT?1:fail(e,n,"Cannot inspect Growl descriptor.");
    if(fclose(f)||s->count>=C3_BATTLE_STAGE_MODELS)return fail(e,n,"Growl model capacity.");
    if(!c3_wzx_growl_load(&s->growl_program,path,e,n))return 0;
    for(uint32_t i=0;i<s->count;++i)if(s->slots[i]==2||s->slots[i]==3){const C3Title *m=&s->models[i];
        if(s->roles[i][2]!=1||m->fps!=120||m->clips[1].frame_count!=367||m->clips[1].loop)
            return fail(e,n,"Growl requires admitted Zigzagoon entry1.");
    }
    if(snprintf(path,sizeof path,"%s/growl_model.c3t",root)>=(int)sizeof path)return 0;
    uint32_t i=s->count++;s->growl_model=i;s->slots[i]=UINT32_MAX-5u;
    if(!c3_title_load(&s->models[i],path,e,n))return 0;
    C3Title *m=&s->models[i];uint8_t digest[20];
    if(m->file_bytes>UINT32_MAX)return fail(e,n,"Growl model hash bound.");
    c3_save_codec_sha1(digest,m->file_data,(uint32_t)m->file_bytes);
    if(memcmp(digest,s->growl_program.model_sha1,20)||m->clip_count!=1||m->fps!=120||
       m->clips[0].frame_count!=79||m->clips[0].loop||m->batch_count!=22||
       m->bone_count<=s->growl_program.retained_bone||!c3_title_animate(m,0,0))
        return fail(e,n,"Growl model identity or retained part mismatch.");
    for(unsigned k=0;k<3;++k)if(fabsf(m->pose.matrices[s->growl_program.retained_bone][4*k+3]-s->growl_program.emitter_anchor[k])>.00001f)
        return fail(e,n,"Growl linked source-part9 translation mismatch.");
    s->draw[i]=m;s->transforms[i][4]=1;
    if(snprintf(path,sizeof path,"%s/growl.c3p",root)>=(int)sizeof path)return 0;
    f=fopen(path,"rb");if(!f)return fail(e,n,"Missing Growl particles.");
    int ok=!fseek(f,0,SEEK_END);long length=ok?ftell(f):-1;
    if(length<=0||(unsigned long)length>C3_PARTICLE_PACK_MAX_BYTES||fseek(f,0,SEEK_SET))ok=0;
    if(ok){s->growl_bytes=malloc((size_t)length);ok=s->growl_bytes&&fread(s->growl_bytes,1,(size_t)length,f)==(size_t)length&&!ferror(f);}
    if(fclose(f))ok=0;
    if(!ok)return fail(e,n,"Cannot read Growl particles.");
    if(!c3_particle_pack_decode_reachable(&s->growl_pack,s->growl_bytes,(size_t)length,
        s->growl_program.particles_sha1,C3_PARTICLE_PROFILE_GROWL_V1,e,n))return 0;
    if(snprintf(path,sizeof path,"%s/audio.c3u",root)>=(int)sizeof path)return 0;
    C3BattleAudio audio={0};if(!c3_battle_audio_load(&audio,path,e,n))return 0;
    ok=c3_battle_audio_find(&audio,0x210)!=NULL;c3_battle_audio_close(&audio);
    if(!ok)return fail(e,n,"Growl requires selected sound0x210.");
    if(snprintf(path,sizeof path,"%s/growl_camera.c3c",root)>=(int)sizeof path)return 0;
    errno=0;f=fopen(path,"rb");
    if(f){if(fclose(f)||!c3_battle_camera_growl_load(&s->growl_camera_program,path,e,n))return 0;
        c3_battle_camera_init(&s->growl_camera,0x47524f57u);s->growl_camera_available=1;
    }else if(errno!=ENOENT)return fail(e,n,"Cannot inspect Growl camera.");
    s->growl_available=1;
    if(s->trace){fprintf(s->trace,"wzx_growl_admitted model=source part58=one_shot particle_part9=%lu source_ids=18,93,111,112 app_srt=qualified camera=%s\n",(unsigned long)s->growl_program.retained_bone,s->growl_camera_available?"source":"explicitly_incomplete");fflush(s->trace);}
    return 1;
}
static int growl_event(C3BattleStage *s,const C3BattlePreviewEvent *ev,char *e,size_t n)
{
    if(s->bite.active||s->damage.active||s->reflect.active||s->confusion_active||s->tail.active||s->growl.active)
        return fail(e,n,"Overlapping source effect ownership.");
    unsigned owner=s->count;
    for(unsigned i=0;i<s->count;++i)if(s->slots[i]==ev->actor){owner=i;break;}
    if(owner==s->count||ev->actor<2||ev->actor>3)return fail(e,n,"Unqualified Growl owner.");
    if(!s->sound_cue||!s->sound_status||!s->sound_stop)return fail(e,n,"Growl requires source audio status and stop.");
    if(!c3_wzx_growl_begin(&s->growl,&s->growl_program))return fail(e,n,"Growl initialization failed.");
    s->growl_owner=owner;s->growl_event=ev->sequence;s->growl_cue=0;s->growl_particles_started=0;
    s->growl_teardown_started=s->growl_keyoff_sent=0;s->growl_audio_wait_ms=0;
    s->growl_motion[owner]=1;s->growl_motion_units[owner]=0;
    c3_particle_cancel(&s->particles);s->clips[owner]=s->roles[owner][2];s->clocks[owner]=0;
    if(s->growl_camera_available){C3BattleCameraActor actors[4];camera_actors(s,actors);
        if(!c3_battle_camera_begin_move(&s->growl_camera,&s->growl_camera_program,45,C3_BATTLE_CAMERA_ATTACK,ev->actor,ev->target<4?ev->target:0,actors,&s->camera,e,n))return 0;
        s->camera=s->growl_camera.camera;
        if(s->trace)fprintf(s->trace,"wzx_growl_camera_begin owner=%u motion=%lu duration=%lu fov_pattern=%lu draws=%lu\n",(unsigned)ev->actor,(unsigned long)s->growl_camera.motion,(unsigned long)s->growl_camera.duration,(unsigned long)s->growl_camera.fov_pattern,(unsigned long)s->growl_camera.random_draws);
    }
    c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;
    if(s->trace){fprintf(s->trace,"wzx_growl_start event=%lu owner=%u dispatcher=attack_once stop_tick=230\n",(unsigned long)ev->sequence,(unsigned)ev->actor);fflush(s->trace);}
    return 1;
}
static int growl_step(C3BattleStage *s,uint32_t ms,char *e,size_t n)
{
    if(!s->growl.active)return 1;
    uint32_t starts=s->growl.model_starts;
    if(!c3_wzx_growl_step(&s->growl,ms))return fail(e,n,"Growl clock overload.");
    if(!starts&&s->growl.model_starts){float model[3],emitter[3];const float *owner=s->transforms[s->growl_owner];
        if(!c3_wzx_growl_anchors(&s->growl_program,owner,owner[3],owner[4],model,emitter,e,n))return 0;
        memcpy(s->transforms[s->growl_model],model,sizeof model);s->transforms[s->growl_model][3]=owner[3];s->transforms[s->growl_model][4]=s->growl_program.class_scale;
        for(unsigned k=0;k<3;++k)emitter[k]*=10;
        if(!c3_particle_begin_position_flags(&s->particles,&s->growl_pack.bank,18,0x47524f57u^s->growl_event,emitter,owner[3],.75f,.75f,0x10a1,e,n))return 0;
        s->growl_particles_started=1;
        if(s->trace){fprintf(s->trace,"wzx_growl_resources_start units=%llu original_part9=%lu model_xyz=%.5f,%.5f,%.5f emitter_source=%.5f,%.5f,%.5f\n",(unsigned long long)s->growl.units,(unsigned long)s->growl_program.retained_bone,model[0],model[1],model[2],emitter[0],emitter[1],emitter[2]);fflush(s->trace);}
    }else if(s->growl_particles_started&&!s->growl.stop_requested&&!c3_particle_step(&s->particles,ms,0,e,n))return 0;
    if(s->growl.sound_requests&&!s->growl_cue){
        if(s->cue_serial==UINT32_MAX)return fail(e,n,"Audio cue identity exhausted.");
        s->growl_cue=++s->cue_serial;
        if(!s->sound_cue(s->growl_cue,0x210,e,n))return 0;
    }
    if(s->growl.stop_requested){
        if(!s->growl_teardown_started){
            c3_particle_cancel(&s->particles);growl_release_owner(s);growl_camera_restore(s);
            s->growl_teardown_started=1;
        }
        if(!s->growl_cue)return fail(e,n,"Growl stop has no source cue identity.");
        if(!s->growl_keyoff_sent){
            if(!s->sound_stop(s->growl_cue,0x210,e,n))return 0;
            s->growl_keyoff_sent=1;
        }
        int status=s->sound_status(s->growl_cue,0x210,e,n);
        if(status<0)return 0;
        if(status==C3_BATTLE_AUDIO_CUE_ACTIVE){
            if(ms>10000u-s->growl_audio_wait_ms)return fail(e,n,"Growl keyoff acknowledgement timed out.");
            s->growl_audio_wait_ms+=ms;return 1;
        }
        C3WzxGrowlAudio audio=status==C3_BATTLE_AUDIO_CUE_STOPPED?C3_GROWL_AUDIO_STOPPED:
            status==C3_BATTLE_AUDIO_CUE_FINISHED?C3_GROWL_AUDIO_FINISHED:C3_GROWL_AUDIO_PENDING;
        if(audio==C3_GROWL_AUDIO_PENDING)return fail(e,n,"Growl keyoff acknowledgement unavailable.");
        uint32_t emitted=s->particles.emitted,peak=s->particles.peak_particles;uint64_t draws=s->particles.cosmetic_draws;
        if(!c3_wzx_growl_acknowledge(&s->growl,c3_particle_finished(&s->particles),audio,1,!s->growl_camera.active)||!c3_wzx_growl_finish(&s->growl))return fail(e,n,"Growl source teardown acknowledgement failed.");
        if(s->trace){fprintf(s->trace,"wzx_growl_complete event=%lu owner=%lu units=%llu emitted=%lu peak=%lu cosmetic_draws=%llu family_cancelled=1 audio_status=%s cue=%lu owner_released=1 motion_continues_at_half_rate=1 camera_restored=1\n",(unsigned long)s->growl_event,(unsigned long)s->slots[s->growl_owner],(unsigned long long)s->growl.units,(unsigned long)emitted,(unsigned long)peak,(unsigned long long)draws,audio==C3_GROWL_AUDIO_STOPPED?"stopped":"finished",(unsigned long)s->growl_cue);fflush(s->trace);}
        return 1;
    }
    if(s->growl_camera.active){C3BattleCameraActor actors[4];camera_actors(s,actors);
        if(!c3_battle_camera_step(&s->growl_camera,ms,actors,e,n))return 0;
        s->camera=s->growl_camera.camera;
    }
    return 1;
}
