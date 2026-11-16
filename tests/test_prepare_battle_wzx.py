"""Host-side checks of tools/prepare_battle_wzx.py: synthetic WZX parse and
C3WX / C3WI round trips (no game data). With a private output present
(private/battle3d/wzx.json), every written pack is re-read as well."""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import prepare_battle_wzx as wzx  # noqa: E402


def node(key, kind, payload, flags=0, link=0, part=0, state=0, timing=(0,) * 16):
    head = bytearray(0x70)
    struct.pack_into(">8i", head, 0, key, kind, 2, 1, link, 0, 0, 1)
    struct.pack_into(">16i", head, 0x20, *timing)
    struct.pack_into(">IiIi", head, 0x60, flags, part, 2, state)
    return bytes(head) + payload


def synthetic_member():
    root = bytearray(0x70)
    struct.pack_into(">I", root, 0x68, 2)
    header = struct.pack(">6I", 8, 3, 0x00208048, 2, 5, 0)
    data = bytes(root) + header
    data += bytes(wzx.align32(len(data)) - len(data))
    data += node(1, 5, struct.pack(">Iii", 0x220, 3, 0), timing=(0, 30) + (0,) * 14)
    data += node(2, 1, struct.pack(">iii", 0, 30, 0), flags=1, link=1)
    return data + bytes(wzx.align32(len(data)) - len(data))


class WzxFormatTests(unittest.TestCase):
    def test_parse_and_member_round_trip(self):
        data = synthetic_member()
        seq = wzx.parse_member(data)
        self.assertEqual(seq["count"], 2)
        self.assertEqual(seq["flags"], 0x00208048 & ~0x1F8000)
        self.assertEqual([n["kind"] for n in seq["nodes"]], [5, 1])
        records = [dict(payload=data[n["payload_at"]:n["payload_end"]], file="", status=0, clips=0,
                        bones=0, joint_map=[]) for n in seq["nodes"]]
        records[1].update(file="wxm_0123456789ab.c3t", status=1, clips=1, bones=3, joint_map=[0, 0xFFFF, 2])
        camera = [(1., 2., 3., 4., 5., 6., 0., 45.)] * 3
        blob = wzx.encode_member(seq, records, camera)
        back = wzx.read_member(blob)
        self.assertEqual(back["entry"], 8)
        self.assertEqual(back["raw_flags"], 0x00208048)
        self.assertEqual(len(back["nodes"]), 2)
        self.assertEqual(back["nodes"][0]["payload"], struct.pack(">Iii", 0x220, 3, 0))
        self.assertEqual(back["nodes"][0]["timing"][1], 30)
        self.assertEqual(back["nodes"][1]["file"], "wxm_0123456789ab.c3t")
        self.assertEqual(back["nodes"][1]["joint_map"], [0, 0xFFFF, 2])
        self.assertEqual(back["nodes"][1]["status"], 1)
        self.assertEqual(len(back["camera"]), 3)
        self.assertAlmostEqual(back["camera"][0][7], 45.)

    def test_index_round_trip_and_order(self):
        rows = [(3, 4, 5, 0xFFFF, 0, "wx_001_00000001.c3w"), (0, 2, 7, 0xFFFF, 0, "wx_002_00000002.c3w"),
                (1, 1, 7, 9, 0, "wx_003_00000003.c3w"), (0, 1, 7, 0xFFFF, 0, "wx_002_00000002.c3w")]
        back = wzx.read_index(wzx.encode_index(rows))
        self.assertEqual([(r["category"], r["kind"], r["id"]) for r in back], [(0, 1, 7), (0, 2, 7), (1, 1, 7), (3, 4, 5)])
        self.assertEqual(back[2]["pkx"], 9)
        self.assertEqual(back[0]["pack"], "wx_002_00000002.c3w")

    def test_truncated_member_rejected(self):
        with self.assertRaises((ValueError, struct.error)):
            wzx.parse_member(synthetic_member()[:0xC0])

    def test_private_output_if_present(self):
        output = ROOT / "private/battle3d"
        if not (output / "wzx.json").exists():
            self.skipTest("no private WZX output")
        problems, counts = wzx.verify(output)
        self.assertEqual(problems, [])
        self.assertGreater(counts["members"], 0)


if __name__ == "__main__":
    unittest.main()
