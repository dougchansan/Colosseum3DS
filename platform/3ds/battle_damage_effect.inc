/* Source-qualified Bite defender side; included by battle_stage.c. */
static void camera_actors(const C3BattleStage *s,C3BattleCameraActor actors[4])
{
    memset(actors,0,sizeof(C3BattleCameraActor)*4);
    for(uint32_t i=0;i<s->count;++i)if(s->slots[i]<4){unsigned slot=s->slots[i];
        actors[slot].slot=slot;actors[slot].species=slot==0?197:slot==1?196:288;
        actors[slot].source_facing=slot<2?1:-1;actors[slot].yaw=s->transforms[i][3];
        memcpy(actors[slot].position,s->transforms[i],12);
    }
}
static int camera_begin(C3BattleStage *s,C3BattleCameraPhase phase,unsigned owner,unsigned target,char *error,size_t cap)
{
    if(!s->camera_available)return 1;
    C3BattleCameraActor actors[4];camera_actors(s,actors);
    if(s->source_camera.active){c3_battle_camera_cancel(&s->source_camera);s->camera=s->source_camera.camera;}
    if(!c3_battle_camera_begin(&s->source_camera,&s->camera_program,phase,owner,target,actors,&s->camera,error,cap))return 0;
    s->camera=s->source_camera.camera;
    if(s->trace){fprintf(s->trace,"wzx_camera_begin phase=%u motion=%lu owner=%u target=%u random_draws=%lu cosmetic_rng=separate\n",(unsigned)phase,(unsigned long)s->source_camera.motion,owner,target,(unsigned long)s->source_camera.random_draws);fflush(s->trace);}
    return 1;
}
static int damage_load(C3BattleStage *s,const char *root,char *error,size_t cap)
{
    char path[512];
    if(snprintf(path,sizeof path,"%s/bite_damage.c3w",root)>=(int)sizeof path)return 0;
    errno=0;FILE *f=fopen(path,"rb");
    if(!f)return errno==ENOENT?1:fail(error,cap,"Cannot inspect Bite damage descriptor.");
    if(fclose(f)||!s->bite_available||s->count>=C3_BATTLE_STAGE_MODELS)return fail(error,cap,"Damage requires the admitted attack pack.");
    if(!c3_wzx_damage_load(&s->damage_program,path,error,cap))return 0;
    if(snprintf(path,sizeof path,"%s/bite_damage.c3t",root)>=(int)sizeof path)return 0;
    uint32_t i=s->count++;s->damage_model=i;s->slots[i]=UINT32_MAX-2u;
    if(!c3_title_load(&s->models[i],path,error,cap))return 0;
    C3Title *m=&s->models[i];uint8_t hash[20];
    c3_save_codec_sha1(hash,m->file_data,(uint32_t)m->file_bytes);
    if(memcmp(hash,s->damage_program.model_sha1,20)||m->clip_count!=1||m->fps!=120||
       m->clips[0].loop||m->clips[0].frame_count!=51||m->bone_count<=s->damage_program.particle_bone)
        return fail(error,cap,"Damage model identity or retained bone mismatch.");
    s->draw[i]=m;s->transforms[i][4]=1;
    for(unsigned j=0;j<9;++j)s->roles[i][j]=0;
    if(snprintf(path,sizeof path,"%s/bite_damage.c3p",root)>=(int)sizeof path)return 0;
    f=fopen(path,"rb");if(!f)return fail(error,cap,"Missing Bite particle pack.");
    int ok=fseek(f,0,SEEK_END)==0;long length=ok?ftell(f):-1;
    if(length<=0||(unsigned long)length>C3_PARTICLE_PACK_MAX_BYTES||fseek(f,0,SEEK_SET))ok=0;
    if(ok){s->particle_bytes=malloc((size_t)length);ok=s->particle_bytes&&fread(s->particle_bytes,1,(size_t)length,f)==(size_t)length&&!ferror(f);}
    if(fclose(f))ok=0;
    if(!ok)return fail(error,cap,"Cannot read bounded Bite particle pack.");
    if(!c3_particle_pack_decode(&s->particle_pack,s->particle_bytes,(size_t)length,s->damage_program.particles_sha1,error,cap))return 0;
    s->damage_available=1;
    if(snprintf(path,sizeof path,"%s/bite_camera.c3c",root)>=(int)sizeof path)return 0;
    errno=0;f=fopen(path,"rb");
    if(f){if(fclose(f)||!c3_battle_camera_load(&s->camera_program,path,error,cap))return 0;
        c3_battle_camera_init(&s->source_camera,0x43414d45u);s->camera_available=1;
    }else if(errno!=ENOENT)return fail(error,cap,"Cannot inspect source battle camera pack.");
    if(s->trace){fprintf(s->trace,"wzx_damage_admitted source=04E32000 model_part=12 sampled_bone=11 generator=3 model_tick=0 hp_sound_tick=22 owner_start=22 owner_end=380\n");fflush(s->trace);}
    return 1;
}
static int damage_step(C3BattleStage *,uint32_t,char *,size_t);
static int damage_begin(C3BattleStage *s,char *error,size_t cap)
{
    if(!c3_wzx_damage_begin(&s->damage,&s->damage_program))return fail(error,cap,"Damage clock initialization failed.");
    uint32_t i=s->damage_owner;const float *owner=s->transforms[i],*a=s->damage_program.anchor;
    float *t=s->transforms[s->damage_model],co=cosf(owner[3]),si=sinf(owner[3]);
    t[0]=owner[0]+owner[4]*(co*a[0]+si*a[2]);t[1]=owner[1]+owner[4]*a[1];
    t[2]=owner[2]+owner[4]*(-si*a[0]+co*a[2]);t[3]=owner[3];t[4]=owner[4];
    s->clips[i]=s->damage_program.owner_clip;s->clocks[i]=0;s->fresh[i]=1;
    s->damage_pending=0;s->particles_started=0;
    if(!camera_begin(s,C3_BATTLE_CAMERA_DAMAGE,s->slots[i],0,error,cap))return 0;
    if(s->trace){fprintf(s->trace,"wzx_damage_begin event=%lu owner=%lu attack_stop_tick=165\n",(unsigned long)s->bite_sequence,(unsigned long)s->slots[i]);fflush(s->trace);}
    return damage_step(s,0,error,cap);
}
static int damage_step(C3BattleStage *s,uint32_t ms,char *error,size_t cap)
{
    if(!s->damage.active)return 1;
    uint32_t sound=s->damage.sound_requests;
    if(!c3_wzx_damage_step(&s->damage,ms))return fail(error,cap,"Damage clock overload.");
    if(s->damage.particle_starts&&!s->particles_started){
        if(!c3_particle_begin(&s->particles,&s->particle_pack.bank,s->damage_program.root_generator,0x42495445u^s->bite_sequence,error,cap))return 0;
        s->particles_started=1;
        if(s->trace){fprintf(s->trace,"wzx_damage_particles_start tick=0 generator=3 cosmetic_rng=separate\n");fflush(s->trace);}
    }else if(s->particles_started&&!c3_particle_step(&s->particles,ms,0,error,cap))return 0;
    if(sound!=s->damage.sound_requests){
        if(s->sound_cue&&!s->sound_cue(s->bite_sequence,s->damage_program.sound_id,error,cap))return 0;
        if(s->trace){fprintf(s->trace,"wzx_damage_hp_gate tick=22 sound=86 playback=%s\n",s->sound_cue?"requested":"unavailable");fflush(s->trace);}
    }
    s->clocks[s->damage_owner]=s->damage.owner_seconds;
    if(c3_wzx_damage_finish(&s->damage,s->damage.model_finished,
        s->particles_started&&c3_particle_finished(&s->particles),s->damage.floor_finished)){
        s->clips[s->damage_owner]=s->roles[s->damage_owner][0];s->clocks[s->damage_owner]=0;
        if(s->source_camera.active){c3_battle_camera_cancel(&s->source_camera);s->camera=s->source_camera.camera;}
        if(s->trace){fprintf(s->trace,"wzx_damage_complete tick=%llu particles=%lu peak=%lu cosmetic_draws=%llu hp_faint_gate=released\n",
            (unsigned long long)(s->damage.units/1000),(unsigned long)s->particles.emitted,
            (unsigned long)s->particles.peak_particles,(unsigned long long)s->particles.cosmetic_draws);fflush(s->trace);}
    }
    return 1;
}
static int damage_bind_particles(C3BattleStage *s,char *error,size_t cap)
{
    if(!s->particles_started||!s->damage.active)return c3_stage_renderer_particles(NULL,NULL);
    const float *b=s->models[s->damage_model].pose.matrices[s->damage_program.particle_bone];
    const float *t=s->transforms[s->damage_model];float co=cosf(t[3]),si=sinf(t[3]),a[12];
    /* Translation is already native; only the source particle basis gets .1. */
    for(unsigned j=0;j<3;++j){a[j]=t[4]*.1f*(co*b[j]+si*b[8+j]);a[4+j]=t[4]*.1f*b[4+j];a[8+j]=t[4]*.1f*(-si*b[j]+co*b[8+j]);}
    a[3]=t[0]+t[4]*(co*b[3]+si*b[11]);a[7]=t[1]+t[4]*b[7];a[11]=t[2]+t[4]*(-si*b[3]+co*b[11]);
    if(!c3_stage_renderer_particles(&s->particles,a))return fail(error,cap,"Invalid live damage particle attachment.");
    return 1;
}
