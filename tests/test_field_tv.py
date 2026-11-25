"""Field television (field-camera texture entries) conversion; no original assets.

Floor 0x1C (Pyrite Building 4F) drew its wall monitor as a flat red quad:
map model 2 carries only an 8x8 red placeholder texture, which the original
replaces with entry renders (fn_80117E58 / fn_80117AE4 / fn_80117D14). The
rows below are that floor's texture-change table (REL 066B1C00 +0x24C).
"""
from pathlib import Path
import struct
import sys
from types import SimpleNamespace as Obj
import unittest

try:
    import numpy as np
except ImportError:
    np = None

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import field_tv
import prepare_floor as floor

F01C_ROWS = ((256, 256, 0, 0x00A91002, 0x10331000, 0, 0x10321800),
             (256, 256, 1, 0x00A91002, 0x10331000, 1, 0x10321800),
             (256, 256, 2, 0x00A91002, 0x10A11000, 0, 0x10A01800))
F01C_MEMBERS = {0x00A90200, 0x10330400, 0x10A10400, 0x10321800, 0x10A01800, 0x066B1C00}


def rel_with(rows, lead=0x24C, tail=b"\x00\xb4\x00\x00" + b"\x3e\x4c\xcc\xcd" * 8):
    data = bytearray(b"\x00" * lead)
    for row in rows:
        data += field_tv.ENTRY.pack(*row)
    return bytes(data + tail)


class TextureTableTests(unittest.TestCase):
    def test_floor_1c_table_is_found_by_its_rows(self):
        rows = field_tv.texture_table(rel_with(F01C_ROWS), F01C_MEMBERS, 0x00A90200)
        self.assertEqual([row["offset"] for row in rows], [0x24C, 0x264, 0x27C])
        self.assertEqual([(row["id"], row["model_member"], row["clip"], row["camera"]) for row in rows],
                         [(0, 0x10330400, 0, 0x10321800), (1, 0x10330400, 1, 0x10321800),
                          (2, 0x10A10400, 0, 0x10A01800)])
        self.assertEqual({row["screen"] & 255 for row in rows}, {2})

    def test_rows_must_name_the_floor_map_and_archive_members(self):
        self.assertEqual(field_tv.texture_table(rel_with(F01C_ROWS), F01C_MEMBERS, 0x00AB0200), [])
        self.assertEqual(field_tv.texture_table(rel_with(F01C_ROWS), F01C_MEMBERS - {0x10321800, 0x10A01800},
                                                0x00A90200), [])
        self.assertEqual(field_tv.texture_table(b"\x00" * 64, F01C_MEMBERS, 0x00A90200), [])

    def test_two_tables_are_refused(self):
        data = rel_with(F01C_ROWS[:1], tail=b"\x00" * 8) + rel_with(F01C_ROWS[1:], lead=0)
        with self.assertRaises(ValueError):
            field_tv.texture_table(data, F01C_MEMBERS, 0x00A90200)

    def test_script_ids_are_floor_start_and_literal_native_ab(self):
        lines = ["00d3: native words=2  ; native ab(0x1)", "0614: native words=2  ; native ab(0x0)",
                 "2a76: native words=2  ; native ab(0x2)", "1000: native words=2  ; native ab(local[0x0])",
                 "2000: native words=1  ; native d3()"]
        self.assertEqual(field_tv.script_entry_ids(lines), {0, 1, 2})
        self.assertEqual(field_tv.script_entry_ids([]), {0})

    def test_floor_1c_plan_binds_the_presenter_and_nascour_packs(self):
        rows = field_tv.texture_table(rel_with(F01C_ROWS), F01C_MEMBERS, 0x00A90200)
        packs, entries, omitted = field_tv.plan(rows, {0, 1, 2})
        self.assertEqual([(pack["model_member"], pack["camera"], pack["clips"]) for pack in packs],
                         [(0x10330400, 0x10321800, [0, 1]), (0x10A10400, 0x10A01800, [0])])
        self.assertEqual(entries, [(0, 0, 0), (1, 0, 1), (2, 1, 0)])
        self.assertEqual(omitted, [])
        data = field_tv.encode(0, 2, len(packs), entries)
        self.assertEqual(data[:4], b"C3TV")
        self.assertEqual(struct.unpack_from("<6I", data, 4), (1, 64, 0, 2, 2, 3))
        self.assertEqual(struct.unpack_from("<9I", data, 28), (0, 0, 0, 1, 0, 1, 2, 1, 0))
        # tests/test_field_tv.c parses exactly these bytes.
        self.assertEqual(len(data), 64)

    def test_pack_bound_keeps_entry_zero_first(self):
        rows = [dict(offset=i * 24, id=i, model=0x10000000 + (i << 16) | 0x1000, camera=0x18001800 + (i << 16),
                     model_member=0x10000400 + (i << 16), model_index=0, clip=0) for i in range(4)]
        packs, entries, omitted = field_tv.plan(rows, {3, 2, 1, 0})
        self.assertEqual(len(packs), field_tv.MAX_PACKS)
        self.assertEqual([entry[0] for entry in entries], [0, 1])
        self.assertEqual(omitted, [2, 3])

    def test_encode_refuses_a_missing_pack(self):
        with self.assertRaises(ValueError):
            field_tv.encode(0, 2, 1, [(0, 1, 0)])


class ScreenBindingTests(unittest.TestCase):
    def test_screen_is_the_one_map_batch_of_the_screen_model(self):
        reports = [{"file": "f01c_map0.c3t", "source_meshes": [
                        {"hsd_root_address": 0x100, "texture_index": 0},
                        {"hsd_root_address": 0x200, "texture_index": 1},
                        {"hsd_root_address": 0x300, "texture_index": 2}]},
                   {"file": "f01c_map1.c3t", "source_meshes": [{"hsd_root_address": 0x100, "texture_index": 0}]},
                   {"file": "f01c_wes.c3t", "source_meshes": [{"hsd_root_address": 0x300, "texture_index": 0}]}]
        self.assertEqual(floor.television_screen("f01c", reports, 0x300), (0, 2))
        # The renderer binds a texture only one batch of its pack uses.
        reports[0]["source_meshes"][1]["texture_index"] = 2
        self.assertIsNone(floor.television_screen("f01c", reports, 0x300))
        self.assertIsNone(floor.television_screen("f01c", reports, 0x999))


@unittest.skipIf(np is None, "numpy is required")
class PixelEngineTests(unittest.TestCase):
    def test_source_alpha_blend_with_flags_0x39_is_translucent_with_depth_writes(self):
        import prepare_field_geometry as geometry
        import prepare_title as title
        pe = Obj(flags=57, type=1, source_factor=4, destination_factor=5, z_comp=3)
        flags = geometry.pixel_engine_flags(pe, 0)
        self.assertEqual(flags & (title.TRANSLUCENT | title.ADDITIVE | title.NO_DEPTH_WRITE), title.TRANSLUCENT)
        self.assertEqual(flags, geometry.pixel_engine_flags(Obj(flags=49, type=1, source_factor=4,
                                                                destination_factor=5, z_comp=3), 0))


if __name__ == "__main__":
    unittest.main()
