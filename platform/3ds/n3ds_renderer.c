#include "n3ds_platform.h"

#include <3ds.h>
#include <citro3d.h>
#include <stdio.h>
#include <string.h>

#include "port_shader_shbin.h"

typedef struct {
    float x, y, z;
    float r, g, b;
} PortVertex;

/* Original test geometry. This is deliberately not presented as a game model. */
static const PortVertex vertices[] = {
    {0, 1, 0, 1, .7f, .2f}, {1, 0, 0, 1, .7f, .2f}, {0, 0, 1, 1, .7f, .2f},
    {0, 1, 0, .2f, .8f, 1}, {0, 0, 1, .2f, .8f, 1}, {-1, 0, 0, .2f, .8f, 1},
    {0, 1, 0, .6f, .3f, 1}, {-1, 0, 0, .6f, .3f, 1}, {0, 0, -1, .6f, .3f, 1},
    {0, 1, 0, .3f, 1, .6f}, {0, 0, -1, .3f, 1, .6f}, {1, 0, 0, .3f, 1, .6f},
    {0, -1, 0, .8f, .3f, .2f}, {0, 0, 1, .8f, .3f, .2f}, {1, 0, 0, .8f, .3f, .2f},
    {0, -1, 0, .1f, .5f, .8f}, {-1, 0, 0, .1f, .5f, .8f}, {0, 0, 1, .1f, .5f, .8f},
    {0, -1, 0, .4f, .2f, .8f}, {0, 0, -1, .4f, .2f, .8f}, {-1, 0, 0, .4f, .2f, .8f},
    {0, -1, 0, .2f, .7f, .3f}, {1, 0, 0, .2f, .7f, .3f}, {0, 0, -1, .2f, .7f, .3f}
};

static bool c3d_started, program_started;
static C3D_RenderTarget *top_target;
static DVLB_s *shader;
static shaderProgram_s program;
static void *vertex_buffer;
static int projection_location, model_view_location;
static C3D_Mtx projection;

int c3_n3ds_renderer_init(char *error, size_t capacity)
{
    C3D_AttrInfo *attributes;
    C3D_BufInfo *buffers;
    C3D_TexEnv *stage;
    const uint32_t transfer_flags =
        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);

    if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE)) {
        snprintf(error, capacity, "citro3d initialization failed.");
        return -1;
    }
    c3d_started = true;
    top_target = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH24_STENCIL8);
    if (!top_target) {
        snprintf(error, capacity, "Top-screen render target allocation failed.");
        goto fail;
    }
    C3D_RenderTargetSetOutput(top_target, GFX_TOP, GFX_LEFT, transfer_flags);
    shader = DVLB_ParseFile((u32 *)port_shader_shbin, port_shader_shbin_size);
    if (!shader || shader->numDVLE == 0) {
        snprintf(error, capacity, "PICA shader load failed.");
        goto fail;
    }
    if (R_FAILED(shaderProgramInit(&program))) {
        snprintf(error, capacity, "Shader program initialization failed.");
        goto fail;
    }
    program_started = true;
    if (R_FAILED(shaderProgramSetVsh(&program, &shader->DVLE[0]))) {
        snprintf(error, capacity, "Vertex shader initialization failed.");
        goto fail;
    }
    projection_location = shaderInstanceGetUniformLocation(program.vertexShader, "projection");
    model_view_location = shaderInstanceGetUniformLocation(program.vertexShader, "modelView");
    if (projection_location < 0 || model_view_location < 0) {
        snprintf(error, capacity, "Required shader uniforms are missing.");
        goto fail;
    }
    C3D_BindProgram(&program);
    attributes = C3D_GetAttrInfo();
    AttrInfo_Init(attributes);
    AttrInfo_AddLoader(attributes, 0, GPU_FLOAT, 3);
    AttrInfo_AddLoader(attributes, 1, GPU_FLOAT, 3);
    vertex_buffer = linearAlloc(sizeof(vertices));
    if (!vertex_buffer) {
        snprintf(error, capacity, "GPU vertex-buffer allocation failed.");
        goto fail;
    }
    memcpy(vertex_buffer, vertices, sizeof(vertices));
    GSPGPU_FlushDataCache(vertex_buffer, sizeof(vertices));
    buffers = C3D_GetBufInfo();
    BufInfo_Init(buffers);
    if (BufInfo_Add(buffers, vertex_buffer, sizeof(PortVertex), 2, 0x10) < 0) {
        snprintf(error, capacity, "GPU vertex-buffer setup failed.");
        goto fail;
    }
    Mtx_PerspTilt(&projection, C3D_AngleFromDegrees(55.0f), C3D_AspectRatioTop, 0.1f, 20.0f, false);
    stage = C3D_GetTexEnv(0);
    C3D_TexEnvInit(stage);
    C3D_TexEnvSrc(stage, C3D_Both, GPU_PRIMARY_COLOR, 0, 0);
    C3D_TexEnvFunc(stage, C3D_Both, GPU_REPLACE);
    C3D_DepthTest(true, GPU_GEQUAL, GPU_WRITE_ALL);
    C3D_CullFace(GPU_CULL_NONE);
    if (capacity) error[0] = '\0';
    return 0;

fail:
    c3_n3ds_renderer_shutdown();
    return -1;
}

void c3_n3ds_renderer_draw(float angle_x, float angle_y)
{
    C3D_Mtx model_view;
    if (!c3d_started || !C3D_FrameBegin(C3D_FRAME_SYNCDRAW)) return;
    C3D_RenderTargetClear(top_target, C3D_CLEAR_ALL, 0x101727FF, 0);
    C3D_FrameDrawOn(top_target);
    Mtx_Identity(&model_view);
    Mtx_Translate(&model_view, 0.0f, 0.0f, -3.6f, true);
    Mtx_RotateX(&model_view, angle_x, true);
    Mtx_RotateY(&model_view, angle_y, true);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, projection_location, &projection);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, model_view_location, &model_view);
    C3D_DrawArrays(GPU_TRIANGLES, 0, sizeof(vertices) / sizeof(vertices[0]));
    gfxFlushBuffers();
    C3D_FrameEnd(0);
}

void c3_n3ds_renderer_shutdown(void)
{
    if (top_target) {
        /* Deleting a linked target drains the GPU queue before freeing it.
         * Do not wait for VBlank here: citro3d disables those callbacks while
         * suspended, and closing from HOME may exit without restoring them. */
        C3D_RenderTargetDelete(top_target);
        top_target = NULL;
    }
    if (vertex_buffer) {
        linearFree(vertex_buffer);
        vertex_buffer = NULL;
    }
    if (program_started) {
        shaderProgramFree(&program);
        program_started = false;
    }
    if (shader) {
        DVLB_Free(shader);
        shader = NULL;
    }
    if (c3d_started) {
        C3D_Fini();
        c3d_started = false;
    }
}
