import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_battle_party_sort", ROOT / "tools/prepare_battle_party_sort.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class PartySortExtraction(unittest.TestCase):
    def test_complete_source_closure_and_pointer_lowering(self):
        output = prepare.generate(ROOT / "vendor/colosseum")
        for names in prepare.FUNCTIONS.values():
            for name in names:
                self.assertIn("c3s_src_" + name + "(C3SortContext *context,", output)
        self.assertIn("c3s_src_pokemonCheckValid(context, iVar16)", output)
        self.assertIn("uintptr_t *auStack_310 = context->candidates", output)
        self.assertIn("u8 savedTrainer0[340]", output)
        self.assertIn("u8 savedTrainer1[340]", output)
        self.assertIn("c3s_slot(context, r8", output)
        self.assertIn("c3s_category(context, local_318", output)
        self.assertNotIn("* 4", output)
        self.assertNotIn("int iVar16;", output)
        self.assertNotIn("int iVar6;", output)
        self.assertNotIn("extern ", output)
        self.assertNotIn("lbl_", output)
        self.assertNotIn("context, )", output)

    def test_source_algorithm_branches_and_draw_owners_retained(self):
        output = prepare.generate(ROOT / "vendor/colosseum")
        self.assertEqual(output.count("c3s_src_fn_800E0C54(context)"), 5)
        for label in ("loop_type0_top:", "loop_type1_top:", "loop_type2_top:", "loop_type3_top:", "loop_fill_top:"):
            self.assertIn(label, output)
        self.assertIn("c3s_src_fn_8010C54C(context, move,0)", output)
        self.assertIn("memcpy((void*)iVar6, savedTrainer0, 340)", output)
        self.assertIn("memcpy((void*)iVar5, uStack_198, 340)", output)
        self.assertIn("context->state->scratch", output)
        self.assertIn("c3s_src_fn_801EF634(context, trainer)", output)
        self.assertIn("c3s_type_value(context, typeA, typeB)", output)

    def test_each_source_pin_and_crlf(self):
        expected = prepare.generate(ROOT / "vendor/colosseum")
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            for name in prepare.PINS:
                path = target / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes((ROOT / "vendor/colosseum" / name).read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
            self.assertEqual(prepare.generate(target), expected)
            for name in prepare.PINS:
                with self.subTest(name=name):
                    path = target / name
                    before = path.read_bytes()
                    path.write_bytes(before + b"\n/* changed */\n")
                    with self.assertRaisesRegex(ValueError, "differs from pin"):
                        prepare.generate(target)
                    path.write_bytes(before)

    def test_manifest_cannot_repin(self):
        manifest = json.loads(prepare.MANIFEST.read_text())
        manifest["files"][0]["sha256"] = "0" * 64
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            path.write_text(json.dumps(manifest))
            with patch.object(prepare, "MANIFEST", path):
                with self.assertRaisesRegex(ValueError, "lock changed"):
                    prepare.generate(ROOT / "vendor/colosseum")


if __name__ == "__main__":
    unittest.main()
