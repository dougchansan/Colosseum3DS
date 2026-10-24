/* GREEN special0x29: committed successful Defense drops, in event order. */
static int stat_down_restore_color(C3BattleStage *s)
{
    if(s->stat_down_color_owned&&s->initialized){
        if(!c3_stage_renderer_set_modulation(s->stat_down_owner,
            s->stat_down_prior_enabled?s->stat_down.prior_rgb:NULL))return 0;
        s->stat_down_color_owned=0;
    }
    return 1;
}
static void stat_down_restore_camera(C3BattleStage *s)
{
    if(s->stat_down_camera.active){
        c3_battle_camera_cancel(&s->stat_down_camera);
        s->camera=s->stat_down_camera.camera;
    }
}
static void stat_down_cancel(C3BattleStage *s)
{
    if(s->stat_down.active){
        if(s->stat_down_cue&&!s->stat_down_keyoff_sent&&s->sound_stop){char error[160]={0};
            if(!s->sound_stop(s->stat_down_cue,0x19b,error,sizeof error)&&s->trace)
                fprintf(s->trace,"wzx_stat_down_cancel_audio_error error=%s\n",error);
        }
        if(s->particles.bank.profile==C3_PARTICLE_PROFILE_GREEN_DOWN_V1)
            c3_particle_cancel(&s->particles);
        c3_wzx_stat_down_cancel(&s->stat_down);
    }
    if(!stat_down_restore_color(s)&&s->trace)
        fprintf(s->trace,"wzx_stat_down_cancel_color_error owner=%lu\n",(unsigned long)s->stat_down_owner);
    stat_down_restore_camera(s);
}
static int stat_down_load(C3BattleStage *s,const char *root,char *e,size_t n)
{
    char path[512];
    if(snprintf(path,sizeof path,"%s/stat_down.c3w",root)>=(int)sizeof path)
        return fail(e,n,"Stat-down descriptor path too long.");
    errno=0;FILE *f=fopen(path,"rb");
    if(!f)return errno==ENOENT?1:fail(e,n,"Cannot inspect stat-down descriptor.");
    if(fclose(f)||!c3_wzx_stat_down_load(&s->stat_down_program,path,e,n))return 0;
    for(unsigned slot=0;slot<2;++slot){
        unsigned model=s->count;
        for(unsigned i=0;i<s->count;++i)if(s->slots[i]==slot){model=i;break;}
        if(model==s->count)return fail(e,n,"Missing stat-down player owner.");
        const C3WzxStatDownOwner *owner=&s->stat_down_program.owners[slot];
        C3Title *m=&s->models[model];
        if(s->roles[model][0]!=owner->clip||m->fps!=120||owner->clip>=m->clip_count||
           m->clips[owner->clip].frame_count!=owner->minimum+1||!m->clips[owner->clip].loop||
           !c3_title_animate(m,owner->clip,0))
            return fail(e,n,"Stat-down requires qualified player idle clip.");
        /* The pinned preparer checks original part0's identity transform.
         * Compact C3T bone0 is a different namespace; it is not this part. */
    }
    if(snprintf(path,sizeof path,"%s/stat_down.c3p",root)>=(int)sizeof path)return 0;
    f=fopen(path,"rb");if(!f)return fail(e,n,"Missing stat-down particles.");
    int ok=!fseek(f,0,SEEK_END);long length=ok?ftell(f):-1;
    if(length<=0||(unsigned long)length>C3_PARTICLE_PACK_MAX_BYTES||fseek(f,0,SEEK_SET))ok=0;
    if(ok){s->stat_down_bytes=malloc((size_t)length);
        ok=s->stat_down_bytes&&fread(s->stat_down_bytes,1,(size_t)length,f)==(size_t)length&&!ferror(f);}
    if(fclose(f))ok=0;
    if(!ok)return fail(e,n,"Cannot read stat-down particles.");
    if(!c3_particle_pack_decode_reachable(&s->stat_down_pack,s->stat_down_bytes,(size_t)length,
        s->stat_down_program.particles_sha1,C3_PARTICLE_PROFILE_GREEN_DOWN_V1,e,n))return 0;
    if(snprintf(path,sizeof path,"%s/audio.c3u",root)>=(int)sizeof path)return 0;
    C3BattleAudio audio={0};if(!c3_battle_audio_load(&audio,path,e,n))return 0;
    ok=c3_battle_audio_find(&audio,0x19b)!=NULL;c3_battle_audio_close(&audio);
    if(!ok)return fail(e,n,"Stat-down requires selected sound0x19B.");
    if(snprintf(path,sizeof path,"%s/stat_down_camera.c3c",root)>=(int)sizeof path)return 0;
    errno=0;f=fopen(path,"rb");
    if(f){if(fclose(f)||!c3_battle_camera_stat_down_load(&s->stat_down_camera_program,path,e,n))return 0;
        c3_battle_camera_init(&s->stat_down_camera,0x4752444eu);s->stat_down_camera_available=1;
    }else if(errno!=ENOENT)return fail(e,n,"Cannot inspect stat-down camera.");
    s->stat_down_available=1;
    if(s->trace){fprintf(s->trace,"wzx_stat_down_admitted special=0x29 targets=0,1 particle_part=0 camera=%s\n",
        s->stat_down_camera_available?"source":"explicitly_incomplete");fflush(s->trace);}
    return 1;
}
static int stat_down_event(C3BattleStage *s,const C3BattlePreviewEvent *ev,char *e,size_t n)
{
    unsigned slot=ev->target,owner=s->count;
    if(slot>1||ev->actor<2||ev->actor>3||ev->move!=39)
        return fail(e,n,"Unqualified Defense-down event.");
    if(ev->stages[slot][1]!=s->presentation.stages[slot][1]-1)
        return fail(e,n,"Unqualified Defense-down stage delta.");
    if(s->bite.active||s->damage.active||s->reflect.active||s->confusion_active||
       s->tail.active||s->growl.active||s->stat_down.active)
        return fail(e,n,"Overlapping stat-down effect ownership.");
    for(unsigned i=0;i<s->count;++i)if(s->slots[i]==slot){owner=i;break;}
    if(owner==s->count||!s->sound_cue||!s->sound_status||!s->sound_stop)
        return fail(e,n,"Stat-down requires qualified owner and source audio.");
    uint8_t prior[3];int enabled;
    if(!c3_stage_renderer_get_modulation(owner,prior,&enabled)||
       !c3_wzx_stat_down_begin(&s->stat_down,&s->stat_down_program,slot,prior))
        return fail(e,n,"Stat-down owner-color initialization failed.");
    s->stat_down_owner=owner;s->stat_down_event=ev->sequence;s->stat_down_cue=0;
    s->stat_down_teardown_started=s->stat_down_keyoff_sent=0;s->stat_down_audio_wait_ms=0;
    s->stat_down_prior_enabled=(uint8_t)enabled;s->stat_down_color_owned=1;
    s->stat_down_motion[slot]=1;s->stat_down_motion_units[slot]=0;
    s->growl_motion[owner]=0;s->growl_motion_units[owner]=0;
    s->clips[owner]=s->stat_down.owner_clip;s->clocks[owner]=0;s->fresh[owner]=1;
    const float *t=s->transforms[owner],*a=s->stat_down_program.owners[slot].root_translation;
    float co=cosf(t[3]),si=sinf(t[3]);
    float position[3]={10*(t[0]+t[4]*(co*a[0]+si*a[2])),10*(t[1]+t[4]*a[1]),
                       10*(t[2]+t[4]*(-si*a[0]+co*a[2]))};
    c3_particle_cancel(&s->particles);
    if(!c3_particle_begin_position_flags(&s->particles,&s->stat_down_pack.bank,0,
        0x4752444eu^ev->sequence,position,t[3],s->stat_down_program.class_scale,
        s->stat_down_program.class_scale,s->stat_down_program.particle_mode,e,n))return 0;
    if(s->cue_serial==UINT32_MAX)return fail(e,n,"Audio cue identity exhausted.");
    s->stat_down_cue=++s->cue_serial;
    if(!s->sound_cue(s->stat_down_cue,0x19b,e,n))return 0;
    if(s->stat_down_camera_available){C3BattleCameraActor actors[4];camera_actors(s,actors);
        if(!c3_battle_camera_begin_special(&s->stat_down_camera,&s->stat_down_camera_program,
            0x29,slot,ev->actor,actors,&s->camera,e,n))return 0;
        s->camera=s->stat_down_camera.camera;
        if(s->trace)fprintf(s->trace,"wzx_stat_down_camera_begin target=%u motion=%lu duration=%lu draws=%lu\n",
            slot,(unsigned long)s->stat_down_camera.motion,(unsigned long)s->stat_down_camera.duration,
            (unsigned long)s->stat_down_camera.random_draws);
    }
    c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;
    if(s->trace){fprintf(s->trace,"wzx_stat_down_start event=%lu target=%u special=0x29 delta=-1 cue=%lu\n",
        (unsigned long)ev->sequence,slot,(unsigned long)s->stat_down_cue);fflush(s->trace);}
    return 1;
}
static int stat_down_step(C3BattleStage *s,uint32_t ms,char *e,size_t n)
{
    if(!s->stat_down.active)return 1;
    int status;
    if(!s->stat_down.teardown_requested){
        if(!c3_wzx_stat_down_step(&s->stat_down,ms))return fail(e,n,"Stat-down clock overload.");
        if(!c3_particle_step(&s->particles,ms,0,e,n))return 0;
        if(s->stat_down.color_enabled){
            if(!c3_stage_renderer_set_modulation(s->stat_down_owner,s->stat_down.color_rgb))
                return fail(e,n,"Stat-down actor-color binding failed.");
        }else if(!stat_down_restore_color(s))return fail(e,n,"Stat-down prior actor-color restoration failed.");
        status=s->sound_status(s->stat_down_cue,0x19b,e,n);
        if(status<0)return 0;
        if(!c3_wzx_stat_down_observe(&s->stat_down,c3_particle_finished(&s->particles),
            (C3WzxStatDownAudio)status))return fail(e,n,"Stat-down sound did not finish naturally.");
    }
    if(s->stat_down.teardown_requested){
        if(!s->stat_down_teardown_started){
            c3_particle_cancel(&s->particles);
            if(!stat_down_restore_color(s))return fail(e,n,"Stat-down teardown actor-color restoration failed.");
            stat_down_restore_camera(s);
            s->stat_down_motion_units[s->stat_down.slot]=s->stat_down.units;
            s->fresh[s->stat_down_owner]=1;s->stat_down_teardown_started=1;
        }
        if(!s->stat_down_keyoff_sent){
            if(!s->sound_stop(s->stat_down_cue,0x19b,e,n))return 0;
            s->stat_down_keyoff_sent=1;
        }
        status=s->sound_status(s->stat_down_cue,0x19b,e,n);if(status<0)return 0;
        if(status==C3_BATTLE_AUDIO_CUE_ACTIVE){
            if(ms>10000u-s->stat_down_audio_wait_ms)return fail(e,n,"Stat-down keyoff acknowledgement timed out.");
            s->stat_down_audio_wait_ms+=ms;return 1;
        }
        if(!c3_wzx_stat_down_acknowledge(&s->stat_down,c3_particle_finished(&s->particles),
            (C3WzxStatDownAudio)status,!s->stat_down_color_owned,!s->stat_down_camera.active)||
           !c3_wzx_stat_down_finish(&s->stat_down))return fail(e,n,"Stat-down teardown acknowledgement failed.");
        if(s->trace){fprintf(s->trace,"wzx_stat_down_complete event=%lu target=%lu units=%llu emitted=%lu peak=%lu cosmetic_draws=%llu audio=finished cleanup_audio=%s cue=%lu color_restored=1 camera_restored=1\n",
            (unsigned long)s->stat_down_event,(unsigned long)s->slots[s->stat_down_owner],
            (unsigned long long)s->stat_down.units,(unsigned long)s->particles.emitted,
            (unsigned long)s->particles.peak_particles,(unsigned long long)s->particles.cosmetic_draws,
            status==C3_BATTLE_AUDIO_CUE_STOPPED?"stopped":"finished",(unsigned long)s->stat_down_cue);fflush(s->trace);}
    }else if(s->stat_down_camera.active){C3BattleCameraActor actors[4];camera_actors(s,actors);
        if(!c3_battle_camera_step(&s->stat_down_camera,ms,actors,e,n))return 0;
        s->camera=s->stat_down_camera.camera;
    }
    return 1;
}
