import hashlib
from pathlib import Path
import struct
import sys
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import prepare_wzx_tackle as tackle
import prepare_particle as particle

class TacklePreparationTests(unittest.TestCase):
    def test_typed_nodes(self):
        nodes=[tackle.node_words(i) for i in range(4)]
        self.assertEqual([n[1] for n in nodes],[1,5,3,5])
        self.assertEqual(nodes[0][6],2)
        self.assertEqual(nodes[2][2:4],[2,1])
        self.assertEqual([n[24] for n in nodes],[0,1,2,1])
        self.assertTrue(all(not any(n[8:24]) for n in nodes))

    def test_reject_unpinned_bank(self):
        for raw in (b'',b'GPT1'+bytes(91676)):
            with self.assertRaisesRegex(ValueError,'Tackle original'):
                particle.bank(raw,0,profile='tackle')
        with self.assertRaisesRegex(ValueError,'Tackle original'):
            particle.bank(bytes(91680),1,profile='tackle')

    def test_reject_wrong_pack(self):
        with self.assertRaisesRegex(ValueError,'particle identity'):
            tackle.encode([],[],{},b'not the source pack')

    def test_source_manifest_is_current(self):
        import json
        m=json.loads((ROOT/'config/wzx-tackle-source-files.json').read_text())
        self.assertEqual(m['commit'],tackle.SOURCE_COMMIT)
        for name,digest in m['files'].items():
            self.assertEqual(hashlib.sha256((ROOT/'vendor/colosseum'/name).read_bytes()).hexdigest(),digest,name)

    def test_parse_rejects_header_and_node_drift(self):
        a=bytearray(408);d=bytearray(92092)
        with self.assertRaisesRegex(ValueError,'header'):
            tackle.parse(a,d)
        struct.pack_into('>I',a,104,2);struct.pack_into('>6I',a,112,2,3,120,2,5,0)
        with self.assertRaisesRegex(ValueError,'node'):
            tackle.parse(a,d)

if __name__=='__main__':unittest.main()
