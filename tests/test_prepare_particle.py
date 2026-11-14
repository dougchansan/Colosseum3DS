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

    def test_growl_is_explicit_original_bank_only_and_preserves_source_ids(self):
        with self.assertRaises(ValueError):particle.bank(self.bank(),0,profile='growl')
        with self.assertRaises(ValueError):particle.bank(self.bank(),0,profile='unknown')
        path=Path(__file__).resolve().parents[2]/'growl-source-bank-v1/growl.gpt'
        if not path.exists():self.skipTest('Private original Growl bank required')
        raw=path.read_bytes()
        with self.assertRaisesRegex(ValueError,'generator/reference bound'):particle.bank(raw,18)
        for args in ((0,()),(18,(93,))):
            with self.assertRaises(ValueError):particle.bank(raw,*args,profile='growl')
        changed=bytearray(raw);changed[-1]^=1
        with self.assertRaises(ValueError):particle.bank(changed,18,profile='growl')
        report=particle.bank(raw,18,profile='growl')
        self.assertEqual(report['generator_count'],113)
        self.assertEqual(report['closure'],[18,93,111,112])
        self.assertEqual(len(report['textures']),25)
        self.assertEqual({t['group'] for t in report['textures']},{0,1})
        self.assertEqual(report['references'],list(range(113)))
        pack=particle.encode(raw,report,'12'*32)
        offset,size=struct.unpack_from('<2I',pack,16)
        self.assertEqual(pack[offset:offset+size],raw)

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

    # --- General path (C3PT v2) -------------------------------------------
    def test_general_program_admits_source_table_and_refuses_unsafe(self):
        raw = (b"\xa9" + struct.pack(">f", .3) + b"\x8b" + struct.pack(">2f", 1., 2.) +
               b"\xfa\0\1\xfb\xaa\0\2\0\3\xff\xe1")
        instructions, refs, direct, poses = particle.general_program(raw)
        self.assertEqual(instructions[1]["args"]["values"], [1., 2., 0.])
        self.assertEqual(refs, [2, 3, 4])
        self.assertEqual(instructions[-1]["opcode"], 0xFF)  # tail after FF unreachable
        _, _, direct, _ = particle.general_program(b"\xa5\0\7\xef\0\x09\3\xb9\0\5\xff")
        self.assertEqual(direct, [7, 9, 5])
        for bad, why in ((b"\x81" + struct.pack(">f", 1.) + b"\xff", "partial vector set"),
                         (b"\xfa\0\xfb\xff", "zero-time loop"), (b"\xfc\xfd", "zero-time mark"),
                         (b"\xe1\xff", "outside"), (b"\xa0", "truncated"), (b"\1", "lacks an end")):
            with self.assertRaisesRegex(ValueError, why):
                particle.general_program(bad)
        self.assertEqual(len(particle.general_program(b"\1\xfb\xfd")[0]), 3)  # FB with no FA wraps to 0

    def test_gx_formats_tile_order_and_channels(self):
        ia8 = bytes([0x80, 0x10] * 16)
        self.assertEqual(particle.decode_gx(3, ia8, 4, 4)[:4], bytes((0x10, 0x10, 0x10, 0x80)))
        rgb565 = bytes([0xF8, 0x00] + [0] * 30)
        self.assertEqual(particle.decode_gx(4, rgb565, 4, 4)[:4], bytes((255, 0, 0, 255)))
        rgba8 = bytes([0x7F, 0x11] + [0] * 30 + [0x22, 0x33] + [0] * 30)
        self.assertEqual(particle.decode_gx(6, rgba8, 4, 4)[:4], bytes((0x11, 0x22, 0x33, 0x7F)))
        self.assertEqual(particle.decode_gx(2, bytes([0xA5] * 32), 8, 4)[:4], bytes((0x55, 0x55, 0x55, 0xAA)))
        rgba, w, h = particle.fit_texture(bytes(range(256)) * (128 * 128 * 4 // 256), 128, 128)
        self.assertEqual((w, h, len(rgba)), (64, 64, 64 * 64 * 4))
        rgba, w, h = particle.fit_texture(bytes(4 * 4 * 4), 4, 4)
        self.assertEqual((w, h), (8, 8))
        with self.assertRaises(ValueError):
            particle.decode_gx(8, b"", 8, 8)

    def test_general_bank_disables_children_and_encodes_v2(self):
        raw = self.bank(b"\xf0\0\0\7\xff")  # root spawns itself: a cycle is admitted
        report = particle.general_bank(raw, [0])
        self.assertEqual((report["closure"], report["disabled"]), ([0], {}))
        pack = particle.encode_general(raw, report, "12" * 32)
        magic, version, total, root, offset, size, count, table = struct.unpack_from("<4s7I", pack)
        self.assertEqual((magic, version, total, root, size, count, table), (b"C3PT", 2, len(pack), 0, 228, 1, 128))
        self.assertEqual(struct.unpack_from("<I", pack, 96)[0], 1)
        self.assertEqual(struct.unpack_from("<I", pack, 128 + 32)[0], 0)
        self.assertEqual(offset, 192)
        self.assertEqual(pack[offset:offset + size], raw)
        self.assertEqual(pack[64:96], hashlib.sha256(pack[128:]).digest())
        width, height = struct.unpack_from("<2I", pack, 136)
        self.assertEqual((width, height), (8, 8))  # 8x4 I8 resampled to a PICA edge
        camera = bytearray(raw)
        struct.pack_into(">I", camera, 56, 0x01020000)
        with self.assertRaisesRegex(ValueError, "root generator camera basis"):
            particle.general_bank(camera, [0])

if __name__ == "__main__":
    unittest.main()
