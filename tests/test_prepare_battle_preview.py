"""Synthetic preparer and pinned damage extraction boundary checks."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import prepare_battle_preview as preview
import prepare_battle_damage as damage


class PreviewPreparationTests(unittest.TestCase):
    def test_extracts_only_declared_tables_and_binds_encounter(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / 'sys').mkdir()
            (root / 'files').mkdir()
            rel = bytearray(0x14e000)
            for i, move in enumerate(preview.MOVE_IDS):
                start = 0x184c + 0x11c7c4 + move*56
                rel[start:start+56] = bytes([i+1])*56
            for i, species in enumerate(preview.SPECIES):
                start = 0x184c + 0x121a04 + species*284 + 0x30
                rel[start:start+4] = bytes([i+1])*4
            (root / 'files/common_rel.rel').write_bytes(rel)
            (root / 'sys/main.dol').write_bytes(b'synthetic; the mocked verifier owns admission')
            encounter = bytearray(644)
            encounter[128+56] = 2
            def read(_, address, length):
                if address == 0x80363c00+24:
                    return b'\0\0\0\x1b'+bytes(8)
                return bytes([1])*length
            with patch.object(preview, 'verify_disc', return_value=root) as verify, \
                 patch.object(preview, 'encounter_pack', return_value=(bytes(encounter), {})) as admit, \
                 patch.object(preview.Dol, 'read', read):
                data, report = preview.make_pack(root)
            verify.assert_called_once_with(root)
            admit.assert_called_once_with(root)
            self.assertEqual(len(data), 1024)
            self.assertEqual(data[:4], b'C3P1')
            self.assertEqual(data[56:76], hashlib.sha1(data[128:]).digest())
            self.assertEqual(data[76:96], hashlib.sha1(encounter[128:]).digest())
            for i, move in enumerate(preview.MOVE_IDS):
                self.assertEqual(struct.unpack_from('>H', data, 128+i*60)[0], move)
                self.assertEqual(data[132+i*60:188+i*60], bytes([i+1])*56)
            self.assertEqual(report['scope'], 'Nonpersistent combat preview tables; no retail AI, outcome or save authority')
            self.assertEqual(report['sha256'], hashlib.sha256(data).hexdigest())

    def test_original_admission_failure_is_not_bypassed(self):
        with patch.object(preview, 'verify_disc', side_effect=ValueError('wrong original')):
            with self.assertRaisesRegex(ValueError, 'wrong original'):
                preview.make_pack(Path('/untrusted'))

    def test_pinned_damage_body_and_no_float_rewrite(self):
        source = ROOT / 'vendor/colosseum' / damage.SOURCE
        self.assertTrue(source.is_file(), 'Pinned damage source is required for this test')
        result = damage.extract(source)
        self.assertIn('s32 c3p_src_fn_80232110(', result)
        self.assertIn('extern u32 c3p_src_pokemonGetStatus()', result)
        self.assertNotIn('double', result)
        self.assertNotIn('float', result)
        with tempfile.TemporaryDirectory() as temp:
            changed = Path(temp)/'source.c'
            changed.write_bytes(source.read_bytes()+b'\n/* drift */\n')
            with self.assertRaisesRegex(ValueError, 'differs from pin'):
                damage.extract(changed)

    def test_optional_preview_packs_are_gitignored(self):
        ignored = (ROOT/'.gitignore').read_text().splitlines()
        self.assertIn('*.c3i', ignored)
        self.assertIn('*.c3p', ignored)


if __name__ == '__main__':
    unittest.main()
