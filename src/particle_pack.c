#include "colosseum3ds/particle_pack.h"
#include "save_codec.h"
#include <stdio.h>
#include <string.h>

static uint32_t le32(const uint8_t *p)
{return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;}
static uint32_t be32(const uint8_t *p)
{return (uint32_t)p[0]<<24 | (uint32_t)p[1]<<16 | (uint32_t)p[2]<<8 | p[3];}
static int fail(char *error, size_t capacity, const char *text)
{if(error && capacity)snprintf(error,capacity,"%s",text);return 0;}
static int span(size_t length, size_t offset, size_t bytes)
{return offset<=length && bytes<=length-offset;}
static int overlap(const void *a, size_t n, const void *b, size_t m)
{uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>=y?x-y<m:y-x<n;}
static int zero(const uint8_t *p, size_t bytes)
{for(size_t i=0;i<bytes;++i)if(p[i])return 0;return 1;}
static size_t aligned(size_t bytes)
{return (bytes+31u)&~(size_t)31u;}

const C3ParticlePackTexture *c3_particle_pack_texture(const C3ParticlePack *pack,
    uint32_t group, uint32_t pose)
{
    if(!pack || pack->texture_count>C3_PARTICLE_PACK_MAX_TEXTURES)return NULL;
    for(uint32_t i=0;i<pack->texture_count;++i)
        if(pack->textures[i].group==group && pack->textures[i].pose==pose)return &pack->textures[i];
    return NULL;
}

int c3_particle_pack_decode(C3ParticlePack *out, const void *data, size_t bytes,
    const uint8_t expected_sha1[20], char *error, size_t capacity)
{
    const uint8_t *p=data;uint8_t digest[20];C3ParticlePack next={0};
    if(!out || !p || !expected_sha1 || bytes<128 || bytes>C3_PARTICLE_PACK_MAX_BYTES ||
       overlap(out,sizeof *out,p,bytes))return fail(error,capacity,"Invalid C3PT buffer/size/ownership.");
    if(memcmp(p,"C3PT",4) || le32(p+4)!=1 || le32(p+8)!=bytes ||
       !zero(p+96,32))return fail(error,capacity,"Unsupported C3PT header/version/reserved fields.");
    c3_save_codec_sha1(digest,p,(uint32_t)bytes);
    if(memcmp(digest,expected_sha1,20))return fail(error,capacity,"C3PT whole-pack SHA-1 mismatch.");
    uint32_t root=le32(p+12),bank_at=le32(p+16),bank_bytes=le32(p+20);
    uint32_t count=le32(p+24),table=le32(p+28);
    if(!count || count>C3_PARTICLE_PACK_MAX_TEXTURES || table!=128 ||
       !span(bytes,table,(size_t)count*32) || !bank_bytes || bank_bytes>1024u*1024u ||
       bank_at!=aligned(128u+(size_t)count*32u) || (bank_at&31u) ||
       !span(bytes,bank_at,bank_bytes))return fail(error,capacity,"C3PT bank/table count or offset bound.");
    if(!zero(p+128u+count*32u,bank_at-(128u+count*32u)))
        return fail(error,capacity,"Nonzero C3PT table alignment padding.");
    if(!c3_particle_bank_decode(&next.bank,p+bank_at,bank_bytes,error,capacity))return 0;
    size_t source_txg=be32(next.bank.bytes+8),source_refs=be32(next.bank.bytes+16);
    uint32_t source_tex_bytes=be32(next.bank.bytes+12);
    if(be32(next.bank.bytes+4)<32 || source_txg>source_refs || source_refs>bank_bytes ||
       source_tex_bytes!=source_refs-source_txg ||
       source_refs+4u*next.bank.generator_count!=bank_bytes || !zero(next.bank.bytes+20,12))
        return fail(error,capacity,"C3PT source GPT1 section closure mismatch.");
    if(root>=next.bank.generator_count)return fail(error,capacity,"C3PT root generator missing.");
    size_t cursor=(size_t)bank_at+bank_bytes,next_at=aligned(cursor);
    if(!span(bytes,cursor,next_at-cursor) || !zero(p+cursor,next_at-cursor))
        return fail(error,capacity,"Invalid C3PT bank alignment padding.");
    cursor=next_at;
    uint32_t group_frames[C3_PARTICLE_MAX_TEXTURE_GROUPS]={0};
    for(uint32_t i=0;i<count;++i){
        const uint8_t *r=p+128u+i*32u;C3ParticlePackTexture *t=&next.textures[i];
        t->group=le32(r);t->pose=le32(r+4);t->width=le32(r+8);t->height=le32(r+12);
        uint32_t offset=le32(r+16);t->rgba_bytes=le32(r+20);t->source_format=le32(r+24);
        if(le32(r+28) || t->group>=next.bank.texture_group_count ||
           t->pose>=next.bank.texture_frames[t->group] || t->source_format>1 ||
           t->width<8 || t->width>256 || t->width%8 || t->height>256 ||
           (t->source_format==0?(t->height<8 || t->height%8):(t->height<4 || t->height%4)) ||
           t->rgba_bytes!=t->width*t->height*4u || offset!=cursor || (offset&31u) ||
           !span(bytes,offset,t->rgba_bytes))return fail(error,capacity,"C3PT texture dimensions/format/extent bound.");
        for(uint32_t j=0;j<i;++j)if(next.textures[j].group==t->group && next.textures[j].pose==t->pose)
            return fail(error,capacity,"Duplicate C3PT texture group/pose.");
        /* Check prepared texture metadata against the borrowed source TXG.
         * Group offsets are relative IDs, never PPC/native pointer casts. */
        size_t txg=be32(next.bank.bytes+8),group_ptr=txg+4u+4u*t->group;
        if(!span(bank_bytes,group_ptr,4))return fail(error,capacity,"C3PT source texture table extent.");
        size_t group_at=txg+(size_t)be32(next.bank.bytes+group_ptr);
        if(!span(bank_bytes,group_at,24) || be32(next.bank.bytes+group_at+4)!=t->source_format ||
           be32(next.bank.bytes+group_at+8) || be32(next.bank.bytes+group_at+20) ||
           be32(next.bank.bytes+group_at+12)!=t->width || be32(next.bank.bytes+group_at+16)!=t->height)
            return fail(error,capacity,"C3PT texture metadata differs from source TXG.");
        size_t frame_ptr=group_at+24u+4u*t->pose;
        if(!span(bank_bytes,frame_ptr,4))return fail(error,capacity,"C3PT source texture frame table extent.");
        size_t source_pixels=txg+(size_t)be32(next.bank.bytes+frame_ptr);
        size_t source_size=t->width*t->height/(t->source_format==0?2u:1u);
        if(source_pixels<txg || !span(source_refs,source_pixels,source_size))
            return fail(error,capacity,"C3PT source texture pixel extent.");
        ++group_frames[t->group];t->rgba=p+offset;
        next_at=aligned(cursor+t->rgba_bytes);
        if(!span(bytes,cursor+t->rgba_bytes,next_at-(cursor+t->rgba_bytes)) ||
           !zero(p+cursor+t->rgba_bytes,next_at-(cursor+t->rgba_bytes)))
            return fail(error,capacity,"Invalid C3PT texture alignment padding.");
        cursor=next_at;
    }
    if(cursor!=bytes)return fail(error,capacity,"Unknown C3PT trailing bytes.");
    for(uint32_t i=0;i<next.bank.texture_group_count;++i)
        if(group_frames[i] && group_frames[i]!=next.bank.texture_frames[i])
            return fail(error,capacity,"Incomplete C3PT source texture frame group.");
    if(!group_frames[next.bank.generators[root].tex_group])
        return fail(error,capacity,"C3PT root generator texture group missing.");
    next.bytes=p;next.length=bytes;next.root_generator=root;next.texture_count=count;
    next.source_sha256=p+32;next.payload_sha256=p+64;*out=next;
    if(error && capacity)error[0]=0;
    return 1;
}
