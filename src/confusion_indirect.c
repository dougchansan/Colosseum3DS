#include "colosseum3ds/confusion_indirect.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static int fail(char *e,size_t n,const char *s){if(e&&n)snprintf(e,n,"%s",s);return 0;}
C3ConfusionSamplerProfile c3_confusion_sampler_source_profile(void)
{
    C3ConfusionSamplerProfile p={0,0,7,1,0,0,0,0,0,0,0,1,1,1,1};return p;
}
static int profile_ok(const C3ConfusionSamplerProfile *p)
{
    return p&&p->indirect_stage==0&&p->format==0&&p->bias==7&&p->matrix==1&&
        !p->wrap_s&&!p->wrap_t&&!p->add_previous&&!p->utc_lod&&!p->alpha_selection&&
        !p->scale_s&&!p->scale_t&&p->mask_nearest==1&&p->capture_linear==1&&
        p->texture_clamp==1&&p->tev_replace_rgba==1;
}
static int overlap(const void *a,size_t an,const void *b,size_t bn)
{
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    if(an>UINTPTR_MAX-x||bn>UINTPTR_MAX-y)return 1;
    return x<y+bn&&y<x+an;
}
/* Explicit arithmetic right shift: C right shift of negative integers is
 * implementation-defined; division alone would round the wrong direction. */
static int floor_div(int a,int b){int q=a/b,r=a%b;return q-(r<0);}
static unsigned clamp(int x,unsigned n){return x<0?0:(unsigned)x>=n?n-1:(unsigned)x;}
static int frame_ok(const C3ConfusionType12Frame *f)
{
    if(!f||f->required_capabilities!=3||f->capture_model_flags!=0x3010||
       f->capture_include_particles||f->indirect_exponent!=1)return 0;
    float indirect=1+2*f->indirect_matrix[0][2];
    if(!isfinite(indirect)||indirect<0||indirect>.25f||
       f->indirect_matrix[0][0]!=.5f||f->indirect_matrix[1][1]!=.5f||
       f->indirect_matrix[0][1]!=0||f->indirect_matrix[1][0]!=0||
       f->indirect_matrix[1][2]!=f->indirect_matrix[0][2]||
       f->captured_uv[0][1]!=0||f->captured_uv[1][0]!=0)return 0;
    for(unsigned r=0;r<2;r++)for(unsigned c=0;c<3;c++)
        if(!isfinite(f->captured_uv[r][c])||fabsf(f->captured_uv[r][c])>8)return 0;
    return 1;
}
static int offset(const C3ConfusionType12Frame *f,unsigned char intensity)
{
    int m=512+(int)(1024*f->indirect_matrix[0][2]);
    return floor_div(m*((int)intensity-128),8)*2;
}
/* GX linear texel centers are n+0.5; fixedPoint coordinates have 7 bits.
 * Integer weights avoid host floating-point interpolation differences. */
static void linear(const unsigned char *src,unsigned w,unsigned h,int s,int t,unsigned char *out)
{
    int x=floor_div(s-64,128),y=floor_div(t-64,128);
    unsigned fx=(unsigned)(s-64-x*128),fy=(unsigned)(t-64-y*128);
    unsigned x0=clamp(x,w),x1=clamp(x+1,w),y0=clamp(y,h),y1=clamp(y+1,h);
    for(unsigned c=0;c<4;c++){
        unsigned a=src[((size_t)y0*w+x0)*4+c]*(128-fx)+src[((size_t)y0*w+x1)*4+c]*fx;
        unsigned b=src[((size_t)y1*w+x0)*4+c]*(128-fx)+src[((size_t)y1*w+x1)*4+c]*fx;
        out[c]=(unsigned char)((a*(128-fy)+b*fy)/16384);
    }
}
int c3_confusion_sample_rgba(const C3WzxConfusionProgram *program,
    const C3ConfusionType12Frame *f,const C3ConfusionSamplerProfile *profile,
    const unsigned char *capture,size_t capture_bytes,unsigned cw,unsigned ch,
    const C3ConfusionPixel *pixels,size_t count,unsigned vw,unsigned vh,
    unsigned char *out,size_t out_bytes,char *e,size_t n)
{
    /* An aliased diagnostic must not break transactional failure. */
    if(out&&e&&overlap(out,out_bytes,e,n))return 0;
    if(!program||!f||!capture||!pixels||!out||!profile_ok(profile))return fail(e,n,"Unqualified Confusion GX sampler state.");
    if(!cw||cw>640||!ch||ch>480||!vw||vw>640||!vh||vh>480||
       count!=(size_t)vw*vh||capture_bytes!=(size_t)cw*ch*4||out_bytes!=count*4)
        return fail(e,n,"Confusion image extent/byte count exceeds checked viewport.");
    if(overlap(out,out_bytes,capture,capture_bytes)||overlap(out,out_bytes,pixels,count*sizeof *pixels)||
       overlap(out,out_bytes,program,sizeof *program)||overlap(out,out_bytes,f,sizeof *f)||
       overlap(out,out_bytes,profile,sizeof *profile)|| (e&&overlap(out,out_bytes,e,n)))
        return fail(e,n,"Confusion sampler input/output alias.");
    if(!frame_ok(f))return fail(e,n,"Unqualified Confusion capture/matrix/UV contract.");
    for(size_t i=0;i<count;i++)if(pixels[i].covered>1||
        (pixels[i].covered&&(!isfinite(pixels[i].u)||!isfinite(pixels[i].v)||
        pixels[i].u<0||pixels[i].u>1||pixels[i].v<0||pixels[i].v>1)))
        return fail(e,n,"Confusion coverage UV is invalid.");
    /* GXSetIndTexMtx truncates matrix*1024 into signed 11-bit coefficients.
     * I8 expands RGBA=(I,I,I,I), indirect component order is A,B,G, and STU
     * bias subtracts128 from all three. Matrix dot >>3 then exponent1 left
     * shift1 yields offsets in S17.7 texel coordinates. */
    for(size_t i=0;i<count;i++){
        unsigned char *dst=out+i*4;
        if(!pixels[i].covered){memset(dst,0,4);continue;}
        float u=pixels[i].u,v=pixels[i].v;
        int ms=(int)(u*128*128),mt=(int)(v*128*128);
        unsigned x=clamp(floor_div(ms,128),128),y=clamp(floor_div(mt,128),128);
        int delta=offset(f,program->intensity[y*128+x]);
        int s=(int)((f->captured_uv[0][0]*u+f->captured_uv[0][2])*(float)cw*128);
        int t=(int)((f->captured_uv[1][1]*v+f->captured_uv[1][2])*(float)ch*128);
        linear(capture,cw,ch,s+delta,t+delta,dst);
    }
    if(e&&n)e[0]=0;
    return 1;
}
int c3_confusion_grid_stripe(const C3WzxConfusionProgram *p,const C3ConfusionType12Frame *f,
    const C3ConfusionSamplerProfile *profile,unsigned cw,unsigned ch,unsigned first,unsigned rows,
    C3ConfusionGridVertex *out,size_t count,char *e,size_t n)
{
    if(count>128u*128u*6u)return fail(e,n,"Confusion grid vertex bound exceeded.");
    size_t bytes=count*sizeof *out;
    if(out&&e&&overlap(out,bytes,e,n))return 0;
    if(!p||!out||!profile_ok(profile)||!frame_ok(f)||!cw||cw>640||!ch||ch>480||
       first>=128||!rows||rows>128-first||count!=(size_t)rows*128*6)
        return fail(e,n,"Unqualified Confusion grid stripe.");
    if(overlap(out,bytes,p,sizeof *p)||overlap(out,bytes,f,sizeof *f)||overlap(out,bytes,profile,sizeof *profile))
        return fail(e,n,"Confusion grid input/output alias.");
    for(unsigned k=0;k<3;k++){
        for(unsigned j=0;j<4;j++)if(!isfinite(f->world[j][k])||fabsf(f->world[j][k])>2048)
            return fail(e,n,"Confusion world quad is out of range.");
        if(fabsf(f->world[0][k]+f->world[2][k]-f->world[1][k]-f->world[3][k])>.001f)
            return fail(e,n,"Confusion world quad is not affine.");
    }
    static const unsigned corners[6][2]={{0,0},{1,0},{1,1},{0,0},{1,1},{0,1}};
    size_t i=0;
    for(unsigned y=first;y<first+rows;y++)for(unsigned x=0;x<128;x++){
        int delta=offset(f,p->intensity[y*128+x]);
        for(unsigned j=0;j<6;j++){
            float u=(float)(x+corners[j][0])/128,v=(float)(y+corners[j][1])/128;
            for(unsigned k=0;k<3;k++)out[i].world[k]=f->world[0][k]+
                u*(f->world[1][k]-f->world[0][k])+v*(f->world[3][k]-f->world[0][k]);
            out[i].uv[0]=f->captured_uv[0][0]*u+f->captured_uv[0][2]+(float)delta/((float)cw*128);
            out[i].uv[1]=f->captured_uv[1][1]*v+f->captured_uv[1][2]+(float)delta/((float)ch*128);
            i++;
        }
    }
    if(e&&n)e[0]=0;
    return 1;
}
