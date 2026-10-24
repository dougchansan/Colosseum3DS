"""Synthetic GREEN source ABI and optional private original-export checks."""
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import prepare_wzx_stat_down as stat


class StatDownPreparationTests(unittest.TestCase):
    def fixture(self):
        b=bytearray(2264);struct.pack_into('>I',b,104,2);struct.pack_into('>6I',b,112,0,4,0,2,5,0)
        for i,(kind,link,flags) in enumerate(((3,0,2),(5,1,1),(4,0,1))):
            at=stat.OFFSETS[i]
            struct.pack_into('>28i',b,at,*([i+1,kind,0,1 if i==0 else 0,link,0,0,2]+[0]*16+[flags,0,2,0]))
            payload=stat.PAYLOADS[i];struct.pack_into('>%dI'%len(payload),b,at+112,*payload)
        return b

    def owners(self):
        return [dict(species=p[0],slot=p[1],entry=0,clip=p[4],end=p[5],gate=p[6],root_part=0,camera_part=p[7],selector=-1,root_translation=[0.,0.,0.],hsd_sha1='11'*20,pkx_sha1='22'*20) for p in stat.OWNERS]

    def test_green_source_order_and_zero_start(self):
        nodes=stat.parse(self.fixture())
        self.assertEqual([(n['key'],n['kind'],n['start']) for n in nodes],[(1,3,0),(2,5,0),(3,4,0)])
        self.assertEqual(nodes[0]['resource_words'],[0,0xA1,1644,4])
        self.assertEqual(nodes[1]['resource_words'],[0x19B,3,0])
        self.assertEqual(nodes[2]['resource_words'],stat.COLOR)

    def test_unknown_red_header_nodes_payloads_padding_and_extents_rejected(self):
        data=self.fixture()
        for offset in (104,112,116,120,124,128,132,136,159,0xA8,0xAC,0xB0,0xB4,0xC0,0x104,0x110,0x114,0x118,0x78C,0x810,0x88C,0x8A8):
            changed=bytearray(data);changed[offset]^=1
            with self.subTest(offset=offset),self.assertRaises(ValueError):stat.parse(changed)
        red=bytearray(data);struct.pack_into('>I',red,120,0x78)
        with self.assertRaises(ValueError):stat.parse(red)
        for data in (data[:160],data[:-1],data+bytes(1)):
            with self.assertRaises(ValueError):stat.parse(data)

    def test_add_position_inspection_retains_original_offsets(self):
        raw=b'\x40\0\x8f'+struct.pack('>3f',0,22,0)+b'\xa0\0'+struct.pack('>f',1)+b'\xff'
        before=bytes(raw);instructions,children,poses=stat.inspect_program(raw,1)
        self.assertEqual(raw,before)
        self.assertEqual([(i['opcode'],i['offset']) for i in instructions],[(0x40,0),(0x8F,2),(0xA0,15),(0xFF,21)])
        self.assertEqual(instructions[1]['args']['values'],[0.,22.,0.])
        self.assertFalse(instructions[1]['args']['height_scaled'])
        self.assertFalse(instructions[1]['args']['source_offset_rotation'])
        self.assertEqual(instructions[1]['args']['source_offset_transform_bit'],8)
        self.assertEqual((children,poses),([],[0]))
        for bad in (b'',raw[:14],raw[:2]+b'\x8e'+raw[3:],raw[:3]+struct.pack('>3f',0,23,0)+raw[15:],raw[:15]+b'\xbf\xff'):
            with self.assertRaises(ValueError):stat.inspect_program(bad,1)

    def test_checked_special_namespace_owner_bindings_and_checksums(self):
        nodes=stat.parse(self.fixture());owners=self.owners();b=stat.encode(nodes,owners,b'checked-pack')
        self.assertEqual((b[:4],len(b)),(b'C3SG',1024))
        self.assertEqual(struct.unpack_from('<7I',b,4),(1,1024,0x29,4,2,2,3))
        self.assertEqual(b[116:136],hashlib.sha1(b'checked-pack').digest())
        self.assertEqual(struct.unpack_from('<8I',b,256),(197,0,0,0,59,118,0,42))
        self.assertEqual(struct.unpack_from('<8I',b,352),(196,1,0,1,90,180,0,62))
        self.assertEqual(b[204:236],bytes(32))
        self.assertEqual(b[236:256],hashlib.sha1(b[:236]+bytes(20)+b[256:]).digest())
        for key,value in [('clip',0),('gate',118),('root_translation',[1.,0.,0.]),('selector',0),('hsd_sha1','11')]:
            changed=copy.deepcopy(owners);changed[1][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):stat.encode(nodes,changed,b'pack')
        for key,value in [('flags',0),('resource_words',[0,1,1644,4]),('start',50),('timing',[1]*16)]:
            changed=copy.deepcopy(nodes);changed[0][key]=value
            with self.assertRaises(ValueError):stat.encode(changed,owners,b'pack')

    def test_pinned_source_files(self):
        source=ROOT/'vendor/colosseum';profile=json.loads((ROOT/'config/wzx-stat-down-source-files.json').read_text())
        self.assertEqual(profile['commit'],stat.SOURCE_COMMIT)
        if not (source/'src/game/wazaSequence.c').exists():self.skipTest('Pinned source checkout required')
        for name,digest in profile['files'].items():self.assertEqual(hashlib.sha256((source/name).read_bytes()).hexdigest(),digest)

    def test_private_export_original_bank_and_complete_texture_groups(self):
        path=Path(os.environ.get('C3_STAT_DOWN_FIXTURE',str(ROOT.parent/'stat-down-preparation-v2')))
        if not (path/'stat_down.c3w').exists():self.skipTest('Private original GREEN resources required')
        descriptor=(path/'stat_down.c3w').read_bytes();pack=(path/'stat_down.c3p').read_bytes();raw=(path/'stat_down.gpt').read_bytes()
        report=stat.bank(raw)
        self.assertEqual(hashlib.sha256(raw).hexdigest(),stat.BANK_SHA256)
        self.assertEqual(report['closure'],[0,1,2]);self.assertEqual(len(report['textures']),3)
        self.assertEqual(pack,stat.particle.encode(raw,report,stat.MEMBER_SHA256))
        self.assertEqual(descriptor[116:136],hashlib.sha1(pack).digest())
        at,size=struct.unpack_from('<2I',pack,16);self.assertEqual(pack[at:at+size],raw)
        for t in report['textures']:
            self.assertEqual(t['rgba'][0:4],bytes([t['rgba'][0]])*4)
        for bad in (raw[:-1],bytes(1644),raw[:-1]+bytes([raw[-1]^1])):
            with self.assertRaises(ValueError):stat.bank(bad)


if __name__=='__main__':unittest.main()
