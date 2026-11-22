"""Every floor Rui can walk after the partner event has her pack.

The field scene draws the partner only when <prefix>_partner.c3t is on the
SD card (platform/3ds/field_scene.c); without it she follows unseen while her
lines play (Outskirt Stand, playtest 2026-11-20). Identities only: no
original files. C3_PARTNER_BOOT optionally names a prepared boot directory
(flat, or grouped as boot/packs/<group>/) to check the files themselves.
"""
import os
from pathlib import Path
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import floor_profiles as profiles
from prepare_event_scene import EVENT_SCENES

# Event scenes (tools/prepare_event_scene.py): no people table or walk
# surface, the scripts place Wes on the train; no partner pack is prepared.
NO_PARTNER = {"f%03x" % floor: "subway event scene" for floor in EVENT_SCENES}


def field_rows():
    """(floor, prefix) of each walkable row of src/floor_profile.c."""
    source = (ROOT / "src" / "floor_profile.c").read_text(encoding="utf-8")
    return [(int(floor, 16), prefix) for floor, prefix in
            re.findall(r'\{0x([0-9A-Fa-f]+)u,\s*"(\w+)",\s*"\w+\.c3s"', source)]


class PartnerPackTests(unittest.TestCase):
    def test_every_runtime_floor_profile_has_a_partner_pack_source(self):
        rows = field_rows()
        self.assertIn((0x01, "outskirt"), rows)
        self.assertIn((0x02, "shop"), rows)
        self.assertGreater(len(rows), 30)
        sources = profiles.partner_pack_floors()
        missing = [prefix for _floor, prefix in rows if prefix not in sources]
        self.assertEqual(missing, [], "floors with no partner pack from tools/prepare_area.py")

    def test_every_story_profile_has_a_partner_pack_source(self):
        sources = profiles.partner_pack_floors()
        missing = [row["prefix"] for row in profiles.story_floors().values() if row["prefix"] not in sources]
        self.assertEqual(missing, [])
        self.assertEqual(sources["outskirt"], "--outskirt-partners")
        self.assertEqual(sources["shop"], "--outskirt-partners")
        self.assertEqual(sources["phenac"], "--floor phenac")

    def test_outskirt_partners_follow_their_wes_pack(self):
        for prefix, (archive, _resource, version) in profiles.OUTSKIRT_PARTNERS.items():
            self.assertEqual(profiles.FLOORS[prefix]["archive"], archive)
            self.assertNotIn("map", profiles.FLOORS[prefix])
            self.assertIn(version, (2, 4))

    @unittest.skipUnless(os.environ.get("C3_PARTNER_BOOT"), "C3_PARTNER_BOOT names no prepared boot directory")
    def test_prepared_boot_has_a_partner_pack_for_every_field_floor(self):
        boot = Path(os.environ["C3_PARTNER_BOOT"])
        names = {item.name for item in boot.iterdir() if item.is_file()}
        packs = boot / "packs"
        if packs.is_dir():
            for group in packs.iterdir():
                if group.is_dir():
                    names |= {item.name for item in group.iterdir() if item.is_file()}
        known = {prefix for _floor, prefix in field_rows()} | {row["prefix"] for row in profiles.FLOORS.values()}
        floors = sorted(name[:-4] for name in names if name.endswith(".c3f") and
                        (name[:-4] in known or re.fullmatch(r"f[0-9a-f]{3}", name[:-4])) and
                        name[:-4] + "_wes.c3t" in names)
        self.assertIn("outskirt", floors)
        missing = [prefix for prefix in floors if prefix + "_partner.c3t" not in names and prefix not in NO_PARTNER]
        self.assertEqual(missing, [], "field floors without <prefix>_partner.c3t in %s" % boot)


if __name__ == "__main__":
    unittest.main()
