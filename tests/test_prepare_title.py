"""Synthetic title conversion checks. No originals, Blender or parser required."""
import importlib.util
import math
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace as Obj
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("prepare_title",Path(__file__).resolve().parents[1]/"tools/prepare_title.py")
TITLE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TITLE)


def key(frame,value,mode="LINEAR",slope_in=0,slope_out=0):
    return Obj(frame=frame,value=value,interpolation=mode,slope_in=slope_in,slope_out=slope_out)


class TitlePreparationTests(unittest.TestCase):
    def fixture(self):
        vertex=(1.,2.,3.,0.,1.,0.,.25,.75,11,22,33,255,0)
        batch=(0,3,0,1,4,3,0,0,*([1.]*9),50.,1.,1.,0,0,7,7,0,3,0,0)
        light=(4,0,1.,1.,1.,0.,0.,0.,0.,0.,0.,0.,1.,1.,0.,0.)
        matrix=(1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.)
        material=(1.,1.,0.,0.,1.,1.,0.,0.,0.,1.,1.,1.)
        camera=(0.,0.,5.,0.,0.,0.,0.,45.,.1,100.,4/3,0.)
        frame=struct.pack("<36f",*matrix,*material,*camera)
        return dict(bones=1,vertices=[vertex]*3,batches=[batch],textures=[(8,8,bytes([255])*256,0,1)],
                    clips=[(0,2,1,0)],lights=[light],frames=frame*2,fps=60.)

    def test_canonical_little_endian_layout(self):
        data=TITLE.encode_pack(**self.fixture())
        self.assertEqual(struct.unpack_from("<4s9I",data),(b"C3T1",1,len(data),1,3,1,1,1,2,1))
        self.assertEqual(struct.unpack_from("<f9I",data,40),(60.,128,248,360,384,400,464,752,144,256))
        self.assertEqual(data[80:112],bytes.fromhex(TITLE.ARCHIVE_SHA256))
        self.assertEqual(data[112:128],bytes(16))
        self.assertEqual(struct.unpack_from("<6I",data,360),(8,8,752,256,0,1))
        self.assertEqual(struct.unpack_from("<4I",data,384),(0,2,1,0))
        self.assertEqual(len(data),1008)

    def test_curve_endpoints_constant_linear_and_duplicate_keys(self):
        keys=[key(0,1,"CONSTANT"),key(4,2),key(8,6)]
        self.assertEqual(TITLE.curve_value(keys,-10),1)
        self.assertEqual(TITLE.curve_value(keys,3),1)
        self.assertEqual(TITLE.curve_value(keys,6),4)
        self.assertEqual(TITLE.curve_value(keys,20),6)
        self.assertEqual(TITLE.curve_value([],2,7),7)
        self.assertEqual(TITLE.curve_value([key(0,1),key(0,3),key(1,5)],0),3)

    def test_hsd_asymmetric_slopes_belong_to_the_right_key(self):
        keys=[key(0,0,"BEZIER",999,999),key(2,10,"CONSTANT",3,-1)]
        # Hermite midpoint: 0.5*10 + 0.125*2*3 - 0.125*2*(-1).
        self.assertAlmostEqual(TITLE.curve_value(keys,1),6)
        self.assertEqual(TITLE.curve_value(keys,0),0)
        self.assertEqual(TITLE.curve_value(keys,2),10)

    def test_native_material_alpha_and_tev_quantization(self):
        self.assertEqual(TITLE.material_alpha(0),1)
        self.assertEqual(TITLE.material_alpha(1),0)
        self.assertEqual(TITLE.material_alpha(2),0)
        self.assertEqual(TITLE.material_alpha(-1),1)
        self.assertEqual(TITLE.tev_byte(.5),127/255)
        self.assertEqual(TITLE.tev_byte(2),1)
        self.assertEqual(TITLE.tev_byte(-1),0)

    def test_uv_inverse_scale_repeat_and_negative_translation(self):
        self.assertEqual(TITLE.texture_transform((2,4,1),(.25,-.5,0),(8,4)),(4,1,-1,.5))
        with self.assertRaises(ValueError):
            TITLE.texture_transform((0,1,1),(0,0,0),(1,1))

    def test_node_and_branch_visibility_are_distinct(self):
        bones=[Obj(parent_index=None,is_hidden=False),Obj(parent_index=0,is_hidden=True),
               Obj(parent_index=1,is_hidden=True),Obj(parent_index=0,is_hidden=False)]
        tracks=[{}, {12:[key(0,1)]}, {11:[key(0,0)]},{}]
        self.assertEqual(TITLE.visibility_at(bones,tracks,0),[True,True,False,True])
        tracks=[{11:[key(0,0)]},{},{},{}]
        self.assertEqual(TITLE.visibility_at(bones,tracks,0),[False,False,False,True])

    def test_rejects_bad_source_before_decompression(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"title.fsys"
            path.write_bytes(b"FSYS")
            with patch.object(TITLE.hashlib,"sha256",side_effect=AssertionError("size must reject first")):
                with self.assertRaises(ValueError): TITLE.admit_archive(path)
            path.write_bytes(bytes(TITLE.ARCHIVE_BYTES))
            with self.assertRaisesRegex(ValueError,"differs from the reviewed"):
                TITLE.admit_archive(path)

    def test_rejects_invalid_pack_counts_extents_and_floats(self):
        for name,value in (("bones",65),("fps",math.nan),("frames",b""),("clips",[(1,2,1,0)]),
                           ("textures",[(9,8,bytes(288),0,0)])):
            with self.subTest(name=name),self.assertRaises(ValueError):
                data=self.fixture();data[name]=value;TITLE.encode_pack(**data)
        data=self.fixture();bad=list(data["vertices"][0]);bad[0]=math.inf;data["vertices"]=[bad]*3
        with self.assertRaises(ValueError): TITLE.encode_pack(**data)
        data=self.fixture();frame=bytearray(data["frames"]);struct.pack_into("<f",frame,0,math.nan);data["frames"]=frame
        with self.assertRaises(ValueError): TITLE.encode_pack(**data)


if __name__=="__main__":
    unittest.main()
