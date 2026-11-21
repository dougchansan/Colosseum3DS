"""Floor profile table checks; identities only, no original assets needed."""
import copy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import floor_profiles as profiles
import prepare_floor as floor
from under_profiles import UNDER_FLOORS, UNDER_SCENE_SETS


class FloorProfileTableTests(unittest.TestCase):
    def test_rows_match_the_original_lifecycle_identities(self):
        rows = {name: row for name, row in profiles.FLOORS.items() if name not in UNDER_FLOORS}
        self.assertEqual([(name, row["floor"], row["lifecycle_row"], row["resource_group"], row["archive"])
                          for name, row in rows.items() if name not in profiles.REALGAM_FLOORS],
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
        interiors = {name: row for name, row in profiles.FLOORS.items() if row.get("scene") == "interior" and name.startswith("phenac_")}
        self.assertEqual(len(interiors), 11)
        for name, row in interiors.items():
            self.assertEqual(packs[name], (name, name + ".c3a", name + ".c3s"))
            self.assertTrue(name.startswith("phenac_") and len(name) <= 32)
            self.assertEqual((row["pack_format"], (row["rel"] >> 8) & 255, (row["map"] >> 8) & 255,
                              (row["collision"] >> 8) & 255), (3, 0x1C, 0x02, 0x06))
            # Every interior REL publishes one 40-byte record first; tables follow it.
            self.assertGreaterEqual(min(row["entrance_table"], row["people_table"]), 0x28)

    def test_interior_groups_objects_and_feature_keys(self):
        rows = {name: row for name, row in profiles.FLOORS.items() if name not in UNDER_FLOORS}
        self.assertEqual({name: row.get("objects", ()) for name, row in rows.items() if row.get("objects")},
                         {"phenac": (0, 1, 2, 3, 4, 5),
                          "phenac_mayor1f": (0,), "phenac_pc1f": (3,), "phenac_pregym1f": (0,),
                          "phenac_stadium1f": (0, 1), "phenac_pregymb1": (0,)})
        self.assertEqual(sorted(name for name, row in rows.items() if row.get("path_joints")),
                         ["phenac_houseb1f", "phenac_pc1f", "phenac_pcb1", "phenac_pregymb1", "phenac_stadium1f"])
        self.assertEqual({name: row["material_colors"] for name, row in rows.items() if row.get("material_colors")},
                         {"phenac_pc1f": "frozen", "phenac_pregym1f": "frozen", "phenac_stadium1f": "constant",
                          "phenac_shop1f": "frozen", "phenac_shop2f": "frozen", "phenac_pcb1": "frozen"})
        # Object packs drawn by the original door/elevator table (floor_event.c
        # FloorDoorEntry row -> resource 0x<map group>10<model>). The Pre Gym
        # elevators are rows 136 and 137; unmapped, their packs never drew and
        # the shaft under each showed as an opaque black disc.
        self.assertEqual({name: row["door_objects"] for name, row in rows.items() if row.get("door_objects")
                          and name not in profiles.REALGAM_FLOORS},
                         {"phenac": {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5}, "phenac_pregym1f": {136: 0},
                          "phenac_stadium1f": {6: 0, 7: 1}, "phenac_pregymb1": {137: 0}})
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
        self.assertEqual(sorted(profiles.geometry_floors()), sorted(geometry + list(UNDER_FLOORS)))
        self.assertEqual(sorted(name for name in floor.FLOORS if not profiles.FLOORS[name].get("auto")), sorted(geometry + list(UNDER_FLOORS)))
        self.assertIs(floor.FLOORS["phenac"], profiles.FLOORS["phenac"])
        self.assertEqual(set(profiles.story_floors()),
                         {name for name, row in profiles.FLOORS.items() if "kind" not in row})
        self.assertEqual(profiles.by_floor(3)[0], "phenac")
        self.assertEqual(profiles.by_floor(0x77)[0], "phenac_pcb1")
        self.assertIsNone(profiles.by_floor(0x0E))

    def test_under_profiles_cover_walkable_floors_and_original_door_models(self):
        expected = set(range(0x2F, 0x38)) | set(range(0x39, 0x3F)) | {0x79, 0x7A, 0x7D, 0x7F}
        self.assertEqual({row["floor"] for row in UNDER_FLOORS.values()}, expected)
        self.assertEqual(set(UNDER_SCENE_SETS), {0x3F, 0x40, 0x8A, 0x8B, 0xD3, 0xD4, 0xD5})
        runtime = (Path(__file__).resolve().parents[1] / "src/floor_profile.c").read_text()
        for name, row in UNDER_FLOORS.items():
            with self.subTest(floor=name):
                self.assertIs(profiles.by_floor(row["floor"])[1], row)
                self.assertFalse(row.get("auto"))
                self.assertEqual(row["prefix"], "f%03x" % row["floor"])
                self.assertIn('"%s", "%s"' % (row["prefix"], row["story_pack"]), runtime)
                self.assertTrue(set(row.get("door_objects", {}).values()) <= set(row["objects"]))
        self.assertEqual(sum(len(row.get("door_objects", {})) for row in UNDER_FLOORS.values()), 42)
        # Original elevator rows share a model but choose distinct part indices.
        self.assertEqual(UNDER_FLOORS["under_subway_b2_1"]["door_objects"],
                         {65: 0, 66: 1, 142: 2, 143: 2})
        self.assertEqual(UNDER_FLOORS["under_out"]["door_objects"][168], 11)

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
            lambda r: r["phenac"].update(people_count=49),            # C3_AREA_MAX_ACTORS
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
