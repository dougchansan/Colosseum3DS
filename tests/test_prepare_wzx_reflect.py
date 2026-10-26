"""Synthetic exact Reflect profile/implicit-zero schedule regressions."""
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch
from types import SimpleNamespace
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import prepare_wzx_reflect as reflect


def synthetic():
    data=bytearray(reflect.PROFILE['member_bytes'])
    struct.pack_into('>6I',data,0x70,0,5,0xA4A,2,5,0)
    signatures=[(1,3,2,1,0,0),(2,5,0,0,1,1),(3,4,0,0,0,1),(4,2,2,1,0,0)]
    for offset,(key,kind,attach,pos,link,flags) in zip(reflect.NODES,signatures):
        words=[key,kind,attach,pos,link,0,0,2]+[0]*16+[flags,0,2,0]
        struct.pack_into('>28i',data,offset,*words)
    struct.pack_into('>4I',data,0x110,0,0x18A9,93864,4)
    struct.pack_into('>3I',data,0x17050,0x220,3,0)
    struct.pack_into('>19I',data,0x170CC,0,190,2,1,3,3,0,0x80808080,0x802D2D2D,20,1,0x802D2D2D,0x802D2D2D,110,1,0x802D2D2D,0x80808080,60,1)
    struct.pack_into('>8I',data,0x17188,0xFFFFFFFF,0,0,0,0,0,1,reflect.MODEL_BYTES)
    return data


class ReflectPreparationTests(unittest.TestCase):
    def test_source_links_keep_implicit_zero_and_real_hex_sound(self):
        report,model,bank=reflect.parse(synthetic(),[0,180])
        self.assertEqual([n['start'] for n in report['entries']],[0]*4)
        self.assertEqual(report['normalization_shift'],0)
        self.assertEqual(report['sound_id'],0x220)
        self.assertEqual((len(model),len(bank)),(26647,93864))
        self.assertEqual(report['model']['joint_animation_index'],-1)
        self.assertEqual(report['model']['texture_animation_index'],0)
        with self.assertRaisesRegex(ValueError,'implicit'):
            reflect.parse(synthetic(),[180,0])

    def test_forbidden_source_profile_changes_fail_closed(self):
        for offset,value in [(0x70,1),(0xA0+8,0),(0x110+4,0x10A1),
                             (0x17050,220),(0x170CC+4,180),(0x17188,0)]:
            with self.subTest(offset=offset):
                data=synthetic();struct.pack_into('>I',data,offset,value)
                with self.assertRaises(ValueError):reflect.parse(data,[0,180])
        with self.assertRaises(ValueError):reflect.parse(synthetic()[:-1],[0,180])

    def test_material_only_timeline_never_invents_a_joint_clip(self):
        texture=SimpleNamespace(animation=SimpleNamespace(end_frame=95.),next=None)
        material=SimpleNamespace(animation=None,texture_animation=texture,next=None)
        root=SimpleNamespace(animation=material)
        model=SimpleNamespace(tracks=[],ends=[],materials=[{}],joints=[0,1,2],meshes=[0])
        raw=SimpleNamespace(animated_material_joints=[root])
        with patch.object(reflect.title,'tree_nodes',return_value=[root]):
            self.assertEqual(reflect.texture_only_end(model,raw),95.)
            model.tracks=[{}]
            with self.assertRaisesRegex(ValueError,'static joints'):reflect.texture_only_end(model,raw)
            model.tracks=[];texture.animation.end_frame=94.
            with self.assertRaisesRegex(ValueError,'duration'):reflect.texture_only_end(model,raw)

    def test_idle_parts_export_without_rebuilding_actor_geometry(self):
        if reflect.geometry is None:
            self.skipTest('NumPy is required for the original joint-track preparation fixture')
        def model(end):
            def world(_clip,frame):
                matrix=reflect.geometry.np.zeros((70,4,4))
                matrix[:,0,3]=frame*.1;matrix[:,1,3]=2;matrix[:,2,3]=3
                return matrix,None
            return SimpleNamespace(ends=[0,end],world=world)
        raw=SimpleNamespace(models=[object()]);scene=SimpleNamespace(models=[object()])
        maps=[{'clips':[1],'body_map_bones':[0,0,62]},
              {'clips':[0],'body_map_bones':[0,0,42]}]
        with patch.object(reflect.geometry,'import_parser'),patch.object(reflect,'admit',return_value=b'archive'), \
             patch.object(reflect,'member',return_value=b'source-member'), \
             patch.object(reflect,'load_pkx',return_value=(b'dat',raw,scene,None)), \
             patch.object(reflect.geometry,'Model',side_effect=[model(90.),SimpleNamespace(ends=[59.],world=model(59.).world)]), \
             patch.object(reflect,'clip_mapping',side_effect=[[maps[0]],[maps[1]]]), \
             patch.object(reflect.geometry,'prepare_pack_set') as pack:
            data,report=reflect.prepare_parts(Path('disc'),Path('parser'))
        pack.assert_not_called()
        self.assertEqual(len(data),3728)
        self.assertEqual(data[:4],b'C3RP')
        self.assertEqual([r['frames'] for r in report],[181,119])
        self.assertAlmostEqual(struct.unpack_from('<f',data,128+12)[0],.05,places=6)
        self.assertAlmostEqual(struct.unpack_from('<f',data,2300+118*12)[0],5.9,places=6)

    def test_descriptor_binds_static_joint_texture_only_program(self):
        report,_,_=reflect.parse(synthetic(),[0,180])
        data=reflect.encode(report,b'synthetic-model',b'synthetic-particles',b'synthetic-parts')
        self.assertEqual(data[:4],b'C3WR')
        self.assertEqual(struct.unpack_from('<I',data,108)[0],0xFFFFFFFF)
        self.assertEqual(struct.unpack_from('<I',data,112)[0],0)
        self.assertEqual(struct.unpack_from('<I',data,124)[0],118)
        self.assertEqual(data[180:200],reflect.hashlib.sha1(b'synthetic-model').digest())
        self.assertEqual(data[220:240],reflect.hashlib.sha1(b'synthetic-parts').digest())
        self.assertEqual(data[268:],reflect.hashlib.sha1(data[:268]).digest())


if __name__=='__main__':unittest.main()
