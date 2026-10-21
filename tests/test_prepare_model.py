"""Synthetic converter checks; no proprietary originals or Blender required."""
import importlib.util
import math
import struct
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace as Obj

SPEC = importlib.util.spec_from_file_location(
    "prepare_model", Path(__file__).resolve().parents[1] / "tools/prepare_model.py")
MODEL = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODEL)


class PrepareModelTests(unittest.TestCase):
    def sample(self):
        vertex = ((1.0, 2.0, 3.0), (0.25, 0.75), 0, (255, 255, 255, 255))
        matrix = [1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0.]
        return dict(bones=1, vertices=[vertex] * 3, batches=[(0, 3, 0, 2)],
                    textures=[(8, 8, bytes([255] * 256), 0, 1)],
                    frames=[[matrix], [matrix]], fps=30.,
                    bounds=[0., 0., 0., 3., 3., 3.], archive_sha=bytes(range(32)))

    def test_binary_offsets_sizes_and_reserved_bytes(self):
        data = MODEL.encode_pack(**self.sample())
        self.assertEqual(data[:4], b"C3M1")
        self.assertEqual(struct.unpack_from("<7I", data, 4), (1, len(data), 1, 3, 1, 1, 2))
        offsets = struct.unpack_from("<5I", data, 36)
        self.assertEqual(offsets, (128, 224, 240, 264, 360))
        self.assertEqual(data[80:112], bytes(range(32)))
        self.assertEqual(data[112:128], bytes(16))
        self.assertEqual(struct.unpack_from("<I", data, 156)[0], 0)
        self.assertEqual(struct.unpack_from("<6I", data, offsets[2]), (8, 8, 360, 256, 0, 1))
        self.assertEqual(len(data), 616)

    def test_fan_retains_winding_uv_seams_and_converts_v(self):
        faces = [[0, 1, 2, 3], [0, 2, 4]]
        uvs = [(i, i / 10) for i in range(7)]
        triangles = list(MODEL.triangulate(faces, uvs))
        self.assertEqual([v for v, _uv in triangles], [0, 1, 2, 0, 2, 3, 0, 2, 4])
        self.assertEqual(triangles[0][1], (0, 1.))
        self.assertEqual(triangles[6][1], (4, .6))
        with self.assertRaises(ValueError):
            list(MODEL.triangulate(faces, uvs[:-1]))

    def test_texture_row_flip_and_resampling_preserve_whole_uv_domain(self):
        # IR bottom row red, top row blue.
        pixels = bytes([255, 0, 0, 255, 0, 0, 255, 255])
        top_down = MODEL.rgba_top_down(pixels, 1, 2)
        self.assertEqual(top_down[:4], bytes([0, 0, 255, 255]))
        w, h, image = MODEL.resize_nearest(top_down, 1, 2)
        self.assertEqual((w, h, len(image)), (8, 8, 256))
        self.assertEqual(image[:4], top_down[:4])
        self.assertEqual(image[-4:], top_down[-4:])
        w, h, _image = MODEL.resize_nearest(bytes(64 * 144 * 4), 64, 144)
        self.assertEqual((w, h), (64, 256))

    def test_rejects_invalid_runtime_data(self):
        for key, value in (("bones", 129), ("fps", math.nan), ("bounds", [1] * 3 + [0] * 3),
                           ("batches", [(1, 3, 0, 0)]), ("frames", [[[]]]),
                           ("textures", [(9, 8, bytes(9 * 8 * 4), 0, 0)])):
            with self.subTest(key=key), self.assertRaises(ValueError):
                sample = self.sample()
                sample[key] = value
                MODEL.encode_pack(**sample)

    def test_output_is_exclusive_and_private(self):
        with self.assertRaises(ValueError):
            MODEL.check_output(MODEL.ROOT / "bad-model.c3m")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "model.c3m"
            self.assertEqual(MODEL.check_output(path), path.resolve())
            path.with_suffix(".c3m.json").write_text("{}")
            with self.assertRaises(ValueError):
                MODEL.check_output(path)

    def test_geometry_rejects_multilayer_and_multiple_bone_influences(self):
        image = Obj(image_id=1, palette_id=0, pixels=bytes([255] * 256), width=8, height=8, name="synthetic")
        layer = Obj(image=image, coord_type=Obj(name="UV"), uv_index=0, is_bump=False,
                    combiner=None, rotation=(0, 0, 0), translation=(0, 0, 0), scale=(1, 1, 1),
                    repeat_s=1, repeat_t=1, wrap_s=Obj(name="CLAMP"), wrap_t=Obj(name="REPEAT"))
        mesh = Obj(is_hidden=False, material=Obj(texture_layers=[layer]), shape_keys=[], color_layers=[],
                   bone_weights=Obj(type=Obj(name="WEIGHTED"), assignments=[(i, [("bone", 1)]) for i in range(3)]),
                   vertices=[(0, 0, 0), (1, 0, 0), (0, 1, 0)], faces=[[0, 1, 2]],
                   uv_layers=[Obj(uvs=[(0, 0), (1, 0), (0, 1)])])
        model = Obj(bones=[Obj(name="bone")], meshes=[mesh])
        vertices, batches, textures, _info = MODEL.geometry(model)
        self.assertEqual((len(vertices), len(batches), len(textures)), (3, 1, 1))
        self.assertEqual(vertices[0][1], (0, 1.))
        mesh.material.texture_layers.append(layer)
        with self.assertRaisesRegex(ValueError, "exactly one texture"):
            MODEL.geometry(model)
        mesh.material.texture_layers.pop()
        mesh.bone_weights.assignments[0] = (0, [("bone", .5), ("bone", .5)])
        with self.assertRaisesRegex(ValueError, "one influence"):
            MODEL.geometry(model)

    def test_pose_fidelity_detects_bind_translation_and_nonfinite_results(self):
        sample = self.sample()
        expected = sample["frames"][0]
        actual = [list(expected[0])]
        actual[0][3] = 1e-6
        matrix_error, vertex_error = MODEL.verify_pose_frame(actual, expected, sample["vertices"], 7)
        self.assertAlmostEqual(matrix_error, 1e-6)
        self.assertAlmostEqual(vertex_error, 1e-6)
        actual[0][3] = .01
        with self.assertRaisesRegex(ValueError, "fidelity failed at frame 7"):
            MODEL.verify_pose_frame(actual, expected, sample["vertices"], 7)
        actual[0][3] = math.nan
        with self.assertRaisesRegex(ValueError, "non-finite"):
            MODEL.verify_pose_frame(actual, expected, sample["vertices"], 7)


if __name__ == "__main__":
    unittest.main()
