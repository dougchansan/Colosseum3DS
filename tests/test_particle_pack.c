#include "colosseum3ds/particle_pack.h"
#include "save_codec.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { FIXTURE_BYTES=736, BANK_AT=192, BANK_BYTES=260 };
static void le(uint8_t *p,uint32_t v)
{p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}
static void be(uint8_t *p,uint32_t v)
{p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void fixture(uint8_t p[FIXTURE_BYTES])
{
    memset(p,0,FIXTURE_BYTES);memcpy(p,"C3PT",4);le(p+4,1);le(p+8,FIXTURE_BYTES);
    le(p+16,BANK_AT);le(p+20,BANK_BYTES);le(p+24,2);le(p+28,128);
    memset(p+32,0x12,32);memset(p+64,0x34,32);
    uint8_t *b=p+BANK_AT;memcpy(b,"GPT1",4);be(b+4,32);be(b+8,128);be(b+12,128);be(b+16,256);
    b[33]=0x43;be(b+40,1);be(b+44,16);
    b[53]=1;b[55]=100;be(b+56,0x100);b[108]=0xff;
    be(b+128,1);be(b+132,32);be(b+160,2);be(b+164,1);
    be(b+172,8);be(b+176,4);be(b+184,64);be(b+188,96);
    for(unsigned i=0;i<64;++i)b[192+i]=(uint8_t)i;
    for(unsigned i=0;i<2;++i){uint8_t *r=p+128+32*i;
        le(r+4,i);le(r+8,8);le(r+12,4);le(r+16,480+128*i);le(r+20,128);le(r+24,1);
        memset(p+480+128*i,10+(int)i,128);
    }
}
static void rejected(uint8_t *p,size_t bytes)
{
    C3ParticlePack pack,before;char error[160];uint8_t digest[20];
    memset(&pack,0x5a,sizeof pack);memcpy(&before,&pack,sizeof pack);
    c3_save_codec_sha1(digest,p,(uint32_t)bytes);
    assert(!c3_particle_pack_decode(&pack,p,bytes,digest,error,sizeof error));
    assert(error[0] && !memcmp(&pack,&before,sizeof pack));
}
static void synthetic(void)
{
    uint8_t bytes[FIXTURE_BYTES+32],original[FIXTURE_BYTES],digest[20];char error[160];C3ParticlePack pack;
    fixture(bytes);memcpy(original,bytes,FIXTURE_BYTES);c3_save_codec_sha1(digest,bytes,FIXTURE_BYTES);
    assert(c3_particle_pack_decode(&pack,bytes,FIXTURE_BYTES,digest,error,sizeof error));
    assert(!error[0] && pack.length==FIXTURE_BYTES && pack.root_generator==0 && pack.texture_count==2);
    assert(pack.bytes==bytes && pack.bank.bytes==bytes+BANK_AT && pack.bank.length==BANK_BYTES);
    const C3ParticlePackTexture *texture=c3_particle_pack_texture(&pack,0,1);
    assert(texture && texture->width==8 && texture->height==4 && texture->rgba==bytes+608 && texture->rgba[0]==11);
    assert(!c3_particle_pack_texture(&pack,0,2) && !c3_particle_pack_texture(NULL,0,0));
    assert(!memcmp(original,bytes,FIXTURE_BYTES));
    assert(pack.source_sha256==bytes+32 && pack.payload_sha256==bytes+64);
    digest[0]^=1;assert(!c3_particle_pack_decode(&pack,bytes,FIXTURE_BYTES,digest,error,sizeof error));
    assert(strstr(error,"SHA-1"));
    const struct {unsigned offset;uint32_t value;} changes[]={
        {4,2},{8,FIXTURE_BYTES-1},{12,1},{16,BANK_AT+1},{20,1048577},{24,0},{24,33},{28,160},
        {96,1},{128+8,7},{128+12,3},{128+16,481},{128+16,0xffffffe0u},{128+20,127},
        {128+24,2},{128+28,1},{160+4,0},{160+16,480},
        {BANK_AT+12,127},{BANK_AT+20,1},{BANK_AT+160,3},{BANK_AT+164,0},
        {BANK_AT+172,16},{BANK_AT+184,0xffffff00u}
    };
    for(unsigned i=0;i<sizeof changes/sizeof changes[0];++i){
        fixture(bytes);le(bytes+changes[i].offset,changes[i].value);rejected(bytes,FIXTURE_BYTES);
    }
    fixture(bytes);bytes[BANK_AT+BANK_BYTES]=1;rejected(bytes,FIXTURE_BYTES);
    fixture(bytes);memset(bytes+FIXTURE_BYTES,0,32);le(bytes+8,FIXTURE_BYTES+32);rejected(bytes,FIXTURE_BYTES+32);
    fixture(bytes);rejected(bytes,FIXTURE_BYTES-1);
    assert(!c3_particle_pack_decode(&pack,bytes,127,digest,error,sizeof error));
    assert(!c3_particle_pack_decode(&pack,bytes,C3_PARTICLE_PACK_MAX_BYTES+1u,digest,error,sizeof error));
    assert(!c3_particle_pack_decode(&pack,bytes,FIXTURE_BYTES,NULL,error,sizeof error));
    assert(!c3_particle_pack_decode(NULL,bytes,FIXTURE_BYTES,digest,error,sizeof error));
    assert(!c3_particle_pack_decode((C3ParticlePack *)(void *)bytes,bytes,FIXTURE_BYTES,digest,error,sizeof error));
}
static unsigned hex(char c)
{if(c>='0'&&c<='9')return (unsigned)(c-'0');if(c>='a'&&c<='f')return (unsigned)(c-'a'+10);assert(0);return 0;}
int main(int argc,char **argv)
{
    synthetic();
    if(argc==3){
        assert(strlen(argv[2])==40);uint8_t digest[20];for(unsigned i=0;i<20;++i)digest[i]=(uint8_t)(16*hex(argv[2][2*i])+hex(argv[2][2*i+1]));
        FILE *f=fopen(argv[1],"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0 && (unsigned long)size<=C3_PARTICLE_PACK_MAX_BYTES);
        assert(!fseek(f,0,SEEK_SET));uint8_t *bytes=malloc((size_t)size);assert(bytes);assert(fread(bytes,1,(size_t)size,f)==(size_t)size);assert(!fclose(f));
        C3ParticlePack pack;char error[160];if(!c3_particle_pack_decode(&pack,bytes,(size_t)size,digest,error,sizeof error)){fprintf(stderr,"%s\n",error);return 1;}
        printf("Private C3PT accepted: %ld bytes, %u generators, %u textures, root %u\n",size,pack.bank.generator_count,pack.texture_count,pack.root_generator);
        free(bytes);
    }else assert(argc==1);
    puts("Particle pack tests passed.");return 0;
}
