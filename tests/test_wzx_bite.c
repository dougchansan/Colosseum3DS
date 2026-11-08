#include "colosseum3ds/wzx_bite.h"
#include "save_codec.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){fprintf(stderr,"check failed at %d: %s\n",__LINE__,#x);exit(1);} } while(0)
static C3WzxBiteProgram synthetic(void)
{
    C3WzxBiteProgram p={0};p.owner_clip=5;p.floor_start=72;p.floor_duration=60;p.model_start=142;
    p.control_start=160;p.control_delay=5;p.model_frames=34;
    p.floor_from=0x80808080;p.floor_to=0x803a3a3a;p.owner_rate=p.model_rate=.5f;
    p.part=59;p.unsupported=1;p.sound_id=0x85;p.sound_start=142;p.flags=0x00201048;
    return p;
}
static void clock_checks(const C3WzxBiteProgram *p)
{
    C3WzxBite a={0},b={0},old;
    CHECK(c3_wzx_bite_begin(&a,p));CHECK(a.active&&!a.floor_enabled&&!a.model_visible);
    CHECK(c3_wzx_bite_step(&a,1000));CHECK(a.floor_rgb[0]==128&&!a.floor_enabled&&a.units==60000);
    CHECK(c3_wzx_bite_step(&a,1366));CHECK(!a.model_visible&&!a.model_starts);
    CHECK(c3_wzx_bite_step(&a,1));CHECK(a.model_visible&&a.model_starts==1&&a.sound_requests==1);
    CHECK(c3_wzx_bite_step(&a,382));CHECK(a.active&&a.units==164940);
    CHECK(c3_wzx_bite_step(&a,1));CHECK(!a.active&&!a.floor_enabled&&!a.model_visible);
    CHECK(a.units==165000&&fabsf(a.owner_seconds-1.375f)<1e-6f);
    old=a;CHECK(c3_wzx_bite_step(&a,UINT32_MAX));CHECK(!memcmp(&a,&old,sizeof a));
    for(unsigned step=1;step<=211;++step){
        CHECK(c3_wzx_bite_begin(&a,p));CHECK(c3_wzx_bite_begin(&b,p));
        unsigned elapsed=0;
        while(elapsed<2750){unsigned ms=step;if(ms>2750-elapsed)ms=2750-elapsed;elapsed+=ms;
            CHECK(c3_wzx_bite_step(&a,ms));CHECK(c3_wzx_bite_begin(&b,p));
            CHECK(c3_wzx_bite_step(&b,elapsed));CHECK(!memcmp(&a,&b,sizeof a));}
        CHECK(a.model_starts==1&&a.sound_requests==1&&!a.active);
    }
    CHECK(c3_wzx_bite_begin(&a,p));CHECK(c3_wzx_bite_step(&a,UINT32_MAX));
    CHECK(!a.active&&a.model_starts==1&&a.sound_requests==1&&a.units==165000);
    CHECK(c3_wzx_bite_begin(&a,p));CHECK(c3_wzx_bite_step(&a,2400));
    c3_wzx_bite_cancel(&a);CHECK(!a.active&&!a.model_visible&&!a.floor_enabled);
    old=a;C3WzxBiteProgram invalid=*p;invalid.model_start=3601;
    CHECK(!c3_wzx_bite_begin(&a,&invalid));CHECK(!memcmp(&a,&old,sizeof a));
    invalid=*p;invalid.anchor[0]=NAN;CHECK(!c3_wzx_bite_begin(&a,&invalid));
    CHECK(!c3_wzx_bite_begin(NULL,p));CHECK(!c3_wzx_bite_step(NULL,0));
}
int main(int argc,char **argv)
{
    C3WzxBiteProgram p=synthetic();clock_checks(&p);
    if(argc==2){char e[192];CHECK(c3_wzx_bite_load(&p,argv[1],e,sizeof e));clock_checks(&p);
        FILE *f=fopen(argv[1],"rb");CHECK(f!=NULL);uint8_t data[C3_WZX_BITE_BYTES];
        CHECK(fread(data,1,sizeof data,f)==sizeof data);CHECK(!fclose(f));
        for(unsigned i=0;i<sizeof data;++i){C3WzxBiteProgram before=p;
            data[i]^=0x80;CHECK(!c3_wzx_bite_decode(&p,data,sizeof data,e,sizeof e));
            CHECK(!memcmp(&p,&before,sizeof p));data[i]^=0x80;}
        for(size_t i=0;i<sizeof data;++i)CHECK(!c3_wzx_bite_decode(&p,data,i,e,sizeof e));
        CHECK(c3_wzx_bite_decode(&p,data,sizeof data,e,sizeof e));
    }
    printf("WZX Bite: %u checks passed (attack slice only; no audio/GPT1/retail parity claim).\n",checks);
    return 0;
}
