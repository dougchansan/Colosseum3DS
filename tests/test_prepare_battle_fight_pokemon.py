import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location(
    "prepare_battle_fight_pokemon", ROOT / "tools/prepare_battle_fight_pokemon.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class FightPokemonExtraction(unittest.TestCase):
    def test_closure_and_explicit_pointer_arguments(self):
        output = prepare.generate(ROOT / "vendor/colosseum")
        for names in prepare.FUNCTIONS.values():
            for name in names:
                self.assertIn("c3fp_src_" + name + "(", output)
        self.assertEqual(output.count("c3fp_src_pokemonInit(context, c3fp_src_pokemonGetStatus"), 2)
        self.assertIn("uintptr_t uVar1;", output)
        self.assertIn("0xcb, 0, (uintptr_t)p2)", output)
        self.assertNotIn("(u32)pokemonGetStatus", output)
        self.assertNotIn("extern ", output)
        self.assertNotIn("pokemonInit();", output)
        self.assertNotIn("fn_800E0C54", output)

    def test_normalized_sources_and_each_pin_rejected(self):
        expected = prepare.generate(ROOT / "vendor/colosseum")
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            for name in prepare.PINS:
                dest = target / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                raw = (ROOT / "vendor/colosseum" / name).read_bytes().replace(b"\r\n", b"\n")
                dest.write_bytes(raw.replace(b"\n", b"\r\n"))
            self.assertEqual(prepare.generate(target), expected)
            for name in prepare.PINS:
                with self.subTest(name=name):
                    path = target / name
                    original = path.read_bytes()
                    path.write_bytes(original + b"\n/* changed */\n")
                    with self.assertRaisesRegex(ValueError, "differs from pin"):
                        prepare.generate(target)
                    path.write_bytes(original)

    def test_manifest_cannot_repin_or_change_commit(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "manifest.json"
            original = json.loads(prepare.MANIFEST.read_text())
            for changed in ("commit", "sha256"):
                manifest = json.loads(json.dumps(original))
                if changed == "commit":
                    manifest["commit"] = "0" * 40
                else:
                    manifest["files"][0]["sha256"] = "0" * 64
                path.write_text(json.dumps(manifest))
                with patch.object(prepare, "MANIFEST", path):
                    with self.assertRaisesRegex(ValueError, "lock changed"):
                        prepare.generate(ROOT / "vendor/colosseum")


if __name__ == "__main__":
    unittest.main()
