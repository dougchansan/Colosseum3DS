"""Source-only checks for the move-script length analysis (no original data)."""
import importlib.util
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("analyze_battle_scripts", ROOT / "tools/analyze_battle_scripts.py")
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)


@unittest.skipUnless((ROOT / "vendor/colosseum/src/game/data/rodata_80279B78.c").exists(), "decomp submodule absent")
class ScriptLengths(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.names = tool.opcode_table()
        cls.bodies = tool.function_bodies()

    def length(self, op):
        adv, jumps, unconditional, found = tool.analyse(self.names[op], self.bodies)
        self.assertTrue(found, self.names[op])
        if unconditional:
            return ("goto", max(jumps))
        return max(adv), sorted(j for j in jumps if j + 4 <= max(adv))

    def test_table_shape(self):
        self.assertEqual(len(self.names), 255)
        self.assertEqual(self.names[0], "WS_HITCHECK")
        self.assertEqual(self.names[4], "WS_CRITICAL_CHECK")
        self.assertEqual(self.names[0x3E], "WS_SEQEND")

    def test_handler_lengths(self):
        # Lengths read from the handlers' own PC writes.
        self.assertEqual(self.length(0x00)[0], 1)              # attack canceler
        self.assertEqual(self.length(0x02)[0], 1)              # attack string
        self.assertEqual(self.length(0x03)[0], 1)              # PP reduce (via ++)
        self.assertEqual(self.length(0x06)[0], 1)              # type check (via helper)
        self.assertEqual(self.length(0x1F), (8, [4]))          # side-condition check: jump @4
        self.assertEqual(self.length(0x21), (11, [2, 7]))      # effect check: u32 @2, jump @7
        self.assertEqual(self.length(0x22), (7, [3]))          # type check jump @3
        self.assertEqual(self.length(0x29), (11, [2, 7]))      # byte compare: pointer @2, jump @7
        self.assertEqual(self.length(0x2A), (12, [2, 8]))      # halfword compare: pointer @2, jump @8
        self.assertEqual(self.length(0x28), ("goto", 1))       # unconditional jump
        self.assertEqual(self.length(0xF9)[0], 20)             # status get16

    def test_native_name_table_in_sync(self):
        text = (ROOT / "src/battle_script_names.inc").read_text()
        native = __import__("re").findall(r'"([^"]+)"', text)
        expected = [n if not n.startswith("0x") else "(null)" for n in self.names]
        self.assertEqual(native, expected)

    def test_canonical_prefers_exact_units(self):
        srcs = tool.canonical("WS_TYPE_CHECK", self.bodies)
        self.assertTrue(srcs and all("exact" in name for name, _ in srcs))


if __name__ == "__main__":
    unittest.main()
