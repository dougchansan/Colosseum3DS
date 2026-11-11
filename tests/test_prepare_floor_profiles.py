"""Floor profile table checks; identities only, no original assets needed."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import floor_profiles as profiles
import prepare_floor as floor


class FloorProfileTableTests(unittest.TestCase):
    def test_rows_match_the_original_lifecycle_identities(self):
        rows = profiles.FLOORS
        self.assertEqual([(name, row["floor"], row["lifecycle_row"], row["resource_group"])
                          for name, row in rows.items()],
                         [("outskirt", 1, 0, 0x1D), ("shop", 2, 1, 0x1E), ("phenac", 3, 2, 0x1F)])
        phenac = rows["phenac"]
        self.assertEqual((phenac["archive"], phenac["rel"], phenac["module"]),
                         ("M1_out.fsys", 0x06611C00, 15))
        # lbl_15_data_A0 holds 243 words: 27 person rows of 36 bytes.
        self.assertEqual((phenac["people_table"], phenac["people_count"]), (0xA0, 27))
        self.assertEqual(phenac["people_count"] * 36, 243 * 4)
        # The entrance table ends where the person table starts.
        self.assertEqual(phenac["entrance_table"] + phenac["entrance_count"] * 16, phenac["people_table"])
        self.assertEqual((phenac["script"], phenac["messages"], phenac["collision"]),
                         (0x013E0E00, 0x015A0A00, 0x00640600))
        self.assertEqual((phenac["script"] >> 8) & 255, 0x0E)
        self.assertEqual((phenac["messages"] >> 8) & 255, 0x0A)

    def test_pack_names_follow_the_runtime_floor_profile(self):
        # src/floor_profile.c: <prefix>.c3f/.c3a/_wes.c3t/_map%u.c3t and the story file.
        self.assertEqual({name: (row["prefix"], profiles.area_pack(row), row["story_pack"])
                          for name, row in profiles.story_floors().items()},
                         {"outskirt": ("outskirt", "outskirt.c3a", "outskirt.c3s"),
                          "shop": ("shop", "shop.c3a", "outskirt_shop.c3s"),
                          "phenac": ("phenac", "phenac.c3a", "phenac.c3s")})

    def test_geometry_and_story_views(self):
        # The first area keeps its dedicated geometry converters.
        self.assertEqual(sorted(profiles.geometry_floors()), ["phenac"])
        self.assertEqual(sorted(floor.FLOORS), ["phenac"])
        self.assertIs(floor.FLOORS["phenac"], profiles.FLOORS["phenac"])
        self.assertEqual(sorted(profiles.story_floors()), ["outskirt", "phenac", "shop"])
        self.assertEqual(profiles.by_floor(3)[0], "phenac")
        self.assertIsNone(profiles.by_floor(4))

    def test_rows_hold_identities_not_original_bytes(self):
        def scalars(value):
            if isinstance(value, dict):
                for item in value.values():
                    yield from scalars(item)
            elif isinstance(value, (tuple, list)):
                for item in value:
                    yield from scalars(item)
            else:
                yield value
        for row in profiles.FLOORS.values():
            for value in scalars(row):
                self.assertIsInstance(value, (int, str))
                self.assertNotIsInstance(value, (bytes, bytearray))
                if isinstance(value, str):
                    self.assertLessEqual(len(value), 64)

    def test_validation_rejects_malformed_or_ambiguous_rows(self):
        def changed(mutate):
            rows = copy.deepcopy(profiles.FLOORS)
            mutate(rows)
            return rows
        mutations = (
            lambda r: r["phenac"].update(floor=1),                    # duplicate floor
            lambda r: r["phenac"].update(prefix="shop"),              # duplicate prefix
            lambda r: r["phenac"].update(story_pack="outskirt.c3s"),  # duplicate story pack
            lambda r: r["phenac"].update(sha256="85b1"),              # not an exact digest
            lambda r: r["phenac"].update(sha256="G" * 64),
            lambda r: r["phenac"].update(bytes=True),
            lambda r: r["phenac"].update(floor=0),
            lambda r: r["phenac"].update(resource_group=65536),
            lambda r: r["phenac"].update(archive="../M1_out.fsys"),
            lambda r: r["phenac"].update(archive="M1_out.bin"),
            lambda r: r["phenac"].update(prefix="phenac/../x"),
            lambda r: r["phenac"].update(prefix=""),
            lambda r: r["phenac"].update(story_pack="phenac.c3a"),
            lambda r: r["phenac"].update(people_count=33),            # C3_AREA_MAX_ACTORS
            lambda r: r["phenac"].pop("messages"),                    # partial story keys
            lambda r: r["phenac"].pop("lifecycle_row"),
        )
        for index, mutate in enumerate(mutations):
            with self.subTest(mutation=index):
                with self.assertRaises(ValueError):
                    profiles.validate(changed(mutate))
        self.assertIs(profiles.validate(), profiles.FLOORS)
        # A geometry-only row (no story keys at all) is admissible.
        rows = copy.deepcopy(profiles.FLOORS)
        for key in profiles.STORY_KEYS:
            rows["phenac"].pop(key)
        self.assertEqual(sorted(profiles.validate(rows)), sorted(profiles.FLOORS))


if __name__ == "__main__":
    unittest.main()
