import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location("prepare_battle_trainer",ROOT/"tools/prepare_battle_trainer.py")
prepare=importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)

class TrainerExtraction(unittest.TestCase):
    def test_pinned_parent_hero_and_item_closure(self):
        output=prepare.generate(ROOT/"vendor/colosseum")
        for names in prepare.FUNCTIONS.values():
            for name in names:self.assertIn("c3t_src_"+name+"(",output)
        self.assertIn("u8* pokemon = context.scratch;",output)
        self.assertIn("random_low = c3t_src_fn_800E0C54() & 0xFFFF",output)
        self.assertIn("random_high = c3t_src_fn_800E0C54() << 16",output)
        self.assertIn("C3ItemWord*",output)
        self.assertIn("C3ItemWord temp;",output)
        self.assertIn("default: context.error = 1; return NULL;",output)
        self.assertIn("memmove(dst, src, 0xB18)",output)
        self.assertIn("uintptr_t trainer_name;",output)
        self.assertIn("(uintptr_t)&empty_name",output)
        self.assertNotIn("u8 pokemon[0x138]",output)
        self.assertNotIn("extern ",output)
        self.assertNotIn("asm void",output)
        self.assertNotIn("#include",output)
        self.assertNotIn("lbl_80478BD8",output)
        self.assertNotIn("*(u32*)(u32)",output)

    def test_crlf_and_source_tamper(self):
        with tempfile.TemporaryDirectory() as directory:
            target=Path(directory)
            for name in prepare.PINS:
                dst=target/name;dst.parent.mkdir(parents=True,exist_ok=True)
                raw=(ROOT/"vendor/colosseum"/name).read_bytes().replace(b"\r\n",b"\n")
                dst.write_bytes(raw.replace(b"\n",b"\r\n"))
            self.assertEqual(prepare.generate(target),prepare.generate(ROOT/"vendor/colosseum"))
            path=target/next(iter(prepare.FUNCTIONS))
            path.write_bytes(path.read_bytes()+b"\n/* changed */\n")
            with self.assertRaisesRegex(ValueError,"differs from pin"):prepare.generate(target)

    def test_manifest_cannot_repin(self):
        manifest=json.loads(prepare.MANIFEST.read_text());manifest["files"][0]["sha256"]="0"*64
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/"manifest.json";path.write_text(json.dumps(manifest))
            with patch.object(prepare,"MANIFEST",path):
                with self.assertRaisesRegex(ValueError,"lock changed"):prepare.generate(ROOT/"vendor/colosseum")

if __name__=="__main__":unittest.main()
