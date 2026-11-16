import sys
from pathlib import Path
import struct
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
import prepare_title_characters as chars


class TitleCharacterPreparationTests(unittest.TestCase):
    def resources(self):
        textures = [(8, 8, bytes([i])*256) for i in range(17)]
        items = [(i % 17, p, c, 0, 0, 8, 8, 0) for i, (p,c) in enumerate(chars.PAGE_COLUMNS)]
        return textures, items

    def test_pack_tables_and_contiguous_rgba(self):
        textures, items = self.resources()
        data = chars.encode_pack(textures, items)
        self.assertEqual(struct.unpack_from('<4s7I', data), (b'C3TC',1,len(data),17,19,4,0,0))
        offset = 32 + 17*16 + 19*32
        for i, texture in enumerate(textures):
            self.assertEqual(struct.unpack_from('<4I', data, 32+i*16), (8,8,offset,256))
            self.assertEqual(data[offset:offset+256], texture[2])
            offset += 256
        for i,item in enumerate(items):
            self.assertEqual(struct.unpack_from('<8i',data,32+17*16+i*32),item)
        self.assertEqual(offset,len(data))

    def test_reject_bad_counts_and_textures(self):
        textures, items = self.resources()
        for ts, its in ((textures[:-1],items),(textures,items[:-1]), ([(8,8,b'')]+textures[1:],items), ([(513,8,bytes(513*8*4))]+textures[1:],items)):
            with self.assertRaises(ValueError):chars.encode_pack(ts,its)

    def test_reject_memory_budget(self):
        textures, items = self.resources()
        original_limit = chars.MAX_BYTES
        try:
            chars.MAX_BYTES = 1024
            with self.assertRaises(ValueError):chars.encode_pack(textures,items)
        finally:
            chars.MAX_BYTES = original_limit

    def test_reject_bad_layout_fields(self):
        textures, items = self.resources()
        for field,value in ((0,-1),(0,17),(1,4),(2,6),(3,-1),(3,640),(4,480),(5,0),(5,9),(6,9),(7,1)):
            bad = list(items)
            row = list(bad[0]);row[field]=value;bad[0]=tuple(row)
            with self.subTest(field=field,value=value), self.assertRaises(ValueError):chars.encode_pack(textures,bad)

    def synthetic_dol(self):
        address = 0x802E0000
        dol = bytearray(0x30000)
        for base,value in ((0,256),(0x48,address),(0x90,len(dol)-256)):
            struct.pack_into('>I',dol,base,value)
        for i,(page,column) in enumerate(chars.PAGE_COLUMNS):
            item_id = 0xEFB+i
            offset = 256+0x802EF0A8+item_id*28-address
            struct.pack_into('>5h',dol,offset+2,0,0,8,8,i)
            struct.pack_into('>I',dol,offset+20,0x8001F304)
            struct.pack_into('>H',dol,offset+24,item_id+1 if i<18 else 0x2C3)
            sprite = 256+0x802E7CE8+i*24-address
            struct.pack_into('>4HI',dol,sprite+8,0,0,8,8,chars.TEXTURE_IDS[i%17])
        return dol

    def test_extracts_dol_coordinates_and_sprite_references(self):
        dol = self.synthetic_dol()
        _, items = self.resources()
        self.assertEqual(chars.extract_layout(dol),items)
        offset = chars.dol_offset(dol,0x802EF0A8+0xEFB*28)
        for field in (offset+20,offset+24):
            bad = bytearray(dol);bad[field] ^= 1
            with self.assertRaises(ValueError):chars.extract_layout(bad)

    def test_rejects_unmapped_and_truncated_dol(self):
        for dol in (bytes(), bytes(256), self.synthetic_dol()[:512]):
            with self.assertRaises(ValueError):chars.extract_layout(dol)


if __name__ == '__main__':
    unittest.main()
