#include "colosseum3ds/confusion_indirect.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static C3WzxConfusionProgram program;
static unsigned char capture[128*128*4],output[128*128*4],before[sizeof output];
static C3ConfusionPixel pixels[128*128];
static C3ConfusionType12Frame frame;
static C3ConfusionSamplerProfile profile;
static char error[128];
static C3ConfusionGridVertex grid[128*6],grid_before[128*6];
static int sample(void){return c3_confusion_sample_rgba(&program,&frame,&profile,capture,sizeof capture,128,128,pixels,128*128,128,128,output,sizeof output,error,sizeof error);}
static void setup(float indirect)
{
    memset(&program,0,sizeof program);memset(&frame,0,sizeof frame);
    profile=c3_confusion_sampler_source_profile();
    frame.required_capabilities=3;frame.capture_model_flags=0x3010;frame.indirect_exponent=1;
    frame.captured_uv[0][0]=frame.captured_uv[1][1]=1;
    frame.captured_uv[0][2]=frame.captured_uv[1][2]=indirect;
    frame.indirect_matrix[0][0]=frame.indirect_matrix[1][1]=.5f;
    frame.indirect_matrix[0][2]=frame.indirect_matrix[1][2]=.5f*(indirect-1);
    frame.world[0][0]=frame.world[3][0]=-1;frame.world[1][0]=frame.world[2][0]=1;
    frame.world[0][1]=frame.world[1][1]=-1;frame.world[2][1]=frame.world[3][1]=1;
    for(unsigned y=0;y<128;y++)for(unsigned x=0;x<128;x++){
        size_t i=y*128+x;pixels[i]=(C3ConfusionPixel){(x+.5f)/128,(y+.5f)/128,1};
        capture[4*i]=(unsigned char)x;capture[4*i+1]=(unsigned char)y;
        capture[4*i+2]=(unsigned char)(x^y);capture[4*i+3]=(unsigned char)(x+y);
    }
}
static int floor8(int x){return x>=0?x/8:-((-x+7)/8);}
static void test_grid(void)
{
    size_t checked=0;
    for(unsigned phase=0;phase<=16;phase++){
        setup((float)phase/64);
        for(unsigned y=0;y<128;y++)for(unsigned x=0;x<128;x++)
            program.intensity[y*128+x]=(unsigned char)((x*17+y*29)&255);
        for(unsigned y=0;y<128;y++){
            assert(c3_confusion_grid_stripe(&program,&frame,&profile,128,128,y,1,grid,128*6,error,sizeof error));
            for(unsigned x=0;x<128;x++)for(unsigned triangle=0;triangle<2;triangle++){
                const C3ConfusionGridVertex *g=grid+x*6;
                unsigned b=triangle?2:1,c=triangle?5:2;
                float u=((float)x+(triangle?.25f:.75f))/128;
                float v=((float)y+(triangle?.75f:.25f))/128;
                float gu=.25f*g[0].uv[0]+.5f*g[b].uv[0]+.25f*g[c].uv[0];
                float gv=.25f*g[0].uv[1]+.5f*g[b].uv[1]+.25f*g[c].uv[1];
                /* Second triangle weights for point(.25,.75) are .25,.25,.5. */
                if(triangle){gu=.25f*g[3].uv[0]+.25f*g[4].uv[0]+.5f*g[5].uv[0];
                    gv=.25f*g[3].uv[1]+.25f*g[4].uv[1]+.5f*g[5].uv[1];}
                int intensity=(int)program.intensity[y*128+x]-128;
                int a=(int)(frame.indirect_matrix[0][0]*1024);
                int z=(int)(frame.indirect_matrix[0][2]*1024);
                int delta=floor8(a*intensity+z*intensity)*2;
                int cpu_s=(int)((u+frame.captured_uv[0][2])*128*128)+delta;
                int cpu_t=(int)((v+frame.captured_uv[1][2])*128*128)+delta;
                assert(fabsf(gu*128-(float)cpu_s/128)<1.01f/128);
                assert(fabsf(gv*128-(float)cpu_t/128)<1.01f/128);
                float wx=.25f*g[0].world[0]+.5f*g[b].world[0]+.25f*g[c].world[0];
                if(triangle)wx=.25f*g[3].world[0]+.25f*g[4].world[0]+.5f*g[5].world[0];
                assert(fabsf(wx-(-1+2*u))<1e-6f);checked++;
            }
        }
    }
    memcpy(grid_before,grid,sizeof grid);frame.world[2][0]=NAN;
    assert(!c3_confusion_grid_stripe(&program,&frame,&profile,128,128,0,1,grid,128*6,error,sizeof error));
    assert(!memcmp(grid_before,grid,sizeof grid));
    setup(0);assert(!c3_confusion_grid_stripe(&program,&frame,&profile,128,128,127,2,grid,128*6,error,sizeof error));
    assert(!c3_confusion_grid_stripe(&program,&frame,&profile,128,128,0,1,grid,128*6-1,error,sizeof error));
    printf("Confusion grid: %zu interior triangle samples matched independent fixed-point UV within1/128texel\n",checked);
}
int main(void)
{
    setup(0);assert(sample());assert(!memcmp(capture,output,sizeof output));
    /* Exact .25 amplitude: matrix coefficients512,-384, exponent1. Original
     * zero mask: -32 texels cancel capture UV translation+32 texels. */
    setup(.25f);assert(sample());assert(!memcmp(capture,output,sizeof output));
    memset(program.intensity,128,sizeof program.intensity);assert(sample());
    assert(output[(20*128+10)*4]==42);assert(output[(20*128+10)*4+1]==52);
    assert(output[(127*128+127)*4]==127);assert(output[(127*128+127)*4+3]==254);
    memset(program.intensity,255,sizeof program.intensity);assert(sample());
    /* Offset63.75: capture is sampled at texel73.75 for local texel10. */
    assert(output[(10*128+10)*4]==73);
    setup(.25f);for(unsigned y=0;y<128;y++)for(unsigned x=0;x<128;x++)program.intensity[y*128+x]=(unsigned char)x;
    assert(sample());assert(output[(20*128+20)*4]==25);assert(output[(20*128+40)*4]==50);
    /* Quantized coefficient: .249 amplitude yields trunc(-384.512)=-384,
     * not full precision curve multiplication; verify with independent
     * coordinate arithmetic using an isolated point. */
    setup(.249f);memset(program.intensity,255,sizeof program.intensity);assert(sample());
    {float s=(pixels[0].u+frame.captured_uv[0][2])*128*128;
     int fixed=(int)s+4064; /* (128*127 >>3)*2 =4064 */
     int expected=(fixed-64)/128;
     assert(output[0]==expected);}
    setup(0);pixels[0].u=0;pixels[0].v=0;pixels[1].u=1;pixels[1].v=1;
    pixels[2].covered=0;assert(sample());assert(output[0]==0&&output[4]==127);
    assert(output[8]==0&&output[9]==0&&output[10]==0&&output[11]==0);
    /* Preflight is transactional, even if invalid UV occurs at final pixel. */
    memcpy(before,output,sizeof output);pixels[128*128-1].u=NAN;assert(!sample());
    assert(!memcmp(before,output,sizeof output)&&error[0]);
    setup(0);profile.bias=0;assert(!sample());assert(!memcmp(before,output,sizeof output));
    setup(0);frame.indirect_exponent=0;assert(!sample());
    setup(0);frame.indirect_matrix[0][0]=NAN;assert(!sample());
    setup(0);assert(!c3_confusion_sample_rgba(&program,&frame,&profile,capture,sizeof capture,128,128,pixels,128*128,128,128,capture,sizeof capture,error,sizeof error));
    assert(!c3_confusion_sample_rgba(&program,&frame,&profile,capture,sizeof capture,641,128,pixels,128*128,128,128,output,sizeof output,error,sizeof error));
    assert(!c3_confusion_sample_rgba(&program,&frame,&profile,capture,sizeof capture,128,128,pixels,128*128-1,128,128,output,sizeof output,error,sizeof error));
    test_grid();
    puts("Confusion CPU indirect sampler: identity, mask gradient, nonzero warp, fixed-point matrix, edges, alpha, invalid-state transaction passed");
    return 0;
}
