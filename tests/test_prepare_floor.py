"""Synthetic developer-floor conversion regressions; no original assets needed."""
from pathlib import Path
import sys
from types import SimpleNamespace as Obj
import unittest

try:
    import numpy as np
except ImportError:
    np = None

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare_floor as floor
import prepare_outskirt as stage
if np is not None:
    import prepare_field_geometry as geometry


def joint(address, flags=0, child=None, next=None):
    return Obj(address=address, flags=flags, child=child, next=next, property=None)


class FloorProfileTests(unittest.TestCase):
    def test_phenac_profile_matches_lifecycle_row_and_rel_layout(self):
        profile = floor.FLOORS["phenac"]
        self.assertEqual((profile["floor"], profile["resource_group"], profile["lifecycle_row"]), (3, 0x1F, 2))
        self.assertEqual(profile["archive"], "M1_out.fsys")
        self.assertEqual(profile["module"], 15)
        grouped = sorted(i for indices, _ in profile["groups"] for i in indices)
        # Models 0-5 are the six door-table doors (object packs, rows 0-5).
        self.assertEqual(profile["objects"], (0, 1, 2, 3, 4, 5))
        self.assertEqual(profile["door_objects"], {row: row for row in range(6)})
        self.assertEqual(sorted(grouped + list(profile["objects"])), list(range(profile["models"])))
        # REL15 publishes its entrances in section 3, like REL131.
        rel = bytearray(0x200)
        rel[0:4] = (15).to_bytes(4, "big")
        rel[12:16] = (10).to_bytes(4, "big")
        rel[16:20] = (0x40).to_bytes(4, "big")
        rel[0x40 + 24:0x40 + 32] = (0x100).to_bytes(4, "big") + (16).to_bytes(4, "big")
        rel[0x100:0x104] = b"\x00\xb4\x00\x00"
        self.assertEqual(bytes(stage.rel_data(bytes(rel), 15)[:4]), b"\x00\xb4\x00\x00")

    def test_static_schedule_has_one_sample_and_floor_rels_share_one_layout(self):
        self.assertEqual(floor.schedule_frames("static", 0), ([0.], 60))
        for module in (5, 15, 16, 26, 131):
            rel = bytearray(0x200)
            rel[0:4] = module.to_bytes(4, "big")
            rel[12:16] = (10).to_bytes(4, "big")
            rel[16:20] = (0x40).to_bytes(4, "big")
            rel[0x40 + 24:0x40 + 32] = (0x100).to_bytes(4, "big") + (8).to_bytes(4, "big")
            rel[0x100:0x104] = b"\x00\xb4\x00\x00"
            self.assertEqual(bytes(stage.rel_data(bytes(rel), module)), b"\x00\xb4\x00\x00" + bytes(4))
            with self.assertRaises(ValueError):
                stage.rel_data(bytes(rel), module + 1)  # the header names another module
            rel[12:16] = (11).to_bytes(4, "big")
            with self.assertRaises(ValueError):
                stage.rel_data(bytes(rel), module)      # a floor REL has ten sections
        for module in (0, -1, True, 0x10000, "15"):
            with self.assertRaises(ValueError):
                stage.rel_data(bytes(0x200), module)

    def test_runtime_rejected_texture_modes_are_an_error_or_a_listed_omission(self):
        def mesh(index, color, alpha):
            batch = [0] * 28
            batch[4], batch[5] = color, alpha
            return {"index": index, "bone": index + 10, "batch": batch, "vertices": [()] * (3 * (index + 1))}
        meshes = [mesh(0, 4, 0), mesh(1, 2, 0), mesh(2, 3, 2), mesh(3, 0, 1), mesh(4, 0, 3)]
        with self.assertRaisesRegex(ValueError, "texture mode"):
            floor.runtime_meshes(meshes, 2, False, [])
        omitted = []
        kept = floor.runtime_meshes(meshes, 2, True, omitted)
        self.assertEqual([m["index"] for m in kept], [0, 2, 4])
        self.assertEqual(omitted, [
            {"model": 2, "mesh_index": 1, "owner_joint": 11, "vertices": 6, "color_mode": 2, "alpha_mode": 0},
            {"model": 2, "mesh_index": 3, "owner_joint": 13, "vertices": 12, "color_mode": 0, "alpha_mode": 1}])
        # Nothing is omitted, and nothing recorded, when every mesh is loadable.
        clean = []
        self.assertEqual(floor.runtime_meshes(kept, 0, False, clean), kept)
        self.assertEqual(clean, [])
        self.assertEqual(sorted(name for name, row in floor.FLOORS.items()
                                if row.get("omit_unsupported_texture_modes")), ["phenac_pc1f", "phenac_pcb1"])

    def test_interior_manifest_records_objects_limits_and_ccd_object_count(self):
        base = {"developer_only": True, "floor": 13, "limitations": ["old"], "packs": []}
        extras = {"objects": [{"source_models": [1], "file": "x_obj1.c3t", "source_clip_ends": [19.0, 20.0]}],
                  "frozen_material_tracks": {"0": [{"clip": 0, "dobj": "00001234", "channel": 4}]},
                  "path_joints": {"2": [84]}, "omitted_meshes": [{"model": 2, "mesh_index": 13}], "ccd_objects": 10}
        manifest = floor.interior_manifest(base, {"pack_format": 3}, extras)
        self.assertEqual(base["developer_only"], True)  # the input manifest is not mutated
        self.assertEqual((manifest["developer_only"], manifest["scene"], manifest["pack_format"],
                          manifest["ccd_objects"]), (False, "interior", 3, 10))
        self.assertEqual(manifest["objects"], [{"model": 1, "file": "x_obj1.c3t", "source_clip_ends": [19.0, 20.0]}])
        text = " ".join(manifest["limitations"])
        for word in ("frozen_material_tracks", "cardinal-spline", "omitted_meshes", "Object packs"):
            self.assertIn(word, text)
        plain = floor.interior_manifest(base, {}, {"objects": [], "frozen_material_tracks": {}, "path_joints": {},
                                                   "omitted_meshes": [], "ccd_objects": 3})
        self.assertEqual(plain["pack_format"], 2)
        self.assertFalse(any(word in " ".join(plain["limitations"])
                             for word in ("frozen_material_tracks", "cardinal-spline", "omitted_meshes")))

    def test_sky_schedule_uses_exclusive_loop_and_sparse_samples(self):
        frames, fps = floor.schedule_frames("sky", 24999)
        self.assertEqual(len(frames), 1001)
        self.assertAlmostEqual(frames[-1], 24999)
        self.assertAlmostEqual((len(frames) - 1) / fps, 24999 / 30)


def cardinal(xs, lengths=None, tension=.5, kind=3):
    """Collinear, equally spaced control points: a straight line of speed 10."""
    count = len(xs) - 2
    node = type("Spline", (), {})()
    node.flags, node.n, node.f0, node.f1 = kind << 8, count, tension, 10. * (count - 1)
    node.s1 = [[x, 2., -1.] for x in xs]
    node.s2 = [i / (count - 1) for i in range(count)] if lengths is None else lengths
    node.s3 = [[0., 0., 0., 0., 100.] for _ in range(count - 1)]
    return node


@unittest.skipUnless(np is not None, "NumPy is required for the offline numeric exporter")
class PathAndCollapseTests(unittest.TestCase):
    def test_cardinal_path_follows_arc_length_like_spline_c(self):
        spline = geometry.admitted_spline(cardinal([-10., 0., 10., 20., 30.]))
        self.assertEqual((spline["count"], len(spline["cv"]), len(spline["poly"])), (3, 5, 2))
        for value, x in ((0., 0.), (.25, 5.), (.5, 10.), (.75, 15.), (.999, 19.98)):
            point = geometry.path_point(spline, value)
            self.assertAlmostEqual(point[0], x, places=3)
            self.assertAlmostEqual(point[1], 2., places=9)
            self.assertAlmostEqual(point[2], -1., places=9)
        # JObjUpdateFunc clamps the track value; at 1 the last control point is returned.
        self.assertEqual(geometry.path_point(spline, 1.), (20., 2., -1.))
        self.assertEqual(geometry.path_point(spline, 7.), (20., 2., -1.))
        self.assertEqual(geometry.path_point(spline, -.001), (0., 2., -1.))
        self.assertEqual(geometry.spline_parameter(spline, 0.), 0.)
        self.assertEqual(geometry.spline_parameter(spline, 1.), 1.)
        self.assertAlmostEqual(geometry.spline_parameter(spline, .75), .75, places=4)
        with self.assertRaises(ValueError):
            geometry.spline_point(spline, 1.5)

    def test_only_reviewed_well_formed_cardinal_splines_are_admitted(self):
        with self.assertRaisesRegex(ValueError, "no spline object"):
            geometry.admitted_spline(None)
        with self.assertRaisesRegex(ValueError, "spline type"):
            geometry.admitted_spline(cardinal([-10., 0., 10., 20.], kind=1))
        for mutate in (lambda s: s.s1.pop(), lambda s: s.s2.append(2.), lambda s: s.s3.pop(),
                       lambda s: setattr(s, "s3", None), lambda s: setattr(s, "n", 1)):
            spline = cardinal([-10., 0., 10., 20.])
            mutate(spline)
            with self.assertRaisesRegex(ValueError, "Malformed"):
                geometry.admitted_spline(spline)
        for mutate in (lambda s: s.s1[0].__setitem__(0, float("nan")), lambda s: s.s3[0].pop(),
                       lambda s: setattr(s, "f0", float("inf"))):
            spline = cardinal([-10., 0., 10., 20.])
            mutate(spline)
            with self.assertRaisesRegex(ValueError, "Nonfinite"):
                geometry.admitted_spline(spline)
        for mutate in (lambda s: setattr(s, "s2", [.5, 1.]), lambda s: setattr(s, "f1", 0.),
                       lambda s: setattr(s, "s2", [0., -1.])):
            spline = cardinal([-10., 0., 10., 20.])
            mutate(spline)
            with self.assertRaisesRegex(ValueError, "cumulative"):
                geometry.admitted_spline(spline)
        broken = geometry.admitted_spline(cardinal([-10., 0., 10., 20.]))
        broken["poly"] = [(0., 0., 0., 0., -5.)]
        with self.assertRaisesRegex(ValueError, "Negative"):
            geometry.path_point(broken, .5)

    def test_point_and_line_scales_have_no_surface_but_a_flat_plane_does(self):
        cases = ((np.zeros((3, 3)), True), (np.diag((0., 1., 0.)), True), (np.diag((.05, 0., 0.)), True),
                 (np.diag((1., 1., 0.)), False), (np.eye(3), False), (np.eye(3) * 1e-5, False))
        for matrix, collapsed in cases:
            self.assertEqual(bool(geometry.surface_collapsed(matrix)), collapsed)
        stack = np.asarray([np.eye(3), np.diag((0., 2., 0.))])
        self.assertEqual(geometry.surface_collapsed(stack).tolist(), [False, True])

    def test_additive_interior_state_keeps_its_depth_write(self):
        import prepare_title as title
        def state(*values):
            return Obj(flags=values[0], type=values[1], source_factor=values[2],
                       destination_factor=values[3], z_comp=values[4])
        flags = geometry.pixel_engine_flags(state(57, 1, 1, 1, 3), title.LIT)
        self.assertEqual(flags, title.LIT | geometry.ADDITIVE_ONE | title.TRANSLUCENT)
        without_depth = geometry.pixel_engine_flags(state(25, 1, 1, 1, 3), title.LIT)
        self.assertEqual(without_depth, flags | title.NO_DEPTH_WRITE)
        with self.assertRaisesRegex(ValueError, "pixel-engine"):
            geometry.pixel_engine_flags(state(57, 1, 1, 0, 3), 0)


@unittest.skipUnless(np is not None, "NumPy is required for the offline numeric exporter")
class InstanceTests(unittest.TestCase):
    def test_joint_walk_does_not_descend_into_instance_child(self):
        shared = joint(4)
        leaf = joint(5)
        shared.child = leaf
        second = joint(3, geometry.JOBJ_INSTANCE, child=shared, next=shared)
        first = joint(2, geometry.JOBJ_INSTANCE, child=shared, next=second)
        root = joint(1, child=first)
        self.assertEqual([n.address for n in geometry.joint_nodes(root)], [1, 2, 3, 4, 5])
        # Without instances the walk matches prepare_title.tree_nodes exactly.
        plain = joint(1, child=joint(2, child=joint(3)), next=joint(4))
        import prepare_title as title
        self.assertEqual([n.address for n in geometry.joint_nodes(plain)],
                         [n.address for n in title.tree_nodes(plain)])

    def test_instance_draws_shared_subtree_with_instance_times_inverse_child(self):
        model = geometry.Model.__new__(geometry.Model)
        # bones: 0 root, 1 instance, 2 shared subtree root, 3 its child, 4 sibling
        model.model = Obj(bones=[Obj(parent_index=None), Obj(parent_index=0), Obj(parent_index=0),
                                 Obj(parent_index=2), Obj(parent_index=0)])
        model.instances = [(1, 2)]
        world = np.asarray([np.eye(4) for _ in range(5)])
        world[1][:3, 3] = (10, 0, 0)
        world[2][:3, 3] = (2, 0, 0)
        world[3][:3, 3] = (3, 1, 0)
        meshes = [{"model": model, "bone": 3, "descriptors": [("rigid", 3)]},
                  {"model": model, "bone": 4, "descriptors": [("rigid", 4)]}]
        copies = geometry.instance_meshes(model, meshes)
        self.assertEqual(len(copies), 1)  # the shared root's sibling is not drawn
        descriptor = copies[0]["descriptors"][0]
        self.assertEqual(descriptor, ("instance", 1, 2, ("rigid", 3)))
        self.assertEqual(model.base_descriptor(descriptor), ("rigid", 3))
        self.assertEqual(tuple(model.deform(descriptor, world)[:3, 3]), (11, 1, 0))
        self.assertEqual(copies[0]["instance"], 1)

    def test_baked_mesh_applies_sample_transform_and_has_identity_track(self):
        model = geometry.Model.__new__(geometry.Model)
        model.model = Obj(bones=[Obj(parent_index=None)])
        model.tracks = [[{}]]
        model.rest = np.asarray([np.eye(4)])
        moved = np.eye(4)
        moved[:3, :3] = ((0, 0, 1), (0, 1, 0), (-1, 0, 0))  # quarter turn about Y
        moved[:3, 3] = (5, 0, 0)
        model.world = lambda clip, frame: (np.asarray([moved]), [True])
        mesh = {"model": model, "bone": 0, "descriptors": [("rigid", 0)],
                "vertices": [(1., 0., 0., 1., 0., 0., .5, .5, 255, 255, 255, 255, 0, 0., 0.)]}
        baked = geometry.bake_mesh(mesh, 0, 0.)
        vertex = baked["vertices"][0]
        np.testing.assert_allclose(vertex[:3], (5, 0, -1), atol=1e-12)
        np.testing.assert_allclose(vertex[3:6], (0, 0, -1), atol=1e-12)
        self.assertEqual(vertex[6:], mesh["vertices"][0][6:])
        self.assertEqual(baked["descriptors"], [("baked",)])
        np.testing.assert_array_equal(model.deform(("baked",), np.asarray([moved])), np.eye(4))
        collapsed = np.zeros((4, 4))
        model.world = lambda clip, frame: (np.asarray([collapsed]), [True])
        self.assertIsNone(geometry.bake_mesh(mesh, 0, 0.))


if __name__ == "__main__":
    unittest.main()
