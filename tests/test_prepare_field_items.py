"""Synthetic converter/source-admission regressions, never original asset fixtures."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import prepare_field_items as prep
import prepare_field_items_source as source


class FieldItemPreparation(unittest.TestCase):
    def setUp(self):
        self.records = bytearray(397*40)
        self.mapping = bytearray(prep.COUNT*2)
        self.params = bytearray(64*16)
        self.records[40] = 2
        self.records[44] = 1
        struct.pack_into(">I", self.records, 56, 17)
        struct.pack_into(">H", self.mapping, 26, 1)
        self.params[16+10] = 20

    def build(self, name="SYNTHETIC"):
        return prep.make_payload(self.records, self.mapping, self.params, lambda _: name)

    def test_original_record_and_parameter_bytes_are_retained(self):
        p = self.build()
        self.assertEqual(len(p), prep.COUNT*128)
        self.assertEqual(p[13*128:13*128+40], self.records[40:80])
        self.assertEqual(p[13*128+40:13*128+56], self.params[16:32])
        self.assertEqual(p[13*128+56:13*128+66], b"SYNTHETIC\0")
        self.assertEqual(p[:128], bytes(128))

    def test_bad_row_indices_and_spans(self):
        struct.pack_into(">H", self.mapping, 26, 397)
        with self.assertRaises(ValueError): self.build()
        struct.pack_into(">H", self.mapping, 26, 1)
        self.records.pop()
        with self.assertRaises(ValueError): self.build()

    def test_bad_pocket_parameter_and_name(self):
        self.records[40] = 7
        with self.assertRaises(ValueError): self.build()
        self.records[40] = 2
        self.records[44] = 64
        with self.assertRaises(ValueError): self.build()
        self.records[44] = 1
        for name in ("", "x"*64):
            with self.assertRaises(ValueError): self.build(name)

    def test_transliteration_is_display_only(self):
        p = self.build("POKé BALL")
        self.assertEqual(p[13*128+56:13*128+66], b"POK? BALL\0")
        self.assertEqual(p[13*128:13*128+56], self.build()[13*128:13*128+56])

    def test_generated_hp_code_and_source_drift(self):
        code = source.extract()
        self.assertIn("newHp = oldHp + recoveryAmount;", code)
        self.assertIn("recoveryAmount = maxHp / 2;", code)
        self.assertIn("static void c3fi_src_hpRecover__", code)
        self.assertNotIn("pokemonBiosGetHp(pokemon)", code.replace("c3fi_src_pokemonBiosGetHp(pokemon)", ""))
        spec = json.loads((ROOT/"config/field-items-source-files.json").read_text())
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root/"config").mkdir()
            (root/"config/field-items-source-files.json").write_text(json.dumps(spec))
            (root/"source-lock.json").write_bytes((ROOT/"source-lock.json").read_bytes())
            for item in spec["files"]:
                path = root/"vendor/colosseum"/item["path"]
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes((ROOT/"vendor/colosseum"/item["path"]).read_bytes())
            self.assertEqual(source.extract(root), code)
            path = root/"vendor/colosseum"/spec["files"][0]["path"]
            path.write_bytes(path.read_bytes()+b"\n")
            with self.assertRaisesRegex(ValueError, "source drift"): source.extract(root)

    def test_incorrect_inputs_are_rejected_before_extraction(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dol, rel = root/"main.dol", root/"common_rel.rel"
            dol.write_bytes(b"not a disc")
            rel.write_bytes(b"not a rel")
            with self.assertRaisesRegex(ValueError, "main.dol"): prep.make_pack(dol, rel)


if __name__ == "__main__":
    unittest.main()
