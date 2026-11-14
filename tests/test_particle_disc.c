/* Private, optional: every C3PT v2 pack written by
 *   python3 tools/prepare_particle.py --batch <list|dir> --out <dir>
 * decodes with the runtime's own general closure (which must equal the
 * converter's), begins from each admitted root, simulates N ticks and builds a
 * quad for every live particle without error. No game bytes are embedded.
 * Usage: test_particle_disc <batch dir> [ticks=600] */
#include "colosseum3ds/particle_pack.h"
#include "save_codec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static C3ParticlePack pack;
static C3ParticleSystem sys;
static uint8_t *read_file(const char *path,size_t *bytes)
{
    FILE *f=fopen(path,"rb");if(!f)return NULL;
    fseek(f,0,SEEK_END);long n=ftell(f);fseek(f,0,SEEK_SET);
    uint8_t *p=n>0?malloc((size_t)n):NULL;
    if(p&&fread(p,1,(size_t)n,f)!=(size_t)n){free(p);p=NULL;}
    fclose(f);*bytes=(size_t)n;return p;
}
static int cmp_u32(const void *a,const void *b)
{uint32_t x=*(const uint32_t*)a,y=*(const uint32_t*)b;return x<y?-1:x>y;}
int main(int argc,char **argv)
{
    if(argc<2){fprintf(stderr,"usage: %s <batch dir> [ticks]\n",argv[0]);return 2;}
    unsigned ticks=argc>2?(unsigned)atoi(argv[2]):600u;
    char path[4096];snprintf(path,sizeof path,"%s/particle_batch_closures.txt",argv[1]);
    FILE *list=fopen(path,"r");
    if(!list){printf("particle disc check skipped: %s missing\n",path);return 0;}
    static const C3ParticleCamera camera={{1,0,0},{0,1,0},{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}};
    static const float attachment[12]={.1f,0,0,0, 0,.1f,0,0, 0,0,.1f,0};
    char name[256],error[160];unsigned packs=0,launches=0,failures=0,finished=0,saturated=0;uint32_t peak=0,dropped=0;
    while(fscanf(list,"%255s",name)==1){
        unsigned root_count,closure_count;uint32_t roots[C3_PARTICLE_PACK_MAX_ROOTS],closure[C3_PARTICLE_MAX_GENERATORS];
        if(fscanf(list,"%u",&root_count)!=1||root_count>C3_PARTICLE_PACK_MAX_ROOTS)return fprintf(stderr,"bad list\n"),1;
        for(unsigned i=0;i<root_count;++i)if(fscanf(list,"%u",&roots[i])!=1)return 1;
        if(fscanf(list,"%u",&closure_count)!=1||closure_count>C3_PARTICLE_MAX_GENERATORS)return fprintf(stderr,"bad list\n"),1;
        for(unsigned i=0;i<closure_count;++i)if(fscanf(list,"%u",&closure[i])!=1)return 1;
        snprintf(path,sizeof path,"%s/%s",argv[1],name);
        size_t bytes;uint8_t *data=read_file(path,&bytes),digest[20];
        if(!data){printf("FAIL %s: unreadable\n",name);++failures;continue;}
        c3_save_codec_sha1(digest,data,(uint32_t)bytes);++packs;
        if(!c3_particle_pack_decode_general(&pack,data,bytes,digest,error,sizeof error)){printf("FAIL %s: decode: %s\n",name,error);++failures;free(data);continue;}
        uint32_t ids[C3_PARTICLE_MAX_GENERATORS];
        for(unsigned i=0;i<pack.bank.generator_count;++i)ids[i]=pack.bank.source_ids[i];
        qsort(ids,pack.bank.generator_count,sizeof *ids,cmp_u32);
        if(pack.bank.generator_count!=closure_count||memcmp(ids,closure,closure_count*sizeof *ids)){
            printf("FAIL %s: runtime closure (%u) differs from converter (%u)\n",name,pack.bank.generator_count,closure_count);++failures;
        }
        for(unsigned r=0;r<root_count;++r){
            const float origin[3]={0,0,0};++launches;
            if(!c3_particle_begin_position_flags(&sys,&pack.bank,roots[r],0x1234u+r,origin,0,1,1,0x10a9,error,sizeof error)){
                printf("FAIL %s root %u: begin: %s\n",name,roots[r],error);++failures;continue;}
            int ok=1;
            for(unsigned t=0;t<ticks&&ok;t+=60){
                if(!c3_particle_step_ticks(&sys,ticks-t<60?ticks-t:60,0,error,sizeof error)){printf("FAIL %s root %u tick %u: %s\n",name,roots[r],t,error);ok=0;break;}
                for(unsigned i=0;i<C3_PARTICLE_MAX_PARTICLES;++i){
                    C3ParticleQuad q;if(!sys.particles[i].active)continue;
                    int drawn=c3_particle_quad(&sys.particles[i],attachment,&camera,&q,error,sizeof error);
                    if(drawn<0){printf("FAIL %s root %u quad: %s\n",name,roots[r],error);ok=0;break;}
                    if(drawn&&!c3_particle_pack_texture(&pack,q.texture_group,q.texture_frame)){printf("FAIL %s root %u: texture %u/%u absent\n",name,roots[r],q.texture_group,q.texture_frame);ok=0;break;}
                }
            }
            if(!ok){++failures;continue;}
            if(sys.peak_particles>peak)peak=sys.peak_particles;
            if(sys.peak_particles>=C3_PARTICLE_MAX_PARTICLES){++saturated;
                if(getenv("C3_PARTICLE_DISC_VERBOSE"))printf("pool %s root %u: %u spawns dropped\n",name,roots[r],sys.dropped);}
            dropped+=sys.dropped;finished+=c3_particle_finished(&sys)!=0;
        }
        free(data);
    }
    fclose(list);
    printf("particle disc check: %u packs, %u launches, %u failures; %u finished within %u ticks; peak %u live (%u launches reach the pool); %u spawns dropped\n",
           packs,launches,failures,finished,ticks,peak,saturated,dropped);
    return failures!=0;
}
