#include "colosseum3ds/title.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HEADER_BYTES 128u
#define BATCH_BYTES 112u
#define TEXTURE_BYTES 24u
#define CLIP_BYTES 16u

static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 |
           (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static float f32(const uint8_t *p)
{
    uint32_t bits = u32(p);
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static int fail(char *error, size_t capacity, const char *message)
{
    if (error && capacity) {
        (void)snprintf(error, capacity, "%s", message);
    }
    return 0;
}

static int bounded(float value, float magnitude)
{
    return isfinite(value) && fabsf(value) <= magnitude;
}

static int unit(float value)
{
    return isfinite(value) && value >= 0.0f && value <= 1.0f;
}

/* The source title uses rigid joints, but inverse-transpose is still required
 * for authored nonuniform rest scaling and interpolated affine skin matrices. */
static int normal_matrix(const float *m, float *n)
{
    /* Absolute determinants reject small, well-conditioned authored scales.
     * Normalize the basis for the singularity test, then restore inverse units. */
    double scale=0;
    static const unsigned basis[9]={0,1,2,4,5,6,8,9,10};
    for(unsigned k=0;k<9;++k){
        double value=fabs((double)m[basis[k]]);
        if(!isfinite(value))return 0;
        if(value>scale)scale=value;
    }
    if(scale==0)return 0;
    double a=m[0]/scale,b=m[1]/scale,c=m[2]/scale;
    double d=m[4]/scale,e=m[5]/scale,f=m[6]/scale;
    double g=m[8]/scale,h=m[9]/scale,i=m[10]/scale;
    double determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    if(!isfinite(determinant)||fabs(determinant)<1e-12)return 0;
    double inverse=1.0/(determinant*scale);
    n[0]=(float)((e*i-f*h)*inverse);
    n[1]=(float)((f*g-d*i)*inverse);
    n[2]=(float)((d*h-e*g)*inverse);
    n[3]=(float)((c*h-b*i)*inverse);
    n[4]=(float)((a*i-c*g)*inverse);
    n[5]=(float)((b*g-a*h)*inverse);
    n[6]=(float)((b*f-c*e)*inverse);
    n[7]=(float)((c*d-a*f)*inverse);
    n[8]=(float)((a*e-b*d)*inverse);
    for(unsigned k=0;k<9;++k)if(!isfinite(n[k]))return 0;
    return 1;
}

static void read_floats(float *out, const uint8_t *p, uint32_t count)
{
    uint32_t i;
    for (i = 0; i < count; ++i) out[i] = f32(p + i*4u);
}

/* A collapsed affine has no inverse normal. It is admissible only while
 * every batch consuming that bone is hidden; positions remain exact so the
 * neighboring visible sample interpolates toward the authored collapse. */
static int bone_hidden(const C3Title *title, const uint8_t *materials, uint32_t bone)
{
    for(uint32_t batch=0;batch<title->batch_count;++batch){
        const C3TitleBatch *b=title->batches+batch;
        if(f32(materials+batch*title->material_frame_bytes+20u)!=0.0f){
            for(uint32_t i=b->first;i<b->first+b->count;++i)
                if(title->vertices[i].bone==bone)return 0;
        }
    }
    return 1;
}

static int validate_frames(const C3Title *title)
{
    uint32_t frame, bone, batch, i;
    for (frame = 0; frame < title->frame_count; ++frame) {
        const uint8_t *p = title->file_data + title->frame_offset + frame*title->frame_bytes;
        for (bone = 0; bone < title->bone_count; ++bone, p += 48u) {
            float matrix[12], normal[9];
            read_floats(matrix, p, 12);
            for (i = 0; i < 12; ++i) if (!bounded(matrix[i], 1000000.0f)) return 0;
            if (!normal_matrix(matrix, normal) &&
                !bone_hidden(title, p+(title->bone_count-bone)*48u, bone)) return 0;
        }
        for (batch = 0; batch < title->batch_count; ++batch, p += title->material_frame_bytes) {
            for (i = 0; i < 4; ++i) if (!bounded(f32(p+i*4u), 1000000.0f)) return 0;
            for (i = 4; i < 12; ++i) if (!unit(f32(p+i*4u))) return 0;
            if (f32(p+20u) != 0.0f && f32(p+20u) != 1.0f) return 0;
            if (title->format_version >= 2u) {
                for (i=12; i<16; ++i) if (!bounded(f32(p+i*4u),1000000.0f)) return 0;
            }
            if (title->format_version >= 3u) {
                for (i=16; i<23; ++i) if (!bounded(f32(p+i*4u),1000000.0f)) return 0;
                if (u32(p+92u)) return 0;
            }
        }
        for (i = 0; i < 11; ++i) if (!bounded(f32(p+i*4u), 1000000.0f)) return 0;
        if (f32(p+28u) <= 1.0f || f32(p+28u) >= 179.0f ||
            f32(p+32u) <= 0.0f || f32(p+36u) <= f32(p+32u) ||
            f32(p+40u) <= 0.0f || f32(p+40u) > 10.0f || u32(p+44u)) return 0;
    }
    return 1;
}

static int decode_owned(C3Title *title, char *error, size_t capacity)
{
    const uint8_t *data = title->file_data;
    uint32_t offsets[7], i, j, next_vertex = 0, next_frame = 0, next_pixel, vertex_bytes, light_bytes;
    uint64_t expected;
    if (title->file_bytes < HEADER_BYTES || title->file_bytes > C3_TITLE_MAX_FILE_BYTES ||
        u32(data+8) != title->file_bytes)
        return fail(error, capacity, "Invalid C3T header or file length");
    title->format_version=u32(data+4);
    if ((title->format_version != 1u || memcmp(data,"C3T1",4)) &&
        (title->format_version != 2u || memcmp(data,"C3T2",4)) &&
        (title->format_version != 3u || memcmp(data,"C3T3",4)) &&
        (title->format_version != 4u || memcmp(data,"C3T4",4)))
        return fail(error, capacity, "Unsupported C3T magic or version");
    vertex_bytes=title->format_version>=2u ? 48u:40u;
    light_bytes=title->format_version==4u ? 80u:64u;
    title->material_frame_bytes=title->format_version>=3u ? 96u:(title->format_version==2u ? 64u:48u);
    title->bone_count = u32(data+12);
    title->vertex_count = u32(data+16);
    title->batch_count = u32(data+20);
    title->texture_count = u32(data+24);
    title->clip_count = u32(data+28);
    title->frame_count = u32(data+32);
    title->light_count = u32(data+36);
    title->fps = f32(data+40);
    if (!title->bone_count || title->bone_count > C3_TITLE_MAX_BONES ||
        !title->vertex_count || title->vertex_count > C3_TITLE_MAX_VERTICES || title->vertex_count%3u ||
        !title->batch_count || title->batch_count > C3_TITLE_MAX_BATCHES ||
        !title->texture_count || title->texture_count > C3_TITLE_MAX_TEXTURES ||
        !title->clip_count || title->clip_count > C3_TITLE_MAX_CLIPS ||
        !title->frame_count || title->frame_count > C3_TITLE_MAX_FRAMES ||
        !title->light_count || title->light_count > C3_TITLE_MAX_LIGHTS ||
        !isfinite(title->fps) || title->fps < 1.0f || title->fps > 120.0f)
        return fail(error, capacity, "Title section count or timing exceeds limits");
    for (i = 112u; i < HEADER_BYTES; ++i) if (data[i]) return fail(error, capacity, "Reserved title header bytes must be zero");
    for (i = 0; i < 7u; ++i) offsets[i] = u32(data+44u+i*4u);
    title->frame_offset = offsets[5];
    title->frame_bytes = u32(data+72u);
    if (title->frame_bytes != (title->bone_count+1u)*48u+title->batch_count*title->material_frame_bytes)
        return fail(error, capacity, "Invalid title frame stride");
    expected = HEADER_BYTES;
    if (offsets[0] != expected) return fail(error, capacity, "Noncanonical title vertex offset");
    expected += (uint64_t)title->vertex_count*vertex_bytes;
    if (offsets[1] != expected) return fail(error, capacity, "Noncanonical title batch offset");
    expected += (uint64_t)title->batch_count*BATCH_BYTES;
    if (offsets[2] != expected) return fail(error, capacity, "Noncanonical title texture offset");
    expected += (uint64_t)title->texture_count*TEXTURE_BYTES;
    if (offsets[3] != expected) return fail(error, capacity, "Noncanonical title clip offset");
    expected += (uint64_t)title->clip_count*CLIP_BYTES;
    if (offsets[4] != expected) return fail(error, capacity, "Noncanonical title light offset");
    expected += (uint64_t)title->light_count*light_bytes;
    if (offsets[5] != expected) return fail(error, capacity, "Noncanonical title frame offset");
    expected += (uint64_t)title->frame_count*title->frame_bytes;
    if (offsets[6] != expected || expected+u32(data+76u) != title->file_bytes)
        return fail(error, capacity, "Title section extents exceed file bounds");
    title->vertices = (C3TitleVertex *)calloc(title->vertex_count, sizeof *title->vertices);
    title->draw_vertices = (C3TitleDrawVertex *)calloc(title->vertex_count, sizeof *title->draw_vertices);
    title->batches = (C3TitleBatch *)calloc(title->batch_count, sizeof *title->batches);
    title->textures = (C3TitleTexture *)calloc(title->texture_count, sizeof *title->textures);
    if (!title->vertices || !title->draw_vertices || !title->batches || !title->textures)
        return fail(error, capacity, "Not enough memory for title geometry");
    title->resident_bytes = sizeof *title + title->file_bytes +
        title->vertex_count*(sizeof *title->vertices + sizeof *title->draw_vertices) +
        title->batch_count*sizeof *title->batches + title->texture_count*sizeof *title->textures;
    for (i = 0; i < title->vertex_count; ++i) {
        const uint8_t *p = data+offsets[0]+i*vertex_bytes;
        C3TitleVertex *v = title->vertices+i;
        read_floats(v->pos,p,3);
        read_floats(v->normal,p+12u,3);
        read_floats(v->uv,p+24u,2);
        memcpy(v->rgba,p+32u,4);
        v->bone = u32(p+36u);
        if (title->format_version>=2u) read_floats(v->uv2,p+40u,2);
        for (j=0; j<8; ++j) if (!bounded(f32(p+j*4u),1000000.0f)) return fail(error,capacity,"Nonfinite or excessive title vertex");
        for (j=0; j<3; ++j) if (!bounded(v->normal[j],1.001f)) return fail(error,capacity,"Invalid title vertex normal");
        if (v->bone >= title->bone_count) return fail(error,capacity,"Title vertex bone is out of range");
        for (j=0; j<2; ++j) if (!bounded(v->uv2[j],1000000.0f)) return fail(error,capacity,"Invalid second texture coordinate");
    }
    for (i = 0; i < title->batch_count; ++i) {
        const uint8_t *p = data+offsets[1]+i*BATCH_BYTES;
        C3TitleBatch *b = title->batches+i;
        b->first=u32(p); b->count=u32(p+4); b->texture=u32(p+8); b->flags=u32(p+12);
        b->color_mode=u32(p+16); b->alpha_mode=u32(p+20);
        b->render_mode=u32(p+24); b->texture_flags=u32(p+28);
        read_floats(b->diffuse,p+32,3); read_floats(b->ambient,p+44,3); read_floats(b->specular,p+56,3);
        b->shininess=f32(p+68); b->alpha=f32(p+72); b->texture_blend=f32(p+76);
        b->alpha_ref0=u32(p+80); b->alpha_ref1=u32(p+84);
        b->alpha_compare0=u32(p+88); b->alpha_compare1=u32(p+92);
        b->alpha_op=u32(p+96); b->depth_compare=u32(p+100);
        b->texture2=u32(p+104);
        b->texture2_blend=title->format_version>=3u ? f32(p+108):0.0f;
        if (b->first != next_vertex || !b->count || b->count%3u || b->count > title->vertex_count-next_vertex ||
            b->texture >= title->texture_count || b->flags & ~(title->format_version>=3u ? 131071u:(title->format_version==2u ? 8191u:1023u)) ||
            ((b->flags&C3_TITLE_SECOND_UV_BLEND)&&!(b->flags&C3_TITLE_REFLECTION_BLEND)) ||
            ((b->flags&C3_TITLE_PAIRED_NORMAL_EXT)&&(!(b->flags&C3_TITLE_REFLECTION_BLEND)||(b->flags&C3_TITLE_SECOND_UV_BLEND))) ||
            ((b->flags&C3_TITLE_SOURCE_ALPHA_MASK)&&((b->flags&(C3_TITLE_REFLECTION_BLEND|C3_TITLE_SECOND_UV_BLEND))!=(C3_TITLE_REFLECTION_BLEND|C3_TITLE_SECOND_UV_BLEND)||(b->flags&C3_TITLE_PAIRED_NORMAL_EXT))) ||
            (b->flags & (C3_TITLE_CULL_BACK|C3_TITLE_CULL_FRONT)) == (C3_TITLE_CULL_BACK|C3_TITLE_CULL_FRONT) ||
            (b->color_mode!=0u && b->color_mode!=3u && b->color_mode!=4u) ||
            (b->alpha_mode!=0u && b->alpha_mode!=2u && b->alpha_mode!=3u) ||
            b->alpha_ref0>255u || b->alpha_ref1>255u || b->alpha_compare0>7u || b->alpha_compare1>7u ||
            b->alpha_op>3u || b->depth_compare>7u ||
            (title->format_version<3u && u32(p+108)) || !unit(b->texture2_blend) ||
            ((b->flags&(C3_TITLE_DUAL_MODULATE|C3_TITLE_REFLECTION_BLEND)) ? (b->texture2>=title->texture_count ||
              b->color_mode!=4u || b->alpha_mode!=0u) : b->texture2!=0u) ||
            ((b->flags&C3_TITLE_DUAL_MODULATE) && (!(b->flags&C3_TITLE_TEV_LERP) ||
              (b->flags&C3_TITLE_REFLECTION_BLEND))) ||
            (!(b->flags&C3_TITLE_REFLECTION_BLEND) && b->texture2_blend!=0.0f))
            return fail(error,capacity,"Unsupported or noncanonical title batch");
        {
            uint32_t blend=b->flags&(C3_TITLE_ADDITIVE|C3_TITLE_SUBTRACT|C3_TITLE_ADDITIVE_ONE);
            if ((blend && (blend&(blend-1u))) || (blend && !(b->flags&C3_TITLE_TRANSLUCENT)))
                return fail(error,capacity,"Conflicting title blend modes");
        }
        for (j=0; j<9; ++j) if (!unit(f32(p+32+j*4u))) return fail(error,capacity,"Invalid title material color");
        if (!bounded(b->shininess,10000.0f) || b->shininess<0 || !unit(b->alpha) || !unit(b->texture_blend))
            return fail(error,capacity,"Invalid title material scalar");
        next_vertex += b->count;
    }
    if (next_vertex != title->vertex_count) return fail(error,capacity,"Title batches do not cover vertices");
    next_pixel=offsets[6];
    for (i=0; i<title->texture_count; ++i) {
        const uint8_t *p=data+offsets[2]+i*TEXTURE_BYTES;
        C3TitleTexture *t=title->textures+i;
        uint32_t pixel_offset=u32(p+8),pixel_bytes=u32(p+12);
        t->width=u32(p); t->height=u32(p+4); t->wrap_s=u32(p+16); t->wrap_t=u32(p+20);
        if (t->width<8u || t->width>1024u || t->height<8u || t->height>1024u ||
            (t->width&(t->width-1u)) || (t->height&(t->height-1u)) ||
            t->wrap_s>2u || t->wrap_t>2u || pixel_bytes!=t->width*t->height*4u ||
            pixel_offset!=next_pixel || (uint64_t)pixel_offset+pixel_bytes>title->file_bytes)
            return fail(error,capacity,"Invalid title texture extent or wrapping");
        t->pixels=data+pixel_offset; t->size=pixel_bytes; next_pixel+=pixel_bytes;
    }
    if (next_pixel!=title->file_bytes) return fail(error,capacity,"Trailing title pixels or missing texture bytes");
    for (i=0; i<title->clip_count; ++i) {
        const uint8_t *p=data+offsets[3]+i*CLIP_BYTES;
        C3TitleClip *clip=title->clips+i;
        clip->first_frame=u32(p); clip->frame_count=u32(p+4); clip->loop=u32(p+8);
        if (clip->first_frame!=next_frame || !clip->frame_count || clip->frame_count>title->frame_count-next_frame ||
            clip->loop>1u || u32(p+12)) return fail(error,capacity,"Invalid title clip interval");
        next_frame+=clip->frame_count;
    }
    if (next_frame!=title->frame_count) return fail(error,capacity,"Title clips do not cover frames");
    for (i=0; i<title->light_count; ++i) {
        const uint8_t *p=data+offsets[4]+i*light_bytes;
        C3TitleLight *light=title->lights+i;
        light->flags=u32(p); light->attenuation_flags=u32(p+4);
        read_floats(light->rgba,p+8,4); read_floats(light->pos,p+24,3); read_floats(light->interest,p+36,3);
        light->reference_brightness=f32(p+48); light->reference_distance=f32(p+52); light->distance_function=f32(p+56);
        light->spot_function=title->format_version==4u ? u32(p+64):0u;
        light->spot_cutoff=title->format_version==4u ? f32(p+68):0.0f;
        for (j=0; j<4; ++j) if (!unit(light->rgba[j])) return fail(error,capacity,"Invalid title light color");
        for (j=0; j<6; ++j) if (!bounded(f32(p+24+j*4u),1000000.0f)) return fail(error,capacity,"Invalid title light position");
        if (light->flags&~63u || ((light->flags&3u)==3u && title->format_version<4u) || light->attenuation_flags ||
            !unit(light->reference_brightness) || !bounded(light->reference_distance,1000000.0f) || light->reference_distance<=0 ||
            !isfinite(light->distance_function) || light->distance_function<0 || light->distance_function>3 ||
            light->distance_function!=floorf(light->distance_function) || u32(p+60))
            return fail(error,capacity,"Unsupported title light attenuation");
        if (title->format_version==4u &&
            (light->spot_function>6u || !bounded(light->spot_cutoff,360.0f) ||
             ((light->flags&3u)!=3u && (light->spot_function || light->spot_cutoff!=0.0f)) ||
             u32(p+72) || u32(p+76)))
            return fail(error,capacity,"Invalid title spotlight parameters");
    }
    if (!validate_frames(title) || !c3_title_animate(title,0u,0.0f))
        return fail(error,capacity,"Invalid title animation sample");
    if (error && capacity) error[0]='\0';
    return 1;
}

void c3_title_close(C3Title *title)
{
    if (title && title->borrowed_storage) {
        free(title->draw_vertices);
        memset(title, 0, sizeof *title);
        return;
    }
    if (!title) return;
    free(title->vertices); free(title->draw_vertices); free(title->batches); free(title->textures);
    free(title->file_data);
    memset(title,0,sizeof *title);
}

int c3_title_instance(C3Title *instance, const C3Title *owner, char *error, size_t capacity)
{
    if (!instance || !owner || instance == owner || owner->borrowed_storage ||
        !owner->file_data || !owner->draw_vertices || !owner->vertex_count)
        return fail(error, capacity, "Invalid scene instance owner");
    C3TitleDrawVertex *sampled = malloc(owner->vertex_count * sizeof *sampled);
    if (!sampled) return fail(error, capacity, "Scene instance allocation failed");
    memcpy(sampled, owner->draw_vertices, owner->vertex_count * sizeof *sampled);
    c3_title_close(instance);
    *instance = *owner;
    instance->draw_vertices = sampled;
    instance->borrowed_storage = 1;
    instance->resident_bytes = sizeof *instance + owner->vertex_count * sizeof *sampled;
    if (error && capacity) error[0] = '\0';
    return 1;
}

int c3_title_parse(C3Title *title, const void *data, size_t bytes, char *error, size_t capacity)
{
    C3Title *temporary;
    if (!title || !data || bytes<HEADER_BYTES || bytes>C3_TITLE_MAX_FILE_BYTES)
        return fail(error,capacity,"Invalid title buffer or size");
    temporary=(C3Title *)calloc(1,sizeof *temporary);
    if (!temporary) return fail(error,capacity,"Not enough memory for title state");
    temporary->file_data=(uint8_t *)malloc(bytes);
    if (!temporary->file_data) { free(temporary); return fail(error,capacity,"Not enough memory for title data"); }
    /* Copy first: data is allowed to refer to the currently loaded title. */
    memcpy(temporary->file_data,data,bytes); temporary->file_bytes=bytes;
    if (!decode_owned(temporary,error,capacity)) { c3_title_close(temporary); free(temporary); return 0; }
    c3_title_close(title); *title=*temporary; free(temporary);
    return 1;
}

int c3_title_load(C3Title *title, const char *path, char *error, size_t capacity)
{
    C3Title *temporary;
    FILE *file;
    long bytes;
    if (!title || !path) return fail(error,capacity,"Invalid title load arguments");
    file=fopen(path,"rb");
    if (!file) return fail(error,capacity,"Cannot open title.c3t");
    if (fseek(file,0,SEEK_END)!=0 || (bytes=ftell(file))<(long)HEADER_BYTES ||
        bytes>(long)C3_TITLE_MAX_FILE_BYTES || fseek(file,0,SEEK_SET)!=0) {
        fclose(file); return fail(error,capacity,"Invalid title file size");
    }
    temporary=(C3Title *)calloc(1,sizeof *temporary);
    if (!temporary) { fclose(file); return fail(error,capacity,"Not enough memory for title state"); }
    temporary->file_data=(uint8_t *)malloc((size_t)bytes); temporary->file_bytes=(size_t)bytes;
    if (!temporary->file_data) { fclose(file); free(temporary); return fail(error,capacity,"Not enough memory for title file"); }
    if (fread(temporary->file_data,1,(size_t)bytes,file)!=(size_t)bytes || fgetc(file)!=EOF || ferror(file)) {
        fclose(file); c3_title_close(temporary); free(temporary); return fail(error,capacity,"Title read failed or file changed");
    }
    if (fclose(file)!=0) { c3_title_close(temporary); free(temporary); return fail(error,capacity,"Title file close failed"); }
    if (!decode_owned(temporary,error,capacity)) { c3_title_close(temporary); free(temporary); return 0; }
    c3_title_close(title); *title=*temporary; free(temporary);
    return 1;
}

static float interpolate(const uint8_t *a, const uint8_t *b, uint32_t index, float fraction)
{
    float first=f32(a+index*4u),last=f32(b+index*4u);
    return first+(last-first)*fraction;
}

int c3_title_animate(C3Title *title, uint32_t clip_index, float seconds)
{
    C3TitlePose pose;
    float normals[C3_TITLE_MAX_BONES][9];
    const C3TitleClip *clip;
    const uint8_t *first,*last;
    uint32_t index,next,bone,batch,i,j;
    double frame;
    float fraction;
    if (!title || !title->file_data || !title->draw_vertices || clip_index>=title->clip_count || !isfinite(seconds)) return 0;
    clip=title->clips+clip_index;
    frame=seconds>0 ? (double)seconds*(double)title->fps : 0.0;
    if (clip->loop && clip->frame_count>1u) frame=fmod(frame,(double)(clip->frame_count-1u));
    else if (frame>(double)(clip->frame_count-1u)) frame=(double)(clip->frame_count-1u);
    index=(uint32_t)frame;
    next=index+1u<clip->frame_count ? index+1u : index;
    fraction=(float)(frame-(double)index);
    first=title->file_data+title->frame_offset+(clip->first_frame+index)*title->frame_bytes;
    last=title->file_data+title->frame_offset+(clip->first_frame+next)*title->frame_bytes;
    memset(&pose,0,sizeof pose);
    for (bone=0; bone<title->bone_count; ++bone,first+=48u,last+=48u) {
        for (i=0;i<12;++i) pose.matrices[bone][i]=interpolate(first,last,i,fraction);
        if (!normal_matrix(pose.matrices[bone],normals[bone])) {
            if(!bone_hidden(title,first+(title->bone_count-bone)*48u,bone))return 0;
            memset(normals[bone],0,sizeof normals[bone]);
            normals[bone][0]=normals[bone][4]=normals[bone][8]=1.0f;
        }
    }
    for (batch=0;batch<title->batch_count;++batch,first+=title->material_frame_bytes,last+=title->material_frame_bytes) {
        C3TitleMaterialState *state=pose.materials+batch;
        for (i=0;i<2;++i) {
            state->uv_scale[i]=interpolate(first,last,i,fraction);
            state->uv_offset[i]=interpolate(first,last,i+2u,fraction);
            state->uv2_scale[i]=title->format_version>=2u ? interpolate(first,last,i+12u,fraction):1.0f;
            state->uv2_offset[i]=title->format_version>=2u ? interpolate(first,last,i+14u,fraction):0.0f;
            state->uv_cross[i]=title->format_version>=3u ? interpolate(first,last,i+16u,fraction):0.0f;
            state->uv2_cross[i]=title->format_version>=3u ? interpolate(first,last,i+18u,fraction):0.0f;
        }
        for (i=0;i<3;++i) state->specular_origin[i]=title->format_version>=3u ? interpolate(first,last,i+20u,fraction):0.0f;
        state->alpha=interpolate(first,last,4u,fraction); state->visible=f32(first+20u);
        for (i=0;i<3;++i) { state->tev0[i]=f32(first+24u+i*4u); state->konst[i]=f32(first+36u+i*4u); }
    }
    for (i=0;i<3;++i) { pose.camera.eye[i]=interpolate(first,last,i,fraction); pose.camera.target[i]=interpolate(first,last,i+3u,fraction); }
    pose.camera.roll=interpolate(first,last,6u,fraction);
    pose.camera.fov_y_degrees=interpolate(first,last,7u,fraction);
    pose.camera.near_plane=interpolate(first,last,8u,fraction);
    pose.camera.far_plane=interpolate(first,last,9u,fraction);
    pose.camera.aspect=interpolate(first,last,10u,fraction);
    for (batch=0;batch<title->batch_count;++batch) {
        const C3TitleBatch *b=title->batches+batch;
        const C3TitleMaterialState *state=pose.materials+batch;
        for (i=b->first;i<b->first+b->count;++i) {
            const C3TitleVertex *source=title->vertices+i;
            C3TitleDrawVertex *target=title->draw_vertices+i;
            const float *m=pose.matrices[source->bone],*n=normals[source->bone];
            double length;
            for (j=0;j<3;++j) {
                target->pos[j]=m[j*4u]*source->pos[0]+m[j*4u+1u]*source->pos[1]+m[j*4u+2u]*source->pos[2]+m[j*4u+3u];
                target->normal[j]=n[j*3u]*source->normal[0]+n[j*3u+1u]*source->normal[1]+n[j*3u+2u]*source->normal[2];
            }
            length=sqrt((double)target->normal[0]*target->normal[0]+(double)target->normal[1]*target->normal[1]+(double)target->normal[2]*target->normal[2]);
            if (length>0.0) for (j=0;j<3;++j) target->normal[j]=(float)(target->normal[j]/length);
            target->uv[0]=source->uv[0]*state->uv_scale[0]+source->uv[1]*state->uv_cross[0]+state->uv_offset[0];
            target->uv[1]=source->uv[0]*state->uv_cross[1]+source->uv[1]*state->uv_scale[1]+state->uv_offset[1];
            target->uv2[0]=source->uv2[0]*state->uv2_scale[0]+source->uv2[1]*state->uv2_cross[0]+state->uv2_offset[0];
            target->uv2[1]=source->uv2[0]*state->uv2_cross[1]+source->uv2[1]*state->uv2_scale[1]+state->uv2_offset[1];
            memcpy(target->rgba,source->rgba,4);
        }
    }
    title->pose=pose; title->active_clip=clip_index; title->active_frame=(float)frame;
    return 1;
}
