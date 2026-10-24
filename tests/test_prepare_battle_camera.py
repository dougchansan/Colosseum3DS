"""Synthetic camera profile/bounds preparation checks; no original asset bytes."""
import copy
import hashlib
import os
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
    def test_confusion_profile_and_exact_pattern_admission(self):
        for entry,count,flags in [(1,6,0x200848),(8,11,0x48)]:
            b=bytearray(160);struct.pack_into('>6I',b,112,entry,count,flags,2,5,0)
            self.assertEqual(camera.profile(b,flags,5,2,entry,count)['entry'],entry)
            for offset in (116,120,124,128,132,136):
                changed=bytearray(b);changed[offset]^=1
                with self.assertRaises(ValueError):camera.profile(changed,flags,5,2,entry,count)
        p=[dict(flags=0x200848,pattern=5,fov_mask=2,entry=1,owner_start=0),
           dict(flags=0x48,pattern=0xffffffff,fov_mask=0,entry=8,owner_start=44)]
        with self.assertRaises(ValueError):camera.encode(p,self.actors(),True,bytes(988))
        prepared=ROOT/'private/confusion-camera-v1/confusion_camera.c3c'
        if not prepared.exists():self.skipTest('Private original camera descriptor required')
        patterns=prepared.read_bytes()[512:1500]
        b=camera.encode(p,self.actors(),True,patterns)
        self.assertEqual(b[:4],b'C3CC');self.assertEqual(struct.unpack_from('<I',b,12)[0],93)
        self.assertEqual(struct.unpack_from('<I',b,192+80)[0],1500)
        self.assertEqual(b[512:1500],patterns)
        self.assertEqual(b[480:500],hashlib.sha1(b[:480]+bytes(20)+b[500:]).digest())
        changed=bytearray(patterns);changed[0]^=1
        with self.assertRaises(ValueError):camera.encode(p,self.actors(),True,changed)

    def test_tailwhip_header_and_missing_exact_short_table_fail_closed(self):
        b=bytearray(160);struct.pack_into('>6I',b,112,0,4,0x78,2,5,0)
        self.assertEqual(camera.profile(b,0x78,0xffffffff,0,0,4)['entry'],0)
        for offset in (112,116,120,124,128,132,136,159):
            changed=bytearray(b);changed[offset]^=1
            with self.subTest(offset=offset):
                with self.assertRaises(ValueError):camera.profile(changed,0x78,0xffffffff,0,0,4)
        with self.assertRaises(ValueError):camera.encode_tailwhip([],bytes(608))

    def test_tailwhip_checked_pack_and_negative_bindings(self):
        path=Path(os.environ.get('C3_TAILWHIP_CAMERA',str(ROOT/'private/tailwhip-camera-v1/tailwhip_camera.c3c')))
        if not path.exists():self.skipTest('Private TailWhip camera descriptor required')
        patterns=path.read_bytes()[512:1120]
        actors=[dict(species=species,selector=-1,minimum=[-3.,0.,-3.],maximum=[3.,10.,3.],timing=[(118,180,98)[i],0,0,0],part=(42,62,53)[i],kind=2,track=[[.1,.2,.3],[.3,.2,.1]]) for i,(species,_,_) in enumerate(camera.ACTORS)]
        b=camera.encode_tailwhip(actors,patterns)
        self.assertEqual(b[:4],b'C3TC');self.assertEqual(struct.unpack_from('<I',b,12)[0],39);self.assertEqual(struct.unpack_from('<I',b,24)[0],1);self.assertEqual(b[512:1120],patterns)
        self.assertEqual(struct.unpack_from('<5I',b,64),(0x78,0xffffffff,0,0,0));self.assertEqual(b[128:192],bytes(64))
        self.assertEqual(b[480:500],hashlib.sha1(b[:480]+bytes(20)+b[500:]).digest())
        for key,value in [('selector',0),('species',25),('part',1),('kind',4),('timing',[98,1,0,0]),('track',[[float('nan'),0,0]])]:
            changed=copy.deepcopy(actors);changed[0][key]=value
            with self.subTest(key=key):
                with self.assertRaises(ValueError):camera.encode_tailwhip(changed,patterns)
        changed=bytearray(patterns);changed[0]^=1
        with self.assertRaises(ValueError):camera.encode_tailwhip(actors,changed)

    def test_growl_exact_profile_and_source_duration(self):
        raw=bytearray(160);struct.pack_into('>6I',raw,112,1,6,0x200858,2,5,0)
        self.assertEqual(camera.profile(raw,0x200858,5,2,1,6)['entry'],1)
        for offset in (112,116,120,124,128,132,136,159):
            changed=bytearray(raw);changed[offset]^=1
            with self.assertRaises(ValueError):camera.profile(changed,0x200858,5,2,1,6)
        path=Path(os.environ.get('C3_GROWL_CAMERA',str(ROOT/'private/growl-camera-v1/growl_camera.c3c')))
        if not path.exists():self.skipTest('Private Growl camera descriptor required')
        patterns=path.read_bytes()[512:1120]
        actors=[dict(species=species,selector=-1,minimum=[-3.,0.,-3.],maximum=[3.,10.,3.],timing=[130,244,366,0],part=(42,62,53)[i],kind=4,track=[[0,.2,0],[0,.3,0]]) for i,(species,_,_) in enumerate(camera.ACTORS)]
        packed=camera.encode_growl(actors,patterns)
        self.assertEqual(packed[:4],b'C3GC');self.assertEqual(struct.unpack_from('<I',packed,12)[0],45);self.assertEqual(struct.unpack_from('<5I',packed,64),(0x200858,5,2,1,0));self.assertEqual(packed[128:192],bytes(64))
        self.assertEqual(packed[480:500],hashlib.sha1(packed[:480]+bytes(20)+packed[500:]).digest())
        for key,value in [('kind',2),('timing',[130,100,366,0]),('timing',[130,244,366,1]),('selector',0),('part',1)]:
            bad=copy.deepcopy(actors);bad[0][key]=value
            with self.assertRaises(ValueError):camera.encode_growl(bad,patterns)
        # Runtime raw table offsets: count4*4+4 =>offset20 =>timing1=244.
        owner=bytearray(32);struct.pack_into('>I',owner,0,4);struct.pack_into('>4I',owner,12,0,130,244,366)
        count=struct.unpack_from('>I',owner)[0];duration=struct.unpack_from('>I',owner,count*4+4)[0]
        self.assertEqual(duration,244)

    def test_green_special_camera_namespace_and_target_bindings(self):
        raw=bytearray(160);struct.pack_into('>6I',raw,112,0,4,0,2,5,0)
        self.assertEqual(camera.profile(raw,0,0xffffffff,0,0,4)['entry'],0)
        for offset in (112,116,120,124,128,132,136,159):
            bad=bytearray(raw);bad[offset]^=1
            with self.assertRaises(ValueError):camera.profile(bad,0,0xffffffff,0,0,4)
        path=Path(os.environ.get('C3_STAT_DOWN_CAMERA',str(ROOT/'private/stat-down-camera-v1/stat_down_camera.c3c')))
        if not path.exists():self.skipTest('Private GREEN special camera descriptor required')
        patterns=path.read_bytes()[512:1120]
        actors=[dict(species=species,selector=-1,minimum=[-3.,0.,-3.],maximum=[3.,10.,3.],timing=[(118,180)[i],0,0,0],part=(42,62)[i],kind=2,track=[[0,.2,.1]]*(119,181)[i]) for i,(species,_,_) in enumerate(camera.ACTORS[:2])]
        b=camera.encode_stat_down(actors,patterns);self.assertEqual(b[:4],b'C3SC');self.assertEqual(struct.unpack_from('<I',b,12)[0],0x29);self.assertEqual(struct.unpack_from('<I',b,20)[0],2);self.assertEqual(struct.unpack_from('<5I',b,64),(0,0xffffffff,0,0,0));self.assertEqual(b[384:480],bytes(96));self.assertEqual(b[480:500],hashlib.sha1(b[:480]+bytes(20)+b[500:]).digest())
        for key,value in [('species',288),('part',53),('kind',4),('timing',[118,1,0,0])]:
            bad=copy.deepcopy(actors);bad[0][key]=value
            with self.assertRaises(ValueError):camera.encode_stat_down(bad,patterns)
        with self.assertRaises(ValueError):camera.encode_tailwhip(actors,patterns)

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
