import importlib.util
import json
from pathlib import Path
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_battle_action", ROOT / "tools/prepare_battle_action.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class BattleActionSourceTests(unittest.TestCase):
    def test_extracts_original_queue_and_initializer(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "queue.inc"
            module.prepare(path)
            text = path.read_text()
            self.assertIn("void fightActionFifoInit(void)", text)
            self.assertIn("fightActionFifoDrop();", text)
            self.assertIn("return fightActionFlowEntry(action, entry);", text)
            self.assertIn("((u8)result != 1)", text)
            self.assertIn("*entry = *(FightActionFifoEntry*)action;", text)

    def test_rejects_each_changed_pin_before_output(self):
        manifest = json.loads((ROOT / "config/battle-action-source-files.json").read_text())
        with tempfile.TemporaryDirectory() as folder:
            donor = Path(folder) / "donor"
            for entry in manifest["files"]:
                target = donor / entry["path"]
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / "vendor/colosseum" / entry["path"], target)
            for entry in manifest["files"]:
                with self.subTest(path=entry["path"]):
                    target = donor / entry["path"]
                    original = target.read_bytes()
                    target.write_bytes(original + b"\n")
                    output = Path(folder) / "unchanged.inc"
                    output.write_text("preserve existing output")
                    with self.assertRaisesRegex(ValueError, "source drift"):
                        module.prepare(output, donor)
                    self.assertEqual(output.read_text(), "preserve existing output")
                    target.write_bytes(original)
