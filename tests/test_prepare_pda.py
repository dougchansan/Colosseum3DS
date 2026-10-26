import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare_pda as pda
from prepare_bottom_ui import decode_texture


class PdaPreparationTests(unittest.TestCase):
    def test_directory_offsets_order_and_digest(self):
        data = pda.encode({(3, 2): bytes(44), (1, 9): b"\0A\0\0"})
        self.assertEqual(data[:4], b"C3PD")
        self.assertEqual(struct.unpack_from("<II", data, 8), (len(data), 2))
        self.assertEqual(data[16:36], hashlib.sha1(data[64:]).digest())
        self.assertEqual(struct.unpack_from("<IIII", data, 64), (1, 9, 96, 4))
        self.assertEqual(struct.unpack_from("<IIII", data, 80), (3, 2, 100, 44))

    def test_reject_empty_or_unknown_directory_entries(self):
        for entries in ({}, {(0, 1): b"x"}, {(20, 1): b"x"}, {(1, 1): b""}):
            with self.assertRaises(ValueError):
                pda.encode(entries)

    def test_message_group_extents_and_raw_name_control(self):
        data = bytearray(32)
        struct.pack_into(">H", data, 4, 1)
        struct.pack_into(">II", data, 16, 10, 24)
        data[24:] = b"\xff\xff\x2b\0!\0\0\0"
        result = pda.messages(bytes(data))
        self.assertEqual(result[10][:3], b"\xff\xff\x2b")
        struct.pack_into(">I", data, 20, 0xFFFFFFFF)
        with self.assertRaises(ValueError):
            pda.messages(data)

    def test_message_group_rejects_duplicate_ids(self):
        data = bytearray(40)
        struct.pack_into(">H", data, 4, 2)
        struct.pack_into(">IIII", data, 16, 5, 32, 5, 36)
        with self.assertRaises(ValueError):
            pda.messages(data)

    def test_original_mail_cmpr_tile_order_and_transparency(self):
        header = bytearray(128)
        struct.pack_into(">HH", header, 0, 8, 8)
        struct.pack_into(">I", header, 8, 0xB0)
        struct.pack_into(">I", header, 40, 128)
        blocks = (struct.pack(">HHI", 0xF800, 0, 0) + struct.pack(">HHI", 0x07E0, 0, 0) +
                  struct.pack(">HHI", 0x001F, 0, 0) + struct.pack(">HHI", 0, 0xFFFF, 0xFFFFFFFF))
        w, h, pixels = decode_texture(header + blocks)
        self.assertEqual((w, h), (8, 8))
        def at(x, y):
            return pixels[(y*8+x)*4:(y*8+x+1)*4]
        self.assertEqual(at(0, 0), bytes((255, 0, 0, 255)))
        self.assertEqual(at(4, 0), bytes((0, 255, 0, 255)))
        self.assertEqual(at(0, 4), bytes((0, 0, 255, 255)))
        self.assertEqual(at(4, 4), bytes(4))
        with self.assertRaises(ValueError):
            decode_texture(header + blocks[:-1])

    def test_resource_sources_are_fixed_to_the_pinned_inputs(self):
        self.assertEqual(len(pda.PIN), 20)
        self.assertEqual(set(pda.ARCHIVES), {"common.fsys", "pda_menu.fsys", "pocket_menu.fsys", "mail000.fsys", "mail001.fsys", "mail002.fsys"})


if __name__ == "__main__":
    unittest.main()
