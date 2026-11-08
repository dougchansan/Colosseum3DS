"""Regression coverage for the lossless three-to-two source texture adapter."""
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace as N

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from prepare_pda_mask_layers import SOURCE_ALPHA_MASK, build_mask_mesh


def fixture():
    textures = [N(flags=f, source=4+i, tev=None, blending=1,
                  texture_id=i, next=None) for i, f in enumerate((0x30010, 0x10010, 0x10010))]
    for a, b in zip(textures, textures[1:]):
        a.next = b
    images = [N(width=4, height=4, pixels=bytes((208,168,132,255))*16),
              N(pixels=bytes((10,20,30,0))), N(pixels=bytes((40,50,60,128)))]
    mesh = N(uv_layers=[object(), object(), object()],
             material=N(texture_layers=[N(image=i) for i in images]))
    dobj = N(address=123, mobject=N(texture=textures[0]))
    return N(meshes=[(0, dobj, object())], model=N(meshes=[mesh]), materials=[{}]), textures


class MaskLayersTest(unittest.TestCase):
    def test_keeps_pixels_uvs_and_source_objects(self):
        model, textures = fixture()
        source_mesh = model.model.meshes[0]
        def builder(adapted, index, **kwargs):
            self.assertIsNot(adapted, model)
            self.assertEqual(adapted.model.meshes[0].uv_layers, source_mesh.uv_layers[1:])
            self.assertEqual(adapted.model.meshes[0].material.texture_layers,
                             source_mesh.material.texture_layers[1:])
            t = adapted.meshes[0][1].mobject.texture
            self.assertEqual((t.flags,t.source,t.next.flags,t.next.source),
                             (0x30010,4,0x30010,5))
            self.assertIsNone(t.next.next)
            return {"model":adapted,"dobj":adapted.meshes[0][1],
                    "batch":[0]*28,"textures":[l.image.pixels for l in adapted.model.meshes[0].material.texture_layers]}
        result = build_mask_mesh(model, 0, builder)
        self.assertIs(result["model"], model)
        self.assertIs(result["dobj"], model.meshes[0][1])
        self.assertEqual(result["raw_textures"], textures[1:])
        self.assertEqual(result["batch"][3], SOURCE_ALPHA_MASK)
        self.assertEqual(result["batch"][8:11], [208/255,168/255,132/255])
        self.assertEqual(result["textures"], [l.image.pixels for l in source_mesh.material.texture_layers[1:]])
        self.assertEqual([t.source for t in textures], [4,5,6])
        self.assertEqual([t.flags for t in textures], [0x30010,0x10010,0x10010])
        self.assertEqual(len(source_mesh.uv_layers), 3)

    def test_rejects_nonconstant_base(self):
        model, _ = fixture()
        model.model.meshes[0].material.texture_layers[0].image.pixels = bytes(64)
        with self.assertRaisesRegex(ValueError, "solid opaque"):
            build_mask_mesh(model, 0, None)

    def test_rejects_other_expression_and_animated_base(self):
        model, textures = fixture()
        textures[1].flags = 0x30010
        with self.assertRaisesRegex(ValueError, "Unreviewed"):
            build_mask_mesh(model, 0, None)
        textures[1].flags = 0x10010
        model.materials = [{123: ({}, {0: {1: object()}})}]
        with self.assertRaisesRegex(ValueError, "animated images"):
            build_mask_mesh(model, 0, None)

    def test_other_layer_counts_are_left_to_builder(self):
        model, textures = fixture()
        textures[1].next = None
        self.assertIsNone(build_mask_mesh(model, 0, None))


if __name__ == "__main__":
    unittest.main()
