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


def convert(factor):
    first = NS(flags=0x40010, source=4, tev=None)
    second = NS(flags=0x30081, source=5, tev=None, blending=factor)
    first.next = second
    second.next = None
    mesh = NS(bone_weights=NS(type=NS(value="RIGID")), shape_keys=[],
              uv_layers=[NS()], faces=[[0, 1, 2]],
              material=NS(texture_layers=[NS(image="base"), NS(image="reflection")]))
    material = NS(texture=first, render_mode=1, pixel_engine_data=None,
                  material=NS(diffuse=NS(asRGBList=lambda: [1., 1., 1.])))
    dobj = NS(mobject=material, address=123)
    model = NS(model=NS(meshes=[mesh]), meshes=[(0, dobj, None)], constant_material_colors={})
    batch = [0] * 28
    batch[1:6] = [3, 0, 33, 4, 0]
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


if __name__ == "__main__":
    unittest.main()
