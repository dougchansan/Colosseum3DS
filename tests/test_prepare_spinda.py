"""Spinda source arithmetic and admission regressions (no private assets needed)."""
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import prepare_spinda as spinda

class SpindaTests(unittest.TestCase):
    def test_all_pid_bytes_low_and_high_nibbles(self):
        for index in range(4):
            for byte in (0,15,240,255):
                # Isolate this spot while checking the exact source linear writes.
                spot=[1]*1024
                actual=spinda.synthesize([0]*16384,spot,byte<<(index*8))
                expected=[0]*16384
                pid=byte<<(index*8)
                for j in range(4):
                    b=(pid>>(8*j))&255
                    x=spinda.ANCHORS[j*2]+int(3.4*((b&15)-8))
                    y=spinda.ANCHORS[j*2+1]+int(3.4*((b>>4)-8))
                    for row in range(32):
                        start=x+(y+row)*128
                        if 0<=start<16384:
                            for col in range(32):
                                if start+col<16384: expected[start+col]=1
                self.assertEqual(actual,expected,(index,byte))

    def test_unsigned_rgb16_max_precedes_expansion(self):
        base=[0x7800]*16384;spot=[0x07e0]*1024
        self.assertEqual(spinda.synthesize(base,spot,0xffffffff),base)
        self.assertEqual(spinda.rgba([0xffff,0x07e0]),bytes([248,252,248,255,0,252,0,255]))

    def test_bad_source_is_rejected(self):
        with self.assertRaises(ValueError):spinda.export_spinda(bytes(20),bytes(256),'.')
        with self.assertRaises(ValueError):spinda.linear_rgb16(bytes(128+32768),128,128)
        with self.assertRaises(ValueError):spinda.synthesize([],[],0)

    def test_rgb16_tiles_become_linear_rows(self):
        raw=bytearray(128+32);struct.pack_into('>HH',raw,0,4,4);struct.pack_into('>I',raw,8,0x44);struct.pack_into('>I',raw,40,128)
        for i in range(16):struct.pack_into('>H',raw,128+i*2,i)
        self.assertEqual(spinda.linear_rgb16(raw,4,4),list(range(16)))

if __name__=='__main__':unittest.main()
