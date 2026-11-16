"""Structural C3A insertion checks; no original data in the fixture."""
import importlib.util
from pathlib import Path
import struct
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/"tools"))
from prepare_realgam_dynamic import insert_actor

class DynamicRegistry(unittest.TestCase):
    def test_transform_only_encoder(self):
        from prepare_title import encode_pack
        identity=(1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.)
        camera=(1.,2.,3.,0.,0.,0.,0.,45.,.1,100.,1.,0.)
        light=(4,0,1.,1.,1.,0.,0.,0.,0.,0.,0.,0.,1.,1.,0.,0.)
        frame=struct.pack("<24f",*(identity+camera))
        blob=encode_pack(1,[],[],[],[(0,1,0,0)],[light],frame)
        self.assertEqual(struct.unpack_from("<3I",blob,16),(0,0,0))
        self.assertEqual(struct.unpack_from("<I",blob,72)[0],96)
        for vertices,batches,textures in (([],[],[(8,8,b"" ,0,0)]),([],[(0,3)],[])):
            with self.assertRaises(ValueError):encode_pack(1,vertices,batches,textures,[(0,1,0,0)],[light],frame)

    def test_insertion_preserves_existing_sections(self):
        blob=bytearray(128+160+56+56+32)
        blob[:4]=b"C3A1"
        for offset,value in ((8,len(blob)),(20,1),(32,128),(36,288),(40,344),(60,400)):
            struct.pack_into("<I",blob,offset,value)
        blob[128:]=bytes(i%251 for i in range(len(blob)-128))
        row=bytes([77])*160
        result=insert_actor(blob,row)
        self.assertEqual(result[128:288],blob[128:288])
        self.assertEqual(result[288:448],row)
        self.assertEqual(result[448:],blob[288:])
        self.assertEqual(struct.unpack_from("<I",result,20)[0],2)
        for offset in (36,40,60):
            self.assertEqual(struct.unpack_from("<I",result,offset)[0],struct.unpack_from("<I",blob,offset)[0]+160)
        self.assertEqual(struct.unpack_from("<I",result,8)[0],len(result))

if __name__=="__main__":unittest.main()
