import hashlib
from fractions import Fraction
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare_boot_media as media
import prepare_staffroll as credits


class StaffrollTests(unittest.TestCase):
    def test_long_movie_needs_its_own_admission_bound(self):
        with self.assertRaises(ValueError):
            media.encode_header(5250, Fraction(30000, 1001), 0)
        data = media.encode_header(5250, Fraction(30000, 1001), 0, **{
            k: v for k, v in credits.LIMITS.items() if k != "max_input_bytes"})
        fields = struct.unpack("<4s15I", data)
        self.assertEqual(fields[2], 806400064)
        self.assertEqual(fields[10], fields[2])
        self.assertEqual(fields[11:14], (0, 0, 0))

    def test_overrun_rejected_before_starting_decoder(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "credits.c3v"
            with mock.patch.object(media, "admit_movie", return_value=(5251, Fraction(30000, 1001))), \
                 mock.patch.object(media, "decode_to") as decoder:
                with self.assertRaises(ValueError):
                    media.convert_movie(Path("unused"), output, credits.SOURCE, "unused", **credits.LIMITS)
                decoder.assert_not_called()
                self.assertFalse(output.exists())

    def test_size_bound_does_not_bypass_movie_identity(self):
        data = bytearray(80)
        struct.pack_into(">4sI", data, 0, b"THP\0", 0x11000)
        struct.pack_into(">fI", data, 16, 29.97, 5250)
        profile = {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
                   "frames": 5250, "audio": False}
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "staffroll.thp"
            path.write_bytes(data)
            self.assertEqual(media.admit_movie(path, profile, max_input_bytes=80),
                             (5250, Fraction(30000, 1001)))
            data[-1] ^= 1
            path.write_bytes(data)
            with self.assertRaises(ValueError):
                media.admit_movie(path, profile, max_input_bytes=80)

    def test_existing_output_rejected_before_conversion(self):
        with tempfile.TemporaryDirectory() as temporary:
            with mock.patch.object(media, "verify_disc", return_value=Path("unused")), \
                 mock.patch.object(media, "convert_movie") as decoder:
                with self.assertRaises(ValueError):
                    credits.prepare(Path("unused"), Path(temporary), "unused")
                decoder.assert_not_called()


if __name__ == "__main__":
    unittest.main()
