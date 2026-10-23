"""Synthetic GPT1/C3PT bounds and source-instruction regressions; no game bytes."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare_particle as particle
import prepare_wzx_damage as damage


class ParticlePreparationTests(unittest.TestCase):
    def bank(self, program=b"\x40\0\xff"):
        raw = bytearray(228)
        struct.pack_into(">5I", raw, 0, 0x47505431, 32, 128, 96, 224)
        struct.pack_into(">HH3I", raw, 32, 0x43, 0, 0, 1, 16)
        struct.pack_into(">4HI12f", raw, 48, 0, 0, 1, 1000, 0x01000000, *([0.] * 12))
        raw[108:128] = b"\xff" * 20
        raw[108:108 + len(program)] = program
        struct.pack_into(">II", raw, 128, 1, 32)
        struct.pack_into(">7I", raw, 160, 1, 1, 0, 8, 4, 0, 64)
        raw[192:224] = bytes(range(32))
        return raw

    def test_bank_resolves_checked_offsets_and_intensity_alpha(self):
        report = particle.bank(self.bank(), 0)
        self.assertEqual(report["closure"], [0])
        generator = report["generators"][0]
        self.assertEqual(generator["initialized_kind"], 0x09000000)
        self.assertEqual(generator["particle_life"], 1000)
        self.assertEqual(generator["generator_life"], 1)
        self.assertEqual(report["textures"][0]["rgba"][:8], b"\0" * 4 + b"\1" * 4)

    def test_bank_rejects_section_generator_reference_and_texture_bounds(self):
        for offset, value in ((4, 500), (8, 31), (16, 128), (40, 65), (44, 1000), (184, 9999)):
            raw = self.bank()
            struct.pack_into(">I", raw, offset, value)
            with self.assertRaises(ValueError):
                particle.bank(raw, 0)
        with self.assertRaisesRegex(ValueError, "REF table"):
            particle.bank(self.bank(b"\xf0\0\1\7\xff"), 0)
        with self.assertRaisesRegex(ValueError, "recursion"):
            particle.bank(self.bank(b"\xf0\0\0\7\xff"), 0)

    def test_ac_reads_both_source_floats_before_next_wait(self):
        raw = b"\xac\3" + struct.pack(">2f", 1., 2.) + b"\3\xff"
        instructions, children, frames = particle.program(raw)
        self.assertEqual(instructions[0]["args"], {"time": 3, "values": [1., 2.]})
        self.assertEqual(instructions[1]["offset"], 10)
        self.assertEqual(instructions[1]["args"], {"wait": 3})
        self.assertEqual((children, frames), ([], []))

    def test_unknown_truncated_nonfinite_and_unterminated_programs_fail(self):
        for raw in (b"\x81\xff", b"\xac\3", b"\xe8" + struct.pack(">f", float("nan")), b"\1", b"\xff\x12"):
            with self.assertRaises(ValueError):
                particle.program(raw)
        instructions, children, _ = particle.program(b"\xf0\0\5\7\xff")
        self.assertEqual(children, [5])
        self.assertEqual(instructions[0]["args"]["kind"], 7)

    def test_color_and_time_fields_preserve_all_channels_and_long_wait(self):
        raw = b"\xcf\x80\x81\1\2\3\4\x21\x02\xff"
        instructions, _, _ = particle.program(raw)
        self.assertEqual(instructions[0]["args"], {"time": 129, "rgba": [1, 2, 3, 4]})
        self.assertEqual(instructions[1]["args"]["wait"], 258)

    def test_i8_block_order_and_malformed_dimensions(self):
        data = bytes(range(64))
        rgba = particle.decode_i8(data, 16, 4)
        self.assertEqual(rgba[8 * 4:9 * 4], bytes((32,)) * 4)
        self.assertEqual(rgba[16 * 4:17 * 4], bytes((8,)) * 4)
        for width, height in ((7, 4), (8, 3), (512, 4)):
            with self.assertRaises(ValueError):
                particle.decode_i8(data, width, height)

    def test_i4_expands_full_range_including_alpha(self):
        rgba = particle.decode_i4(b"\xf8" + bytes(31), 8, 8)
        self.assertEqual(rgba[:8], b"\xff" * 4 + b"\x88" * 4)
        with self.assertRaises(ValueError):
            particle.decode_i4(bytes(31), 8, 8)

    def test_reflect_mark_loop_requires_a_wait_and_preserves_source_jump(self):
        instructions, _, _ = particle.program(b"\xfc\1\xfd\0\0")
        self.assertEqual([i["name"] for i in instructions], ["MARK", "WAIT", "JUMP_MARK"])
        self.assertEqual(instructions[-1]["args"]["target_instruction"], 1)
        for raw in (b"\xfc\xfd\0", b"\xfb\xff", b"\xfa\0\xff"):
            with self.assertRaises(ValueError):
                particle.program(raw)

    def test_extended_source_commands_have_exact_operand_boundaries(self):
        raw = (b"\xe9\x1f\x10\x40\x40\0\0" + b"\xb6\x40" + struct.pack(">f", -3.) +
               b"\xf3\1" + struct.pack(">2f", .5, -.5) + b"\x20" +
               b"\x9f" + struct.pack(">3f", 1., 2., 3.) + b"\xf1\0\2\xbc\1\2\xff")
        instructions, children, frames = particle.program(raw)
        self.assertEqual(instructions[0]["args"]["signed_ranges"], [64, 64, 0, 0])
        self.assertEqual(instructions[1]["offset"], 7)
        self.assertEqual(instructions[1]["args"]["time"], 64)
        self.assertEqual(instructions[2]["args"]["time"], 32)
        self.assertEqual(instructions[3]["args"]["values"], [1., 2., 3.])
        self.assertEqual(children, [2])
        self.assertEqual(frames, [1, 2])

    def test_native_container_preserves_original_bank_and_hash_binding(self):
        raw = self.bank()
        report = particle.bank(raw, 0)
        pack = particle.encode(raw, report, "12" * 32)
        magic, version, total, root, offset, size, count, table = struct.unpack_from("<4s7I", pack)
        self.assertEqual((magic, version, total, root, size, count, table), (b"C3PT", 1, len(pack), 0, 228, 1, 128))
        self.assertEqual(pack[offset:offset + size], raw)
        self.assertEqual(pack[32:64], bytes.fromhex("12" * 32))
        self.assertEqual(pack[64:96], hashlib.sha256(pack[128:]).digest())
        self.assertEqual(pack[96:128], bytes(32))
        self.assertNotIn("rgba", particle.report_json(report)["textures"][0])

    def test_damage_descriptor_uses_separate_source_sound_time_and_hashes(self):
        report = {"owner_clip": 2, "anchor": [0., 0., 0.], "particle_attachment_sampled_bone": 11}
        packed = damage.encode(report, b"synthetic-model", b"synthetic-particles")
        self.assertEqual(len(packed), 256)
        self.assertEqual(struct.unpack_from("<4s", packed)[0], b"C3WD")
        self.assertEqual(struct.unpack_from("<I", packed, 4)[0], 2)
        self.assertEqual(struct.unpack_from("<4I", packed, 32), (0, 0, 22, 0))
        self.assertEqual(struct.unpack_from("<2I", packed, 52), (22, 380))
        self.assertEqual(struct.unpack_from("<2I", packed, 220), (22, 22))
        self.assertEqual(packed[228:236], bytes(8))
        self.assertEqual(struct.unpack_from("<3I", packed, 84), (0, 12, 11))
        self.assertEqual(packed[180:200], hashlib.sha1(b"synthetic-model").digest())
        self.assertEqual(packed[200:220], hashlib.sha1(b"synthetic-particles").digest())
        self.assertEqual(packed[236:], hashlib.sha1(packed[:236]).digest())


if __name__ == "__main__":
    unittest.main()
