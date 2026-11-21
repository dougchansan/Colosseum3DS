import importlib.util
import tempfile
import unittest
from pathlib import Path

TOOL = Path(__file__).resolve().parent.parent / "tools" / "sd_pack_layout.py"
spec = importlib.util.spec_from_file_location("sd_pack_layout", TOOL)
layout = importlib.util.module_from_spec(spec)
spec.loader.exec_module(layout)


class SdPackLayoutTest(unittest.TestCase):
    def test_groups_use_longest_floor_prefix(self):
        prefixes = layout.floor_prefixes()
        self.assertEqual(layout.group_of("phenac_pc1f_map0.c3t", prefixes), "phenac_pc1f")
        self.assertEqual(layout.group_of("phenac_pc1f.c3a", prefixes), "phenac_pc1f")
        self.assertEqual(layout.group_of("phenac_npc26.c3t", prefixes), "phenac")
        self.assertEqual(layout.group_of("outskirt_shop.c3s", prefixes), "outskirt")
        self.assertEqual(layout.group_of("f387_obj0.c3t", prefixes), "f387")
        self.assertIsNone(layout.group_of("pda.c3pd", prefixes))
        self.assertIsNone(layout.group_of("phenac_intro.c3v", prefixes))
        self.assertIsNone(layout.group_of("phenacx.c3t", prefixes))

    def test_apply_and_undo_round_trip_locally(self):
        names = ["phenac.c3f", "phenac_map0.c3t", "phenac_pc1f_npc0.c3t", "f02f.c3s", "ui.c3ui", "tpc.c3v"]
        with tempfile.TemporaryDirectory() as root:
            boot = Path(root, "boot")
            boot.mkdir()
            for name in names:
                (boot / name).write_bytes(name.encode())
            self.assertEqual(layout.main(["--local", root, "--apply"]), 0)
            self.assertEqual(sorted(p.name for p in boot.iterdir()), ["tpc.c3v", "ui.c3ui"])
            self.assertEqual(Path(root, "packs/phenac_pc1f/phenac_pc1f_npc0.c3t").read_bytes(), b"phenac_pc1f_npc0.c3t")
            self.assertTrue(Path(root, "packs/f02f/f02f.c3s").is_file())
            self.assertEqual(layout.main(["--local", root, "--undo", "--apply"]), 0)
            self.assertEqual(sorted(p.name for p in boot.iterdir()), sorted(names))
            for name in names:
                self.assertEqual((boot / name).read_bytes(), name.encode())


if __name__ == "__main__":
    unittest.main()
