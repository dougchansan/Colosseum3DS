"""Synthetic structural/rejection tests; no original assets are checked in."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import prepare_story_index as index


def word(data, at, value):
    struct.pack_into(">I", data, at, value)


def bank(code, entries=None):
    entries = [28] if entries is None else entries
    start = 24 + len(entries) * 4
    data = bytearray(start)
    struct.pack_into(">HHH", data, 0, 0x100, 0x101, len(entries))
    word(data, 16, start)
    for i, pc in enumerate(entries):
        word(data, 24 + 4 * i, pc)
    return bytes(data) + code


def push(value):
    return bytes((8, 2)) + struct.pack(">I", value)


def call(count):
    return b"\x25\0\0" + struct.pack(">H", count)


def archive():
    data = bytearray(160)
    data[:4] = b"FSYS"
    for at, value in ((12, 1), (24, 64), (32, 160), (64, 80), (80, 96),
                      (96, 0x01000E00), (100, 144), (104, 8), (116, 8)):
        word(data, at, value)
    return data


def common_rel():
    data = bytearray(0x100 + 0x14A800 + 32)
    for at, value in ((0, 125), (12, 11), (16, 0x50), (0x70, 0x100),
                      (0x74, 0x14A800), (0x28, len(data) - 32), (0x2C, 8)):
        word(data, at, value)
    for _, _, count_at, count in index.TABLES.values():
        word(data, 0x100 + count_at, count)
    imp = len(data) - 32
    word(data, imp, 0)
    word(data, imp + 4, imp + 8)
    # A raw-zero callback relocates into DOL code. Large skip uses NOP.
    struct.pack_into(">HBBI", data, imp + 8, 0, 202, 4, 0)
    target = 0x8BCF4 + 15 * 4
    chunks = []
    while target > 65535:
        chunks.append(struct.pack(">HBBI", 65535, 201, 0, 0))
        target -= 65535
    chunks.append(struct.pack(">HBBI", target, 1, 0, 0x800361C0))
    chunks.append(struct.pack(">HBBI", 0, 203, 0, 0))
    data[imp + 16:] = b"".join(chunks)
    return data


class StoryIndexTests(unittest.TestCase):
    def test_literal_flag_writer_and_unknown_native_return(self):
        data = bank(b"\x02" + push(0x4E) + push(0x476) + call(2) +
                    push(0x98) + push(1) + push(0) + call(4) + b"\x01")
        report = index.bank_index(data)
        first, battle = report["native_calls"]
        self.assertEqual(first["dependency"]["kind"], "flag-set-one")
        self.assertEqual(first["dependency"]["target"], 0x476)
        self.assertEqual(first["functions"], [0])
        # Return slot of the preceding call cannot be invented as encounter.
        self.assertIsNone(battle["native"])

    def test_immediate_load_preserves_constant_variable_load_does_not(self):
        for descriptor, expected in ((0x82, 0x4E), (2, None)):
            data = bank(b"\x02" + push(0x4E) + bytes((0x13, descriptor)) + push(7) + call(2) + b"\x01")
            self.assertEqual(index.bank_index(data)["native_calls"][0]["native"], expected)

    def test_null_entries_and_duplicate_entry_ids_are_retained(self):
        result = index.bank_index(bank(b"\x01", [36, 0, 36]))
        self.assertEqual([f["offset"] for f in result["functions"]], [36, 0, 36])
        self.assertEqual(result["functions"][1]["status"], "null-entry")

    def test_local_function_argument_order_and_script_context_binding(self):
        # Opcode4 has result-slot count followed by argument count.
        code = push(441) + push(442) + push(2169) + b"\x04" + struct.pack(">IHH", 1, 0, 3) + b"\x01"
        data = bank(code + b"\x05", [32, 32 + len(code)])
        result = index.bank_index(data)
        edge = result["control_flow_edges"][0]
        self.assertEqual([a["value"] for a in edge["arguments"]], [441, 442, 2169])
        self.assertEqual(edge["return_slots"], 0)
        self.assertEqual(edge["caller_functions"], [0])
        result["archive_group"] = 149
        self.assertEqual(index.script_reference([result], 149, 0x1000001)["candidates"],
                         [{"bank_index": 0, "function": 1}])
        self.assertEqual(index.script_reference([result], 150, 0x1000001)["status"], "unresolved")

    def test_reject_branch_operand_overlap_and_truncation(self):
        # Branch targets the middle of a push operand.
        bad = bank(push(0) + b"\x07" + struct.pack(">I", 30) + b"\x01")
        with self.assertRaises(ValueError):
            index.bank_index(bad)
        with self.assertRaises(ValueError):
            index.bank_index(bank(b"\x08\x02"))

    def test_conditional_branch_unknown_and_block_literals_unknown(self):
        code = push(0x51) + push(1) + b"\x07" + struct.pack(">I", 50) + call(1) + b"\x01"
        result = index.bank_index(bank(code))
        self.assertEqual(result["control_flow_edges"][0]["condition"], "unknown")
        self.assertIsNone(result["native_calls"][0]["native"])

    def test_fsys_entry_extent_count_and_overlap_rejection(self):
        valid = archive()
        self.assertEqual(index.archive_entries(valid)[0]["resource_id"], 0x01000E00)
        for at, value in ((12, 0xFFFFFFFF), (100, 159), (100, 100)):
            bad = bytearray(valid)
            word(bad, at, value)
            with self.assertRaises(ValueError):
                index.archive_entries(bad)
        word(valid, 108, 0x80000000)
        with self.assertRaises(ValueError):
            index.archive_entries(valid)

    def test_rel_metadata_retains_all_rows_and_raw_zero_relocation(self):
        data = common_rel()
        result = index.metadata(data)
        self.assertEqual({k: len(v) for k, v in result.items()},
                         {"lifecycle": 196, "events": 594, "names": 607})
        # All-zero duplicate floors/events/names remain separate indexed rows.
        self.assertEqual(result["lifecycle"][-1]["index"], 195)
        cb = result["lifecycle"][0]["native_callbacks"]["pre_native"]
        self.assertEqual(cb["raw_word"], 0)
        self.assertEqual(cb["status"], "relocated")
        self.assertEqual(cb["relocations"][0]["addend"], 0x800361C0)
        self.assertEqual(cb["relocations"][0]["import_module"], 0)

    def test_rel_rejects_bad_counts_sections_and_unterminated_stream(self):
        for mutate in (lambda d: word(d, 0x100 + 0x14A670, 0xFFFFFFFF),
                       lambda d: (word(d, 0x78, 0x100), word(d, 0x7C, 0x100)),
                       lambda d: d.__setitem__(slice(-8, None), b"\0" * 8)):
            data = common_rel()
            mutate(data)
            with self.assertRaises(ValueError):
                index.metadata(data)

    def test_source_admission_and_toc_path_escape(self):
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "source"
            path.write_bytes(b"test")
            with self.assertRaises(ValueError):
                index.pinned(path, 4, "0" * 64)
        toc = bytearray(64)
        toc[:4] = b"GLLA"
        for at, value in ((8, 1), (16, 32), (32, 1), (36, 48)):
            word(toc, at, value)
        toc[48:56] = b"../evil\0"
        with self.assertRaises(ValueError):
            index.archive_names(toc)
        toc[48:56] = b"safeeeee"
        # TOC names must terminate before their directory; a truncated string
        # is invalid even if the older shared name reader accepts its bytes.
        word(toc, 16, 48)
        word(toc, 48, 1)
        word(toc, 52, 60)
        toc[60:] = b"safe"
        with self.assertRaises(ValueError):
            index.toc_names(toc)

    def test_atomic_new_directory_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as td:
            out = Path(td) / "new"
            index.write_index(out, {"summary": {"rows": 1}})
            self.assertEqual(json.loads((out / "summary.json").read_text()), {"rows": 1})
            before = (out / "story-source-index.json").read_bytes()
            with self.assertRaises(ValueError):
                index.write_index(out, {"summary": {}})
            self.assertEqual((out / "story-source-index.json").read_bytes(), before)
            self.assertFalse(list(Path(td).glob(".story-index-*")))


if __name__ == "__main__":
    unittest.main()
