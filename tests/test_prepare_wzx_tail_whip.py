"""Synthetic source ABI checks; optional actual private export/native acceptance."""
import copy
import hashlib
import math
import os
from pathlib import Path
import shlex
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import prepare_wzx_tail_whip as tail


class TailWhipPreparationTests(unittest.TestCase):
    def fixture(self):
        b=bytearray(tail.MEMBER_BYTES)
        struct.pack_into('>I',b,104,2)
        struct.pack_into('>6I',b,112,0,4,0x78,2,5,0)
        for key,kind,selector,pos,flags,offset in ((1,2,0,4,0,0xA0),(2,2,16,1,0,0x400),(3,5,0,0,1,0x21EC0)):
            struct.pack_into('>28i',b,offset,*([key,kind,selector,pos,0,0,0,2]+[0]*16+[flags,0,2,0]))
        struct.pack_into('>8I',b,0x110,0,0,0,0,1,1,1,683)
        struct.pack_into('>8I',b,0x470,0,0,0,0,0,0,0,137747)
        struct.pack_into('>3I',b,0x21F30,0x1DB,3,0)
        return b

    def tracks(self):
        identity=[1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.]
        return [identity*2 for _ in range(63)]

    def test_exact_nodes_source_zero_timing_and_payload(self):
        nodes=tail.parse(self.fixture())
        self.assertEqual([(n['kind'],n['attachment'],n['position_type'],n['start']) for n in nodes],[(2,0,4,0),(2,16,1,0),(5,0,0,0)])
        self.assertEqual([n['timing'][1] for n in nodes],[31,45,0])
        self.assertEqual(nodes[2]['resource_words'][:3],[0x1DB,3,0])

    def test_structural_unknowns_fail_closed(self):
        raw=self.fixture()
        for offset in (104,112,116,120,124,128,132,136,159,0xA0,0xA4,0xA8,0xAC,0xB0,0xB4,0xB8,0xBC,0xC0,0x110,0x120,0x12C,0x408,0x46C,0x48C,0x21F30,0x21F34):
            changed=bytearray(raw);changed[offset]^=1
            with self.subTest(offset=offset),self.assertRaises(ValueError):tail.parse(changed)
        for data in (raw[:159],raw[:-1],raw+bytes(1)):
            with self.assertRaises(ValueError):tail.parse(data)

    def test_motion_yaw_only_full_matrices_and_integrity(self):
        tracks=self.tracks()
        yaw=.2
        tracks[1]=tracks[1][:12]+[math.cos(yaw),0,math.sin(yaw),0,0,1,0,0,-math.sin(yaw),0,math.cos(yaw),0]
        b=tail.encode_motion(tracks,tail.MODEL_PROFILES[0][3])
        self.assertEqual((b[:4],len(b)),(b'C3TM',6176))
        self.assertEqual(struct.unpack_from('<5I',b,12),(39,2,63,60,1))
        self.assertEqual(b[84:104],hashlib.sha1(b[128:]).digest())
        self.assertEqual(b[104:124],hashlib.sha1(b[:104]+bytes(20)+b[124:]).digest())
        self.assertAlmostEqual(struct.unpack_from('<f',b,128+96+14*4)[0],math.sin(yaw),places=6)
        for index,value in ((0,0),(13,.1),(15,1),(17,2),(22,-1),(12,float('nan'))):
            changed=copy.deepcopy(tracks);changed[2][index]=value
            with self.subTest(index=index),self.assertRaises(ValueError):tail.encode_motion(changed,tail.MODEL_PROFILES[0][3])
        for tracks in ([],self.tracks()[:-1],[[0]*23]*63):
            with self.assertRaises(ValueError):tail.encode_motion(tracks,tail.MODEL_PROFILES[0][3])

    def test_descriptor_bindings_and_source_admission(self):
        nodes=tail.parse(self.fixture())
        motion=tail.encode_motion(self.tracks(),tail.MODEL_PROFILES[0][3])
        b=tail.encode_descriptor(nodes,motion,b'synthetic-drawable',[.1,.2,.3],'11'*20)
        self.assertEqual((b[:4],len(b)),(b'C3TW',512))
        self.assertEqual(b[84:104],hashlib.sha1(motion).digest())
        self.assertEqual(struct.unpack_from('<5I',b,144),(0x78,2,0x1DB,98,0))
        self.assertEqual(b[480:500],hashlib.sha1(b[:480]+bytes(20)+b[500:]).digest())
        for key,value in [('attachment',1),('position_type',3),('start',1),('link',1),('resource_words',[0]*8),('timing',[1]*16)]:
            changed=copy.deepcopy(nodes);changed[0][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):tail.encode_descriptor(changed,motion,b'draw',[0,0,0],'11'*20)
        with self.assertRaises(ValueError):tail.encode_descriptor(nodes,motion,b'draw',[float('nan'),0,0],'11'*20)

    def test_source_files_match_pinned_semantics(self):
        source=ROOT/'vendor/colosseum'
        if not (source/'src/game/wazaSequenceEntry.c').exists():self.skipTest('Pinned source checkout required')
        manifest=tail.verify_source(source)
        self.assertEqual(manifest['commit'],tail.SOURCE_COMMIT)
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary)
            name=next(iter(manifest['files']))
            target=root/name;target.parent.mkdir(parents=True);target.write_bytes(b'drift')
            with self.assertRaisesRegex(ValueError,'source drift'):tail.verify_source(root)

    def test_actual_private_exports_and_native_geometry(self):
        fixture=Path(os.environ.get('C3_TAIL_WHIP_FIXTURE',str(ROOT.parent/'tail-whip-preparation-v1')))
        if not (fixture/'tail_whip.c3w').exists():self.skipTest('Private original Tail Whip exports required')
        descriptor=(fixture/'tail_whip.c3w').read_bytes()
        controller=(fixture/'tail_whip_controller.c3m').read_bytes()
        drawable=(fixture/'tail_whip_effect.c3t').read_bytes()
        self.assertEqual(descriptor[84:104],hashlib.sha1(controller).digest())
        self.assertEqual(descriptor[104:124],hashlib.sha1(drawable).digest())
        self.assertEqual(drawable[:4],b'C3T3')
        frames=[list(struct.unpack_from('<24f',controller,128+96*i)) for i in range(63)]
        self.assertEqual(controller,tail.encode_motion(frames,tail.MODEL_PROFILES[0][3]))
        cc=shlex.split(os.environ.get('CC','cc'))
        if not shutil.which(cc[0]):self.skipTest('Host compiler required for actual C3T acceptance')
        harness='''#include "colosseum3ds/title.h"
#include <assert.h>
#include <math.h>
int main(int argc,char **argv){assert(argc==2);C3Title t={0};char e[256];assert(c3_title_load(&t,argv[1],e,sizeof e));
for(unsigned i=0;i<=90;i++){assert(c3_title_animate(&t,0,(float)i/120));for(unsigned j=0;j<t.vertex_count;j++)for(unsigned k=0;k<3;k++)assert(isfinite(t.draw_vertices[j].pos[k]));}
c3_title_close(&t);return 0;}'''
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'accept.c';exe=Path(tmp)/'accept';source.write_text(harness)
            flags=shlex.split(os.environ.get('CFLAGS','-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror'))
            subprocess.run([*cc,*flags,'-I'+str(ROOT/'include'),str(ROOT/'src/title.c'),str(source),'-lm','-o',str(exe)],check=True,capture_output=True,text=True)
            subprocess.run([str(exe),str(fixture/'tail_whip_effect.c3t')],check=True,capture_output=True,text=True)


if __name__=='__main__':unittest.main()
