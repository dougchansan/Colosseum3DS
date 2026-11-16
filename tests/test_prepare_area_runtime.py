"""Synthetic metadata only; test the private converter's admission boundary."""
import copy
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("prepare_area_runtime", ROOT / "tools/prepare_area_runtime.py")
area_runtime = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(area_runtime)


def fixture():
    actor = {"index": 1, "model_resource": 0x123400, "people_info": 10,
             "people_info_raw": "04"+"00"*43,
             "raw_words": [0x87654321, 0, 0x10203040, 0x50607080],
             "move_script": 0x1000020, "talk_script": 0x1000030,
             "position": [2, 3, 4], "yaw_degrees": 90, "radius": .4,
             "walk_distance": .9, "run_distance": 1.3,
             "motion_indices": [1, 2, 3, 255, 255, 255, 255, 255, 8], "file": "actor_1.c3t"}
    surface = {"vertices": [0, -3, -3, 0, 3, -3, 0, 0, 3], "normal": [1, 0, 0],
               "flags": 0x1234, "source_id": 7, "object": 2}
    area = {"floor": 1, "actors": [actor], "transition_surfaces": [surface],
            "interaction_surfaces": [copy.deepcopy(surface)]}
    links = [{"floor": 1, "kind": kind, "source_id": 7, "script": "0x2000001",
              "args": [100, -1, 0xFFFFFFFF, -0x80000000], "index": kind+40} for kind in (1, 3)]
    lifecycle = [{"floor": 1, "resource_group": 29, "raw_words": [0]*10+[0x1000010],
                  "init_script": 0x1000011, "pre_script": 0x1000012, "main_script": 0x1000013,
                  "post_script": 0x1000014, "pre_native": 0, "main_native": 0, "post_native": 0}]
    names = [{"floor": 1, "actor": 1, "name_index": 500, "hear_flag": 2000,
              "name_message": 0x54321, "talk_start": 2, "talk_end": 1}]
    return area, links, lifecycle, names


class AreaRuntimeTests(unittest.TestCase):
    def test_exact_layout_lifecycle_names_and_id_namespaces(self):
        inputs = fixture()
        encoded = area_runtime.encode(*inputs)
        self.assertEqual(len(encoded), 128+160+2*56+2*32)
        self.assertEqual(encoded[:4], b"C3A1")
        self.assertEqual(struct.unpack_from("<15I", encoded, 4),
                         (1, len(encoded), 1, 29, 1, 1, 1, 128, 288, 344, 160, 56, 0, 2, 400))
        self.assertEqual(encoded[64:84], bytes.fromhex(area_runtime.PIN))
        self.assertEqual(struct.unpack_from("<5I", encoded, 84), tuple(range(0x1000010, 0x1000015)))
        self.assertEqual(encoded[104:128], bytes(24))
        self.assertEqual(struct.unpack_from("<5I", encoded, 128+136), (500, 2000, 0x54321, 2, 1))
        self.assertEqual(encoded[128+156:288], bytes(4))
        self.assertEqual(encoded[128+69:128+72], b"\x01\0\0")
        self.assertEqual(struct.unpack_from("<8I", encoded, 400), (1, 7, 0x2000001, 100, 0xFFFFFFFF, 0xFFFFFFFF, 0x80000000, 41))
        self.assertEqual(struct.unpack_from("<2I", encoded, 432), (3, 7))
        self.assertEqual(inputs, fixture())  # Preparation never mutates source metadata.

    def test_actor_bounds_and_shape(self):
        mutations = {"index": (-1, 256, True), "model_resource": (0, 1 << 32),
                     "people_info": (150, -1), "raw_words": ([], [0, 0, -1, 0]),
                     "people_info_raw": (False, "04", "00"*43, "00"*45, "xx"*44),
                     "move_script": (-1, 1 << 32), "talk_script": (-1,),
                     "position": ([0, 1], [0, 1, 2, 3], [100001, 0, 0], [math.nan, 0, 0]),
                     "yaw_degrees": (math.nan, math.inf, 6000),
                     "radius": (0, -1, 101, 1e-100, math.inf),
                     "walk_distance": (-1, 101, math.nan), "run_distance": (-1, 101, math.inf),
                     "motion_indices": ([1]*8, [1]*10, [256]*9, [-1]*9),
                     "file": (False, 3, "../a.c3t", "x/ok.c3t", "x\\ok.c3t", ".hidden.c3t", "a..b.c3t", "x.C3T", "x.c3t\x00", "é.c3t", "a"*60+".c3t")}
        for key, values in mutations.items():
            for value in values:
                with self.subTest(key=key, value=value):
                    inputs = fixture()
                    inputs[0]["actors"][0][key] = value
                    with self.assertRaises(ValueError):
                        area_runtime.encode(*inputs)

    def test_bounded_reviewed_attachment_part_mapping(self):
        for bone in (0, 9, 63):
            inputs = fixture()
            inputs[0]["actors"][0]["source_part_bones"] = {"1": bone}
            self.assertEqual(struct.unpack_from("<I", area_runtime.encode(*inputs), 128+156), (bone+1,))
        for parts in (None, [], {"0": 1}, {"1": 64}, {"1": -1}, {"1": True}, {"1": 1, "2": 2}):
            inputs = fixture()
            inputs[0]["actors"][0]["source_part_bones"] = parts
            with self.assertRaises(ValueError):
                area_runtime.encode(*inputs)
        inputs = fixture()
        inputs[0]["actors"][0].update(file=None, source_part_bones={"1": 9})
        with self.assertRaises(ValueError):
            area_runtime.encode(*inputs)

    def test_original_collision_type_bits(self):
        for kind in range(4):
            inputs = fixture()
            inputs[0]["actors"][0]["people_info_raw"] = f"{0xf3|(kind<<2):02x}"+"00"*43
            self.assertEqual(area_runtime.encode(*inputs)[128+69], kind)

    def test_valid_actor_boundaries(self):
        inputs = fixture()
        a = inputs[0]["actors"][0]
        a.update(index=255, people_info=149, position=[-100000, 100000, 0], yaw_degrees=math.degrees(100),
                 radius=100, walk_distance=0, run_distance=100, file="a"*59+".c3t")
        inputs[3][0].update(actor=255, name_index=65535, hear_flag=65535, name_message=0xFFFFF, talk_start=3, talk_end=3)
        encoded = area_runtime.encode(*inputs)
        self.assertEqual(encoded[200:264], ("a"*59+".c3t").encode()+b"\0")
        self.assertEqual(struct.unpack_from("<f", encoded, 128+44), (100,))
        a["file"] = None
        self.assertEqual(area_runtime.encode(*inputs)[200:264], bytes(64))

    def test_unique_actors_names_and_complete_lifecycle(self):
        for mutation in (
                lambda x: x[0]["actors"].append(copy.deepcopy(x[0]["actors"][0])),
                lambda x: x[0].update(actors=x[0]["actors"]*33),
                lambda x: x[2].clear(), lambda x: x[2].append(copy.deepcopy(x[2][0])),
                lambda x: x[3].clear(), lambda x: x[3].append(copy.deepcopy(x[3][0])),
                lambda x: x[2][0].update(raw_words=[]), lambda x: x[2][0].update(resource_group=30),
                lambda x: x[2][0].update(pre_native=1), lambda x: x[2][0].update(init_script=-1),
                lambda x: x[0]["actors"][0].pop("people_info")):
            inputs = fixture()
            mutation(inputs)
            with self.assertRaises(ValueError):
                area_runtime.encode(*inputs)
        for key, value in (("name_index", 65536), ("hear_flag", 65536), ("name_message", 0x100000),
                           ("talk_start", 4), ("talk_end", 4)):
            inputs = fixture()
            inputs[3][0][key] = value
            with self.assertRaises(ValueError):
                area_runtime.encode(*inputs)

    def test_surface_validation_and_separate_tables(self):
        mutations = {"vertices": ([0]*8, [math.nan]*9, [100001]*9),
                     "normal": ([1, 0], [0, 0, 0], [2, 0, 0], [math.inf, 0, 0]),
                     "object": (256, -1), "flags": (65536, -1), "source_id": (65536, -1)}
        for table in ("transition_surfaces", "interaction_surfaces"):
            for key, values in mutations.items():
                for value in values:
                    with self.subTest(table=table, key=key, value=value):
                        inputs = fixture()
                        inputs[0][table][0][key] = value
                        with self.assertRaises(ValueError):
                            area_runtime.encode(*inputs)
            inputs = fixture()
            inputs[0][table] *= 1025
            with self.assertRaises(ValueError):
                area_runtime.encode(*inputs)
        inputs = fixture()
        inputs[0]["interaction_surfaces"][0]["source_id"] = 65535
        inputs[0]["interaction_surfaces"][0]["object"] = 255
        data = area_runtime.encode(*inputs)
        self.assertEqual(struct.unpack_from("<H", data, 288+50), (7,))
        self.assertEqual(struct.unpack_from("<2H", data, 344+50), (65535, 255))

    def test_links_fail_closed_without_silent_word_wrapping(self):
        for key, values in {"kind": (0, 5), "script": (0, -1, "not hex"),
                            "source_id": (-1, 1 << 32), "index": (-1,),
                            "args": ([0]*3, [0]*5, [0, 0, 0, 1 << 32], [-0x80000001, 0, 0, 0])}.items():
            for value in values:
                inputs = fixture()
                inputs[1][0][key] = value
                with self.assertRaises(ValueError):
                    area_runtime.encode(*inputs)
        inputs = fixture()
        inputs[1][1]["kind"] = 1
        with self.assertRaises(ValueError):
            area_runtime.encode(*inputs)
        inputs = fixture()
        inputs[1].extend(copy.deepcopy(inputs[1][0]) for _ in range(127))
        with self.assertRaises(ValueError):
            area_runtime.encode(*inputs)

    def test_prepare_rejects_late_failure_and_duplicate_floor(self):
        area, links, life, names = fixture()
        for areas in ([], [area, area], [area, {"floor": 99}], [area, None]):
            with self.assertRaises(ValueError):
                area_runtime.prepare(areas, links, life, names)
        files, report = area_runtime.prepare([area], links, life, names)
        self.assertEqual(set(files), {"outskirt.c3a"})
        self.assertEqual(report[0]["sha256"], hashlib.sha256(files["outskirt.c3a"]).hexdigest())

    def test_profile_floor_publishes_its_own_pack_and_group(self):
        self.assertEqual({floor: area_runtime.GROUPS[floor] for floor in (1, 2, 3, 5, 0x0D, 0x77)},
                         {1: 0x1D, 2: 0x1E, 3: 0x1F, 5: 0x22, 0x0D: 0x2B, 0x77: 0x21})
        self.assertEqual({floor: area_runtime.PACKS[floor] for floor in (1, 2, 3, 5, 0x0D, 0x77)},
                         {1: "outskirt.c3a", 2: "shop.c3a", 3: "phenac.c3a", 5: "phenac_mayor1f.c3a",
                          0x0D: "phenac_stadium1f.c3a", 0x77: "phenac_pcb1.c3a"})
        self.assertEqual(sorted(area_runtime.GROUPS), sorted(area_runtime.PACKS))
        self.assertEqual(len(set(area_runtime.PACKS.values())), len(area_runtime.PACKS))
        area, links, life, names = fixture()
        area["floor"] = 3
        for row in links + life + names:
            row["floor"] = 3
        with self.assertRaisesRegex(ValueError, "lifecycle"):
            area_runtime.prepare([area], links, life, names)  # group 29 belongs to floor 1
        life[0]["resource_group"] = 0x1F
        files, report = area_runtime.prepare([area], links, life, names)
        self.assertEqual(set(files), {"phenac.c3a"})
        self.assertEqual(report, [{"floor": 3, "file": "phenac.c3a", "bytes": len(files["phenac.c3a"]),
                                   "sha256": hashlib.sha256(files["phenac.c3a"]).hexdigest()}])
        self.assertEqual(struct.unpack_from("<2I", files["phenac.c3a"], 12), (3, 0x1F))
        # All profile floors may be published together; a floor without a profile may not.
        first = fixture()
        both, _ = area_runtime.prepare([first[0], area], first[1] + links, first[2] + life, first[3] + names)
        self.assertEqual(set(both), {"outskirt.c3a", "phenac.c3a"})
        self.assertEqual(both["phenac.c3a"], files["phenac.c3a"])
        area["floor"] = 0x0E
        with self.assertRaisesRegex(ValueError, "Unsupported original floor"):
            area_runtime.prepare([area], links, life, names)

    def test_signed_facing_and_records_sharing_one_model_pack(self):
        inputs = fixture()
        inputs[0]["actors"][0]["yaw_degrees"] = -90
        other = copy.deepcopy(inputs[0]["actors"][0])
        other.update(index=2, yaw_degrees=-140, position=[5, 6, 7])
        inputs[0]["actors"].append(other)
        inputs[3].append({**inputs[3][0], "actor": 2})
        encoded = area_runtime.encode(*inputs)
        self.assertEqual(struct.unpack_from("<I", encoded, 20), (2,))
        first, second = 128, 128 + 160
        self.assertEqual(struct.unpack_from("<f", encoded, first + 44),
                         struct.unpack("<f", struct.pack("<f", math.radians(-90))))
        self.assertEqual(struct.unpack_from("<f", encoded, second + 44),
                         struct.unpack("<f", struct.pack("<f", math.radians(-140))))
        # Two person records may name one prepared pack (same model resource).
        self.assertEqual(encoded[first + 72:first + 136], encoded[second + 72:second + 136])
        self.assertEqual(encoded[first + 72:first + 84], b"actor_1.c3t\0")
        self.assertEqual(struct.unpack_from("<2I", encoded, second), (2, 0x123400))

    def test_publish_success_and_existing_output_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)/"packs"
            files = {"one.c3a": b"one", "two.c3a": b"two"}
            area_runtime.publish(output, files)
            self.assertEqual({p.name: p.read_bytes() for p in output.iterdir()}, files)
            with self.assertRaises(FileExistsError):
                area_runtime.publish(output, {"one.c3a": b"changed"})
            self.assertEqual((output/"one.c3a").read_bytes(), b"one")
            self.assertEqual(list(Path(temp).iterdir()), [output])

    def test_partial_write_and_rename_failures_leave_no_output(self):
        write_bytes = Path.write_bytes
        def fail_second(path, data):
            if path.name == "two.c3a":
                raise OSError("injected disk write failure")
            return write_bytes(path, data)
        with tempfile.TemporaryDirectory() as temp:
            output = Path(temp)/"packs"
            with mock.patch.object(Path, "write_bytes", fail_second):
                with self.assertRaisesRegex(OSError, "disk write"):
                    area_runtime.publish(output, {"one.c3a": b"one", "two.c3a": b"two"})
            self.assertEqual(list(Path(temp).iterdir()), [])
            with mock.patch.object(area_runtime.os, "rename", side_effect=OSError("rename failure")):
                with self.assertRaisesRegex(OSError, "rename failure"):
                    area_runtime.publish(output, {"one.c3a": b"one"})
            self.assertEqual(list(Path(temp).iterdir()), [])

    def test_cli_manifest_hashes_exact_inputs_and_failure_does_not_publish(self):
        with tempfile.TemporaryDirectory() as temp:
            base = Path(temp)
            area, links, life, names = fixture()
            command = [sys.executable, str(ROOT/"tools/prepare_area_runtime.py")]
            snapshots = {}
            for key, value in (("metadata", [area]), ("links", links), ("lifecycle", life), ("names", names)):
                path = base/(key+".json")
                snapshots[key] = json.dumps(value).encode()
                path.write_bytes(snapshots[key])
                command.extend(("--"+key, str(path)))
            output = base/"packs"
            result = subprocess.run(command+["--output", str(output)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            manifest = json.loads((output/"area-runtime-manifest.json").read_bytes())
            for key, value in snapshots.items():
                self.assertEqual(manifest[key+"_sha256"], hashlib.sha256(value).hexdigest())
            (base/"metadata.json").write_text(json.dumps([area, {"floor": 99}]))
            result = subprocess.run(command+["--output", str(base/"bad-packs")], capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("Traceback", result.stderr)
            self.assertFalse((base/"bad-packs").exists())
            self.assertFalse(any(p.name.startswith(".bad-packs-") for p in base.iterdir()))


if __name__ == "__main__":
    unittest.main()
