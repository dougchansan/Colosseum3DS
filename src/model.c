#include "colosseum3ds/model.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_FILE (16u * 1024u * 1024u)
static uint32_t u32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static float f32(const uint8_t *p)
{
    uint32_t bits = u32(p); float f; memcpy(&f, &bits, sizeof(f)); return f;
}
static int fail(char *error, size_t capacity, const char *message)
{
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return -1;
}
void c3_model_close(C3Model *m)
{
    if (!m) return;
    free(m->vertices); free(m->draw_vertices); free(m->batches);
    free(m->textures); free(m->matrices); free(m->file_data);
    memset(m, 0, sizeof(*m));
}
static int power_dimension(uint32_t n) { return n >= 8 && n <= 1024 && !(n & (n - 1)); }
int c3_model_parse(C3Model *m, const void *data, size_t bytes, char *error, size_t capacity)
{
    const uint8_t *p = data;
    uint32_t bones, vertices, batches, textures, frames;
    size_t vo = 128, bo, to, mo, po, matrix_count, end;
    C3Model result = {0};
    if (!m) return fail(error, capacity, "Missing model");
    /* Do not invalidate caller input by releasing the model it points into.
     * Subtract only after ordering integer addresses to avoid range overflow. */
    if (p && m->file_data && (uintptr_t)p >= (uintptr_t)m->file_data &&
        (uintptr_t)p - (uintptr_t)m->file_data < m->file_bytes)
        return fail(error, capacity, "Model input aliases owned storage");
    c3_model_close(m);
    if (!p || bytes < 128 || bytes > MAX_FILE) return fail(error, capacity, "Invalid model size");
    if (memcmp(p, "C3M1", 4) || u32(p+4) != 1 || u32(p+8) != bytes)
        return fail(error, capacity, "Invalid model header");
    bones=u32(p+12); vertices=u32(p+16); batches=u32(p+20); textures=u32(p+24); frames=u32(p+28);
    if (!bones || bones>128 || !vertices || vertices>8192 || !batches || batches>32 ||
        !textures || textures>16 || !frames || frames>240 || !isfinite(f32(p+32)) || f32(p+32)<=0 || f32(p+32)>120)
        return fail(error, capacity, "Model limits exceeded");
    bo=vo+(size_t)vertices*32; to=bo+(size_t)batches*16; mo=to+(size_t)textures*24;
    matrix_count=(size_t)frames*bones*12; po=mo+matrix_count*4;
    if (po>bytes || u32(p+36)!=vo || u32(p+40)!=bo || u32(p+44)!=to || u32(p+48)!=mo || u32(p+52)!=po)
        return fail(error, capacity, "Invalid section offsets");
    for (size_t i=112;i<128;i++) if (p[i]) return fail(error, capacity, "Nonzero header reserved bytes");
    for (size_t i=0;i<6;i++) {
        result.bounds[i]=f32(p+56+i*4);
        if (!isfinite(result.bounds[i]) || fabsf(result.bounds[i])>10000)
            return fail(error, capacity, "Invalid bounds");
    }
    for (size_t i=0;i<3;i++) if (result.bounds[i]>result.bounds[i+3]) return fail(error, capacity, "Reversed bounds");
    for (uint32_t i=0;i<vertices;i++) {
        const uint8_t *v=p+vo+(size_t)i*32;
        for (size_t j=0;j<5;j++) {
            float value=f32(v+j*4);
            if (!isfinite(value) || fabsf(value)>(j<3 ? 10000.0f : 1000000.0f))
                return fail(error, capacity, "Invalid vertex component");
        }
        if (u32(v+20)>=bones || u32(v+28)) return fail(error, capacity, "Invalid vertex bone or reserved bytes");
    }
    end=0;
    for (uint32_t i=0;i<batches;i++) {
        const uint8_t *b=p+bo+(size_t)i*16; uint32_t count=u32(b+4);
        if (u32(b)!=end || !count || count%3 || count>vertices-end || u32(b+8)>=textures || (u32(b+12)&~3u))
            return fail(error, capacity, "Invalid triangle batches");
        end+=count;
    }
    if (end!=vertices) return fail(error, capacity, "Incomplete batch coverage");
    end=po;
    for (uint32_t i=0;i<textures;i++) {
        const uint8_t *t=p+to+(size_t)i*24; uint32_t w=u32(t), h=u32(t+4); size_t size=(size_t)w*h*4;
        if (!power_dimension(w) || !power_dimension(h) || u32(t+8)!=end || u32(t+12)!=size ||
            u32(t+16)>1 || u32(t+20)>1 || size>bytes-end)
            return fail(error, capacity, "Invalid RGBA texture");
        end+=size;
    }
    if (end!=bytes) return fail(error, capacity, "Trailing model bytes");
    for (size_t i=0;i<matrix_count;i++) {
        float value=f32(p+mo+i*4);
        if (!isfinite(value) || fabsf(value)>1000000)
            return fail(error, capacity, "Invalid matrix component");
    }
    result.file_data=malloc(bytes); result.vertices=calloc(vertices,sizeof(*result.vertices));
    result.draw_vertices=calloc(vertices,sizeof(*result.draw_vertices)); result.batches=calloc(batches,sizeof(*result.batches));
    result.textures=calloc(textures,sizeof(*result.textures)); result.matrices=malloc(matrix_count*sizeof(float));
    if (!result.file_data || !result.vertices || !result.draw_vertices || !result.batches || !result.textures || !result.matrices) {
        c3_model_close(&result); return fail(error, capacity, "Model allocation failed");
    }
    memcpy(result.file_data,p,bytes); result.file_bytes=bytes;
    result.bone_count=bones; result.vertex_count=vertices; result.batch_count=batches;
    result.texture_count=textures; result.frame_count=frames; result.fps=f32(p+32);
    for (uint32_t i=0;i<vertices;i++) {
        const uint8_t *v=p+vo+(size_t)i*32; C3ModelVertex *vout=&result.vertices[i];
        for (size_t j=0;j<3;j++) vout->pos[j]=f32(v+j*4);
        for (size_t j=0;j<2;j++) vout->uv[j]=f32(v+12+j*4);
        vout->bone=u32(v+20); memcpy(vout->rgba,v+24,4);
    }
    for (uint32_t i=0;i<batches;i++) {
        const uint8_t *b=p+bo+(size_t)i*16;
        result.batches[i]=(C3ModelBatch){u32(b),u32(b+4),u32(b+8),u32(b+12)};
    }
    for (uint32_t i=0;i<textures;i++) {
        const uint8_t *t=p+to+(size_t)i*24;
        result.textures[i]=(C3ModelTexture){u32(t),u32(t+4),u32(t+16),u32(t+20),result.file_data+u32(t+8),u32(t+12)};
    }
    for (size_t i=0;i<matrix_count;i++) result.matrices[i]=f32(p+mo+i*4);
    result.resident_bytes=bytes+vertices*(sizeof(*result.vertices)+sizeof(*result.draw_vertices))+
        batches*sizeof(*result.batches)+textures*sizeof(*result.textures)+matrix_count*sizeof(float);
    result.peak_load_bytes=result.resident_bytes+bytes;
    *m=result; c3_model_animate(m,0);
    if (error && capacity) error[0]=0;
    return 0;
}
int c3_model_load(C3Model *m, const char *path, char *error, size_t capacity)
{
    FILE *file; long length; uint8_t *data; int status;
    if (!m) return fail(error,capacity,"Missing model");
    c3_model_close(m);
    if (!path || !(file=fopen(path,"rb"))) return fail(error,capacity,"Cannot open model");
    if (fseek(file,0,SEEK_END) || (length=ftell(file))<128 || length>(long)MAX_FILE || fseek(file,0,SEEK_SET)) {
        fclose(file); return fail(error,capacity,"Invalid model file size");
    }
    data=malloc((size_t)length);
    if (!data) { fclose(file); return fail(error,capacity,"Model allocation failed"); }
    if (fread(data,1,(size_t)length,file)!=(size_t)length) {
        free(data); fclose(file); return fail(error,capacity,"Cannot read model");
    }
    fclose(file); status=c3_model_parse(m,data,(size_t)length,error,capacity); free(data); return status;
}
void c3_model_animate(C3Model *m, float seconds)
{
    uint32_t frame=0,next=0; double phase=0; float blend=0;
    if (!m || !m->matrices || !m->vertices || !m->draw_vertices) return;
    if (m->frame_count>1 && isfinite(seconds)) {
        phase=fmod((double)seconds*m->fps,(double)m->frame_count-1);
        if (phase<0) phase+=m->frame_count-1;
        frame=(uint32_t)phase; next=frame+1; blend=(float)(phase-frame);
    }
    for (uint32_t i=0;i<m->vertex_count;i++) {
        const C3ModelVertex *v=&m->vertices[i]; C3ModelDrawVertex *d=&m->draw_vertices[i]; float xyz[3];
        const float *a=m->matrices+((size_t)frame*m->bone_count+v->bone)*12;
        const float *b=m->matrices+((size_t)next*m->bone_count+v->bone)*12;
        for (size_t row=0;row<3;row++) {
            double value=0;
            for (size_t col=0;col<4;col++) {
                double av=a[row*4+col], bv=b[row*4+col];
                value+=(av+(bv-av)*blend)*(col==3 ? 1.0 : v->pos[col]);
            }
            xyz[row]=(float)value;
        }
        d->x=xyz[0]; d->y=xyz[1]; d->z=xyz[2]; d->u=v->uv[0]; d->v=v->uv[1]; memcpy(d->rgba,v->rgba,4);
    }
}
