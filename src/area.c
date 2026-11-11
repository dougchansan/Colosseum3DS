#include "colosseum3ds/area.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER 128u
#define ACTOR 160u
#define TRIANGLE 56u
#define LINK 32u
#define MAX_BYTES (256u*1024u)
static uint32_t word(const uint8_t *p)
{return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint16_t half(const uint8_t *p){return (uint16_t)(p[0]|p[1]<<8);}
static float real(const uint8_t *p){uint32_t w=word(p);float f;memcpy(&f,&w,4);return f;}
static int fail(char *error,size_t capacity,const char *message)
{if(error&&capacity)snprintf(error,capacity,"%s",message);return 0;}
static int bounded(float v,float limit){return isfinite(v)&&fabsf(v)<=limit;}

void c3_area_close(C3Area *area)
{
    if(!area)return;
    free(area->actors);free(area->transitions);free(area->interactions);free(area->links);
    memset(area,0,sizeof(*area));
}

static int filename(const char *s)
{
    if(!s[0])return 1;
    size_t n=0;
    while(n<64&&s[n]){
        char c=s[n++];
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return 0;
    }
    return n<64&&n>4&&s[0]!='.'&&!strstr(s,"..")&&!strcmp(s+n-4,".c3t");
}

static int read_triangles(C3FieldTriangle *out,const uint8_t *p,uint32_t count)
{
    for(uint32_t i=0;i<count;++i,p+=TRIANGLE){
        for(unsigned j=0;j<9;++j){out[i].vertices[j]=real(p+j*4);if(!bounded(out[i].vertices[j],100000))return 0;}
        float norm=0;
        for(unsigned j=0;j<3;++j){out[i].normal[j]=real(p+36+j*4);norm+=out[i].normal[j]*out[i].normal[j];}
        if(!bounded(norm,1.01f)||norm<.99f||half(p+54))return 0;
        out[i].flags=half(p+48);out[i].source_id=half(p+50);out[i].object=half(p+52);
        if(out[i].object>=256)return 0;
    }
    return 1;
}

int c3_area_parse(C3Area *area,const void *data,size_t bytes,char *error,size_t capacity)
{
    static const uint8_t pin[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,
        0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
    const uint8_t *p=data;C3Area next={0};
    if(!area||!p||bytes<HEADER||bytes>MAX_BYTES||memcmp(p,"C3A1",4)||word(p+4)!=1||word(p+8)!=bytes||
       word(p+44)!=ACTOR||word(p+48)!=TRIANGLE||word(p+52)||memcmp(p+64,pin,20))
        return fail(error,capacity,"Invalid C3A1 area metadata header.");
    for(unsigned i=104;i<HEADER;++i)if(p[i])return fail(error,capacity,"Unknown C3A1 extension.");
    next.floor=word(p+12);next.group=word(p+16);next.actor_count=word(p+20);
    next.transition_count=word(p+24);next.interaction_count=word(p+28);next.link_count=word(p+56);
    next.load_script=word(p+84);next.init_script=word(p+88);next.pre_script=word(p+92);
    next.main_script=word(p+96);next.post_script=word(p+100);
    if(!next.floor||next.floor>65535||!next.group||next.group>65535||next.actor_count>C3_AREA_MAX_ACTORS||
       next.transition_count>C3_AREA_MAX_SURFACES||next.interaction_count>C3_AREA_MAX_SURFACES||next.link_count>C3_AREA_MAX_LINKS)
        return fail(error,capacity,"Area metadata exceeds its bounds.");
    size_t transition=HEADER+(size_t)next.actor_count*ACTOR;
    size_t interaction=transition+(size_t)next.transition_count*TRIANGLE;
    size_t links=interaction+(size_t)next.interaction_count*TRIANGLE;
    if(word(p+32)!=HEADER||word(p+36)!=transition||word(p+40)!=interaction||word(p+60)!=links||
       links+(size_t)next.link_count*LINK!=bytes)return fail(error,capacity,"Area sections are not contiguous and exact.");
    if(next.actor_count)next.actors=calloc(next.actor_count,sizeof(*next.actors));
    if(next.transition_count)next.transitions=calloc(next.transition_count,sizeof(*next.transitions));
    if(next.interaction_count)next.interactions=calloc(next.interaction_count,sizeof(*next.interactions));
    if(next.link_count)next.links=calloc(next.link_count,sizeof(*next.links));
    if((next.actor_count&&!next.actors)||(next.transition_count&&!next.transitions)||
       (next.interaction_count&&!next.interactions)||(next.link_count&&!next.links)){
        c3_area_close(&next);return fail(error,capacity,"Area metadata allocation failed.");
    }
    for(uint32_t i=0;i<next.actor_count;++i){
        const uint8_t *a=p+HEADER+i*ACTOR;C3AreaActor *b=next.actors+i;
        b->index=word(a);b->resource=word(a+4);b->people_info=word(a+8);b->flags=word(a+12);
        b->move_script=word(a+16);b->talk_script=word(a+20);b->aux2=word(a+24);b->aux3=word(a+28);
        for(unsigned j=0;j<3;++j)b->position[j]=real(a+32+j*4);
        b->yaw=real(a+44);b->radius=real(a+48);b->walk_distance=real(a+52);b->run_distance=real(a+56);
        memcpy(b->motions,a+60,9);b->collision_type=a[69];memcpy(b->file,a+72,64);
        if(b->index>=256||b->people_info>=150||!b->resource||!filename(b->file)||
           !bounded(b->yaw,100)||!bounded(b->radius,100)||b->radius<=0||
           !bounded(b->walk_distance,100)||b->walk_distance<0||!bounded(b->run_distance,100)||b->run_distance<0)goto invalid;
        for(unsigned j=0;j<3;++j)if(!bounded(b->position[j],100000))goto invalid;
        if(b->collision_type>3||a[70]||a[71])goto invalid;
        b->name_index=word(a+136);b->hear_flag=word(a+140);b->name_message=word(a+144);
        b->talk_start=word(a+148);b->talk_end=word(a+152);b->part1_bone_plus_one=word(a+156);
        if(b->name_index>65535||b->hear_flag>65535||b->name_message>0xfffff||b->talk_start>3||b->talk_end>3||b->part1_bone_plus_one>64)goto invalid;
        for(uint32_t j=0;j<i;++j)if(next.actors[j].index==b->index)goto invalid;
    }
    if(!read_triangles(next.transitions,p+transition,next.transition_count)||
       !read_triangles(next.interactions,p+interaction,next.interaction_count))goto invalid;
    for(uint32_t i=0;i<next.link_count;++i){
        const uint8_t *a=p+links+i*LINK;C3AreaLink *b=next.links+i;
        b->kind=word(a);b->source_id=word(a+4);b->script=word(a+8);
        for(unsigned j=0;j<4;++j)b->args[j]=word(a+12+j*4);
        b->source_index=word(a+28);
        if(b->kind<1||b->kind>4||!b->script)goto invalid;
        for(uint32_t j=0;j<i;++j)if(next.links[j].kind==b->kind&&next.links[j].source_id==b->source_id)goto invalid;
    }
    c3_area_close(area);*area=next;if(error&&capacity)error[0]=0;return 1;
invalid:
    c3_area_close(&next);return fail(error,capacity,"Invalid original actor or event metadata.");
}

int c3_area_load(C3Area *area,const char *path,char *error,size_t capacity)
{
    if(!area||!path)return fail(error,capacity,"Invalid area metadata path.");
    FILE *f=fopen(path,"rb");long bytes;
    if(!f)return fail(error,capacity,"Prepared area interaction data was not found.");
    if(fseek(f,0,SEEK_END)||(bytes=ftell(f))<0||bytes>(long)MAX_BYTES||fseek(f,0,SEEK_SET)){
        fclose(f);return fail(error,capacity,"Invalid area metadata size.");
    }
    uint8_t *data=malloc(bytes?(size_t)bytes:1);
    if(!data){fclose(f);return fail(error,capacity,"Area metadata read allocation failed.");}
    int ok=fread(data,1,(size_t)bytes,f)==(size_t)bytes&&!ferror(f);
    if(fclose(f))ok=0;
    if(ok)ok=c3_area_parse(area,data,(size_t)bytes,error,capacity);
    else fail(error,capacity,"Area metadata read failed.");
    free(data);return ok;
}

const C3AreaLink *c3_area_link(const C3Area *area,uint32_t kind,uint32_t source_id)
{
    if(area)for(uint32_t i=0;i<area->link_count;++i){const C3AreaLink *l=area->links+i;
        if(l->kind==kind&&l->source_id==source_id)return l;}
    return NULL;
}

static double dot(const double a[3],const double b[3]){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
/* Intersect a segment interval with a linear half-space. Keeping the whole
 * interval matters: an oblique step can enter a doorway and finish beyond it. */
static int clip_above(double start,double end,double minimum,double *lo,double *hi)
{
    double delta=end-start;
    if(delta==0)return start>=minimum;
    double crossing=(minimum-start)/delta;
    if(delta>0){if(crossing>*lo)*lo=crossing;}
    else if(crossing<*hi)*hi=crossing;
    return *lo<=*hi;
}
const C3FieldTriangle *c3_area_transition(const C3Area *area,const float before[3],const float after[3],float radius)
{return c3_area_transition_active(area,NULL,before,after,radius);}

const C3FieldTriangle *c3_area_transition_active(const C3Area *area,const C3FieldMap *map,const float before[3],const float after[3],float radius)
{
    if(!area||!before||!after||!isfinite(radius)||radius<=0)return NULL;
    for(unsigned j=0;j<3;++j)if(!isfinite(before[j])||!isfinite(after[j]))return NULL;
    for(uint32_t i=0;i<area->transition_count;++i){
        const C3FieldTriangle *t=area->transitions+i;double a[3],b[3],u[3],v[3],n[3];
        if(map&&!c3_field_object_enabled(map,t->object))continue;
        for(unsigned j=0;j<3;++j){
            a[j]=(double)before[j]-t->vertices[j];b[j]=(double)after[j]-t->vertices[j];
            u[j]=(double)t->vertices[3+j]-t->vertices[j];v[j]=(double)t->vertices[6+j]-t->vertices[j];n[j]=t->normal[j];
        }
        double normal_length=sqrt(dot(n,n));
        if(!isfinite(normal_length)||normal_length<=0)continue;
        double first=dot(a,n)/normal_length,last=dot(b,n)/normal_length;
        if(last>=first-1e-7||first<0)continue;
        double approach=first-last,lo=fmax(0,(first-radius)/approach),hi=fmin(1,(first+radius)/approach);
        if(lo>hi)continue;
        double uu=dot(u,u),uv=dot(u,v),vv=dot(v,v),d=uu*vv-uv*uv;
        if(d<=1e-14)continue;
        double a0=(dot(a,u)*vv-dot(a,v)*uv)/d,b0=(dot(a,v)*uu-dot(a,u)*uv)/d;
        double a1=(dot(b,u)*vv-dot(b,v)*uv)/d,b1=(dot(b,v)*uu-dot(b,u)*uv)/d;
        if(clip_above(a0,a1,-1e-6,&lo,&hi)&&clip_above(b0,b1,-1e-6,&lo,&hi)&&
           clip_above(1-a0-b0,1-a1-b1,-1e-6,&lo,&hi))return t;
    }
    return NULL;
}

/* GScolsys2Thru.c getCpPointPoly / getCpPointLine / getCpPointPoint: the
 * face, the flagged edges and the flagged corners of one event triangle
 * against a sphere centre on the triangle's front side. */
static int touch_pass(const C3FieldTriangle *t,const double point[3],double radius_sq,unsigned pass)
{
    static const uint16_t masks[3]={1,2,4};
    double v[3][3],n[3],offset[3];
    for(unsigned i=0;i<3;++i)for(unsigned j=0;j<3;++j)v[i][j]=t->vertices[i*3+j];
    for(unsigned j=0;j<3;++j){n[j]=t->normal[j];offset[j]=point[j]-v[0][j];}
    double side=dot(offset,n),nn=dot(n,n);
    if(side<0||nn<=0)return 0;
    if(pass==0){
        double cp[3],a[3],u[3],w[3];
        if(side*side/nn>=radius_sq)return 0;
        for(unsigned j=0;j<3;++j){cp[j]=point[j]-n[j]*side/nn;a[j]=cp[j]-v[0][j];u[j]=v[1][j]-v[0][j];w[j]=v[2][j]-v[0][j];}
        double uu=dot(u,u),uw=dot(u,w),ww=dot(w,w),d=uu*ww-uw*uw;
        if(d<=1e-14)return 0;
        double s=(dot(a,u)*ww-dot(a,w)*uw)/d,r=(dot(a,w)*uu-dot(a,u)*uw)/d;
        return s>=0&&r>=0&&s+r<=1;
    }
    for(unsigned i=0;i<3;++i){
        if(pass==1){
            unsigned next=i+1<3?i+1:0;double edge[3],to[3],cp[3];
            if(!(t->flags&masks[i]))continue;
            for(unsigned j=0;j<3;++j){edge[j]=v[next][j]-v[i][j];to[j]=point[j]-v[i][j];}
            double length=dot(edge,edge);if(length<=0)continue;
            double at=dot(to,edge)/length;if(at<0||at>1)continue;
            for(unsigned j=0;j<3;++j)cp[j]=point[j]-(v[i][j]+edge[j]*at);
            if(dot(cp,cp)<radius_sq)return 1;
        }else{
            unsigned prev=i+2<3?i+2:i-1;double to[3];
            if(!(t->flags&masks[i])||!(t->flags&masks[prev]))continue;
            for(unsigned j=0;j<3;++j)to[j]=point[j]-v[i][j];
            if(dot(to,to)<radius_sq)return 1;
        }
    }
    return 0;
}

unsigned c3_area_touch_events(const C3Area *area,const C3FieldMap *map,const float before[3],
                              const float after[3],float radius,uint16_t ids[4])
{
    unsigned count=0;double direction[3];
    if(!area||!before||!after||!ids||!isfinite(radius)||radius<=0)return 0;
    for(unsigned j=0;j<3;++j){
        if(!isfinite(before[j])||!isfinite(after[j]))return 0;
        direction[j]=(double)after[j]-before[j];
    }
    double length=sqrt(dot(direction,direction));
    if(!(length>0))return 0;
    /* GScolsys2ThruGetEventID: sample the step every `radius` along it. */
    double step=radius/length,position=0;if(step>1)step=1;
    while(position<1&&count<4){
        double sample=position+step,point[3];if(sample>1)sample=1;
        for(unsigned j=0;j<3;++j)point[j]=before[j]+direction[j]*sample;
        for(unsigned pass=0;pass<3&&count<4;++pass)for(uint32_t i=0;i<area->transition_count&&count<4;++i){
            const C3FieldTriangle *t=area->transitions+i;double n[3]={t->normal[0],t->normal[1],t->normal[2]};unsigned k;
            if(map&&!c3_field_object_enabled(map,t->object))continue;
            if(pass&&!(t->flags&7u))continue;
            for(k=0;k<count;++k)if(ids[k]==t->source_id)break;
            if(k<count||dot(n,direction)>=0)continue;
            if(touch_pass(t,point,(double)radius*radius,pass))ids[count++]=t->source_id;
        }
        position+=step;
    }
    return count;
}
