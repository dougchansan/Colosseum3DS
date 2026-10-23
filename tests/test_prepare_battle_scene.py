"""Synthetic battle asset bounds and affine-envelope regressions."""
import importlib.util
from pathlib import Path
import struct
import sys
import types
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare_battle_scene as battle


class BattleSceneTests(unittest.TestCase):
    def scene_records(self):
        return [{"file": "pokemon.c3t", "actor_slot": i, "idle_clip": 0,
                 "roles": [0] + [battle.UNSUPPORTED] * 8,
                 "transform": [0., 0., 0., 0., 1.]} for i in range(6)]

    def test_scene_descriptor_layout_and_independent_shared_actor(self):
        records = self.scene_records()
        records[3]["shared_from"] = 2
        data = battle.encode_scene([0.] * 12, records)
        self.assertEqual(struct.unpack_from("<4s4I", data), (b"C3BV", 1, 896, 6, 6))
        self.assertEqual(struct.unpack_from("<II", data, 128 + 3 * 128 + 48), (3, 2))
        self.assertEqual(data[80:128], bytes(48))
        self.assertEqual(data[128 + 116:128 + 128], bytes(12))

    def test_source_role_entries_resolve_to_clips_and_keep_zigzagoon_independent(self):
        def actor(name):
            return {"packs": [{"file": name}], "animation_entries": [
                {"clips": [i + 1]} for i in range(17)]}
        report = {"packs": [{"file": "arena0.c3t"}],
                  "pokemon": [actor("umbreon.c3t"), actor("espeon.c3t"), actor("zig.c3t")],
                  "trainers": [actor("wes.c3t"), actor("willie.c3t")]}
        records = battle.scene_records(report)
        self.assertEqual(records[1]["roles"][:6], [1, 3, 2, 9, 11, 1])
        self.assertEqual(records[4]["shared_from"], 3)
        self.assertNotEqual(records[3]["transform"], records[4]["transform"])
        self.assertEqual(records[5]["roles"][-2:], [6, 11])
        self.assertEqual(records[5]["roles"][6], battle.UNSUPPORTED)
        self.assertEqual(battle.MOVE_ENTRIES, {44: 2, 290: 1, 269: 0, 289: 1,
            93: 1, 216: 2, 115: 0, 270: 1, 33: 2, 45: 1, 39: 0, 165: 2})

    def test_scene_descriptor_rejects_missing_actor_bad_share_and_paths(self):
        records = self.scene_records()
        with self.assertRaises(ValueError):
            battle.encode_scene([0.] * 12, records[:5])
        for key, value in (("shared_from", 1), ("file", "../pokemon.c3t"),
                           ("transform", [0., 0., 0., 0., 0.])):
            records = self.scene_records()
            records[0][key] = value
            with self.assertRaises(ValueError):
                battle.encode_scene([0.] * 12, records)

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "NumPy required for geometry converter")
    def test_duplicate_image_tables_compare_content_and_reject_distinct_pixels(self):
        from unittest.mock import patch
        import prepare_field_geometry as geometry
        image_a = types.SimpleNamespace(pixels=b"abcd", address=1)
        image_b = types.SimpleNamespace(pixels=b"abcd", address=2)
        model = types.SimpleNamespace(image_swaps=[{(16, 0): [image_a]}, {(16, 0): [image_b]}])
        mesh = {"model": model, "dobj": types.SimpleNamespace(address=16),
                "raw_textures": [types.SimpleNamespace(image=None)]}
        module = types.SimpleNamespace(_build_ir_image=lambda texture: texture.image)
        with patch.dict(sys.modules, {"importer.phases.describe.helpers.materials": module}), \
             patch.object(geometry, "_texture", side_effect=lambda _t, image: (1, 1, image.pixels, 0, 0)):
            variants = geometry.expand_image_swaps([mesh])
            self.assertEqual(len(variants), 1)
            self.assertEqual(variants[0]["textures"], [(1, 1, b"abcd", 0, 0)])
            image_b.pixels = b"different"
            with self.assertRaisesRegex(ValueError, "distinct"):
                geometry.expand_image_swaps([mesh])

    def toc(self, name=b"arena\0", identity=5):
        data = bytearray(64)
        data[:4] = b"GLLA"
        struct.pack_into(">I", data, 8, 1)
        struct.pack_into(">I", data, 16, 32)
        struct.pack_into(">II", data, 32, identity, 40)
        data[40:40 + len(name)] = name
        return data

    def test_toc_resolves_identity_and_rejects_path(self):
        self.assertEqual(battle.archive_names(self.toc()), {5: "arena.fsys"})
        with self.assertRaises(ValueError):
            battle.archive_names(self.toc(b"../arena\0"))

    def test_toc_rejects_extent_and_count(self):
        for offset, value in ((8, 4097), (16, 63)):
            data = self.toc()
            struct.pack_into(">I", data, offset, value)
            with self.assertRaises(ValueError):
                battle.archive_names(data)

    def test_clip_resolution_checks_actual_indices(self):
        entry = types.SimpleNamespace(anim_type=2, sub_anims=[types.SimpleNamespace(
            motion_type=0, anim_index=1)], timing=(1, 0, 0, 0), damage_flags=0,
            body_map_bones=[0])
        header = types.SimpleNamespace(anim_entries=[entry])
        self.assertEqual(battle.clip_mapping(header, [59, 90])[0]["clips"], [1])
        with self.assertRaises(ValueError):
            battle.clip_mapping(header, [59])

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "NumPy required for affine sampling")
    def test_non_root_envelope_coordinate_cancels(self):
        import numpy as np
        from prepare_field_geometry import Model, _sample_descriptor
        model = Model.__new__(Model)
        model.model = types.SimpleNamespace(bones=[types.SimpleNamespace(flags=0)] * 2)
        model.rest = np.asarray([np.eye(4), np.eye(4)])
        model.invbind = [np.eye(4), np.eye(4)]
        animated = model.rest.copy()
        animated[1, 0, 3] = 3
        descriptor = ("skin", 0, ((1, 1.),))
        actual = _sample_descriptor(model, descriptor, [(animated, None)])
        coordinate = np.eye(4)
        coordinate[1, 3] = 9
        coordinate[0, 0] = 2
        expected = animated[1] @ coordinate @ np.linalg.inv(model.rest[1] @ coordinate)
        np.testing.assert_allclose(actual[0], expected[:3])

        descriptor = ("skin", 0, ((0, .25), (1, .75)))
        actual = _sample_descriptor(model, descriptor, [(animated, None)])
        rest = .25 * model.rest[0] + .75 * model.rest[1]
        animated_blend = .25 * animated[0] + .75 * animated[1]
        expected = animated_blend @ coordinate @ np.linalg.inv(rest @ coordinate)
        np.testing.assert_allclose(actual[0], expected[:3])

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "NumPy required for affine sampling")
    def test_tiny_uniform_scale_is_invertible_but_collapsed_axis_is_not(self):
        import numpy as np
        from prepare_field_geometry import Model, _sample_descriptor
        model = Model.__new__(Model)
        model.rest = np.asarray([np.eye(4)])
        world = model.rest.copy()
        world[0, :3, :3] *= .0001
        actual = _sample_descriptor(model, ("rigid", 0), [(world, None)])
        np.testing.assert_allclose(actual[0, :3, :3], np.eye(3) * .0001)
        world[0, 2, 2] = 0
        with self.assertRaisesRegex(ValueError, "Singular"):
            _sample_descriptor(model, ("rigid", 0), [(world, None)])
        world[0, :3, :3] = 0
        actual = _sample_descriptor(model, ("rigid", 0), [(world, None)])
        np.testing.assert_allclose(actual[0, :3, :3], np.eye(3))

    @unittest.skipUnless(importlib.util.find_spec("numpy"), "NumPy required for affine sampling")
    def test_zero_scale_rigid_batch_is_hidden_without_removing_geometry(self):
        import numpy as np
        import tempfile
        from unittest.mock import patch
        import prepare_field_geometry as geometry
        class Model:
            tracks = []
            raw = types.SimpleNamespace(root_joint=types.SimpleNamespace(address=16))
            rest = [np.eye(4)]
            def world(self, _clip, _frame):
                world = np.eye(4)
                world[:3, :3] = 0
                return [world], [True]
            def deform(self, descriptor, world):
                return world[descriptor[1]]
        model = Model()
        batch = [0] * 28
        batch[1] = 3
        mesh = {"model": model, "descriptors": [("rigid", 0)] * 3, "bone": 0,
                "index": 0, "dobj": types.SimpleNamespace(mobject=types.SimpleNamespace(render_mode=0)),
                "batch": batch, "vertices": [(0.,) * 8 + (255,) * 4 + (0, 0., 0.)] * 3,
                "textures": [(1, 1, b"\xff" * 4, 0, 0)]}
        with tempfile.TemporaryDirectory() as output, \
             patch.object(geometry, "material_state", return_value=(0.,) * 20) as material, \
             patch.object(geometry.title, "encode_pack", return_value=b"synthetic") as encode:
            geometry.prepare_pack_set([mesh], [(0, [0.], False)], 60, [(0.,) * 12],
                [], "00" * 32, Path(output), "zero", expected_count=1, format_version=3)
        self.assertEqual(material.call_args.args[3], [False])
        self.assertEqual(len(encode.call_args.args[1]), 3)



if __name__ == "__main__":
    unittest.main()
