"""Synthetic field conversion regressions; no original assets/parser needed."""
import math
from pathlib import Path
import struct
import sys
from types import SimpleNamespace as Obj
import unittest

try:
    import numpy as np
except ImportError:
    np=None

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
import prepare_outskirt as stage
if np is not None:
    import prepare_field_geometry as geometry


def archive_fixture(payload,unpacked):
    packed=b"LZSS"+struct.pack(">III",unpacked,16+len(payload),0)+payload
    data=bytearray(128+len(packed))
    data[:4]=b"FSYS"
    for offset,value in ((12,1),(24,64),(32,len(data)),(64,72),(72,80),
                         (80,0x12340200),(84,128),(88,unpacked),(92,0x80000000),(100,len(packed))):
        struct.pack_into(">I",data,offset,value)
    data[128:]=packed
    return data


def collision_fixture():
    data=bytearray(200)
    struct.pack_into(">II",data,0,16,1)
    struct.pack_into(">9f",data,16,0,0,0,0,0,0,1,1,1)
    struct.pack_into(">II",data,16+36,80,88)
    struct.pack_into(">4I",data,80,96,1,148,1)
    struct.pack_into(">12fHH",data,96,0,10,0,10,10,0,0,10,10,0,1,0,0x1234,77)
    struct.pack_into(">12fHH",data,148,0,0,0,0,10,0,0,0,10,1,0,0,7,88)
    return data


class OutskirtPreparationTests(unittest.TestCase):
    def test_lzss_overlapping_copy_and_zero_history(self):
        self.assertEqual(stage.member(archive_fixture(b"\x01A\xee\xf2",6),0x12340200),b"AAAAAA")
        self.assertEqual(stage.member(archive_fixture(b"\0\0\x03",6),0x12340200),bytes(6))

    def test_lzss_rejects_short_flags_references_and_output_overflow(self):
        for payload,size in ((b"",1),(b"\0\0",3),(b"\x01",1),(b"\0\0\x03",5)):
            with self.subTest(payload=payload),self.assertRaises(ValueError):
                stage.member(archive_fixture(payload,size),0x12340200)
        with self.assertRaises(ValueError):
            stage.member(archive_fixture(b"\x01A",1),0xFFFFFFFF)

    def test_collision_preserves_source_normals_flags_ids_and_distinct_tables(self):
        walks,walls=stage.collision(collision_fixture())
        self.assertEqual(len(walks),1);self.assertEqual(len(walls),1)
        self.assertEqual(walks[0][:3],(0,1,0))
        self.assertEqual(walks[0][9:],(0,1,0,0x1234,77,0,0))
        self.assertEqual(walls[0][9:],(1,0,0,7,88,0,0))

    def test_collision_rejects_nonstatic_objects_bad_normals_and_extents(self):
        for offset,fmt,value in ((16,">f",1),(16+60,">H",1),(84,">I",4097),
                                 (80,">I",0xFFFFFFF0),(96+40,">f",2)):
            data=collision_fixture();struct.pack_into(fmt,data,offset,value)
            with self.subTest(offset=offset),self.assertRaises(ValueError):stage.collision(data)

    @unittest.skipUnless(np is not None,"NumPy is required for the offline numeric exporter")
    def test_no_pole_ik_uses_parent_origin_and_source_bend_sign(self):
        initial=np.eye(4);initial[:3,3]=(100,200,300)
        origin=np.array((2.,3.,4.));target=origin+np.array((1.,0.,0.))
        for flip,sign in ((False,1),(True,-1)):
            upper=geometry.solve_ik_upper(initial,origin,target,1,1,(1,1,1),flip)
            lower=geometry.solve_ik_lower(upper,target,1,(1,1,1))
            np.testing.assert_allclose(upper[:3,3],origin)
            np.testing.assert_allclose(lower[:3,3],origin+(0.5,sign*math.sqrt(.75),0))
            np.testing.assert_allclose(lower[:3,3]+lower[:3,0],target,atol=1e-10)
            self.assertAlmostEqual(np.linalg.det(upper[:3,:3]),1)

    @unittest.skipUnless(np is not None,"NumPy is required for the offline numeric exporter")
    def test_ik_unreachable_target_extends_without_stretching_source_lengths(self):
        upper=geometry.solve_ik_upper(np.eye(4),np.zeros(3),np.array((4.,0.,0.)),1,1,(1,1,1),False)
        lower=geometry.solve_ik_lower(upper,np.array((4.,0.,0.)),1,(1,1,1))
        np.testing.assert_allclose(lower[:3,3]+lower[:3,0],(2,0,0))
        with self.assertRaises(ValueError):
            geometry.solve_ik_upper(np.eye(4),np.zeros(3),np.zeros(3),1,1,(1,1,1),False)

    @unittest.skipUnless(np is not None,"NumPy is required for the offline numeric exporter")
    def test_single_envelope_omits_inverse_bind_but_multiweight_uses_it(self):
        model=geometry.Model.__new__(geometry.Model)
        model.model=Obj(bones=[Obj(flags=2)])
        model.invbind=[np.eye(4),np.eye(4)]
        model.invbind[0][0,3]=-7
        world=np.asarray([np.eye(4),np.eye(4)])
        world[0][0,3]=10;world[1][0,3]=6
        self.assertEqual(model.deform(("skin",0,((0,1.),)),world)[0,3],10)
        self.assertEqual(model.deform(("skin",0,((0,.5),(1,.5))),world)[0,3],4.5)

    @unittest.skipUnless(np is not None,"NumPy is required for the offline numeric exporter")
    def test_uv_affine_retains_rotation_and_gx_ab11_constant(self):
        diagonal,cross=geometry.texture_affine((2,4),(.25,-.5,0),(8,4),(0,0,0))
        np.testing.assert_allclose(diagonal,(4,1,-1,.5));np.testing.assert_allclose(cross,(0,0))
        diagonal,cross=geometry.texture_affine((1,1),(0,0,0),(1,1),(0,0,math.pi/2))
        np.testing.assert_allclose(diagonal,(0,0,0,0),atol=1e-15)
        np.testing.assert_allclose(cross,(1,-1))
        diagonal,cross=geometry.texture_affine((1,1),(0,0,0),(1,1),(0,math.pi/2,0))
        self.assertAlmostEqual(diagonal[2],1) # UV source's third component is 1.

    @unittest.skipUnless(np is not None,"NumPy is required for the offline numeric exporter")
    def test_loop_sample_period_differs_from_inclusive_movement_query(self):
        self.assertEqual(len(geometry.sample_sequence(30)),61)
        self.assertEqual(len(geometry.sample_sequence(17)),35)
        self.assertEqual(geometry.sample_sequence(17)[-1]/30,17/30)
        with self.assertRaises(ValueError):geometry.sample_sequence(.2)

    @unittest.skipUnless(np is not None,"NumPy is required for the offline numeric exporter")
    def test_custom_pixel_engine_preserves_original_depth_write(self):
        pe=Obj(flags=49,type=1,source_factor=4,destination_factor=5,z_comp=3)
        base=geometry.title.CULL_BACK|geometry.title.LIT|geometry.title.NO_DEPTH_WRITE
        flags=geometry.pixel_engine_flags(pe,base)
        self.assertTrue(flags&geometry.title.TRANSLUCENT)
        self.assertFalse(flags&geometry.title.NO_DEPTH_WRITE)
        self.assertTrue(flags&geometry.title.LIT)
        pe.flags=25
        self.assertTrue(geometry.pixel_engine_flags(pe,base)&geometry.title.NO_DEPTH_WRITE)
        pe.flags=57;pe.type=0
        flags=geometry.pixel_engine_flags(pe,base|geometry.title.TRANSLUCENT)
        self.assertFalse(flags&(geometry.title.TRANSLUCENT|geometry.title.NO_DEPTH_WRITE))
        pe.flags=53 # Destination alpha is not represented by these packs.
        with self.assertRaises(ValueError):geometry.pixel_engine_flags(pe,base)


if __name__=="__main__":
    unittest.main()
