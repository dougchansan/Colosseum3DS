import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("prepare_battle_runtime_trainer",ROOT/"tools/prepare_battle_runtime_trainer.py")
prepare=importlib.util.module_from_spec(spec);spec.loader.exec_module(prepare)

class RuntimeExtraction(unittest.TestCase):
    def test_complete_pinned_source_and_no_draw_proof(self):
        output=prepare.generate(ROOT/"vendor/colosseum")
        for names in prepare.FUNCTIONS.values():
            for name in names:self.assertIn("c3r_src_"+name+"(",output)
        self.assertIn("c3r_src_fn_8011B2C0(ctx, 0x3F, 0)",output)
        self.assertIn("if (max == min)",output)
        self.assertIn("return min;",output)
        self.assertIn("c3r_src_kaisuuBiosGetMax(param)",output)
        self.assertIn("signed32(lbl_80315690 + index * 8 + 4)",output)
        self.assertIn("sizeof(lbl_8035BBA8)/20u",output)
        self.assertIn("sizeof(lbl_80315690)/8u",output)
        self.assertIn("static void c3r_src_fightFloorSetShadow(void) {}",output)
        self.assertIn("C3OutEnemy*",output)
        self.assertIn("uintptr_t uVar2;",output)
        self.assertNotIn("extern ",output)
        self.assertNotIn("(u32)ctx",output)
        self.assertNotIn("(int)pokemonGetStatus",output)
        self.assertNotIn("return table[index].end",output)

    def test_original_table_row_bytes(self):
        root=ROOT/"vendor/colosseum"
        status=prepare.array((root/"src/game/data/data_8035BB50.c").read_text(),"lbl_8035BBA8")
        kaisuu=prepare.array((root/"src/game/data/data_80315690.c").read_text(),"lbl_80315690")
        self.assertEqual(status[63*20:64*20].hex(),"0600010000000000002a00000000000000000000")
        self.assertEqual(kaisuu[:8],b"\xff"*8)
        self.assertEqual((len(status)//20,len(kaisuu)//8),(86,10))

    def test_crlf_and_tampering(self):
        with tempfile.TemporaryDirectory() as directory:
            target=Path(directory)
            for path in prepare.PINS:
                dst=target/path;dst.parent.mkdir(parents=True,exist_ok=True)
                raw=(ROOT/"vendor/colosseum"/path).read_bytes().replace(b"\r\n",b"\n")
                dst.write_bytes(raw.replace(b"\n",b"\r\n"))
            self.assertEqual(prepare.generate(target),prepare.generate(ROOT/"vendor/colosseum"))
            path=target/next(iter(prepare.FUNCTIONS));path.write_bytes(path.read_bytes()+b"\n/* changed */\n")
            with self.assertRaisesRegex(ValueError,"differs from pin"):prepare.generate(target)

    def test_manifest_cannot_repin(self):
        manifest=json.loads(prepare.MANIFEST.read_text());manifest["files"][0]["sha256"]="0"*64
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"manifest.json";path.write_text(json.dumps(manifest))
            with patch.object(prepare,"MANIFEST",path):
                with self.assertRaisesRegex(ValueError,"lock changed"):prepare.generate(ROOT/"vendor/colosseum")
if __name__=="__main__":unittest.main()
