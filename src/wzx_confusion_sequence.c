#include "colosseum3ds/wzx_confusion_sequence.h"
#include "save_codec.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define HEADER 448u
#define ROW 128u
#define MAX_BYTES 65536u
static uint32_t word(const uint8_t *b){return (uint32_t)b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;}
static float real(const uint8_t *b){uint32_t v=word(b);float f;memcpy(&f,&v,4);return f;}
static int fail(char *e,size_t n,const char *m){if(e&&n)snprintf(e,n,"%s",m);return 0;}
void c3_wzx_confusion_sequence_close(C3WzxConfusionSequenceProgram *p){if(p){free(p->bytes);memset(p,0,sizeof *p);}}
int c3_wzx_confusion_sequence_decode(C3WzxConfusionSequenceProgram *out,const void *data,size_t length,char *e,size_t n)
{
 const uint8_t *b=data;
 if(!out||!b||length<HEADER+15*ROW||length>MAX_BYTES||memcmp(b,"C3CS",4)||word(b+4)!=1||word(b+8)!=length||word(b+12)!=93||word(b+16)!=15||word(b+20)!=120||word(b+24)!=ROW||word(b+28)!=HEADER)return fail(e,n,"Unsupported Confusion sequence contract.");
 static const uint8_t pin[20]={0x51,0xbf,0xe4,0x31,0x4a,0x21,0x8d,0xfa,0xf9,0x59,0x2f,0xc8,0x07,0x23,0xc5,0x04,0x98,0xd0,0xee,0x55};
 if(memcmp(b+32,pin,20))return fail(e,n,"Confusion sequence source pin mismatch.");
 C3WzxConfusionSequenceProgram p={0};p.bytes=malloc(length);if(!p.bytes)return fail(e,n,"Confusion sequence allocation failed.");
 memcpy(p.bytes,b,length);memset(p.bytes+72,0,20);uint8_t hash[20];c3_save_codec_sha1(hash,p.bytes,(uint32_t)length);
 if(memcmp(hash,b+72,20)){c3_wzx_confusion_sequence_close(&p);return fail(e,n,"Confusion sequence checksum mismatch.");}
 if(real(b+92)!=.5f||word(b+352)!=0||word(b+356)!=145||word(b+360)!=44||word(b+364)!=402){c3_wzx_confusion_sequence_close(&p);return fail(e,n,"Unqualified Confusion owner clock.");}
 p.owner_rate=.5f;p.owner_start[0]=0;p.owner_end[0]=145;p.owner_start[1]=44;p.owner_end[1]=402;
 static const uint8_t node_pin[20]={0x0e,0x3f,0xa1,0xe1,0x09,0x23,0xf9,0x25,0x9d,0x0e,0x23,0xa5,0x80,0xef,0x86,0x76,0xb7,0x47,0x81,0x07};
 memcpy(p.particles_sha1,b+368,40);c3_save_codec_sha1(hash,b+HEADER,15*ROW);if(memcmp(hash,b+408,20)||memcmp(hash,node_pin,20)){c3_wzx_confusion_sequence_close(&p);return fail(e,n,"Confusion node contract checksum mismatch.");}
 for(unsigned k=428;k<HEADER;++k)if(b[k]){c3_wzx_confusion_sequence_close(&p);return fail(e,n,"Confusion header extension.");}
 memcpy(p.bytes+72,b+72,20);p.length=length;memcpy(p.type12_sha1,b+52,20);
 uint32_t offset=HEADER+15*ROW;
 static const uint32_t species[4]={196,196,288,288},part[4]={62,62,53,53},entry[4]={0,1,0,8},clip[4]={1,3,0,2},frames[4]={181,251,99,359};
 for(unsigned i=0;i<4;++i){const uint8_t *r=b+96+i*64;C3ConfusionSequenceTrack *t=&p.tracks[i];
  t->species=word(r);t->part=word(r+4);t->clip=word(r+8);t->frames=word(r+12);t->offset=word(r+16);t->bytes=word(r+20);t->entry=word(r+24);memcpy(t->pkx_sha1,r+32,20);
  if(t->species!=species[i]||t->part!=part[i]||t->entry!=entry[i]||t->clip!=clip[i]||t->frames!=frames[i]||t->offset!=offset||t->bytes!=12*t->frames||t->bytes>length-offset||word(r+28))goto invalid;
  for(unsigned k=52;k<64;++k)if(r[k])goto invalid;
  for(uint32_t k=0;k<t->frames*3;++k){float f=real(b+offset+k*4);if(!isfinite(f)||fabsf(f)>=1000)goto invalid;}
  offset+=t->bytes;
 }
 if(offset!=length)goto invalid;
 static const uint32_t kind[15]={1,3,4,4,5,3,3,3,5,3,4,4,5,3,5};
 static const uint32_t start[15]={130,44,44,44,44,44,44,12,12,0,0,0,0,44,44};
 static const uint32_t resource[15]={0,1,0,12,0x443,7,4,3,0xE9,2,0,12,0x472,8,0x473};
 for(unsigned i=0;i<15;++i){const uint8_t *r=b+HEADER+i*ROW;C3ConfusionSequenceNode *q=&p.nodes[i];uint32_t v[32];for(unsigned k=0;k<32;++k)v[k]=word(r+k*4);
  q->phase=v[0];q->key=v[1];q->kind=v[2];q->start=v[3];q->attachment=v[4];q->position_type=v[5];q->link=v[6];q->source=v[7];q->target=v[8];q->timing_index=v[9];q->flags=v[10];q->part=v[11];q->resource_key=v[12];memcpy(q->resource,v+13,sizeof q->resource);
  if(q->phase!=(i>=5)||q->key!=(i<5?i+1:i-4)||q->kind!=kind[i]||q->start!=start[i]||q->resource[0]!=resource[i])goto invalid;
  if(q->kind==3&&(q->attachment!=2||q->position_type!=1||q->resource[1]!=(i==1?1u:0x10A1u)))goto invalid;
 }
 c3_wzx_confusion_sequence_close(out);*out=p;if(e&&n)e[0]=0;return 1;
invalid:c3_wzx_confusion_sequence_close(&p);return fail(e,n,"Unqualified Confusion sequence entry or source-part track.");
}
int c3_wzx_confusion_sequence_load(C3WzxConfusionSequenceProgram *p,const char *path,char *e,size_t n)
{
 if(!p||!path)return fail(e,n,"Missing Confusion sequence path.");
 FILE *f=fopen(path,"rb");if(!f)return fail(e,n,"Cannot open Confusion sequence.");
 uint8_t *b=malloc(MAX_BYTES);if(!b){fclose(f);return fail(e,n,"Confusion sequence file allocation failed.");}
 size_t length=fread(b,1,MAX_BYTES,f);int extra=fgetc(f),io=ferror(f);if(fclose(f))io=1;
 int ok=!io&&extra==EOF&&c3_wzx_confusion_sequence_decode(p,b,length,e,n);free(b);return ok;
}
int c3_wzx_confusion_sequence_position(const C3WzxConfusionSequenceProgram *p,unsigned species,unsigned entry,float seconds,const float root[3],float yaw,float scale,float out[3],char *e,size_t n)
{
 if(!p||!p->bytes||!root||!out||!isfinite(seconds)||seconds<0||!isfinite(yaw)||!isfinite(scale)||scale<=0)return fail(e,n,"Invalid Confusion source position arguments.");
 const C3ConfusionSequenceTrack *t=NULL;for(unsigned i=0;i<4;++i)if(p->tracks[i].species==species&&p->tracks[i].entry==entry)t=&p->tracks[i];
 if(!t)return fail(e,n,"Unsupported Confusion source motion.");
 double frame=(double)seconds*120,end=t->frames-1;if(!entry){frame=fmod(frame,end);}else if(frame>end)frame=end;
 uint32_t lo=(uint32_t)frame,hi=lo+1<t->frames?lo+1:lo;float a=(float)(frame-lo),v[3],result[3];
 for(unsigned k=0;k<3;++k){float x=real(p->bytes+t->offset+lo*12+k*4),y=real(p->bytes+t->offset+hi*12+k*4);v[k]=x+(y-x)*a;if(!isfinite(root[k]))return fail(e,n,"Invalid Confusion root position.");}
 float co=cosf(yaw),si=sinf(yaw);result[0]=root[0]+scale*(co*v[0]+si*v[2]);result[1]=root[1]+scale*v[1];result[2]=root[2]+scale*(-si*v[0]+co*v[2]);
 for(unsigned k=0;k<3;++k)if(!isfinite(result[k]))return fail(e,n,"Confusion position overflow.");
 memcpy(out,result,sizeof result);return 1;
}
unsigned c3_wzx_confusion_sequence_crossed(const C3WzxConfusionSequenceProgram *p,unsigned phase,uint64_t previous,uint64_t current,int include_start,uint8_t out[15])
{
 if(!p||!p->bytes||!out||phase>1||current<previous||current>3600000)return 0;
 unsigned count=0;for(unsigned i=0;i<15;++i){const C3ConfusionSequenceNode *q=&p->nodes[i];uint64_t at=(uint64_t)q->start*1000;
  if(q->phase==phase&&at<=current&&(at>previous||(include_start&&at==previous)))out[count++]=(uint8_t)i;
 }return count;
}
