/* Full source Confusion entries. One attack system reuses the stage's shared
 * pool; the damage pool appends all five roots without resetting RNG/time. */
#include "colosseum3ds/battle_audio.h"
static void confusion_cancel(C3BattleStage *s)
{
    s->confusion_active=s->confusion_pending=s->confusion_floor_enabled=0;
    c3_confusion_type12_cancel(&s->confusion_type12);
    if(s->confusion_damage_particles)c3_particle_cancel(s->confusion_damage_particles);
}
static int confusion_file(const char *path,uint8_t **out,size_t *bytes,char *e,size_t n)
{
    FILE *f=fopen(path,"rb");if(!f)return fail(e,n,"Missing prepared Confusion dependency.");
    int ok=!fseek(f,0,SEEK_END);long length=ok?ftell(f):-1;
    if(length<=0||(unsigned long)length>C3_PARTICLE_PACK_MAX_BYTES||fseek(f,0,SEEK_SET))ok=0;
    uint8_t *b=ok?malloc((size_t)length):NULL;
    if(!b||fread(b,1,(size_t)length,f)!=(size_t)length||ferror(f))ok=0;
    if(fclose(f))ok=0;
    if(!ok){free(b);return fail(e,n,"Cannot read bounded Confusion dependency.");}
    *out=b;*bytes=(size_t)length;return 1;
}
static int confusion_load(C3BattleStage *s,const char *root,char *e,size_t n)
{
    char path[512];if(snprintf(path,sizeof path,"%s/confusion_sequence.c3s",root)>=(int)sizeof path)return 0;
    errno=0;FILE *f=fopen(path,"rb");if(!f)return errno==ENOENT?1:fail(e,n,"Cannot inspect Confusion sequence.");
    if(fclose(f)||!c3_wzx_confusion_sequence_load(&s->confusion_program,path,e,n))return 0;
    if(snprintf(path,sizeof path,"%s/confusion_type12.c3f",root)>=(int)sizeof path||!c3_wzx_confusion_load(&s->confusion_mask,path,e,n))return 0;
    uint8_t *mask=NULL,hash[20];size_t length=0;
    if(!confusion_file(path,&mask,&length,e,n))return 0;
    c3_save_codec_sha1(hash,mask,(uint32_t)length);free(mask);
    if(memcmp(hash,s->confusion_program.type12_sha1,20))return fail(e,n,"Confusion type12 differs from sequence contract.");
    for(unsigned p=0;p<2;++p){
        if(snprintf(path,sizeof path,"%s/confusion_%s.c3p",root,p?"damage":"attack")>=(int)sizeof path||!confusion_file(path,&s->confusion_bytes[p],&length,e,n))return 0;
        if(!c3_particle_pack_decode(&s->confusion_pack[p],s->confusion_bytes[p],length,s->confusion_program.particles_sha1[p],e,n))return 0;
    }
    if(snprintf(path,sizeof path,"%s/audio.c3u",root)>=(int)sizeof path)return 0;
    C3BattleAudio audio={0};if(!c3_battle_audio_load(&audio,path,e,n))return 0;
    int audio_ok=c3_battle_audio_find(&audio,0x443)&&c3_battle_audio_find(&audio,0xE9)&&c3_battle_audio_find(&audio,0x472)&&c3_battle_audio_find(&audio,0x473);
    c3_battle_audio_close(&audio);if(!audio_ok)return fail(e,n,"Confusion requires all four prepared source audio cues.");
    s->confusion_damage_particles=calloc(1,sizeof *s->confusion_damage_particles);
    if(!s->confusion_damage_particles)return fail(e,n,"Confusion damage particle allocation failed.");
    for(uint32_t i=0;i<s->count;++i)if(s->slots[i]==1||s->slots[i]==2||s->slots[i]==3){
        unsigned track=s->slots[i]==1?0:2;C3Title *m=&s->models[i];
        unsigned idle=s->roles[i][0],motion=s->roles[i][s->slots[i]==1?2:3];
        if(m->fps!=120||idle!=s->confusion_program.tracks[track].clip||motion!=s->confusion_program.tracks[track+1].clip||m->clips[idle].frame_count!=s->confusion_program.tracks[track].frames||m->clips[motion].frame_count!=s->confusion_program.tracks[track+1].frames)return fail(e,n,"Confusion source-part and rendered actor clocks differ.");
    }
    s->confusion_available=1;return 1;
}
static float confusion_owner_seconds(const C3BattleStage *s)
{
    uint64_t start=(uint64_t)s->confusion_program.owner_start[s->confusion_phase]*1000;
    if(s->confusion_units<start)return s->confusion_idle+(float)((double)s->confusion_units/60000)*s->confusion_program.owner_rate;
    return (float)((double)(s->confusion_units-start)/60000)*s->confusion_program.owner_rate;
}
static int confusion_position(C3BattleStage *s,int type12,float out[3],char *e,size_t n)
{
    unsigned owner=s->confusion_phase?s->confusion_target:s->confusion_owner;
    unsigned species=s->confusion_phase?288:196;
    unsigned entry=s->confusion_phase?(s->confusion_units<44000?0:8):1;
    /* Both motion and idle source body maps bind the checked part62/53.
     * type12 is one-shot; callers do not update its stored center later. */
    (void)type12;const float *t=s->transforms[owner];
    return c3_wzx_confusion_sequence_position(&s->confusion_program,species,entry,confusion_owner_seconds(s),t,t[3],t[4],out,e,n);
}
static int confusion_step(C3BattleStage *,uint32_t,char *,size_t);
static int confusion_phase_begin(C3BattleStage *s,unsigned phase,char *e,size_t n)
{
    s->confusion_phase=(uint8_t)phase;s->confusion_units=0;s->confusion_seen=0;s->confusion_particles_started=0;
    memset(s->confusion_roots,0,sizeof s->confusion_roots);memset(s->confusion_cues,0,sizeof s->confusion_cues);
    unsigned owner=phase?s->confusion_target:s->confusion_owner;s->confusion_idle=s->clocks[owner];
    if(phase)s->clips[owner]=s->roles[owner][0];
    else{s->clips[owner]=s->roles[owner][2];s->clocks[owner]=0;}
    if(s->clips[owner]==UINT32_MAX)return fail(e,n,"Missing Confusion source owner motion.");
    s->fresh[owner]=1;s->confusion_floor_enabled=0;
    c3_confusion_type12_cancel(&s->confusion_type12);
    if(s->trace){fprintf(s->trace,"wzx_confusion_phase event=%lu phase=%u owner=%lu\n",(unsigned long)s->confusion_event,phase,(unsigned long)s->slots[owner]);fflush(s->trace);}
    /* New phase source0 settles at this endpoint before any elapsed tick. */
    return confusion_step(s,0,e,n);
}
static int confusion_event(C3BattleStage *s,const C3BattlePreviewEvent *ev,char *e,size_t n)
{
    if(s->bite.active||s->damage.active||s->reflect.active||s->confusion_active)return fail(e,n,"Overlapping Confusion source ownership.");
    if(!s->sound_cue||!s->sound_done)return fail(e,n,"Confusion requires audio playback/completion callbacks.");
    s->confusion_hit=s->confusion_pending=s->confusion_hp_gate=0;
    for(uint32_t i=0;i<s->count;++i)if(s->slots[i]==ev->target)s->confusion_target=i;
    for(uint32_t i=0;i<s->count;++i)if(s->slots[i]==1)s->confusion_owner=i;
    for(uint32_t j=s->presentation.cursor+1;j<s->presentation.count&&s->presentation.events[j].group==ev->group;++j){const C3BattlePreviewEvent *q=&s->presentation.events[j];
        if(q->kind==C3_PREVIEW_EVENT_HP&&q->actor==1&&q->move==93&&q->target>=2&&q->target<4){
            for(uint32_t i=0;i<s->count;++i)if(s->slots[i]==q->target){s->confusion_target=i;s->confusion_hit=s->confusion_pending=1;}
            break;
        }
    }
    s->confusion_event=ev->sequence;s->confusion_group=ev->group;s->confusion_active=1;
    if(!confusion_phase_begin(s,0,e,n))return 0;
    c3_battle_presentation_wait(&s->presentation,1);s->presentation.duration_ms=0;return 1;
}
static void confusion_floor(C3BattleStage *s)
{
    const C3ConfusionSequenceNode *floor=&s->confusion_program.nodes[s->confusion_phase?10:2];
    if(!(s->confusion_seen&(1u<<(s->confusion_phase?10:2))))return;
    uint64_t elapsed=s->confusion_units/1000-floor->start;const uint32_t *r=floor->resource;
    s->confusion_floor_enabled=1;
    for(unsigned segment=0;segment<r[4];++segment){uint32_t duration=r[9+segment*4];
        if(duration==UINT32_MAX||elapsed<duration){float fraction=duration==UINT32_MAX?0:(float)elapsed/duration;
            uint32_t from=r[7+segment*4],to=r[8+segment*4];
            for(unsigned k=0;k<3;++k){int a=(uint8_t)(from>>(8*k)),b=(uint8_t)(to>>(8*k));s->confusion_floor_rgb[k]=(uint8_t)(a+fraction*(b-a));}
            return;
        }elapsed-=duration;
    }
    s->confusion_floor_enabled=0;
}
static int confusion_step(C3BattleStage *s,uint32_t ms,char *e,size_t n)
{
    if(!s->confusion_active)return 1;
    if(s->confusion_units+(uint64_t)ms*60>3600000)return fail(e,n,"Confusion source clock overload.");
    s->confusion_units+=(uint64_t)ms*60;
    unsigned phase=s->confusion_phase,owner=phase?s->confusion_target:s->confusion_owner;
    C3ParticleSystem *particles=phase?s->confusion_damage_particles:&s->particles;
    if(s->confusion_particles_started){
        if(phase){float pos[3];if(!confusion_position(s,0,pos,e,n))return 0;for(unsigned k=0;k<3;++k)pos[k]*=10;
            /* field05=0 continues following. Attack nodeflags2 sets1 and
             * therefore updates once. Never post-translate emitted particles. */
            for(unsigned root=0;root<5;++root){uint32_t handle=s->confusion_roots[root];if(!handle)continue;int live=0;
                for(unsigned i=0;i<C3_PARTICLE_MAX_EMITTERS;++i)if(particles->emitters[i].active&&particles->emitters[i].serial==handle){live=1;break;}
                if(live&&!c3_particle_position_root(particles,handle,pos,e,n))return 0;
            }
        }
        if(!c3_particle_step(particles,ms,0,e,n))return 0;
    }
    if(s->confusion_type12.active&&!c3_confusion_type12_step(&s->confusion_type12,ms,e,n))return 0;
    for(unsigned i=0;i<15;++i){const C3ConfusionSequenceNode *q=&s->confusion_program.nodes[i];
        if(q->phase!=phase||(s->confusion_seen&(1u<<i))||s->confusion_units<(uint64_t)q->start*1000)continue;
        if(q->kind==3){float pos[3];if(!confusion_position(s,0,pos,e,n))return 0;for(unsigned k=0;k<3;++k)pos[k]*=10;
            uint32_t handle=0;
            if(!s->confusion_particles_started){
                if(!c3_particle_begin_position_flags(particles,&s->confusion_pack[phase].bank,q->resource[0],0x434f4e46u^s->confusion_event,pos,s->transforms[owner][3],.75f,.75f,(uint16_t)q->resource[1],e,n))return 0;
                handle=particles->root_emitter_serial;s->confusion_particles_started=1;
            }else if(!c3_particle_append_position_flags(particles,&s->confusion_pack[phase].bank,q->resource[0],pos,s->transforms[owner][3],.75f,.75f,(uint16_t)q->resource[1],&handle,e,n))return 0;
            unsigned index=0;while(index<5&&s->confusion_roots[index])++index;if(index==5)return fail(e,n,"Confusion root count exceeded.");s->confusion_roots[index]=handle;
            if(s->trace){fprintf(s->trace,"wzx_confusion_particle phase=%u key=%lu tick=%lu units=%llu root=%lu flags=%04lx attachment=%s owner=%lu part=%u center=%.6f,%.6f,%.6f handle=%lu\n",phase,(unsigned long)q->key,(unsigned long)q->start,(unsigned long long)s->confusion_units,(unsigned long)q->resource[0],(unsigned long)q->resource[1],phase?"follow":"one_shot",(unsigned long)s->slots[owner],phase?53u:62u,(double)(pos[0]*.1f),(double)(pos[1]*.1f),(double)(pos[2]*.1f),(unsigned long)handle);fflush(s->trace);}
        }else if(q->kind==4&&q->resource[0]==12){float pos[3];if(!confusion_position(s,1,pos,e,n)||!c3_confusion_type12_begin(&s->confusion_type12,&s->confusion_mask.phase[phase],s->slots[owner],phase?1:s->slots[s->confusion_target],q->phase?53:62,pos,e,n))return 0;
            if(s->trace){fprintf(s->trace,"wzx_confusion_type12 phase=%u key=%lu tick=%lu units=%llu owner=%lu part=%u attachment=one_shot center=%.6f,%.6f,%.6f\n",phase,(unsigned long)q->key,(unsigned long)q->start,(unsigned long long)s->confusion_units,(unsigned long)s->slots[owner],phase?53u:62u,(double)pos[0],(double)pos[1],(double)pos[2]);fflush(s->trace);}
        }else if(q->kind==5){unsigned index=0;while(index<4&&s->confusion_cues[index])++index;
            if(index==4||s->cue_serial==UINT32_MAX)return fail(e,n,"Confusion audio cue identity exhausted.");
            uint32_t cue=++s->cue_serial;if(!s->sound_cue(cue,q->resource[0],e,n))return 0;s->confusion_cues[index]=cue;
            if(s->trace){fprintf(s->trace,"wzx_confusion_sound phase=%u key=%lu tick=%lu sound=%lu cue=%lu event=%lu\n",phase,(unsigned long)q->key,(unsigned long)q->start,(unsigned long)q->resource[0],(unsigned long)cue,(unsigned long)s->confusion_event);fflush(s->trace);}
        }
        s->confusion_seen|=1u<<i;
    }
    if(phase&&s->confusion_units>=44000&&!s->confusion_hp_gate){s->confusion_hp_gate=1;s->clips[owner]=s->roles[owner][3];s->clocks[owner]=0;s->fresh[owner]=1;
        if(s->trace){fprintf(s->trace,"wzx_confusion_hp_gate tick=44 units=%llu target=%lu\n",(unsigned long long)s->confusion_units,(unsigned long)s->slots[owner]);fflush(s->trace);}
    }
    confusion_floor(s);
    if(s->confusion_units<(uint64_t)s->confusion_program.owner_end[phase]*1000)return 1;
    if(!phase)s->confusion_floor_enabled=0;
    int audio_done=1;unsigned cue=0;
    for(unsigned i=0;i<15;++i){const C3ConfusionSequenceNode *q=&s->confusion_program.nodes[i];if(q->phase==phase&&q->kind==5){
        if(!s->confusion_cues[cue])return fail(e,n,"Missing Confusion crossed audio cue.");
        int done=s->sound_done(s->confusion_cues[cue++],q->resource[0],e,n);if(done<0)return 0;if(!done)audio_done=0;
    }}
    if(!audio_done||s->confusion_type12.active||!s->confusion_particles_started||!c3_particle_finished(particles)||s->confusion_floor_enabled)return 1;
    if(s->trace){fprintf(s->trace,"wzx_confusion_complete phase=%u units=%llu emitted=%lu peak=%lu cosmetic_draws=%llu audio_done=1 cues=%lu,%lu,%lu,%lu\n",phase,(unsigned long long)s->confusion_units,(unsigned long)particles->emitted,(unsigned long)particles->peak_particles,(unsigned long long)particles->cosmetic_draws,(unsigned long)s->confusion_cues[0],(unsigned long)s->confusion_cues[1],(unsigned long)s->confusion_cues[2],(unsigned long)s->confusion_cues[3]);fflush(s->trace);}
    s->clips[owner]=s->roles[owner][0];s->clocks[owner]=0;s->fresh[owner]=1;
    if(!phase&&s->confusion_pending){s->confusion_pending=0;return confusion_phase_begin(s,1,e,n);}
    s->confusion_active=0;return 1;
}
static int confusion_bind_particles(C3BattleStage *s,char *e,size_t n)
{
    static const float identity[12]={.1f,0,0,0,0,.1f,0,0,0,0,.1f,0};
    const C3ParticleSystem *particles=s->confusion_phase?s->confusion_damage_particles:&s->particles;
    if(!c3_stage_renderer_particles(s->confusion_particles_started?particles:NULL,identity))return fail(e,n,"Confusion particle binding failed.");
    return 1;
}
