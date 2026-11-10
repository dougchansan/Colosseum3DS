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
        self.assertEqual(grouped, list(range(profile["models"])))
        # REL15 publishes its entrances in section 3, like REL131.
        rel = bytearray(0x200)
        rel[0:4] = (15).to_bytes(4, "big")
        rel[12:16] = (10).to_bytes(4, "big")
        rel[16:20] = (0x40).to_bytes(4, "big")
        rel[0x40 + 24:0x40 + 32] = (0x100).to_bytes(4, "big") + (16).to_bytes(4, "big")
        rel[0x100:0x104] = b"\x00\xb4\x00\x00"
        self.assertEqual(bytes(stage.rel_data(bytes(rel), 15)[:4]), b"\x00\xb4\x00\x00")

    def test_sky_schedule_uses_exclusive_loop_and_sparse_samples(self):
        frames, fps = floor.schedule_frames("sky", 24999)
        self.assertEqual(len(frames), 1001)
        self.assertAlmostEqual(frames[-1], 24999)
        self.assertAlmostEqual((len(frames) - 1) / fps, 24999 / 30)


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
