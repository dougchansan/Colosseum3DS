"""Synthetic format/control checks; no original messages or disc files."""
import importlib.util
from pathlib import Path
import struct
import unittest

SPEC = importlib.util.spec_from_file_location(
    "prepare_game_menu", Path(__file__).resolve().parents[1] / "tools/prepare_game_menu.py")
MENU = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MENU)


def glyphs(text):
    return b"".join(struct.pack(">H", ord(char)) for char in text)


def compressed(payload, expected):
    return b"LZSS" + struct.pack(">III", expected, 16 + len(payload), 0) + payload


class GameMenuPreparationTests(unittest.TestCase):
    def test_mixed_width_controls_preserve_glyph_alignment(self):
        raw = glyphs("A") + b"\xff\xff\x00" + glyphs("B") + b"\xff\xff\x07\x02" + \
            b"\xff\xff\x4d" + glyphs(" C") + b"\xff\xff\x02\x00\x00"
        self.assertEqual(MENU.console_text(raw), "A\nB{name} C")

    def test_only_reviewed_control_and_console_glyphs_are_accepted(self):
        self.assertEqual(MENU.console_text(glyphs("caf\xe9") + b"\0\0"), "cafe")
        for raw in (b"\xff\xff", b"\xff\xff\x07", b"\xff\xff\x99\0\0",
                    b"\0\x1b\0\0", b"\0\x7f\0\0", glyphs("No terminator"),
                    glyphs("A" * 512) + b"\0\0"):
            with self.subTest(raw=raw[:20]), self.assertRaises(ValueError):
                MENU.console_text(raw)

    def test_lzss_zero_window_and_overlapping_backreference(self):
        self.assertEqual(MENU.unpack_lzss(compressed(b"\x00\x00\x00", 3), 3), b"\0" * 3)
        self.assertEqual(MENU.unpack_lzss(compressed(b"\x01A\xee\xf2", 6), 6), b"A" * 6)

    def test_lzss_bounds_catch_overrun_truncation_and_trailing_input(self):
        for data, expected in ((compressed(b"\x01", 1), 1),
                               (compressed(b"\x00\xee", 3), 3),
                               (compressed(b"\x00\x00\x00", 2), 2),
                               (compressed(b"\x01AB", 1), 1),
                               (compressed(b"\x01A", 1), 2)):
            with self.subTest(data=data, expected=expected), self.assertRaises(ValueError):
                MENU.unpack_lzss(data, expected)

    def test_message_extents_are_bounded_by_the_next_string(self):
        header = bytearray(16)
        struct.pack_into(">H", header, 4, 2)
        group = bytes(header) + struct.pack(">4I", 10, 32, 20, 36) + b"\0A\0\0\0B\0\0"
        self.assertEqual(MENU.message_bytes(group, (20,)), {20: b"\0B\0\0"})
        for bad in (group[:16], group[:20], group[:32],
                    group[:16] + struct.pack(">4I", 20, 32, 10, 36) + group[32:],
                    group[:16] + struct.pack(">4I", 10, 31, 20, 36) + group[32:]):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                MENU.message_bytes(bad, (20,))
        with self.assertRaises(ValueError):
            MENU.message_bytes(group, (30,))

    def test_canonical_pack_is_sorted_little_endian_and_terminated(self):
        packed = MENU.encode_pack({20: "Two", 10: "One\nLine"})
        self.assertEqual(struct.unpack_from("<4sIII", packed), (b"C3U1", 1, 53, 2))
        self.assertEqual(struct.unpack_from("<6I", packed, 16), (10, 40, 9, 20, 49, 4))
        self.assertEqual(packed[40:], b"One\nLine\0Two\0")

    def test_pack_rejects_console_injection_unknown_glyphs_and_oversize(self):
        for messages in ({}, {0: "A"}, {1: "A\x1b[2J"}, {1: "A\0B"},
                         {1: "A\x7f"}, {1: "\xe9"}, {1: "x" * 512},
                         {key: "x" * 511 for key in range(1, 65)}):
            with self.subTest(messages=repr(messages)[:100]), self.assertRaises(ValueError):
                MENU.encode_pack(messages)


if __name__ == "__main__":
    unittest.main()
