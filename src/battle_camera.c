#include "colosseum3ds/battle_camera.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static float f32(const uint8_t *p){uint32_t u=u32(p);float f;memcpy(&f,&u,4);return f;}
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
static int valid_single_camera(const C3BattleCameraProgram *p)
{
    int growl=p&&p->move_id==45;
    if(!p||(p->move_id!=39&&!growl)||!p->samples||!p->sample_count||p->sample_count>3*C3_BATTLE_CAMERA_MAX_TRACK_FRAMES||p->flags[0]!=(growl?0x200858u:0x78u)||p->pattern[0]!=(growl?5u:UINT32_MAX)||p->fov_mask[0]!=(growl?2u:0u)||p->entry[0]!=(growl?1u:0u)||p->owner_start[0]||p->flags[1]||p->pattern[1]||p->fov_mask[1]||p->entry[1]||p->owner_start[1])return 0;
    for(unsigned i=0;i<3;++i){const C3BattleCameraBinding *a=&p->actors[i];
        if(a->species!=(uint32_t[]){197,196,288}[i]||a->scale_selector!=-1||a->animation_kind[0]!=(growl?4u:2u)||a->part[0]!=(uint32_t[]){42,62,53}[i]||!a->timing[0][0]||a->timing[0][0]>1800||!a->track_count[0]||a->track_count[0]>1025||a->track_offset[0]>p->sample_count||a->track_count[0]>p->sample_count-a->track_offset[0]||a->track_count[1]||a->part[1]||a->animation_kind[1])return 0;
        for(unsigned k=0;k<4;++k)if((!growl&&k&&a->timing[0][k])||a->timing[0][k]>1800||a->timing[1][k])return 0;
        if(growl&&(!a->timing[0][1]||a->timing[0][0]>a->timing[0][1]||a->timing[0][1]>a->timing[0][2]||a->timing[0][3]))return 0;
        float size=0;for(unsigned k=0;k<3;++k){if(!isfinite(a->minimum[k])||!isfinite(a->maximum[k])||fabsf(a->minimum[k])>10000||fabsf(a->maximum[k])>10000||a->minimum[k]>a->maximum[k])return 0;float d=a->maximum[k]-a->minimum[k];size+=d*d;}if(size<=0)return 0;
    }
    for(uint32_t i=0;i<p->sample_count;++i)for(unsigned k=0;k<3;++k)if(!isfinite(p->samples[i][k])||fabsf(p->samples[i][k])>1000)return 0;
    return 1;
}
static int valid(const C3BattleCameraProgram *p)
{
    if(p&&(p->move_id==39||p->move_id==45))return valid_single_camera(p);
    static const uint32_t species[3]={197,196,288};
    if(!p||!p->samples||!p->sample_count||p->sample_count>6*C3_BATTLE_CAMERA_MAX_TRACK_FRAMES)return 0;
    if(p->move_id==93){if(p->flags[0]!=0x200848||p->flags[1]!=0x48||p->pattern[0]!=5||p->pattern[1]!=UINT32_MAX||p->fov_mask[0]!=2||p->fov_mask[1]||p->entry[0]!=1||p->entry[1]!=8||p->owner_start[0]||p->owner_start[1]!=44)return 0;}
    else if(p->move_id||p->flags[0]!=0x201048||p->flags[1]!=0x200848||p->pattern[0]!=6||p->pattern[1]!=5||p->fov_mask[0]!=4||p->fov_mask[1]!=2||p->entry[0]!=2||p->entry[1]!=8||p->owner_start[0]!=0||p->owner_start[1]!=22)return 0;
    for(unsigned i=0;i<3;++i){const C3BattleCameraBinding *a=&p->actors[i];
        if(a->species!=species[i]||a->scale_selector!=-1||a->animation_kind[0]!=4||a->animation_kind[1]!=3||a->part[0]!=(uint32_t[]){42,62,53}[i]||a->part[1]!=(uint32_t[]){42,62,53}[i])return 0;
        for(unsigned k=0;k<3;++k)if(!isfinite(a->minimum[k])||!isfinite(a->maximum[k])||a->minimum[k]>a->maximum[k]||fabsf(a->minimum[k])>10000||fabsf(a->maximum[k])>10000)return 0;
        float length=0;for(unsigned k=0;k<3;++k){float d=a->maximum[k]-a->minimum[k];length+=d*d;}if(length<=0)return 0;
        for(unsigned phase=0;phase<2;++phase){if(!a->track_count[phase]||a->track_count[phase]>C3_BATTLE_CAMERA_MAX_TRACK_FRAMES||a->track_offset[phase]>p->sample_count||a->track_count[phase]>p->sample_count-a->track_offset[phase])return 0;
            if(!a->timing[phase][0]||a->timing[phase][0]>a->timing[phase][1])return 0;
            for(unsigned k=0;k<4;++k)if(a->timing[phase][k]>1800)return 0;}
    }
    for(uint32_t i=0;i<p->sample_count;++i)for(unsigned k=0;k<3;++k)if(!isfinite(p->samples[i][k])||fabsf(p->samples[i][k])>1000)return 0;
    return 1;
}
static int decode_program(C3BattleCameraProgram *out,const void *data,size_t bytes,char *e,size_t n,int confusion)
{
    const uint8_t *b=data;C3BattleCameraProgram p={0};uint8_t digest[20];uint32_t base=confusion>=2?1120u:confusion?1500u:512u;p.move_id=confusion==3?45u:confusion==2?39u:confusion?93u:0;
    static const uint8_t pin[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    if(!out||!b||bytes<base||bytes>C3_BATTLE_CAMERA_MAX_BYTES||memcmp(b,confusion==3?"C3GC":confusion==2?"C3TC":confusion?"C3CC":"C3BC",4)||u32(b+4)!=(confusion?1u:3u)||u32(b+8)!=bytes||u32(b+12)!=(confusion==3?45u:confusion==2?39u:confusion?93u:44u)||u32(b+16)!=60||u32(b+20)!=3||u32(b+24)!=(confusion>=2?1u:2u)||memcmp(b+32,pin,20))return fail(e,n,"Unsupported source Bite camera descriptor.");
    uint32_t total=u32(b+28);if(!total||total>6*C3_BATTLE_CAMERA_MAX_TRACK_FRAMES||bytes!=base+12u*total)return fail(e,n,"Source camera track bounds.");
    for(unsigned k=52;k<64;++k)if(b[k])return fail(e,n,"Unknown camera header extension.");
    for(unsigned phase=0;phase<2;++phase){const uint8_t *r=b+64+phase*64;p.flags[phase]=u32(r);p.pattern[phase]=u32(r+4);p.fov_mask[phase]=u32(r+8);p.entry[phase]=u32(r+12);p.owner_start[phase]=u32(r+16);for(unsigned k=20;k<64;++k)if(r[k])return fail(e,n,"Unknown camera profile extension.");}
    for(unsigned i=0;i<3;++i){const uint8_t *r=b+192+96*i;C3BattleCameraBinding *a=&p.actors[i];a->species=u32(r);a->scale_selector=(int32_t)u32(r+4);
        for(unsigned k=0;k<3;++k){a->minimum[k]=f32(r+8+4*k);a->maximum[k]=f32(r+20+4*k);}
        for(unsigned phase=0;phase<2;++phase){for(unsigned k=0;k<4;++k)a->timing[phase][k]=u32(r+32+phase*16+k*4);a->part[phase]=u32(r+64+phase*4);a->animation_kind[phase]=u32(r+72+phase*4);}
        for(unsigned phase=0;phase<2;++phase){uint32_t offset=u32(r+80+phase*8);if(offset<base||(offset-base)%12)return fail(e,n,"Camera part track alignment.");a->track_offset[phase]=(offset-base)/12;a->track_count[phase]=u32(r+84+phase*8);}
    }
    for(unsigned k=500;k<512;++k)if(b[k])return fail(e,n,"Unknown camera checksum extension.");
    uint8_t *checked=malloc(bytes);if(!checked)return fail(e,n,"Camera checksum allocation failed.");
    memcpy(checked,b,bytes);memset(checked+480,0,20);c3_save_codec_sha1(digest,checked,bytes);free(checked);
    if(memcmp(digest,b+480,20))return fail(e,n,"Camera descriptor checksum mismatch.");
    if(confusion>=2){
        static const uint8_t pin_table[20]={0xcb,0xde,0xd7,0xa9,0xed,0x87,0x4d,0x8b,0x68,0xa9,0xe2,0x0c,0x3f,0xe0,0x82,0xd2,0xc7,0x5d,0xef,0x47};
        c3_save_codec_sha1(digest,b+512,608);if(memcmp(digest,pin_table,20))return fail(e,n,"Source short FOV table drift.");
        for(unsigned i=0;i<8;++i)for(unsigned j=0;j<19;++j)p.patterns[i][j]=u32(b+512+76*i+4*j);
    }else if(confusion==1){
        static const uint8_t expected[20]={0x17,0x6a,0x55,0x77,0x01,0x52,0xba,0xb9,0x23,0x80,0x8f,0x8f,0x20,0xaf,0x6a,0x60,0x66,0xb4,0xd7,0x94};
        c3_save_codec_sha1(digest,b+512,988);if(memcmp(digest,expected,20))return fail(e,n,"Confusion source FOV table drift.");
        for(unsigned i=0;i<13;++i)for(unsigned j=0;j<19;++j)p.patterns[i][j]=u32(b+512+76*i+4*j);
    }
    p.samples=malloc(total*sizeof *p.samples);if(!p.samples)return fail(e,n,"Camera part track allocation failed.");p.sample_count=total;p.owned=1;
    for(uint32_t i=0;i<total;++i)for(unsigned k=0;k<3;++k)p.samples[i][k]=f32(b+base+12*i+4*k);
    uint32_t cursor=0;for(unsigned i=0;i<3;++i)for(unsigned phase=0;phase<2;++phase){if(p.actors[i].track_offset[phase]!=cursor){free(p.samples);return fail(e,n,"Camera part tracks must be contiguous.");}cursor+=p.actors[i].track_count[phase];}
    if(cursor!=total||!valid(&p)){free(p.samples);return fail(e,n,"Unsupported source camera flags, bounds, timing or body-map part.");}
    c3_battle_camera_close(out);*out=p;if(e&&n)e[0]=0;return 1;
}
int c3_battle_camera_decode(C3BattleCameraProgram *p,const void *b,size_t z,char *e,size_t n){return decode_program(p,b,z,e,n,0);}
int c3_battle_camera_confusion_decode(C3BattleCameraProgram *p,const void *b,size_t z,char *e,size_t n){return decode_program(p,b,z,e,n,1);}
int c3_battle_camera_tailwhip_decode(C3BattleCameraProgram *p,const void *b,size_t z,char *e,size_t n){return decode_program(p,b,z,e,n,2);}
int c3_battle_camera_growl_decode(C3BattleCameraProgram *p,const void *b,size_t z,char *e,size_t n){return decode_program(p,b,z,e,n,3);}
void c3_battle_camera_close(C3BattleCameraProgram *p){if(p){if(p->owned)free(p->samples);memset(p,0,sizeof *p);}}

static int load_program(C3BattleCameraProgram *out,const char *path,char *e,size_t n,int confusion)
{
    if(!out||!path)return fail(e,n,"Missing camera path.");
    FILE *f=fopen(path,"rb");if(!f)return fail(e,n,"Cannot open source camera descriptor.");
    uint8_t *bytes=malloc(C3_BATTLE_CAMERA_MAX_BYTES);if(!bytes){fclose(f);return fail(e,n,"Camera file allocation failed.");}
    size_t count=fread(bytes,1,C3_BATTLE_CAMERA_MAX_BYTES,f);int extra=fgetc(f),io=ferror(f);if(fclose(f))io=1;
    if(io||count<512||extra!=EOF){free(bytes);return fail(e,n,"Invalid camera descriptor length.");}
    int ok=decode_program(out,bytes,count,e,n,confusion);free(bytes);return ok;
}

int c3_battle_camera_load(C3BattleCameraProgram *p,const char *f,char *e,size_t n){return load_program(p,f,e,n,0);}
int c3_battle_camera_confusion_load(C3BattleCameraProgram *p,const char *f,char *e,size_t n){return load_program(p,f,e,n,1);}
int c3_battle_camera_tailwhip_load(C3BattleCameraProgram *p,const char *f,char *e,size_t n){return load_program(p,f,e,n,2);}
int c3_battle_camera_growl_load(C3BattleCameraProgram *p,const char *f,char *e,size_t n){return load_program(p,f,e,n,3);}
void c3_battle_camera_init(C3BattleCamera *s,uint32_t seed){if(s){memset(s,0,sizeof *s);s->rng=seed?seed:1;s->previous_motion=UINT32_MAX;}}
static uint32_t random_word(C3BattleCamera *s){uint32_t x=s->rng;x^=x<<13;x^=x>>17;x^=x<<5;s->rng=x;++s->random_draws;return x;}
static float random_unit(C3BattleCamera *s){return (float)(random_word(s)>>8)*(1.0f/16777216.0f);}
static uint32_t tail_bounded(C3BattleCamera *s,uint32_t bound){uint32_t a=random_word(s)&65535,b=random_word(s)&65535;return ((b<<16)|a)%bound;}
static int actors_valid(const C3BattleCameraActor a[4])
{
    if(!a)return 0;
    for(unsigned i=0;i<4;++i){if(a[i].slot!=i||a[i].species!=(uint32_t[]){197,196,288,288}[i]||a[i].source_facing!=(i<2?1:-1)||(a[i].source_facing!=-1&&a[i].source_facing!=1)||(a[i].species!=197&&a[i].species!=196&&a[i].species!=288)||!isfinite(a[i].yaw))return 0;
        for(unsigned k=0;k<3;++k)if(!isfinite(a[i].position[k])||fabsf(a[i].position[k])>1000)return 0;}return 1;
}
static void sample(C3BattleCamera *s)
{
    float t=s->duration?fminf(1,(float)s->ticks/(float)s->duration):1;
    float yaw=s->yaw_start+(s->yaw_end-s->yaw_start)*t;
    if((s->move_id==39||s->move_id==45)&&(s->motion==0||s->motion==1)){
        float t=s->motion_duration?fminf(1,(float)s->ticks/s->motion_duration):1;s->camera=s->restored;
        for(unsigned k=0;k<3;++k){s->camera.eye[k]=s->origin[k]+s->eye_from[k]+(s->eye_to[k]-s->eye_from[k])*t;s->camera.target[k]=s->origin[k]+s->view[k];}
        s->camera.roll=0;s->camera.fov_y_degrees=s->fov;return;
    }
    s->camera=s->restored;s->camera.eye[0]=s->origin[0]+sinf(yaw)*s->distance;
    s->camera.eye[1]=s->origin[1]+s->height;s->camera.eye[2]=s->origin[2]+cosf(yaw)*s->distance;
    for(unsigned k=0;k<3;++k)s->camera.target[k]=s->origin[k]+s->view[k];
    s->camera.roll=0;s->camera.fov_y_degrees=s->fov;
}
static float pattern_float(uint32_t v){float f;memcpy(&f,&v,4);return f;}
static int fov_mix(uint32_t flags,float *lo,float *hi){
    if(!flags||flags>7)return 0;
    *lo=0;*hi=1;int first=0,second=0;
    if(flags&1){*lo=0;*hi=.2f;first=1;}
    if(flags&2){if(!first)*lo=.35f;*hi=.6f;second=1;}
    if(flags&4){if(!second)*lo=.75f;*hi=1;}
    return 1;
}
static int confusion_fov(C3BattleCamera *s,const C3BattleCameraProgram *p,const C3BattleCameraBinding *a,float low,float high,char *e,size_t n){
    float choice=random_unit(s),end=0;unsigned selected=13;
    for(unsigned i=0;i<13;++i){float weight=pattern_float(p->patterns[i][0]);if(!isfinite(weight)||weight<0||weight>1)return fail(e,n,"Invalid Confusion FOV weight.");end+=weight;if(choice<end){selected=i;break;}}
    if(selected==13)return fail(e,n,"Unqualified Confusion FOV fallback.");
    const uint32_t *row=p->patterns[selected];float lo,hi;
    if(!fov_mix(row[7],&lo,&hi))return fail(e,n,"Unsupported Confusion initial FOV mask.");
    float current=low+(high-low)*(lo+(hi-lo)*random_unit(s));
    s->fov=current;s->fov_pattern=selected;s->dynamic_fov=1;
    uint32_t previous=0;
    for(unsigned i=0;i<2;++i){const uint32_t *r=row+1+9*i;uint32_t next=a->timing[1][i];
        s->fov_begin[i]=previous;s->fov_finish[i]=next;s->fov_start[i]=s->fov_end[i]=current;
        if(next<previous)return fail(e,n,"Invalid Confusion FOV gate order.");
        if(previous!=next&&(r[0]==3||r[0]==4)){
            uint32_t duration=next-previous,mode=r[1],cap;float random=random_unit(s);
            if(mode==5)cap=random<pattern_float(r[2])?12:45;
            else if(mode==12)cap=random<pattern_float(r[4])?45:duration;
            else if(mode==13)cap=random<pattern_float(r[2])?12:random<pattern_float(r[4])?45:duration;
            else return fail(e,n,"Unsupported Confusion source duration mode.");
            if(duration>cap)duration=cap;
            if(!fov_mix(r[7],&lo,&hi))return fail(e,n,"Unsupported Confusion FOV mask.");
            float fov=low+(high-low)*(lo+(hi-lo)*random_unit(s));
            if(r[8]==2)s->fov_begin[i]=next-duration;
            else if(r[8]==1)s->fov_finish[i]=previous+duration;
            else return fail(e,n,"Unsupported Confusion FOV timing mode.");
            s->fov_end[i]=fov;current=fov;
        }else if(r[0]>4)return fail(e,n,"Unsupported Confusion FOV mode.");
        previous=next;
    }
    return 1;
}
static int begin_program(C3BattleCamera *s,const C3BattleCameraProgram *p,C3BattleCameraPhase phase,unsigned owner,unsigned target,const C3BattleCameraActor actors[4],const C3TitleCamera *base,char *e,size_t n)
{
    if(!s||!s->rng||!valid(p)||(phase!=C3_BATTLE_CAMERA_ATTACK&&phase!=C3_BATTLE_CAMERA_DAMAGE)||owner>=4||target>=4||owner==target||!actors_valid(actors)||!base)return fail(e,n,"Invalid checked camera owner, target or source profile.");
    for(unsigned k=0;k<3;++k)if(!isfinite(base->eye[k])||!isfinite(base->target[k]))return fail(e,n,"Nonfinite inherited camera.");
    if(!isfinite(base->fov_y_degrees)||base->fov_y_degrees<=0||base->fov_y_degrees>=180||!isfinite(base->roll))return fail(e,n,"Invalid inherited camera lens.");
    if(!isfinite(base->aspect)||base->aspect<=0||!isfinite(base->near_plane)||base->near_plane<=0||!isfinite(base->far_plane)||base->far_plane<=base->near_plane)return fail(e,n,"Invalid inherited camera projection.");
    const C3BattleCameraBinding *a=NULL;for(unsigned i=0;i<3;++i)if(p->actors[i].species==actors[owner].species)a=&p->actors[i];
    if(!a)return fail(e,n,"Missing camera owner species binding.");
    C3BattleCamera next=*s;next.move_id=p->move_id;next.owner=owner;next.target=target;next.restored=*base;next.units=0;next.ticks=0;next.paused=0;next.active=1;++next.cuts;
    unsigned retries=0;do{next.motion=random_unit(&next)<.5f?3:2;if(next.motion==3&&next.motion==next.previous_motion)next.motion=2;if(++retries>64)return fail(e,n,"Cosmetic camera motion rejection bound.");}while(next.motion==next.previous_motion);
    next.previous_motion=next.motion;
    /* Runtime PKX table has count4(action)/3(hit), reserved value0 at0x0C,
     * then timing0@0x10. DoPosition selects +4+count*4: timing1(action),
     * timing0(hit), with the next field as a zero fallback. shift=0 for actors. */
    unsigned duration_index=phase==C3_BATTLE_CAMERA_ATTACK?1:0;
    next.duration=a->timing[phase][duration_index];
    if(!next.duration)next.duration=a->timing[phase][duration_index+1];
    int confusion=p->move_id==93;
    float distance_min=(confusion?(phase==C3_BATTLE_CAMERA_ATTACK?35:20):(phase==C3_BATTLE_CAMERA_ATTACK?50:35))*.875f,distance_max=(confusion?(phase==C3_BATTLE_CAMERA_ATTACK?50:60):(phase==C3_BATTLE_CAMERA_ATTACK?60:50))*.875f;if(distance_min<20)distance_min=20;if(distance_min>48)distance_min=48;if(distance_max<40)distance_max=40;if(distance_max>60)distance_max=60;
    float upper=confusion?(phase==C3_BATTLE_CAMERA_ATTACK?11:20):(phase==C3_BATTLE_CAMERA_ATTACK?20:11);
    float low=a->minimum[1],high=a->maximum[1];if(low<6){low=6;if(high<6)high=9;}if(high>upper){high=upper;if(low>upper)low=.8f*upper;}
    next.distance=(distance_min+(distance_max-distance_min)*random_unit(&next))*.1f;
    next.height=(low+(high-low)*random_unit(&next))*.1f;
    float rot0=.31415927f,rot1=confusion&&phase==C3_BATTLE_CAMERA_DAMAGE?1.0995574f:.62831855f;
    next.yaw_start=actors[owner].source_facing<0?actors[owner].yaw-rot0-(rot1-rot0)*random_unit(&next):actors[owner].yaw+rot0+(rot1-rot0)*random_unit(&next);
    next.yaw_end=next.yaw_start;
    if(next.motion==2){next.yaw_end=actors[owner].source_facing<0?actors[owner].yaw-rot0-(rot1-rot0)*random_unit(&next):actors[owner].yaw+rot0+(rot1-rot0)*random_unit(&next);if(next.yaw_start>next.yaw_end){float swap=next.yaw_start;next.yaw_start=next.yaw_end;next.yaw_end=swap;}}
    float length=0;for(unsigned k=0;k<3;++k){float d=a->maximum[k]-a->minimum[k];length+=d*d;next.origin[k]=actors[owner].position[k];}
    float size=.57735026f*sqrtf(length)*1.1f,height=(a->maximum[1]-a->minimum[1])*1.1f,span=fmaxf(.75f*size,height);
    float radius=sqrtf(next.distance*next.distance+next.height*next.height)*10;
    float fov_low=fminf(85,fmaxf(15,57.29578f*2*atan2f(.5f*span,radius))),fov_high=fminf(85,fmaxf(15,57.29578f*2*atan2f(2*span,radius)));
    if(confusion&&phase==C3_BATTLE_CAMERA_ATTACK)(void)random_unit(&next); /* GetPattern always consumes its initial random draw, even a forced entry. */
    float mix=confusion&&phase==C3_BATTLE_CAMERA_DAMAGE?0:p->fov_mask[phase]==4?.75f+.25f*random_unit(&next):.35f+.25f*random_unit(&next);
    next.track=(const float (*)[3])(p->samples+a->track_offset[phase]);next.track_count=a->track_count[phase];next.track_delay=p->owner_start[phase];
    float co=cosf(actors[owner].yaw),si=sinf(actors[owner].yaw);
    next.view[0]=co*next.track[0][0]+si*next.track[0][2];next.view[1]=next.track[0][1];next.view[2]=-si*next.track[0][0]+co*next.track[0][2];
    next.dynamic_fov=0;next.fov=fov_low+(fov_high-fov_low)*mix;
    if(confusion&&phase==C3_BATTLE_CAMERA_DAMAGE&&!confusion_fov(&next,p,a,fov_low,fov_high,e,n))return 0;
    sample(&next);*s=next;if(e&&n)e[0]=0;return 1;
}
int c3_battle_camera_begin(C3BattleCamera *s,const C3BattleCameraProgram *p,C3BattleCameraPhase phase,unsigned owner,unsigned target,const C3BattleCameraActor a[4],const C3TitleCamera *b,char *e,size_t n){
    if(!p||p->move_id)return fail(e,n,"Legacy Bite camera requires its original program.");
    return begin_program(s,p,phase,owner,target,a,b,e,n);
}
static int begin_single_camera(C3BattleCamera *s,const C3BattleCameraProgram *p,C3BattleCameraPhase phase,unsigned owner,unsigned target,const C3BattleCameraActor actors[4],const C3TitleCamera *base,char *e,size_t n)
{
    if(!s||!s->rng||!valid_single_camera(p)||phase!=C3_BATTLE_CAMERA_ATTACK||owner>=4||target>=4||owner==target||!actors_valid(actors)||!base)return fail(e,n,"Unqualified single-phase camera owner, phase or profile.");
    for(unsigned k=0;k<3;++k)if(!isfinite(base->eye[k])||!isfinite(base->target[k]))return fail(e,n,"Nonfinite inherited TailWhip camera.");
    if(!isfinite(base->roll)||!isfinite(base->fov_y_degrees)||base->fov_y_degrees<=0||base->fov_y_degrees>=180||!isfinite(base->aspect)||base->aspect<=0||!isfinite(base->near_plane)||base->near_plane<=0||!isfinite(base->far_plane)||base->far_plane<=base->near_plane)return fail(e,n,"Invalid inherited TailWhip projection.");
    const C3BattleCameraBinding *a=NULL;for(unsigned i=0;i<3;++i)if(p->actors[i].species==actors[owner].species)a=&p->actors[i];if(!a)return fail(e,n,"Missing TailWhip source species.");
    C3BattleCamera next=*s;next.move_id=p->move_id;next.owner=owner;next.target=target;next.restored=*base;next.units=0;next.ticks=0;next.paused=0;next.active=1;next.dynamic_fov=0;++next.cuts;
    int growl=p->move_id==45;unsigned count=growl?3:4;float interval=1.f/count;
    int selected=0;static const unsigned motions[4]={3,0,1,2},growl_motions[3]={3,0,2};
    for(unsigned retry=0;retry<64&&!selected;++retry){float choice=random_unit(&next);for(unsigned i=0;i<count;++i)if(choice<interval*(i+1)){next.motion=growl?growl_motions[i]:motions[i];if(next.motion!=next.previous_motion){selected=1;break;}}}
    if(!selected)return fail(e,n,"TailWhip previous-motion rejection bound.");
    next.previous_motion=next.motion;
    next.duration=a->timing[0][growl?1:0];next.motion_duration=next.duration;
    float low=a->minimum[1],high=a->maximum[1];if(low<6){low=6;if(high<6)high=9;}float upper=growl?11.f:20.f;if(high>upper){high=upper;if(low>upper)low=.8f*upper;}
    float distance_min=growl?30.625f:20.f,distance_range=growl?13.125f:32.5f,rotation_max=growl?.62831855f:1.0995574f;
    float height,distance,rotation=0,radius[3];int reverse=actors[owner].source_facing<0;
    if(next.motion==1){
        float offset=25+10*random_unit(&next);height=1+9*random_unit(&next);distance=20+30*random_unit(&next);float sign=reverse?1.f:-1.f;
        next.eye_from[0]=sign;next.eye_to[0]=sign*(10+offset)*.1f;next.eye_from[1]=next.eye_to[1]=height*.1f;next.eye_from[2]=next.eye_to[2]=distance*.1f;
        radius[0]=sqrtf(offset*offset+height*height);radius[2]=sqrtf(distance*distance+radius[0]*radius[0]);radius[1]=.5f*(radius[0]+radius[2]);
    }else{
        float near_distance,far_distance;
        if(next.motion==0){
            int alternate=random_unit(&next)<=.7f;float chance=random_unit(&next),threshold0=alternate?.3f:.5f,threshold1=alternate?.65f:.75f;uint32_t delay=0;
            if(chance>=threshold0&&chance<threshold1){next.motion_duration=tail_bounded(&next,next.duration);if(next.motion_duration<(next.duration>>1))next.motion_duration=next.duration>>1;}
            else if(chance>=threshold1){delay=tail_bounded(&next,next.duration);if(delay>(next.duration>>1))delay=next.duration>>1;next.motion_duration=next.duration-delay;}
            near_distance=distance_min+distance_range*random_unit(&next);far_distance=distance_min+distance_range*random_unit(&next);
            if((alternate&&near_distance<far_distance)||(!alternate&&near_distance>far_distance)){float swap=near_distance;near_distance=far_distance;far_distance=swap;}
            height=low+(high-low)*random_unit(&next);float r=random_unit(&next);rotation=reverse?actors[owner].yaw-.31415927f-(rotation_max-.31415927f)*r:actors[owner].yaw+.31415927f+(rotation_max-.31415927f)*r;
            next.eye_from[0]=sinf(rotation)*near_distance*.1f;next.eye_from[1]=next.eye_to[1]=height*.1f;next.eye_from[2]=cosf(rotation)*near_distance*.1f;
            next.eye_to[0]=sinf(rotation)*far_distance*.1f;next.eye_to[2]=cosf(rotation)*far_distance*.1f;
        }else{
            near_distance=far_distance=distance_min+distance_range*random_unit(&next);height=low+(high-low)*random_unit(&next);
            float r=random_unit(&next);rotation=reverse?actors[owner].yaw-.31415927f-(rotation_max-.31415927f)*r:actors[owner].yaw+.31415927f+(rotation_max-.31415927f)*r;next.yaw_start=next.yaw_end=rotation;
            if(next.motion==2){float r=random_unit(&next);next.yaw_end=reverse?actors[owner].yaw-.31415927f-(rotation_max-.31415927f)*r:actors[owner].yaw+.31415927f+(rotation_max-.31415927f)*r;if(next.yaw_start>next.yaw_end){float swap=next.yaw_start;next.yaw_start=next.yaw_end;next.yaw_end=swap;}}
        }
        distance=near_distance;radius[0]=sqrtf(near_distance*near_distance+height*height);float middle=.5f*(near_distance+far_distance);radius[1]=sqrtf(middle*middle+height*height);radius[2]=sqrtf(far_distance*far_distance+height*height);
    }
    next.distance=distance*.1f;next.height=height*.1f;
    float length=0;for(unsigned k=0;k<3;++k){float d=a->maximum[k]-a->minimum[k];length+=d*d;next.origin[k]=actors[owner].position[k];}
    float size=.57735026f*sqrtf(length)*1.1f,model_height=(a->maximum[1]-a->minimum[1])*1.1f,span=fmaxf(.75f*size,model_height);
    float fov_low=fminf(85,fmaxf(15,57.29578f*2*atan2f(.5f*span,radius[0]))),fov_high=fminf(85,fmaxf(15,57.29578f*2*atan2f(2*span,radius[0]))),mix_low=.35f,mix_high=.6f;next.fov_pattern=UINT32_MAX;
    if(next.motion!=0){float choice=random_unit(&next),end=0;unsigned pattern=growl?5:8;for(unsigned i=0;!growl&&i<8;++i){float weight=pattern_float(p->patterns[i][0]);if(!isfinite(weight)||weight<0||weight>1)return fail(e,n,"Invalid TailWhip short FOV weight.");end+=weight;if(choice<end){pattern=i;break;}}if(pattern==8)pattern=tail_bounded(&next,8);next.fov_pattern=pattern;if(!fov_mix(p->patterns[pattern][7],&mix_low,&mix_high))return fail(e,n,"Unsupported TailWhip initial FOV mask.");}
    next.fov=fov_low+(fov_high-fov_low)*(mix_low+(mix_high-mix_low)*random_unit(&next));
    next.track=(const float (*)[3])(p->samples+a->track_offset[0]);next.track_count=a->track_count[0];next.track_delay=0;float co=cosf(actors[owner].yaw),si=sinf(actors[owner].yaw);
    next.view[0]=co*next.track[0][0]+si*next.track[0][2];next.view[1]=next.track[0][1];next.view[2]=-si*next.track[0][0]+co*next.track[0][2];
    sample(&next);*s=next;if(e&&n)e[0]=0;return 1;
}
int c3_battle_camera_begin_move(C3BattleCamera *s,const C3BattleCameraProgram *p,uint32_t move,C3BattleCameraPhase phase,unsigned owner,unsigned target,const C3BattleCameraActor a[4],const C3TitleCamera *b,char *e,size_t n){
    if(move==39||move==45){if(!p||p->move_id!=move)return fail(e,n,"Single-phase camera move/program mismatch.");return begin_single_camera(s,p,phase,owner,target,a,b,e,n);}
    if(move==44)return c3_battle_camera_begin(s,p,phase,owner,target,a,b,e,n);
    if(move!=93||!p||p->move_id!=93)return fail(e,n,"Unqualified camera move/program.");
    return begin_program(s,p,phase,owner,target,a,b,e,n);
}
int c3_battle_camera_step(C3BattleCamera *s,uint32_t ms,const C3BattleCameraActor actors[4],char *e,size_t n)
{
    if(!s||!actors_valid(actors)||s->owner>=4||s->target>=4)return fail(e,n,"Invalid active camera actor handles.");
    if(!s->active||s->paused)return 1;
    if(s->units>(uint64_t)C3_BATTLE_CAMERA_MAX_TICKS*1000||s->ticks!=s->units/1000)return fail(e,n,"Invalid source camera clock state.");
    uint64_t next=s->units+(uint64_t)ms*60;if(next>(uint64_t)C3_BATTLE_CAMERA_MAX_TICKS*1000)return fail(e,n,"Source camera sequence/catch-up exceeds3600ticks.");
    C3BattleCamera result=*s;uint32_t ticks=(uint32_t)(next/1000);
    while(result.ticks<ticks){++result.ticks;
        uint32_t age=result.ticks>result.track_delay?result.ticks-result.track_delay:0;
        uint32_t frame=result.move_id==39&&result.track_count>1?age%(result.track_count-1):age<result.track_count?age:result.track_count-1;
        const float *local=result.track[frame];float co=cosf(actors[result.owner].yaw),si=sinf(actors[result.owner].yaw);
        float target[3]={co*local[0]+si*local[2],local[1],-si*local[0]+co*local[2]};
        for(unsigned k=0;k<3;++k)result.view[k]+=(target[k]-result.view[k])/12.0f;
    }
    result.units=next;
    if(result.dynamic_fov){for(unsigned i=0;i<2;++i){
        if(result.ticks<=result.fov_begin[i]){result.fov=result.fov_start[i];break;}
        if(result.ticks<=result.fov_finish[i]){float t=(float)(result.ticks-result.fov_begin[i])/(float)(result.fov_finish[i]-result.fov_begin[i]);result.fov=result.fov_start[i]+(result.fov_end[i]-result.fov_start[i])*t;break;}
        result.fov=result.fov_end[i];
    }}
    sample(&result);*s=result;if(e&&n)e[0]=0;return 1;
}
void c3_battle_camera_pause(C3BattleCamera *s,int paused){if(s)s->paused=(uint8_t)(paused!=0);}
void c3_battle_camera_cancel(C3BattleCamera *s){if(s){s->active=0;s->camera=s->restored;s->track=NULL;s->track_count=0;}}
