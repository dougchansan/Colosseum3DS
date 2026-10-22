/* Source-only native GPU check: two independent packs, two independent UV
 * channels, and the two nonstandard framebuffer modes used by S1_out. */
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include "stage_renderer.h"

static C3Title background, overlay;
static C3TitleDrawVertex background_points[6], overlay_points[12];
static C3TitleBatch background_batch, overlay_batches[2];
static C3TitleTexture background_texture, overlay_textures[2];
static unsigned char gray[8*8*4], tint[8*8*4], corners[8*8*4];

static void quad(C3TitleDrawVertex *points, float top, float bottom, float z)
{
    const float xy[6][2] = {{-.8f,top},{-.8f,bottom},{.8f,bottom},
                            {-.8f,top},{.8f,bottom},{.8f,top}};
    for (unsigned i = 0; i < 6; ++i) {
        points[i].pos[0] = xy[i][0]; points[i].pos[1] = xy[i][1]; points[i].pos[2] = z;
        points[i].normal[2] = 1;
        points[i].uv[0] = xy[i][0] > 0 ? 1 : 0;
        points[i].uv[1] = xy[i][1] == top ? 0 : 1;
        points[i].uv2[0] = 1 - points[i].uv[0]; /* Deliberately independent. */
        points[i].uv2[1] = points[i].uv[1];
        memset(points[i].rgba, 255, 4);
    }
}

int main(void)
{
    gfxInitDefault(); gfxSetDoubleBuffering(GFX_BOTTOM, false); consoleInit(GFX_BOTTOM, NULL);
    quad(background_points, .9f, -.9f, -.1f);
    quad(overlay_points, .8f, 0, 0);
    quad(overlay_points + 6, 0, -.8f, 0);
    for (unsigned y = 0; y < 8; ++y) for (unsigned x = 0; x < 8; ++x) {
        unsigned offset = (y*8 + x)*4;
        gray[offset] = gray[offset+1] = gray[offset+2] = 128; gray[offset+3] = 255;
        tint[offset] = 255; tint[offset+1] = 128; tint[offset+2] = 64; tint[offset+3] = 17;
        corners[offset] = (y < 4 && x < 4) || (y >= 4 && x >= 4) ? 255 : 0;
        corners[offset+1] = x >= 4 ? 255 : 0;
        corners[offset+2] = y >= 4 && x < 4 ? 255 : 0;
        corners[offset+3] = 29; /* Both layers' alpha must pass through. */
    }
    background_batch = (C3TitleBatch){.count=6,.color_mode=4,.alpha_mode=3,.render_mode=0x4012,
        .alpha=1,.texture_blend=1,.alpha_compare0=7,.alpha_compare1=7,.depth_compare=3};
    background_texture = (C3TitleTexture){.width=8,.height=8,.pixels=gray,.size=sizeof(gray)};
    background.draw_vertices=background_points; background.batches=&background_batch;
    background.textures=&background_texture; background.vertex_count=6;
    background.batch_count=background.texture_count=1;
    background.pose.materials[0].visible=background.pose.materials[0].alpha=1;
    overlay_batches[0] = (C3TitleBatch){.count=6,.texture2=1,.color_mode=4,.alpha_mode=0,
        .render_mode=0x60006032,.alpha=1,.texture_blend=1,.alpha_compare0=7,.alpha_compare1=7,
        .depth_compare=3,.flags=C3_TITLE_TEV_LERP|C3_TITLE_DUAL_MODULATE|
        C3_TITLE_TRANSLUCENT|C3_TITLE_NO_DEPTH_WRITE|C3_TITLE_SUBTRACT};
    overlay_batches[1]=overlay_batches[0]; overlay_batches[1].first=6;
    overlay_batches[1].flags=(overlay_batches[1].flags & ~C3_TITLE_SUBTRACT)|C3_TITLE_ADDITIVE_ONE;
    overlay_textures[0]=(C3TitleTexture){.width=8,.height=8,.pixels=tint,.size=sizeof(tint)};
    overlay_textures[1]=(C3TitleTexture){.width=8,.height=8,.pixels=corners,.size=sizeof(corners)};
    overlay.draw_vertices=overlay_points; overlay.batches=overlay_batches; overlay.textures=overlay_textures;
    overlay.vertex_count=12; overlay.batch_count=overlay.texture_count=2;
    for (unsigned i = 0; i < 2; ++i) {
        overlay.pose.materials[i].visible=overlay.pose.materials[i].alpha=1;
        for (unsigned c = 0; c < 3; ++c) overlay.pose.materials[i].konst[c]=127.f/255.f;
    }
    const C3Title *models[] = {&overlay, &background}; /* Opaque pass must run first. */
    C3TitleCamera camera={.eye={0,0,2},.fov_y_degrees=60,.near_plane=.1f,.far_plane=10,.aspect=4.f/3.f};
    char error[256]={0}; int okay=c3_stage_renderer_init(models,2,error,sizeof(error))==0;
    printf("Multipart field material probe\nTop: GX destination-source\nBottom: ONE/ONE addition\nUV1 horizontally reversed\n\n%s\n",okay?"Renderer initialized":error);
    while (aptMainLoop()) {
        hidScanInput(); if (hidKeysDown()&KEY_START) break;
        C3TitleRenderStats stats;
        if (okay) {
            int result=c3_stage_renderer_draw(models,2,&camera,1,&stats,error,sizeof(error));
            if (result==C3_RENDER_BUSY) {svcSleepThread(1000000);continue;}
            if (result==C3_RENDER_ERROR) {okay=0; printf("DRAW FAIL %s\n",error);}
        }
        if (okay && (stats.draw_calls!=3 || stats.vertices!=18 || stats.gpu_buffer_bytes!=1776)) {
            okay=0; printf("Bad aggregate metrics: %lu/%lu/%lu\n",(unsigned long)stats.draw_calls,(unsigned long)stats.vertices,
                (unsigned long)stats.gpu_buffer_bytes);
        }
        if (!okay) {gfxFlushBuffers();gfxSwapBuffers();gspWaitForVBlank();}
    }
    while (!c3_stage_renderer_try_shutdown()) {aptMainLoop();svcSleepThread(1000000);}
    gfxExit(); return okay?0:1;
}
