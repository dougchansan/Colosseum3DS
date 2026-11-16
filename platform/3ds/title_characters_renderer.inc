/* Original window sprites, sharing the title's frame and shader. */
static const C3TitleCharacters *character_pack;
static C3TitleCharacterState character_state;
static unsigned character_textures[17];
static TitleGPUVertex *character_vertices;
static bool characters_visible;

int c3_title_renderer_set_characters(const C3TitleCharacters *pack)
{
    unsigned first = texture_count;
    if (!pack || !pack->data || character_pack || !scene_count ||
        texture_count + 17 > C3_STAGE_MAX_TEXTURES || !c3_stage_renderer_is_idle()) return 0;
    character_vertices = linearAlloc(19 * 6 * sizeof *character_vertices);
    if (!character_vertices) return 0;
    for (unsigned i = 0; i < 17; ++i) {
        C3TitleCharacterTexture image;
        if (!c3_title_characters_texture(pack, i, &image)) goto failed;
        unsigned w = 8, h = 8;
        while (w < image.width) w *= 2;
        while (h < image.height) h *= 2;
        uint8_t *padded = calloc(w * h, 4);
        if (!padded) goto failed;
        /* Pad instead of resampling: retain texel coordinates and alpha edges. */
        for (unsigned y = 0; y < image.height; ++y)
            memcpy(padded + y * w * 4, image.pixels + y * image.width * 4, image.width * 4);
        C3TitleTexture source = {.width=w, .height=h, .pixels=padded, .size=w*h*4};
        bool ok = upload_texture(&textures[texture_count], &source);
        free(padded);
        if (!ok) goto failed;
        character_textures[i] = texture_count++;
    }
    for (unsigned i = first; i < texture_count; ++i) gpu_buffer_bytes += textures[i].size;
    gpu_buffer_bytes += 19 * 6 * sizeof *character_vertices;
    character_pack = pack;
    return 1;
failed:
    while (texture_count > first) C3D_TexDelete(&textures[--texture_count]);
    linearFree(character_vertices); character_vertices = NULL;
    return 0;
}

void c3_title_renderer_characters(const C3TitleCharacterState *state, bool visible)
{
    if (state) character_state = *state;
    characters_visible = visible;
}

static void draw_title_characters(C3TitleRenderStats *stats)
{
    if (!character_pack || !characters_visible) return;
    static const unsigned corners[6][2] = {{0,0},{1,0},{0,1},{0,1},{1,0},{1,1}};
    unsigned count = 0, bindings[19];
    for (unsigned i = 0; i < 19; ++i) {
        C3TitleCharacterItem item;
        c3_title_characters_item(character_pack, i, &item);
        if (item.page != character_state.page) continue;
        C3D_Tex *texture = &textures[character_textures[item.texture]];
        float x,y,w,h;
        c3_title_characters_rect(&item, &character_state, &x,&y,&w,&h);
        if (x + w <= 0 || x >= 400) continue;
        bindings[count] = character_textures[item.texture];
        for (unsigned j = 0; j < 6; ++j) {
            TitleGPUVertex *out = &character_vertices[count*6+j];
            memset(out,0,sizeof *out);
            out->pos[0] = x + w * corners[j][0]; out->pos[1] = y + h * corners[j][1];
            out->uv[0] = (float)item.w / texture->width * corners[j][0];
            out->uv[1] = (float)item.h / texture->height * corners[j][1];
            for (unsigned k = 0; k < 4; ++k) out->rgba[k] = 1;
        }
        ++count;
    }
    if (!count) return;
    C3D_Mtx ortho; Mtx_OrthoTilt(&ortho,0,400,240,0,-1,1,false);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER,mvp_location,&ortho);
    C3D_FVUnifSet(GPU_VERTEX_SHADER,color_pass_location,1,0,0,0);
    C3D_CullFace(GPU_CULL_NONE); C3D_DepthTest(false,GPU_ALWAYS,GPU_WRITE_COLOR);
    C3D_AlphaTest(false,GPU_ALWAYS,0);
    C3D_AlphaBlend(GPU_BLEND_ADD,GPU_BLEND_ADD,GPU_SRC_ALPHA,GPU_ONE_MINUS_SRC_ALPHA,GPU_ZERO,GPU_ONE);
    for (unsigned i = 0; i < 6; ++i) C3D_TexEnvInit(C3D_GetTexEnv(i));
    C3D_TexEnv *env = C3D_GetTexEnv(0);
    C3D_TexEnvSrc(env,C3D_Both,GPU_TEXTURE0,GPU_PRIMARY_COLOR,GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(env,C3D_Both,GPU_MODULATE);
    GSPGPU_FlushDataCache(character_vertices,count*6*sizeof *character_vertices);
    C3D_BufInfo *buffers = C3D_GetBufInfo(); BufInfo_Init(buffers);
    BufInfo_Add(buffers,character_vertices,sizeof *character_vertices,5,0x43210);
    C3D_SetViewport(0,0,240,400);
    for (unsigned i = 0; i < count; ++i) {
        C3D_TexBind(0,&textures[bindings[i]]);
        C3D_DrawArrays(GPU_TRIANGLES,i*6,6);
    }
    if (stats) { stats->draw_calls += count; stats->vertices += count*6; }
}
