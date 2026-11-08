"""Synthetic Growl source contract and optional original-model acceptance."""
import copy
import hashlib
import json
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
import prepare_wzx_growl as growl


class GrowlPreparationTests(unittest.TestCase):
    def fixture(self):
        b=bytearray(431540)
        struct.pack_into('>I',b,104,2);struct.pack_into('>6I',b,112,1,6,0x200858,2,5,0)
        signatures=zip((2,3,5,1,4),(1,0,0,0,0),(1,1,0,0,0),(0,1,1,0,0),(0,0,0,0,1),(1,0,0,1,1),(2,3,1,0,0),(0,9,0,0,0))
        for i,(kind,selector,pos,link,src,target,flags,part) in enumerate(signatures):
            timing=[0]*16
            if i==4:timing[1:3]=[40,220]
            offset=growl.OFFSETS[i]
            struct.pack_into('>28i',b,offset,*[i+1,kind,selector,pos,link,src,target,3 if i==4 else 2,*timing,flags,part,2,0])
            payload=growl.PAYLOADS[i]
            struct.pack_into('>%dI'%len(payload),b,offset+112,*payload)
        return b

    def test_source_nodes_implicit_zero_links_and_declared_bank_length(self):
        nodes=growl.parse(self.fixture())
        self.assertEqual([n['start'] for n in nodes],[130,130,130,130,90])
        self.assertEqual([(n['kind'],n['flags'],n['part']) for n in nodes],[(2,2,0),(3,3,9),(5,1,0),(1,0,0),(4,0,0)])
        self.assertEqual(nodes[1]['resource_words'],[18,0x10A1,365764,4])
        self.assertEqual(nodes[0]['timing'][1],39)

    def test_unknown_header_node_payload_and_bank_padding_rejected(self):
        raw=self.fixture()
        for offset in (104,112,116,120,124,128,132,136,159,0xA8,0xB8,0x110,0x12C,0xFEA0+100,0xFF10,0xFF18,0x693E4,0x69470,0x694EC,0x69568,0x6958C):
            changed=bytearray(raw);changed[offset]^=1
            with self.subTest(offset=offset),self.assertRaises(ValueError):growl.parse(changed)
        for b in (raw[:160],raw[:-1],raw+bytes(1)):
            with self.assertRaises(ValueError):growl.parse(b)

    def test_descriptor_checks_payloads_and_binds_original_resources(self):
        nodes=growl.parse(self.fixture())
        b=growl.encode(nodes,b'model',b'particles','11'*20,22,[1,2,3],[4,5,6])
        self.assertEqual((b[:4],len(b)),(b'C3WG',1280))
        self.assertEqual(b[84:104],hashlib.sha1(b'model').digest())
        self.assertEqual(b[104:124],hashlib.sha1(b'particles').digest())
        self.assertEqual(struct.unpack_from('<8I',b,224),(244,366,230,5,320,39,183,79))
        self.assertEqual(struct.unpack_from('<8I',b,256),(90,120,130,130,130,130,100,18))
        self.assertEqual(b[288:300],bytes(12))
        self.assertEqual(b[300:320],hashlib.sha1(b[:300]+bytes(20)+b[320:]).digest())
        for k,v in [('start',0),('flags',0),('part',58),('resource_words',[0]*4),('timing',[1]*16),('offset',0)]:
            changed=copy.deepcopy(nodes);changed[1][k]=v
            with self.subTest(field=k),self.assertRaises(ValueError):growl.encode(changed,b'model',b'particles','11'*20,22,[0]*3,[0]*3)
        for bone,anchor in ((64,[0]*3),(22,[float('nan'),0,0]),(22,[0,0])):
            with self.assertRaises(ValueError):growl.encode(nodes,b'm',b'p','11'*20,bone,anchor,[0]*3)

    def test_source_semantics_pins_and_rejection(self):
        profile=json.loads((ROOT/'config/wzx-growl-source-files.json').read_text())
        self.assertEqual(profile['commit'],growl.SOURCE_COMMIT)
        source=ROOT/'vendor/colosseum'
        if not (source/'src/game/wazaSequence.c').exists():self.skipTest('Pinned source required')
        for name,digest in profile['files'].items():self.assertEqual(hashlib.sha256((source/name).read_bytes()).hexdigest(),digest)
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/next(iter(profile['files']));p.parent.mkdir(parents=True);p.write_bytes(b'drift')
            with self.assertRaisesRegex(ValueError,'source drift'):growl.verify_source(tmp,None)

    def test_actual_private_model_all_samples_and_unchanged_raw_bank(self):
        path=Path(os.environ.get('C3_GROWL_FIXTURE',str(ROOT.parent/'growl-preparation-v1')))
        if not (path/'growl.json').exists():self.skipTest('Private original Growl exports required')
        report=json.loads((path/'growl.json').read_text())
        descriptor=(path/'growl.c3w').read_bytes();model=(path/'growl_model.c3t').read_bytes()
        particles=(path/'growl.c3p').read_bytes();bank=(path/'growl.gpt').read_bytes()
        self.assertEqual(hashlib.sha256(bank).hexdigest(),growl.BANK_SHA256)
        self.assertEqual(descriptor[84:104],hashlib.sha1(model).digest())
        self.assertEqual(descriptor[104:124],hashlib.sha1(particles).digest())
        at,size=struct.unpack_from('<2I',particles,16)
        self.assertEqual(particles[at:at+size],bank)
        self.assertEqual(report['particles']['closure'],[18,93,111,112])
        self.assertEqual(len(report['particles']['textures']),25)
        self.assertEqual(report['model']['retained_sampled_bone'],22)
        cc=shlex.split(os.environ.get('CC','cc'))
        if not shutil.which(cc[0]):self.skipTest('Host compiler required for native geometry acceptance')
        harness='''#include "colosseum3ds/title.h"
#include <assert.h>
#include <math.h>
int main(int argc,char **argv){assert(argc==2);C3Title t={0};char e[256];assert(c3_title_load(&t,argv[1],e,sizeof e));
for(unsigned i=0;i<=78;i++){assert(c3_title_animate(&t,0,(float)i/120));for(unsigned j=0;j<t.vertex_count;j++)for(unsigned k=0;k<3;k++)assert(isfinite(t.draw_vertices[j].pos[k]));}
c3_title_close(&t);return 0;}'''
        with tempfile.TemporaryDirectory() as tmp:
            source=Path(tmp)/'accept.c';exe=Path(tmp)/'accept';source.write_text(harness)
            flags=shlex.split(os.environ.get('CFLAGS','-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror'))
            subprocess.run([*cc,*flags,'-I'+str(ROOT/'include'),str(ROOT/'src/title.c'),str(source),'-lm','-o',str(exe)],check=True,capture_output=True,text=True)
            subprocess.run([str(exe),str(path/'growl_model.c3t')],check=True,capture_output=True,text=True)


if __name__=='__main__':unittest.main()
