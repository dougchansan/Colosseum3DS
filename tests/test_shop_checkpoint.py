"""Ensure the read-only checkpoint verifier cannot overwrite its inputs."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'tools/sandbox/verify_shop_checkpoint.py'


class ShopCheckpointSafetyTests(unittest.TestCase):
    def run_tool(self, gci, pack, output, seed='1'):
        return subprocess.run([sys.executable, '-O', str(TOOL), '--gci', str(gci),
            '--pack', str(pack), '--seed', seed, '--out', str(output)],
            capture_output=True, text=True)

    def test_report_cannot_overwrite_either_input(self):
        with tempfile.TemporaryDirectory() as directory:
            gci, pack = Path(directory)/'card.gci', Path(directory)/'newgame.c3g'
            gci.write_bytes(b'card original'); pack.write_bytes(b'pack original')
            for output in (gci, pack):
                result = self.run_tool(gci, pack, output)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Report must not replace', result.stderr)
            self.assertEqual(gci.read_bytes(), b'card original')
            self.assertEqual(pack.read_bytes(), b'pack original')

    def test_report_cannot_overwrite_hardlink_alias(self):
        with tempfile.TemporaryDirectory() as directory:
            gci, pack = Path(directory)/'card.gci', Path(directory)/'newgame.c3g'
            alias = Path(directory)/'report.json'
            gci.write_bytes(b'card original'); pack.write_bytes(b'pack original')
            os.link(gci, alias)
            result = self.run_tool(gci, pack, alias)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('input alias', result.stderr)
            self.assertEqual(gci.read_bytes(), b'card original')

    def test_invalid_seed_rejected_before_any_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for seed in ('-1', '0x100000000'):
                result = self.run_tool(root/'missing.gci', root/'missing.c3g', root/'out.json', seed)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('Seed must fit uint32', result.stderr)
                self.assertFalse((root/'out.json').exists())
