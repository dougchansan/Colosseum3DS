"""Resource identity and converter eligibility, without private disc assets."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import floor_profiles as profiles
import prepare_floor


class RealgamProfilesTests(unittest.TestCase):
    def test_all_lane_lifecycle_identities_are_permanent(self):
        expected = set(range(0x66, 0x77)) | {0x7b, 0x7c, 0x7e, 0x387, 0x39f} | set(range(0xdd, 0xe4))
        rows = profiles.REALGAM_FLOORS
        self.assertEqual({row["floor"] for row in rows.values()}, expected)
        profiles.validate(rows)
        for name, row in rows.items():
            self.assertIs(profiles.by_floor(row["floor"])[1], row)
            self.assertEqual(name, "auto_%03x" % row["floor"])

    def test_walkable_floors_use_normal_story_and_geometry_tools(self):
        walkable = set(range(0x66, 0x77)) | {0x7b, 0x7c}
        for number in walkable:
            name, row = profiles.by_floor(number)
            self.assertIn(name, profiles.story_floors())
            self.assertIn(name, prepare_floor.FLOORS)
            self.assertTrue(row["auto"])
            self.assertEqual(row["entrance_table"] + row["entrance_count"] * 16, row["people_table"])

    def test_common_door_rows_map_to_exact_source_model_indices(self):
        expected = {
            0x66: {166: 0}, 0x67: {117: 0, 118: 1, 119: 2},
            0x68: {120: 0}, 0x69: {121: 1, 122: 0}, 0x6b: {123: 0},
            0x6c: {124: 1, 125: 0}, 0x6d: {126: 0, 127: 1},
            0x6f: {128: 6, 129: 5, 130: 7}, 0x70: {131: 0, 132: 1},
            0x72: {133: 0, 134: 1, 158: 2},
            0x75: {135: 0, 159: 1, 160: 3, 161: 2},
            0x76: {162: 1, 170: 6, 171: 0}, 0x7b: {116: 1, 163: 0},
        }
        actual = {row["floor"]: row["door_objects"]
                  for row in profiles.REALGAM_FLOORS.values() if row.get("door_objects")}
        self.assertEqual(actual, expected)
        self.assertEqual(sum(map(len, actual.values())), 29)

    def test_non_field_entries_do_not_invent_walkable_data(self):
        special = {0x7e, 0x387, 0x39f} | set(range(0xdd, 0xe4))
        for number in special:
            name, row = profiles.by_floor(number)
            self.assertNotIn(name, profiles.story_floors())
            self.assertNotIn(name, prepare_floor.FLOORS)
            self.assertIn("kind", row)
            self.assertIn("source_members", row)
        self.assertEqual(profiles.by_floor(0x39f)[1]["kind"], "movie")


if __name__ == "__main__":
    unittest.main()
