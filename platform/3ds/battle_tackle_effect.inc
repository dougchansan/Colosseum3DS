/* Checked successful Tackle: caster stop133, explicit defender-mode2 handoff. */
static void tackle_camera_restore(C3BattleStage *s)
{
    if(s->tackle_camera.active){c3_battle_camera_cancel(&s->tackle_camera);s->camera=s->tackle_camera.camera;}
}
static int tackle_keyoff(C3BattleStage *s,unsigned phase,char *e,size_t n)
{
    if(!s->tackle_cues[phase]||s->tackle_keyoff[phase])return 1;
    if(!s->sound_stop||!s->sound_stop(s->tackle_cues[phase],phase?0xDE:0x4A2,e,n))return 0;
    s->tackle_keyoff[phase]=1;return 1;
}
static void tackle_cancel(C3BattleStage *s)
{
    char error[192]={0};
    for(unsigned phase=0;phase<2;++phase)if(!tackle_keyoff(s,phase,error,sizeof error)&&s->trace)
        fprintf(s->trace,"wzx_tackle_cancel_audio_error phase=%u error=%s\n",phase,error);
    if(s->particles.bank.profile==C3_PARTICLE_PROFILE_TACKLE_V1)c3_particle_cancel(&s->particles);
    c3_wzx_tackle_cancel(&s->tackle);
    for(unsigned i=0;i<s->count;++i)if(s->owner_motion_end[i]){
        s->growl_motion[i]=0;s->growl_motion_units[i]=0;s->owner_motion_end[i]=0;
    }
    tackle_camera_restore(s);
}
static int tackle_load(C3BattleStage *s,const char *root,char *e,size_t n)
{
    char path[512];
    if(snprintf(path,sizeof path,"%s/tackle.c3w",root)>=(int)sizeof path)return fail(e,n,"Tackle descriptor path too long.");
    errno=0;FILE *f=fopen(path,"rb");
    if(!f)return errno==ENOENT?1:fail(e,n,"Cannot inspect Tackle descriptor.");
    if(fclose(f)||!c3_wzx_tackle_load(&s->tackle_program,path,e,n))return 0;
    for(unsigned slot=0;slot<4;++slot){unsigned i=s->count;
        for(unsigned j=0;j<s->count;++j)if(s->slots[j]==slot){i=j;break;}
        if(i==s->count)return fail(e,n,"Tackle requires all four admitted Pokemon owners.");
        C3Title *m=&s->models[i];uint32_t clip=slot<2?s->tackle_program.owners[slot].owner_clip:4;
        uint32_t frames=slot<2?s->tackle_program.owners[slot].owner_minimum+1:241;
        if(m->fps!=120||clip>=m->clip_count||s->roles[i][slot<2?3:1]!=clip||
           m->clips[clip].frame_count!=frames||m->clips[clip].loop||
           s->roles[i][0]>=m->clip_count||!m->clips[s->roles[i][0]].loop)
            return fail(e,n,"Tackle requires source Zigzagoon cast and Umbreon/Espeon hit clips.");
    }
    if(snprintf(path,sizeof path,"%s/tackle.c3p",root)>=(int)sizeof path)return fail(e,n,"Tackle particle path too long.");
    f=fopen(path,"rb");if(!f)return fail(e,n,"Missing prepared Tackle particles.");
    int ok=!fseek(f,0,SEEK_END);long length=ok?ftell(f):-1;
    if(length<=0||(unsigned long)length>C3_PARTICLE_PACK_MAX_BYTES||fseek(f,0,SEEK_SET))ok=0;
    if(ok){s->tackle_bytes=malloc((size_t)length);ok=s->tackle_bytes&&fread(s->tackle_bytes,1,(size_t)length,f)==(size_t)length&&!ferror(f);}
    if(fclose(f))ok=0;
    if(!ok)return fail(e,n,"Cannot read bounded Tackle particles.");
    if(!c3_particle_pack_decode_reachable(&s->tackle_pack,s->tackle_bytes,(size_t)length,
        s->tackle_program.particles_sha1,C3_PARTICLE_PROFILE_TACKLE_V1,e,n))return 0;
    if(snprintf(path,sizeof path,"%s/audio.c3u",root)>=(int)sizeof path)return fail(e,n,"Tackle audio path too long.");
    C3BattleAudio audio={0};if(!c3_battle_audio_load(&audio,path,e,n))return 0;
    ok=c3_battle_audio_find(&audio,0x4A2)&&c3_battle_audio_find(&audio,0xDE);c3_battle_audio_close(&audio);
    if(!ok)return fail(e,n,"Tackle requires selected source sounds4A2 and DE.");
    if(snprintf(path,sizeof path,"%s/tackle_camera.c3c",root)>=(int)sizeof path)return fail(e,n,"Tackle camera path too long.");
    if(!c3_battle_camera_tackle_load(&s->tackle_camera_program,path,e,n))return 0;
    c3_battle_camera_init(&s->tackle_camera,0x5441434Bu);s->tackle_camera_available=s->tackle_available=1;
    if(s->trace){fprintf(s->trace,"wzx_tackle_admitted attacker=288_entry2_clip4 defenders=197_entry8_clip3,196_entry8_clip2 camera=source normal=170,100 minimum=298,176\n");fflush(s->trace);}
    return 1;
}
static int tackle_camera_begin(C3BattleStage *s,unsigned phase,char *e,size_t n)
{
    C3BattleCameraActor actors[4];camera_actors(s,actors);tackle_camera_restore(s);
    unsigned owner=s->slots[phase?s->tackle_target:s->tackle_owner],target=s->slots[phase?s->tackle_owner:s->tackle_target];
    if(!c3_battle_camera_begin_move(&s->tackle_camera,&s->tackle_camera_program,33,
        (C3BattleCameraPhase)phase,owner,target,actors,&s->camera,e,n))return 0;
    s->camera=s->tackle_camera.camera;return 1;
}
/* Completed KizetuEffect mode3 releases its source owner through801DB100.
 * The remaining drawn faint pose is not an active ResetAnimationExcept owner. */
static int tackle_released_faint(const C3BattleStage *s,unsigned i)
{
    unsigned slot=s->slots[i];uint32_t clip=s->clips[i];const C3Title *model=&s->models[i];
    return slot<4&&!s->presentation.hp[slot]&&!s->presentation.visible[slot]&&
        clip==s->roles[i][4]&&clip<model->clip_count&&!model->clips[clip].loop&&isfinite(s->clocks[i])&&
        s->clocks[i]*model->fps>=(float)model->clips[clip].frame_count;
}
/* Source DEF0C preserves an already-running idle, but changes other admitted
 * Pokemon motions to idle. This bounded stage admits normal constructed owners
 * (owner16=0, flags18 bit8 clear); it has no flag setter/locked-owner path.
 * Fainted/hidden/unknown owners need source flag data and are refused. */
static int tackle_reset_except(C3BattleStage *s,char *e,size_t n)
{
    for(unsigned i=0;i<s->count;++i){unsigned slot=s->slots[i];if(i==s->tackle_target||slot>=6||tackle_released_faint(s,i))continue;
        if(slot>=4){if(s->clips[i]!=s->roles[i][0])return fail(e,n,"Tackle reset requires idle trainer owners; source flags unavailable.");continue;}
        if(!s->presentation.hp[slot]||!s->presentation.visible[slot]||s->clips[i]==s->roles[i][4])
            return fail(e,n,"Tackle reset cannot qualify fainted/hidden owner flags.");
        int known=0;for(unsigned role=0;role<4;++role)known|=s->clips[i]==s->roles[i][role];
        if(!known)return fail(e,n,"Tackle reset cannot qualify current Pokemon owner flags.");
    }
    for(unsigned i=0;i<s->count;++i)if(i!=s->tackle_target&&s->slots[i]<4&&!tackle_released_faint(s,i)&&s->clips[i]!=s->roles[i][0]){
        s->clips[i]=s->roles[i][0];s->clocks[i]=0;s->fresh[i]=1;
        s->growl_motion[i]=2;s->growl_motion_units[i]=s->tackle.handoff_units;
        s->owner_motion_end[i]=i==s->tackle_owner?240:0;
        if(s->slots[i]<2){s->stat_down_motion[s->slots[i]]=0;s->stat_down_motion_units[s->slots[i]]=0;}
    }
    return 1;
}
static int tackle_event(C3BattleStage *s,const C3BattlePreviewEvent *ev,char *e,size_t n)
{
    if(s->bite.active||s->damage.active||s->reflect.active||s->confusion_active||s->tail.active||s->growl.active||s->stat_down.active||s->tackle.active)
        return fail(e,n,"Overlapping Tackle source effect ownership.");
    if(ev->actor<2||ev->actor>=4)return fail(e,n,"Unqualified Tackle caster.");
    unsigned target=4;
    for(unsigned j=s->presentation.cursor+1;j<s->presentation.count&&s->presentation.events[j].group==ev->group;++j){const C3BattlePreviewEvent *next=&s->presentation.events[j];
        if(next->kind==C3_PREVIEW_EVENT_HP&&next->actor==ev->actor&&next->target<2){target=next->target;break;}
    }
    if(target==4){s->tackle_hit=0;if(s->trace){fprintf(s->trace,"wzx_tackle_miss event=%lu special57=unsupported actor_only=1 no_bank=1 no_reset=1\n",(unsigned long)ev->sequence);fflush(s->trace);}return 1;}
    if(!s->sound_cue||!s->sound_status||!s->sound_stop)return fail(e,n,"Tackle requires source audio cue/status/keyoff callbacks.");
    unsigned owner=s->count,defender=s->count;
    for(unsigned i=0;i<s->count;++i){if(s->slots[i]==ev->actor)owner=i;if(s->slots[i]==target)defender=i;}
    if(owner==s->count||defender==s->count||!s->presentation.hp[ev->actor]||!s->presentation.hp[target]||!s->presentation.visible[ev->actor]||!s->presentation.visible[target])return fail(e,n,"Tackle requires live admitted caster and defender.");
    if(!c3_wzx_tackle_begin(&s->tackle,&s->tackle_program,target))return fail(e,n,"Tackle source clock initialization failed.");
    s->tackle_owner=owner;s->tackle_target=defender;s->tackle_event=ev->sequence;s->tackle_group=ev->group;
    memset(s->tackle_cues,0,sizeof s->tackle_cues);memset(s->tackle_keyoff,0,sizeof s->tackle_keyoff);
    s->tackle_hit=1;s->tackle_particles_started=0;c3_particle_cancel(&s->particles);
    s->clips[owner]=s->roles[owner][1];s->clocks[owner]=0;s->fresh[owner]=1;
    s->growl_motion[owner]=3;s->growl_motion_units[owner]=0;s->owner_motion_end[owner]=240;
    if(!tackle_camera_begin(s,0,e,n))return 0;
    c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;
    if(s->trace){fprintf(s->trace,"wzx_tackle_start event=%lu owner=%lu target=%u attack130=4A2 stop133=defender_mode2\n",(unsigned long)ev->sequence,(unsigned long)ev->actor,target);fflush(s->trace);}
    return 1;
}
static int tackle_cue(C3BattleStage *s,unsigned phase,char *e,size_t n)
{
    if(s->tackle_cues[phase])return 1;
    if(s->cue_serial==UINT32_MAX)return fail(e,n,"Tackle audio cue identity exhausted.");
    uint32_t cue=++s->cue_serial;
    if(!s->sound_cue(cue,phase?0xDE:0x4A2,e,n))return 0;
    s->tackle_cues[phase]=cue;return 1;
}
static int tackle_step(C3BattleStage *s,uint32_t ms,char *e,size_t n)
{
    if(!s->tackle.active)return 1;
    unsigned phase=s->tackle.phase;
    if(!c3_wzx_tackle_step(&s->tackle,ms))return fail(e,n,"Tackle source clock overload.");
    if(s->tackle.attack_sound_requests&&!s->tackle_cues[0]){
        if(!tackle_cue(s,0,e,n))return 0;
        if(s->trace){fprintf(s->trace,"wzx_tackle_attack_sound source_tick=130 units=%llu sound=4a2 cue=%lu\n",(unsigned long long)s->tackle.units,(unsigned long)s->tackle_cues[0]);fflush(s->trace);}
    }
    uint64_t advance=(uint64_t)ms*60;
    if(phase==1&&s->tackle.attack_complete){
        if(!tackle_keyoff(s,0,e,n))return 0;
        if(!c3_wzx_tackle_defend(&s->tackle,1))return fail(e,n,"Tackle defender handoff failed.");
        if(!tackle_reset_except(s,e,n))return 0;
        unsigned owner=s->tackle_target,slot=s->slots[owner];const C3WzxTackleOwner *binding=&s->tackle_program.owners[slot];
        s->clips[owner]=binding->owner_clip;s->clocks[owner]=0;s->fresh[owner]=1;
        s->stat_down_motion[slot]=0;s->stat_down_motion_units[slot]=0;
        s->growl_motion[owner]=4;s->growl_motion_units[owner]=s->tackle.handoff_units;s->owner_motion_end[owner]=binding->owner_minimum;
        const float *t=s->transforms[owner],*a=binding->anchor;float co=cosf(t[3]),si=sinf(t[3]);
        float position[3]={10*(t[0]+t[4]*(co*a[0]+si*a[2])),10*(t[1]+t[4]*a[1]),10*(t[2]+t[4]*(-si*a[0]+co*a[2]))};
        if(!c3_particle_begin_position_flags(&s->particles,&s->tackle_pack.bank,0,0x5441434Bu^s->tackle_event,
            position,t[3],s->tackle_program.world_scale,1.f,1,e,n)||!tackle_cue(s,1,e,n)||!tackle_camera_begin(s,1,e,n))return 0;
        s->tackle_particles_started=1;advance=s->tackle.handoff_units;
        if(s->trace){fprintf(s->trace,"wzx_tackle_defender_start source_tick=133 units=%llu handoff_units=%llu target=%u bodymap2=%lu flags_raw=2 bank_flags=1 position_flags=1 anchor_source=%.6f,%.6f,%.6f reset_except=1 owner_flags=admitted_normal hp_gate=0 cue=%lu\n",(unsigned long long)s->tackle.units,(unsigned long long)advance,slot,(unsigned long)binding->body_part,position[0],position[1],position[2],(unsigned long)s->tackle_cues[1]);fflush(s->trace);}
    }
    if(s->tackle.phase==2){
        if(!c3_particle_step_units(&s->particles,advance,0,e,n))return 0;
        int status=s->sound_status(s->tackle_cues[1],0xDE,e,n);
        if(status<0)return 0;
        if(status!=C3_BATTLE_AUDIO_CUE_ACTIVE&&status!=C3_BATTLE_AUDIO_CUE_FINISHED)return fail(e,n,"Tackle damage audio requires natural FINISHED status, not keyoff.");
        if(c3_wzx_tackle_finish(&s->tackle,c3_particle_finished(&s->particles),status==C3_BATTLE_AUDIO_CUE_FINISHED,0,0)){
            if(!tackle_keyoff(s,1,e,n))return 0;
            c3_particle_cancel(&s->particles);tackle_camera_restore(s);
            if(s->trace){fprintf(s->trace,"wzx_tackle_complete event=%lu target=%lu units=%llu emitted=%lu peak=%lu draws=%llu audio=finished family_cancelled=1 camera_restored=1\n",(unsigned long)s->tackle_event,(unsigned long)s->slots[s->tackle_target],(unsigned long long)s->tackle.units,(unsigned long)s->particles.emitted,(unsigned long)s->particles.peak_particles,(unsigned long long)s->particles.cosmetic_draws);fflush(s->trace);}
            return 1;
        }
    }
    if(s->tackle_camera.active){C3BattleCameraActor actors[4];camera_actors(s,actors);
        if(!c3_battle_camera_step_units(&s->tackle_camera,advance,actors,e,n))return 0;
        s->camera=s->tackle_camera.camera;
    }
    return 1;
}
