"""Synthetic WZX records and production C3T ingestion; no original fixtures."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import types
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import prepare_wzx_bite as wzx


def fixture():
    data=bytearray(0x320)
    def words(at,*values):
        for i,value in enumerate(values):struct.pack_into('>I',data,at+i*4,value&0xffffffff)
    words(104,2);words(112,2,5,0x00201048,2,5,0)
    def entry(at,key,kind,link=0,source=0,target=0):
        words(at,key,kind,0,0,link,source,target,2)
        words(at+104,2,0)
    entry(0xa0,1,4);words(0x100,1)
    words(0x110,0,60,2,1,2,3,0,0x80808080,0x80304050,60,1,0x80304050,0x80304050,-1,1)
    entry(0x14c,2,1,target=2);words(0x1bc,0,5,3)
    entry(0x1c8,3,2,source=1,target=2)
    words(0x1d0,1,1);words(0x1ec,18);words(0x228,2)
    words(0x238,0,0,0,0,0,0,1,32)
    entry(0x280,4,5,link=3);words(0x2f0,0x85,3,0)
    return bytes(data[:0x2fc])


class WzxBiteTests(unittest.TestCase):
    def test_source_relative_times_and_bounded_model_span(self):
        report,data=wzx.parse(fixture(),[20,40,100,0])
        self.assertEqual(len(data),32)
        self.assertEqual([e['start'] for e in report['entries']],[20,100,82,82])
        self.assertEqual(report['control_delay'],5)
        self.assertEqual(report['dat_offset'],0x260)

    def test_no_truncation_or_trailing_entries_admitted(self):
        data=fixture()
        for length in (0,100,159,160,271,331,459,607,639,751,763):
            with self.subTest(length=length),self.assertRaises(ValueError):wzx.parse(data[:length],[20,40,100,0])
        with self.assertRaises(ValueError):wzx.parse(data+b'\0',[20,40,100,0])

    def test_unknown_kind_bad_attachment_and_oversized_model_rejected(self):
        for offset,value in ((0xa4,3),(0x1d0,7),(0x1dc,99),(0x228,0),(0x254,0xffffffff),(0x2f0,0x999)):
            data=bytearray(fixture());struct.pack_into('>I',data,offset,value)
            with self.subTest(offset=offset),self.assertRaises(ValueError):wzx.parse(data,[20,40,100,0])

    def test_duplicate_links_and_negative_time_adjustment(self):
        nodes=[dict(key=1,link=0,target=0,source=0,timing=[7]),
               dict(key=2,link=1,target=0,source=0,timing=[3])]
        self.assertEqual(wzx.link_times(nodes,[2]),5)
        self.assertEqual([x['start'] for x in nodes],[0,4])
        nodes[1]['key']=1
        with self.assertRaises(ValueError):wzx.link_times(nodes,[2])
        with self.assertRaises(ValueError):wzx.link_times([dict(key=1,link=0,target=2,source=0,timing=[0])],[2])

    def test_descriptor_binds_prepared_model_and_source(self):
        report,_=wzx.parse(fixture(),[20,40,100,0])
        b=wzx.encode(report,[1.,2.,3.],5,34,b'synthetic model')
        self.assertEqual(len(b),256)
        self.assertEqual(b[:4],b'C3WX')
        self.assertEqual(b[156:176],hashlib.sha1(b'synthetic model').digest())
        self.assertEqual(b[188:208],hashlib.sha1(b[:188]).digest())
        self.assertEqual(b[176:188]+b[208:],bytes(60))
        self.assertEqual(struct.unpack_from('<5f',b,64),(.5,.5,1,2,3))
        with self.assertRaises(ValueError):wzx.encode(report,[float('nan'),0,0],5,34,b'x')

    @unittest.skipUnless(importlib.util.find_spec('numpy'),'NumPy required for material preparer')
    def test_only_truly_constant_material_colors_are_folded(self):
        from prepare_field_geometry import constant_material_byte
        def key(value,inside=0,outside=0):return types.SimpleNamespace(value=value,slope_in=inside,slope_out=outside)
        self.assertEqual(constant_material_byte([key(.5)]),127)
        self.assertEqual(constant_material_byte([key(-1),key(-1)]),0)
        self.assertEqual(constant_material_byte([key(2)]),255)
        for keys in ([],[key(.5),key(.6)],[key(.5,1)],[key(.5,0,1)],[key(float('nan'))]):
            with self.assertRaises(ValueError):constant_material_byte(keys)

if __name__=='__main__': unittest.main()
