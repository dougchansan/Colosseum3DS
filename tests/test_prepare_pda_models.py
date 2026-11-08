"""PDA descriptor bounds and unsupported saved-form regressions."""
from pathlib import Path
import struct
import sys
import unittest
import types
import importlib.util
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare_pda_models as pda


class PdaPreviewTests(unittest.TestCase):
    @unittest.skipUnless(importlib.util.find_spec("numpy"), "NumPy is required by the optional geometry exporter")
    def test_four_source_influences_bake_to_one_exact_affine_track(self):
        import numpy as np
        from prepare_field_geometry import Model, _sample_descriptor
        model=Model.__new__(Model)
        model.rest=np.asarray([np.eye(4) for _ in range(4)])
        model.invbind=np.asarray([np.eye(4) for _ in range(4)])
        animated=model.rest.copy()
        for i in range(4):
            animated[i,:3,3]=[i,i*2,-i]
        weights=((0,.1),(1,.4),(2,.4),(3,.1))
        actual=_sample_descriptor(model,("skin",0,weights),[(animated,None)])
        expected=sum(w*animated[i] for i,w in weights)
        np.testing.assert_allclose(actual[0],expected[:3],rtol=1e-6)

    def test_shared_position_corners_keep_original_coordinates_and_envelopes(self):
        positions=[(1.,2.,3.),(4.,5.,6.),(7.,8.,9.)]
        faces=[[0,1,2],[0,2,1]]
        matrix_faces=[[0,0,0],[1,0,0]]
        obj=types.SimpleNamespace(sources=[positions[:],[0,3]],
            face_lists=[[f[:] for f in faces],matrix_faces],property=[object(),object()],
            find_attribute_index=lambda attribute: attribute)
        expected=[[(positions[p],obj.sources[1][m]) for p,m in zip(f,ms)] for f,ms in zip(faces,matrix_faces)]
        self.assertEqual(pda.split_envelope_positions(obj,0,1),1)
        actual=[[(obj.sources[0][p],obj.sources[1][m]) for p,m in zip(f,ms)] for f,ms in zip(obj.face_lists[0],matrix_faces)]
        self.assertEqual(actual,expected)
        self.assertNotEqual(obj.face_lists[0][0][0],obj.face_lists[0][1][0])
        self.assertEqual(pda.split_envelope_positions(obj,0,1),0)

    def test_descriptor_split_part_order_and_bounds(self):
        data = pda.descriptor(196, [{"file": "idle_0.c3t"}, {"file": "idle_1.c3t"}], [-1., 0., -2., 1., 3., 2.])
        self.assertEqual(struct.unpack_from("<4s4I", data), (b"C3PD", 2, 160, 196, 2))
        self.assertEqual(data[44:50], bytes((0,1,2,127,127,127)))
        self.assertEqual(data[64:112].split(b"\0")[0], b"idle_0.c3t")
        self.assertEqual(data[112:].split(b"\0")[0], b"idle_1.c3t")

    def test_descriptor_rejects_unsafe_parts_and_invalid_bounds(self):
        for name in ("../idle_0.c3t", "idle_a.c3t", "idle_0.c3t/evil"):
            with self.assertRaises(ValueError):
                pda.descriptor(196, [{"file": name}], [0.] * 6)
        for bounds in ([0., 0., 0., -1., 1., 1.], [float("nan")] * 6):
            with self.assertRaises(ValueError):
                pda.descriptor(196, [{"file": "idle_0.c3t"}], bounds)
        with self.assertRaises(ValueError):
            pda.descriptor(196, [{"file": "idle_0.c3t"}] * 17, [0.] * 6)

    def test_saved_form_species_rejected_before_source_access(self):
        for species in (0, 201, 412):
            with self.assertRaises(ValueError):
                pda.species_binding(species, b"", b"", b"")


if __name__ == "__main__":
    unittest.main()
