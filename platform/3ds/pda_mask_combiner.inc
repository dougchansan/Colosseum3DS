/* Include in combiner() after resetting all six stages, before other branches.
 * Both images and UV generators remain original. HSD ALPHA_MASK interpolates
 * previous RGB toward texture RGB using that texture's alpha; alpha passes.
 * The removed solid source image is stored exactly in batch->diffuse. */
if (batch->flags & C3_TITLE_SOURCE_ALPHA_MASK) {
    C3D_TexEnv *first_mask = C3D_GetTexEnv(0);
    C3D_TexEnvColor(first_mask, color4(batch->diffuse[0], batch->diffuse[1],
                                    batch->diffuse[2], material->alpha));
    C3D_TexEnvSrc(first_mask, C3D_RGB, GPU_TEXTURE0, GPU_CONSTANT, GPU_TEXTURE0);
    C3D_TexEnvOpRgb(first_mask, GPU_TEVOP_RGB_SRC_COLOR,
                   GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
    C3D_TexEnvFunc(first_mask, C3D_RGB, GPU_INTERPOLATE);
    C3D_TexEnvSrc(first_mask, C3D_Alpha, GPU_CONSTANT, GPU_CONSTANT, GPU_CONSTANT);
    C3D_TexEnvFunc(first_mask, C3D_Alpha, GPU_REPLACE);
    C3D_TexEnv *second_mask = C3D_GetTexEnv(1);
    C3D_TexEnvSrc(second_mask, C3D_RGB, GPU_TEXTURE1, GPU_PREVIOUS, GPU_TEXTURE1);
    C3D_TexEnvOpRgb(second_mask, GPU_TEVOP_RGB_SRC_COLOR,
                   GPU_TEVOP_RGB_SRC_COLOR, GPU_TEVOP_RGB_SRC_ALPHA);
    C3D_TexEnvFunc(second_mask, C3D_RGB, GPU_INTERPOLATE);
    C3D_TexEnv *mask_light = C3D_GetTexEnv(4);
    C3D_TexEnvSrc(mask_light, C3D_Both, GPU_PREVIOUS, GPU_PRIMARY_COLOR, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(mask_light, C3D_Both, GPU_MODULATE);
    C3D_TexEnv *mask_fade = C3D_GetTexEnv(5);
    C3D_TexEnvColor(mask_fade, color4(fade, fade, fade, 1));
    C3D_TexEnvSrc(mask_fade, C3D_RGB, GPU_PREVIOUS, GPU_CONSTANT, GPU_PRIMARY_COLOR);
    C3D_TexEnvFunc(mask_fade, C3D_RGB, GPU_MODULATE);
    return;
}
