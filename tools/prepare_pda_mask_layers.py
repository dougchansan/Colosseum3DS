"""Preserve the reviewed solid-base / two UV alpha-mask HSD expression.

The pinned importer's importer/phases/plan/helpers/materials.py,
_plan_layer_blend, defines ALPHA_MASK as mix(previous RGB, texture RGB,
texture alpha). NONE leaves alpha unchanged. Keep both original images:
precompositing into RGBA8 would introduce rounding and filtering errors.
"""
import copy

SOURCE_ALPHA_MASK = 32768


def build_mask_mesh(model, mesh_index, build_mesh, max_envelope_weights=3):
    """Return a two-texture mesh, or None when this is not a three-layer case.

Call at the beginning of build_mesh only for source_materials=True, passing
build_mesh itself. Recursion terminates because the adapter has two layers.
The ordinary builder retains skinning, triangulation, PE and UV handling.
The constant source base occupies batch.diffuse; the combiner performs both
alpha-mask interpolations before the ordinary lighting/fade operations.
"""
    bone, dobj, pobj = model.meshes[mesh_index]
    textures, texture = [], dobj.mobject.texture
    while texture:
        textures.append(texture)
        texture = texture.next
    if len(textures) != 3:
        return None
    mesh = model.model.meshes[mesh_index]
    if (len(mesh.uv_layers) != 3 or len(mesh.material.texture_layers) != 3 or
            [t.flags for t in textures] != [0x30010, 0x10010, 0x10010] or
            [t.source for t in textures] != [4, 5, 6] or
            any(t.tev or t.blending != 1 for t in textures)):
        raise ValueError("Unreviewed three-layer source alpha mask")
    image = mesh.material.texture_layers[0].image
    pixels = bytes(image.pixels)
    if (image.width != 4 or image.height != 4 or len(pixels) != 64 or
            any(pixels[i:i+4] != pixels[:4] for i in range(0, 64, 4)) or pixels[3] != 255):
        raise ValueError("Source alpha-mask base must be solid opaque 4x4")
    for mapping in model.materials:
        _, tracks = mapping.get(dobj.address, ({}, {}))
        if tracks.get(textures[0].texture_id, {}).get(1):
            raise ValueError("Source alpha-mask solid base has animated images")

    adapted = copy.copy(model)
    adapted.model = copy.copy(model.model)
    adapted.model.meshes = list(model.model.meshes)
    adapted_mesh = copy.copy(mesh)
    adapted_mesh.uv_layers = list(mesh.uv_layers[1:])
    adapted_mesh.material = copy.copy(mesh.material)
    adapted_mesh.material.texture_layers = list(mesh.material.texture_layers[1:])
    adapted.model.meshes[mesh_index] = adapted_mesh
    adapted_dobj = copy.copy(dobj)
    adapted_dobj.mobject = copy.copy(dobj.mobject)
    masks = [copy.copy(t) for t in textures[1:]]
    for i, mask in enumerate(masks):
        # Route through the reviewed two-UV builder, then restore the source
        # expression below. Texture identity and SRT animation stay original.
        mask.flags, mask.source = 0x30010, 4+i
        mask.next = masks[i+1] if i+1 < len(masks) else None
    adapted_dobj.mobject.texture = masks[0]
    adapted.meshes = list(model.meshes)
    adapted.meshes[mesh_index] = (bone, adapted_dobj, pobj)
    result = build_mesh(adapted, mesh_index, max_envelope_weights=max_envelope_weights,
                        source_materials=True)
    result["model"], result["dobj"] = model, dobj
    result["raw_textures"] = textures[1:]
    result["batch"][3] |= SOURCE_ALPHA_MASK
    result["batch"][8:11] = [v/255 for v in pixels[:3]]
    return result
