"""Synthetic camera profile/bounds preparation checks; no original asset bytes."""
import copy
import hashlib
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import prepare_battle_camera as camera
class CameraPreparationTests(unittest.TestCase):
    def profiles(self):
        return [dict(flags=flags,pattern=index,fov_mask=mask,entry=entry,owner_start=0 if entry==2 else 22) for _,_,_,flags,index,mask,entry in camera.WZX]
    def actors(self):
        return [dict(species=species,selector=-1,minimum=[-3.,0.,-3.],maximum=[3.,10.,3.],timing=[[72,100,160,0],[190,358,0,0]],part=[(42,62,53)[i]]*2,kind=[4,3],tracks=[[[0,0,0],[0,1,0]]]*2) for i,(species,_,_) in enumerate(camera.ACTORS)]
    def test_binary_layout_and_checksum(self):
        b=camera.encode(self.profiles(),self.actors())
        self.assertEqual(len(b),656);self.assertEqual(b[:4],b'C3BC')
        self.assertEqual(struct.unpack_from('<4I',b,64),(0x201048,6,4,2))
        self.assertEqual(struct.unpack_from('<4I',b,128),(0x200848,5,2,8))
        self.assertEqual(struct.unpack_from('<Ii',b,192),(197,-1))
        self.assertEqual(b[480:500],hashlib.sha1(b[:480]+bytes(20)+b[500:]).digest())
        self.assertEqual(b[500:512],bytes(12))
    def test_count_selector_part_timing_and_bound_admission(self):
        for key,value in [('selector',0),('species',25),('minimum',[float('nan'),0,0]),('maximum',[-5,0,0]),('part',[1,0]),('kind',[2,3]),('timing',[[0,1,0,0],[1,2,0,0]])]:
            with self.subTest(key=key):
                actors=self.actors();actors[0][key]=value
                with self.assertRaises(ValueError):camera.encode(self.profiles(),actors)
        for count in (0,1,2,4):
            with self.assertRaises(ValueError):camera.encode(self.profiles(),(self.actors()*2)[:count])
    def test_track_bounds_and_finite_samples(self):
        for track in ([],[[0,0,0]]*1026,[[float('nan'),0,0]],[[0,0]],[[1001,0,0]]):
            actors=self.actors();actors[0]['tracks']=[track,[[0,0,0]]]
            with self.assertRaises(ValueError):camera.encode(self.profiles(),actors)
    def test_source_profile_rejects_external_camera_and_unknown_flags(self):
        for _,_,_,flags,index,mask,entry in camera.WZX:
            b=bytearray(160);struct.pack_into('>6I',b,112,entry,5,flags,2,5,0)
            self.assertEqual(camera.profile(b,flags,index,mask,entry)['target_body_map_index'],2)
            for offset in (112,116,120,124,128,132,136,140,159):
                changed=bytearray(b);changed[offset]^=1
                with self.subTest(offset=offset):
                    with self.assertRaises(ValueError):camera.profile(changed,flags,index,mask,entry)
            with self.assertRaises(ValueError):camera.profile(b[:159],flags,index,mask,entry)
    def test_source_files_match_exact_pinned_functions(self):
        for name,digest in camera.SOURCE_FILES.items():
            self.assertEqual(hashlib.sha256((ROOT/'vendor/colosseum'/name).read_bytes()).hexdigest(),digest)
    def test_bounds_expand_original_deformed_vertices_and_reverse_units_once(self):
        try:
            import numpy as np
        except ImportError:
            self.skipTest("NumPy is required for original geometry bound sampling")
        class Model:
            rest='rest'
            def world(self,clip,frame):
                self.sample=(clip,frame);return 'pose',None
            def deform(self,descriptor,state):
                m=np.eye(4)
                if state=='pose':m[1,3]=2
                return m
        model=Model();mesh=dict(vertices=[(-.3,0,-.2),(.3,1,.2)],descriptors=[('rigid',0),('rigid',0)])
        lo,hi=camera.source_bounds(model,[mesh],5)
        self.assertEqual(model.sample,(5,0));np.testing.assert_allclose(lo,[-3,20,-2]);np.testing.assert_allclose(hi,[3,30,2])
if __name__=='__main__':unittest.main()
