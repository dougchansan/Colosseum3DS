import importlib.util
from pathlib import Path
import struct
import sys
import unittest

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
spec = importlib.util.spec_from_file_location("prepare_bottom_ui", TOOLS / "prepare_bottom_ui.py")
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)


class BottomUiPreparationTests(unittest.TestCase):
    def texture(self, fmt, payload):
        header = bytearray(128)
        struct.pack_into(">HH", header, 0, 4, 4)
        struct.pack_into(">I", header, 8, fmt)
        struct.pack_into(">I", header, 40, 128)
        return bytes(header) + payload

    def test_original_rgb5a3_alpha_and_opaque_modes(self):
        values = [0xFC00, 0x83E0, 0x801F, 0x3F00] + [0] * 12
        w, h, pixels = ui.decode_texture(self.texture(0x90, struct.pack(">16H", *values)))
        self.assertEqual((w, h), (4, 4))
        self.assertEqual(pixels[:16], bytes((255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 0, 0, 109)))

    def test_rgba8_plane_order(self):
        _, _, pixels = ui.decode_texture(self.texture(0x45, bytes((128, 10)) * 16 + bytes((20, 30)) * 16))
        self.assertEqual(pixels[:4], bytes((10, 20, 30, 128)))

    def test_texture_rejects_bad_formats_offsets_and_extents(self):
        good = self.texture(0x90, bytes(32))
        for candidate in (b"", good[:-1], good + b"\0", self.texture(0x42, bytes(32))):
            with self.assertRaises(ValueError):
                ui.decode_texture(candidate)
        corrupt = bytearray(good)
        struct.pack_into(">I", corrupt, 40, 0xFFFFFFFF)
        with self.assertRaises(ValueError):
            ui.decode_texture(corrupt)

    def test_crop_preserves_top_down_rows(self):
        image = 4, 4, bytes(range(64))
        self.assertEqual(ui.crop(image, (1, 1, 2, 2)), (2, 2, bytes(range(20, 28)) + bytes(range(36, 44))))
        for rect in ((-1, 0, 1, 1), (3, 3, 2, 2), (0, 0, 0, 2)):
            with self.assertRaises(ValueError):
                ui.crop(image, rect)

    def test_pack_counts_dimensions_and_glyph_bounds(self):
        sprites = [(8, 8, bytes(256))] * 10
        glyphs = [(8, bytes(256))] * 95
        data = ui.encode_pack(sprites, glyphs)
        self.assertEqual(data[:4], b"C3UI")
        self.assertEqual(struct.unpack_from("<I", data, 8)[0], len(data))
        for bad_sprites, bad_glyphs in ((sprites[:-1], glyphs), (sprites, glyphs[:-1]), (sprites, [(17, bytes(256))] * 95)):
            with self.assertRaises(ValueError):
                ui.encode_pack(bad_sprites, bad_glyphs)

    def test_dol_section_mapping_rejects_outside_and_truncated(self):
        dol = bytearray(512)
        struct.pack_into(">I", dol, 0, 256)
        struct.pack_into(">I", dol, 0x48, 0x80000000)
        struct.pack_into(">I", dol, 0x90, 256)
        self.assertEqual(ui.dol_offset(dol, 0x80000010), 272)
        with self.assertRaises(ValueError):
            ui.dol_offset(dol, 0x80000100)
        with self.assertRaises(ValueError):
            ui.dol_offset(dol[:-1], 0x80000010)


if __name__ == "__main__":
    unittest.main()
