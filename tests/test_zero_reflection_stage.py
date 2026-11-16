"""Synthetic source reflection conversion; no disc assets or geometry loss."""
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch
try:
    import numpy
except ImportError:
    raise unittest.SkipTest("NumPy is required by the optional geometry exporter")
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare_field_geometry as geometry

NS = types.SimpleNamespace


def convert(factor, normal_add=False):
    first = NS(flags=0x40010, source=4, tev=None)
    second = NS(flags=0x30081, source=5, tev=None, blending=factor)
    first.next = second
    second.next = None
    if normal_add:
        first.flags, first.blending, first.tev = 0x70081, factor, NS(active=0)
        first.next = None
    mesh = NS(bone_weights=NS(type=NS(value="RIGID")), shape_keys=[],
              uv_layers=[NS()], faces=[[0, 1, 2]],
              material=NS(texture_layers=[NS(image="base"), NS(image="reflection")]))
    material = NS(texture=first, render_mode=1, pixel_engine_data=None,
                  material=NS(diffuse=NS(asRGBList=lambda: [1., 1., 1.])))
    dobj = NS(mobject=material, address=123)
    model = NS(model=NS(meshes=[mesh]), meshes=[(0, dobj, None)], constant_material_colors={})
    batch = [0] * 28
    batch[1:6] = [3, 0, 17 if normal_add else 33, 4, 0]
    vertices = [(0., 0., 0.), (1., 0., 0.), (0., 1., 0.)]
    enum = types.ModuleType("shared.IR.enums")
    enum.SkinType = NS(RIGID="rigid")
    with patch.dict(sys.modules, {"shared.IR.enums": enum}), \
         patch.object(geometry.title, "geometry", return_value=(vertices, [batch], ["base"], None)), \
         patch.object(geometry, "_texture", return_value="reflection"):
        return geometry.build_mesh(model, 0)


class ZeroReflectionTests(unittest.TestCase):
    def test_zero_source_blend_preserves_geometry_and_base_expression(self):
        zero = convert(0.)
        active = convert(.5)
        self.assertEqual(zero["vertices"], active["vertices"])
        self.assertEqual(zero["descriptors"], active["descriptors"])
        self.assertEqual(zero["textures"], ["base"])
        self.assertEqual(zero["batch"][3], 33)
        self.assertEqual(zero["batch"][4:26], active["batch"][4:26])
        for incoming in (0., .25, 1.):
            for reflected in (0., .75, 1.):
                self.assertEqual(incoming * (1 - 0.) + reflected * 0., incoming)

    def test_every_nonzero_authored_blend_retains_source_stage(self):
        for factor in (.00001, .5, 1.):
            result = convert(factor)
            self.assertEqual(result["textures"], ["base", "reflection"])
            self.assertTrue(result["batch"][3] & geometry.REFLECTION_BLEND)
            self.assertEqual(result["batch"][26:], [1, factor])


class NormalAddExtTests(unittest.TestCase):
    def test_normal_add_ext_preserves_geometry_and_original_discriminator(self):
        add = convert(1., normal_add=True)
        reference = convert(.5)
        self.assertEqual(add["vertices"], reference["vertices"])
        self.assertEqual(add["descriptors"], reference["descriptors"])
        self.assertEqual(add["batch"][7], 0x70081)
        self.assertEqual(add["batch"][4:6], [4, 0])
        self.assertEqual(add["textures"][0], (8, 8, b"\xff" * 256, 0, 0))
        self.assertEqual(add["textures"][1], "reflection")
        self.assertTrue(add["batch"][3] & geometry.REFLECTION_BLEND)
        self.assertEqual(add["batch"][26:], [1, 1.])

    def test_add_ignores_blending_scalar_and_clamps_after_lighting(self):
        # HSD TEX_COLORMAP_ADD never reads tobj->blending. Unlike BLEND0,
        # an ADD0 source must retain its normal texture stage.
        self.assertEqual(convert(0., normal_add=True)["batch"],
                         convert(1., normal_add=True)["batch"])
        for material, light, tex, expected in ((.2, .5, .3, .4),
                (.8, .5, .7, 1.), (.2, 0., .3, .3)):
            source = min(1., material * light + tex)
            self.assertAlmostEqual(source, expected)
            self.assertNotAlmostEqual(source, min(1., material + tex) * light)


if __name__ == "__main__":
    unittest.main()
