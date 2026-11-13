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
        self.assertEqual([(name, row["floor"], row["lifecycle_row"], row["resource_group"], row["archive"])
                          for name, row in rows.items()],
                         [("outskirt", 1, 0, 0x1D, "S1_out.fsys"), ("shop", 2, 1, 0x1E, "S1_shop_1F.fsys"),
                          ("phenac", 3, 2, 0x1F, "M1_out.fsys"),
                          ("phenac_mayor1f", 0x05, 63, 0x22, "M1_houseA_1F.fsys"),
                          ("phenac_mayor2f", 0x06, 64, 0x23, "M1_houseA_2F.fsys"),
                          ("phenac_pc1f", 0x04, 62, 0x20, "M1_pc_1F.fsys"),
                          ("phenac_pregym1f", 0x0B, 69, 0x28, "M1_gym_1F.fsys"),
                          ("phenac_stadium1f", 0x0D, 71, 0x2B, "M1_stadium_1F.fsys"),
                          ("phenac_shop1f", 0x09, 67, 0x26, "M1_shop_1F.fsys"),
                          ("phenac_shop2f", 0x0A, 68, 0x27, "M1_shop_2F.fsys"),
                          ("phenac_houseb1f", 0x07, 65, 0x24, "M1_houseB_1F.fsys"),
                          ("phenac_housec1f", 0x08, 66, 0x25, "M1_houseC_1F.fsys"),
                          ("phenac_pregymb1", 0x0C, 70, 0x29, "M1_gym_B1.fsys"),
                          ("phenac_pcb1", 0x77, 115, 0x21, "M1_pc_B1.fsys")])
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
        packs = {name: (row["prefix"], profiles.area_pack(row), row["story_pack"])
                 for name, row in profiles.story_floors().items()}
        self.assertEqual({name: packs[name] for name in ("outskirt", "shop", "phenac")},
                         {"outskirt": ("outskirt", "outskirt.c3a", "outskirt.c3s"),
                          "shop": ("shop", "shop.c3a", "outskirt_shop.c3s"),
                          "phenac": ("phenac", "phenac.c3a", "phenac.c3s")})
        # Interior rows are keyed by their prefix and name every pack after it.
        interiors = {name: row for name, row in profiles.FLOORS.items() if row.get("scene") == "interior"}
        self.assertEqual(len(interiors), 11)
        for name, row in interiors.items():
            self.assertEqual(packs[name], (name, name + ".c3a", name + ".c3s"))
            self.assertTrue(name.startswith("phenac_") and len(name) <= 32)
            self.assertEqual((row["pack_format"], (row["rel"] >> 8) & 255, (row["map"] >> 8) & 255,
                              (row["collision"] >> 8) & 255), (3, 0x1C, 0x02, 0x06))
            # Every interior REL publishes one 40-byte record first; tables follow it.
            self.assertGreaterEqual(min(row["entrance_table"], row["people_table"]), 0x28)

    def test_interior_groups_objects_and_feature_keys(self):
        rows = profiles.FLOORS
        self.assertEqual({name: row.get("objects", ()) for name, row in rows.items() if row.get("objects")},
                         {"phenac": (0, 1, 2, 3, 4, 5),
                          "phenac_mayor1f": (0,), "phenac_pc1f": (3,), "phenac_pregym1f": (0,),
                          "phenac_stadium1f": (0, 1), "phenac_pregymb1": (0,)})
        self.assertEqual(sorted(name for name, row in rows.items() if row.get("path_joints")),
                         ["phenac_houseb1f", "phenac_pc1f", "phenac_pcb1", "phenac_pregymb1", "phenac_stadium1f"])
        self.assertEqual({name: row["material_colors"] for name, row in rows.items() if row.get("material_colors")},
                         {"phenac_pc1f": "frozen", "phenac_pregym1f": "frozen", "phenac_stadium1f": "constant",
                          "phenac_shop1f": "frozen", "phenac_shop2f": "frozen", "phenac_pcb1": "frozen"})
        self.assertEqual(rows["phenac_mayor2f"]["groups"], (((0, 1, 2), ("static", 0)),))
        # The exterior row keeps its exterior pack format; only its six doors
        # are object packs (checked above), as the door table needs.
        for key in ("scene", "pack_format", "path_joints", "material_colors"):
            self.assertNotIn(key, rows["phenac"])

    def test_partner_profile_matches_the_wes_clip_convention(self):
        partner = profiles.PARTNER
        self.assertEqual((partner["archive"], partner["resource"], partner["people_info"], partner["member_id"]),
                         ("field_common.fsys", 0x00F30400, 2, 101))
        self.assertEqual(partner["clips"][:4], (1, 2, 3, 0))  # idle, walk, run, then source motion 0
        self.assertEqual(partner["clips"], (1, 2, 3, 0, 4, 7, 14, 21, 22, 23, 27, 28, 29, 42, 43, 44))
        self.assertIs(profiles.validate_partner(), partner)
        for change in ({"clips": (2, 1, 3, 0)}, {"clips": (1, 2, 3)}, {"clips": (1, 2, 3, 0, 0)},
                       {"clips": (1, 2, 3, *range(4, 34))}, {"clips": (1, 2, 3, -1)},
                       {"locomotion": (1, 2)}, {"clips": (1, 2, 3, True)}):
            with self.subTest(change=change):
                with self.assertRaises(ValueError):
                    profiles.validate_partner({**partner, **change})

    def test_geometry_and_story_views(self):
        # The first area keeps its dedicated geometry converters.
        geometry = sorted(name for name in profiles.FLOORS if name.startswith("phenac"))
        self.assertEqual(len(geometry), 12)
        self.assertEqual(sorted(profiles.geometry_floors()), geometry)
        self.assertEqual(sorted(floor.FLOORS), geometry)
        self.assertIs(floor.FLOORS["phenac"], profiles.FLOORS["phenac"])
        self.assertEqual(sorted(profiles.story_floors()), sorted(profiles.FLOORS))
        self.assertEqual(profiles.by_floor(3)[0], "phenac")
        self.assertEqual(profiles.by_floor(0x77)[0], "phenac_pcb1")
        self.assertIsNone(profiles.by_floor(0x0E))

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
            lambda r: r["phenac_stadium1f"].update(objects=(0, 1, 2)),   # model 2 is also grouped
            lambda r: r["phenac_stadium1f"].update(objects=(0,)),        # model 1 is not covered
            lambda r: r["phenac_mayor2f"].update(groups=(((0, 1, 2), ("loop", 0)),)),
            lambda r: r["phenac_mayor2f"].update(pack_format=4),
            lambda r: r["phenac_pc1f"].update(material_colors="blink"),
            lambda r: r["phenac_pc1f"].update(scene="cave"),
            lambda r: r["phenac_pc1f"].update(models=True),
            lambda r: r["phenac_pcb1"].update(floor=0x04),               # duplicate interior floor
            lambda r: r["phenac_pcb1"].update(story_pack="phenac_pc1f.c3s"),
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
