import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_battle_pokemon", ROOT / "tools/prepare_battle_pokemon.py")
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


class ConstructorExtraction(unittest.TestCase):
    def test_pinned_complete_closure_and_order_lowering(self):
        output = prepare.generate(ROOT / "vendor/colosseum")
        for names in prepare.FUNCTIONS.values():
            for name in names:
                self.assertIn("c3p_src_" + name + "(", output)
        self.assertIn("c3p_src_pokemonOboeWaza(ptr, (u8)learn_level", output)
        self.assertIn("c3p_src_pokemonResetBasisStatus(pokemon)", output)
        self.assertEqual(output.count("c3p_personality()"), 2)
        self.assertNotIn("fn_800E0C54() << 16", output)
        self.assertNotIn("(u32)&local", output)
        self.assertIn("(uintptr_t)&local", output)
        self.assertIn("(const void*)c3p_src_gamedataGetStatus(0, 1)", output)
        self.assertNotIn("extern ", output)
        self.assertNotIn("*(u32*)(u32)lbl_80478F90", output)
        self.assertIn("C3_BATTLE_POKEMON_RETRY_LIMIT", output)

    def test_crlf_normalization_and_tampered_source_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory)
            for name in prepare.PINS:
                dst = target / name
                dst.parent.mkdir(parents=True, exist_ok=True)
                dst.write_bytes((ROOT / "vendor/colosseum" / name).read_bytes().replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))
            expected = prepare.generate(ROOT / "vendor/colosseum")
            self.assertEqual(prepare.generate(target), expected)
            path = target / next(iter(prepare.FUNCTIONS))
            path.write_bytes(path.read_bytes() + b"\n/* changed */\n")
            with self.assertRaisesRegex(ValueError, "differs from pin"):
                prepare.generate(target)

    def test_manifest_cannot_repin_source(self):
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
