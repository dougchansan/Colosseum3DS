"""Synthetic story-pack/metadata regressions; no original assets are needed."""
import hashlib
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import prepare_story as story

DATA = 0x100  # Section 4 file offset in the synthetic common REL.


def word(data, at, value):
    struct.pack_into(">I", data, at, value)


def common_rel():
    """REL125 layout with the published table counts and otherwise zero rows."""
    data = bytearray(DATA + 0x14A800)
    for at, value in ((0, 125), (12, 11), (16, 0x50), (0x70, DATA), (0x74, 0x14A800),
                      (DATA + 0x14A704, 594), (DATA + 0x14A670, 196), (DATA + 0x14A660, 607)):
        word(data, at, value)
    return data


def floor_rel(module, people_offset, rows):
    """Floor REL with ten sections; section 3 is its data."""
    size = people_offset + len(rows) * 36
    data = bytearray(0x100 + size)
    for at, value in ((0, module), (12, 10), (16, 0x40), (0x40 + 24, 0x100), (0x40 + 28, size)):
        word(data, at, value)
    for index, row in enumerate(rows):
        start = 0x100 + people_offset + index * 36
        data[start:start + 36] = row
    return bytes(data)


def raw_archive(members):
    """FSYS with uncompressed members, in the directory layout member() reads."""
    count = len(members)
    table, entries = 72, 72 + 4 * count
    payload = entries + 40 * count
    data = bytearray(payload + sum(len(blob) for _, blob in members))
    data[:4] = b"FSYS"
    for at, value in ((12, count), (24, 64), (32, len(data)), (64, table)):
        word(data, at, value)
    cursor = payload
    for index, (identity, blob) in enumerate(members):
        entry = entries + 40 * index
        word(data, table + 4 * index, entry)
        for at, value in ((0, identity), (4, cursor), (8, len(blob)), (12, 0), (20, len(blob))):
            word(data, entry + at, value)
        data[cursor:cursor + len(blob)] = blob
        cursor += len(blob)
    return bytes(data)


def person(flags, policy, name_index):
    row = bytearray(36)
    row[0], row[1] = flags, policy
    struct.pack_into(">H", row, 8, name_index)
    return bytes(row)


def fixture():
    """One synthetic later floor (3) beside an unrelated floor's rows."""
    rel = common_rel()
    events = DATA + 0x146364
    struct.pack_into(">BBH6I", rel, events + 5 * 28, 1, 0, 3, 9, 0x05960004, 8, 0, 1, 0)
    struct.pack_into(">BBH6I", rel, events + 6 * 28, 2, 0, 3, 12, 0x01000023, 0, 0, 0, 0xFFFFFFFF)
    struct.pack_into(">BBH6I", rel, events + 7 * 28, 1, 0, 1, 4, 0x05960004, 2, 0, 1, 0)
    life = (0x20020000, 0x1F, 0, 3, 0, 0, 0, 0, 0, 3517,
            0x1000000, 0x1000001, 0x1000002, 0x1000007, 0x1000003, 0, 0, 0, 0)
    struct.pack_into(">19I", rel, DATA + 0x8BCF4 + 2 * 0x4C, *life)
    struct.pack_into(">HHI", rel, DATA + 0x1A34 + 604 * 8, 2911, 0, 0x10A8)
    rows = [person(0x90, 0x00, 0), person(0xD0, 0x90, 604)]
    archive = raw_archive([(0x06611C00, floor_rel(15, 0xA0, rows))])
    areas = {3: ("X.fsys", 0x013E0E00, 0x015A0A00, "x.c3s")}
    tables = {3: ("X.fsys", 0x06611C00, 15, 0xA0, 2)}
    return {"common_rel.rel": bytes(rel), "X.fsys": archive}, areas, tables, life


class StoryProfileTests(unittest.TestCase):
    def test_default_tables_are_the_original_first_area(self):
        self.assertEqual(story.PROFILES, {
            "S1_out.fsys": (1468000, "37900ccab1353ab835c7e4acc40cd5e1349237589a53b441c78ce91a891f8034"),
            "S1_shop_1F.fsys": (2947200, "7d74dcb75c7f8aebc7479fee695e87a02ab58112d110dada15e88353d7215329"),
            "common_rel.rel": (1369520, "0c89e679305f5e84eff3c774b542120c727ff350a0841cfc516235b3556df69e")})
        self.assertEqual(list(story.PROFILES), ["S1_out.fsys", "S1_shop_1F.fsys", "common_rel.rel"])
        self.assertEqual(story.AREAS, {1: ("S1_out.fsys", 0x01430E00, 0x01420A00, "outskirt.c3s"),
                                       2: ("S1_shop_1F.fsys", 0x01460E00, 0x01450A00, "outskirt_shop.c3s")})
        self.assertEqual(story.ACTOR_TABLES, {1: ("S1_out.fsys", 0x02571C00, 131, 0x30, 7),
                                              2: ("S1_shop_1F.fsys", 0x02581C00, 5, 0x180, 5)})

    def test_profile_floor_selects_its_own_archive_and_tables(self):
        self.assertEqual(story.area_table(("phenac",)),
                         {3: ("M1_out.fsys", 0x013E0E00, 0x015A0A00, "phenac.c3s")})
        self.assertEqual(story.actor_table(("phenac",)), {3: ("M1_out.fsys", 0x06611C00, 15, 0xA0, 27)})
        profiles = story.source_profiles(("phenac",))
        self.assertEqual(list(profiles), ["M1_out.fsys", "common_rel.rel"])
        self.assertEqual(profiles["M1_out.fsys"],
                         (2554304, "85b1bbd23d9dc32287f90ac19eb8b8bd1b42b83dcfa9c92025490aaf5b2b5987"))
        self.assertEqual(profiles["common_rel.rel"], story.COMMON_REL_PROFILE)
        with self.assertRaises(KeyError):
            story.area_table(("nowhere",))

    def test_cli_rejects_an_unknown_floor_before_reading_anything(self):
        with tempfile.TemporaryDirectory() as temp:
            result = subprocess.run([sys.executable, str(ROOT / "tools/prepare_story.py"),
                                     "--disc", str(Path(temp) / "missing"), "--output",
                                     str(Path(temp) / "out"), "--floor", "nowhere"],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("Traceback", result.stderr)
            self.assertFalse((Path(temp) / "out").exists())


class StoryMetadataTests(unittest.TestCase):
    def test_metadata_is_filtered_by_the_selected_floor(self):
        originals, areas, tables, life = fixture()
        result = story.extract_area_metadata(originals, areas, tables)
        self.assertEqual(result["area-events.json"], [
            {"index": 5, "kind": 1, "floor": 3, "source_id": 9, "script": "0x5960004",
             "args": [8, 0, 1, 0], "file_offset": hex(DATA + 0x146364 + 5 * 28)},
            {"index": 6, "kind": 2, "floor": 3, "source_id": 12, "script": "0x1000023",
             "args": [0, 0, 0, 0xFFFFFFFF], "file_offset": hex(DATA + 0x146364 + 6 * 28)}])
        (row,) = result["area-lifecycle.json"]
        self.assertEqual((row["floor"], row["resource_group"], row["index"]), (3, 0x1F, 2))
        self.assertEqual(row["raw_words"], list(life))
        self.assertEqual([row[key] for key in ("init_script", "pre_script", "main_script", "post_script")],
                         [0x1000001, 0x1000002, 0x1000007, 0x1000003])
        self.assertEqual([row[key] for key in ("pre_native", "main_native", "post_native")], [0, 0, 0])
        self.assertEqual(result["actor-names.json"], [
            {"floor": 3, "actor": 0, "name_index": 0, "hear_flag": 0, "name_message": 0,
             "talk_start": 0, "talk_end": 0},
            {"floor": 3, "actor": 1, "name_index": 604, "hear_flag": 2911, "name_message": 0x10A8,
             "talk_start": 2, "talk_end": 1}])

    def test_metadata_rejects_missing_lifecycle_bad_counts_and_name_index(self):
        originals, areas, tables, _ = fixture()
        with self.assertRaisesRegex(ValueError, "lifecycle"):
            story.extract_area_metadata(originals, {4: areas[3]}, {})
        duplicate = bytearray(originals["common_rel.rel"])
        word(duplicate, DATA + 0x8BCF4 + 9 * 0x4C + 12, 3)
        with self.assertRaisesRegex(ValueError, "lifecycle"):
            story.extract_area_metadata({**originals, "common_rel.rel": bytes(duplicate)}, areas, tables)
        counts = bytearray(originals["common_rel.rel"])
        word(counts, DATA + 0x14A704, 595)
        with self.assertRaisesRegex(ValueError, "table counts"):
            story.extract_area_metadata({**originals, "common_rel.rel": bytes(counts)}, areas, tables)
        rows = [person(0x90, 0, 607)]
        archive = raw_archive([(0x06611C00, floor_rel(15, 0xA0, rows))])
        with self.assertRaisesRegex(ValueError, "name index"):
            story.extract_area_metadata({**originals, "X.fsys": archive}, areas,
                                        {3: ("X.fsys", 0x06611C00, 15, 0xA0, 1)})
        # A people table that leaves the REL data section is refused.
        with self.assertRaises(ValueError):
            story.extract_area_metadata(originals, areas, {3: ("X.fsys", 0x06611C00, 15, 0xA0, 3)})

    def test_story_pack_layout_and_runtime_size_bound(self):
        def blob(identity, size):
            data = bytearray(size)
            struct.pack_into(">H", data, 0, identity)
            return bytes(data)
        banks, messages = [blob(0x100, 40), blob(0x596, 24)], [blob(0x15A, 16), blob(0x3E8, 8), blob(0x3E9, 12)]
        pack = story.encode_pack(3, banks, messages)
        self.assertEqual(struct.unpack_from("<4s7I", pack, 0),
                         (b"C3S1", 1, len(pack), 3, 2, 3, 64, 64 + 32))
        offset = 64 + 16 * 5
        for index, data in enumerate(banks + messages):
            identity, at, size, reserved = struct.unpack_from("<4I", pack, 64 + 16 * index)
            self.assertEqual((identity, at, size, reserved), (struct.unpack_from(">H", data, 0)[0], offset, len(data), 0))
            self.assertEqual(pack[at:at + size], data)
            offset += len(data)
        self.assertEqual(offset, len(pack))
        with self.assertRaisesRegex(ValueError, "allocation limit"):
            story.encode_pack(3, [blob(0x100, 1048576)], [])


def script_bank(code, entries):
    start = 24 + len(entries) * 4
    data = bytearray(start)
    struct.pack_into(">HHH", data, 0, 0x100, 0x101, len(entries))
    word(data, 16, start)
    for index, at in enumerate(entries):
        word(data, 24 + 4 * index, at)
    return bytes(data) + code


class BankAndDoorTests(unittest.TestCase):
    def test_null_function_entries_are_reported_only_when_admitted(self):
        # Two present functions (a lone return each) around one empty slot.
        bank = script_bank(b"\x05\x05", [36, 0, 37])
        with self.assertRaisesRegex(ValueError, "globals"):
            story.inspect_bank(bank)
        info = story.inspect_bank(bank, allow_null_entries=True)
        self.assertEqual(info["functions"], [{"index": 0, "offset": 36}, {"index": 1, "offset": 0},
                                             {"index": 2, "offset": 37}])
        self.assertEqual(info["null_function_entries"], [1])
        self.assertEqual((info["globals"], info["reachable_instructions"]), (0, 2))
        # A bank without empty slots reports exactly what it did before.
        full = script_bank(b"\x05\x05", [32, 33])
        self.assertEqual(story.inspect_bank(full), story.inspect_bank(full, allow_null_entries=True))
        self.assertNotIn("null_function_entries", story.inspect_bank(full, allow_null_entries=True))
        # A call into an empty slot is refused; an all-empty table is not a bank.
        call = b"\x04" + struct.pack(">IHH", 1, 0, 0) + b"\x05"
        with self.assertRaisesRegex(ValueError, "function call"):
            story.inspect_bank(script_bank(call, [32, 0]), allow_null_entries=True)
        with self.assertRaisesRegex(ValueError, "globals"):
            story.inspect_bank(script_bank(b"\x05", [0]), allow_null_entries=True)

    def test_door_records_are_filtered_by_floor_and_keep_their_table_index(self):
        rel = common_rel()
        word(rel, DATA + 0x14A690, 4)
        rows = ((0, 1, -1, -1, 255, -1, -1, 1, 0, 0, 5, 3, 7, 0, 0, 0x00621005),
                (0, 1, -1, -1, 255, -1, -1, 1, 0, 0, 0, 13, 8, 2487, 0, 0x01CE1000),
                (0, 1, -1, -1, 255, 3, 2, 2, 0, 0, -1, 11, 0, 0, 0, 0x01C91000),
                (0, 1, -1, -1, 255, -1, -1, 1, 0, 0, 7, 13, 9, 2487, 0, 0x01CE1001))
        for index, row in enumerate(rows):
            struct.pack_into(">4bB2b2BBh3HHI", rel, DATA + 0x8FE24 + index * 24, *row)
        doors = story.extract_doors(bytes(rel), {13})
        self.assertEqual([door["index"] for door in doors], [1, 3])
        self.assertEqual(doors[1], {"index": 3, "open_anim": 0, "close_anim": 1, "open_end_anim": -1,
                                    "close_end_anim": -1, "pad04": 255, "alternate_anim_a": -1,
                                    "alternate_anim_b": -1, "door_type": 1, "part_index": 0, "pad09": 0,
                                    "collision_id": 7, "floor": 13, "open_flag": 9, "close_flag": 2487,
                                    "pad12": 0, "resource_id": "01ce1001",
                                    "file_offset": hex(DATA + 0x8FE24 + 3 * 24)})
        elevator = story.extract_doors(bytes(rel), {11})[0]
        self.assertEqual((elevator["door_type"], elevator["collision_id"], elevator["alternate_anim_a"],
                          elevator["alternate_anim_b"]), (2, -1, 3, 2))
        self.assertEqual(story.extract_doors(bytes(rel), {99}), [])
        self.assertEqual(len(story.DOOR_FIELDS), 16)
        for count in (0, 1025):
            word(rel, DATA + 0x14A690, count)
            with self.assertRaisesRegex(ValueError, "door table"):
                story.extract_doors(bytes(rel), {13})


class CommonRelAdmissionTests(unittest.TestCase):
    def setUp(self):
        self.rel = bytes(common_rel()[:0x200])
        self.profile = (len(self.rel), hashlib.sha256(self.rel).hexdigest())

    def test_loose_module_and_archive_member_give_the_same_admitted_bytes(self):
        archive = raw_archive([(0x066F1400, b"other"), (0x01C71C00, self.rel)])
        archives = {"common.fsys": (len(archive), hashlib.sha256(archive).hexdigest())}
        with tempfile.TemporaryDirectory() as temp, \
                mock.patch.object(story, "COMMON_REL_PROFILE", self.profile), \
                mock.patch.dict(story.ARCHIVE_PROFILES, archives):
            files = Path(temp)
            (files / "common.fsys").write_bytes(archive)
            self.assertEqual(story.admit_common_rel(files), self.rel)
            (files / "common_rel.rel").write_bytes(self.rel)
            self.assertEqual(story.admit_common_rel(files), self.rel)
            # A present loose file is authoritative: it is never skipped.
            (files / "common_rel.rel").write_bytes(self.rel[:-1] + b"\x01")
            with self.assertRaisesRegex(ValueError, "common_rel.rel"):
                story.admit_common_rel(files)

    def test_archive_fallback_checks_the_archive_and_the_module(self):
        archive = raw_archive([(0x01C71C00, self.rel)])
        archives = {"common.fsys": (len(archive), hashlib.sha256(archive).hexdigest())}
        with tempfile.TemporaryDirectory() as temp, \
                mock.patch.object(story, "COMMON_REL_PROFILE", self.profile):
            files = Path(temp)
            with mock.patch.dict(story.ARCHIVE_PROFILES, archives):
                with self.assertRaises(ValueError):
                    story.admit_common_rel(files)  # neither source exists
                changed = bytearray(archive)
                changed[-1] ^= 1
                (files / "common.fsys").write_bytes(changed)
                with self.assertRaisesRegex(ValueError, "common.fsys"):
                    story.admit_common_rel(files)
                (files / "common.fsys").write_bytes(archive[:-1])
                with self.assertRaisesRegex(ValueError, "common.fsys"):
                    story.admit_common_rel(files)
            # An admitted archive whose module is not the admitted REL fails.
            other = bytearray(self.rel)
            other[-1] ^= 1
            archive = raw_archive([(0x01C71C00, bytes(other))])
            (files / "common.fsys").write_bytes(archive)
            with mock.patch.dict(story.ARCHIVE_PROFILES,
                                 {"common.fsys": (len(archive), hashlib.sha256(archive).hexdigest())}):
                with self.assertRaisesRegex(ValueError, "common_rel.rel"):
                    story.admit_common_rel(files)
            # Zero or two REL125 members are refused rather than guessed.
            for members in ([(0x01C71400, self.rel)], [(0x01C71C00, self.rel), (0x01C81C00, self.rel)]):
                archive = raw_archive(members)
                (files / "common.fsys").write_bytes(archive)
                with mock.patch.dict(story.ARCHIVE_PROFILES,
                                     {"common.fsys": (len(archive), hashlib.sha256(archive).hexdigest())}):
                    with self.assertRaisesRegex(ValueError, "REL125"):
                        story.admit_common_rel(files)


class DoorPackTests(unittest.TestCase):
    """C3D1 as src/area.c c3_area_doors_parse reads it. Synthetic rows only."""

    def row(self, index, floor=3, **changes):
        values = {"index": index, "floor": floor, "open_anim": 0, "close_anim": 1, "open_end_anim": -1,
                  "close_end_anim": -1, "alternate_anim_a": -1, "alternate_anim_b": -1, "door_type": 1,
                  "part_index": 0, "collision_id": index, "open_flag": 2 + index, "close_flag": 0,
                  "resource_id": "%08x" % (0x00621000 + index)}
        values.update(changes)
        return values

    def test_layout(self):
        pack = story.encode_door_pack(3, [self.row(1, collision_id=-1, close_flag=0x987), self.row(0)])
        self.assertEqual(struct.unpack_from("<4s5I", pack), (b"C3D1", 1, 32 + 2 * 64, 3, 2, 64))
        self.assertEqual(pack[24:32], bytes(8))
        first, second = pack[32:96], pack[96:160]
        self.assertEqual(struct.unpack_from("<I6b2BhHHHI", first), (0, 0, 1, -1, -1, -1, -1, 1, 0, 0, 2, 0, 0, 0x00621000))
        self.assertEqual(struct.unpack_from("<I6b2BhHHHI", second), (1, 0, 1, -1, -1, -1, -1, 1, 0, -1, 3, 0x987, 0, 0x00621001))
        self.assertEqual(first[24:] + second[24:], bytes(80))  # no prepared door model names yet

    def test_rejects_rows_the_runtime_cannot_hold(self):
        with self.assertRaises(ValueError):
            story.encode_door_pack(3, [])
        with self.assertRaises(ValueError):
            story.encode_door_pack(3, [self.row(0, floor=13)])
        with self.assertRaises(ValueError):
            story.encode_door_pack(3, [self.row(i) for i in range(17)])
        with self.assertRaises(ValueError):
            story.encode_door_pack(3, [self.row(0, open_anim=64)])
        with self.assertRaises(ValueError):
            story.encode_door_pack(3, [self.row(0, collision_id=-2)])


if __name__ == "__main__":
    unittest.main()
