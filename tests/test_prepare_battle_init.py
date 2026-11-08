"""Synthetic inputs only. Original game bytes are never committed here."""
import hashlib
import importlib.util
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / (name + '.py'))
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


prep = module('prepare_battle_init')
reference = module('prepare_battle_init_reference')


def fixture():
    rel = bytearray(prep.COMMON_BYTES)
    struct.pack_into('>I', rel, 12, 11)
    struct.pack_into('>I', rel, 16, 0x100)
    struct.pack_into('>II', rel, 0x100 + 32, prep.BASE, 0x14A714)
    for _, count_at, _, count in prep.TABLES.values():
        struct.pack_into('>I', rel, prep.BASE + count_at, count)

    def row(table, index):
        off, _, stride, _ = prep.TABLES[table]
        return prep.BASE + off + index * stride

    enc = row('encounter', 10)
    rel[enc:enc + 3] = bytes((2, 1, 1))
    struct.pack_into('>H', rel, enc + 4, 5)
    struct.pack_into('>H', rel, enc + 24, 1)
    struct.pack_into('>I', rel, enc + 28, 1)
    struct.pack_into('>H', rel, enc + 32, 10)
    struct.pack_into('>H', rel, row('floor', 5) + 2, 200)
    struct.pack_into('>H', rel, row('trainer', 1) + 2, 1)
    struct.pack_into('>HH', rel, row('trainer', 10) + 2, 26, 238)
    rel[row('type', 1):row('type', 1) + 3] = bytes((1, 6, 2))
    for i in (238, 239):
        rel[row('pokemon', i) + 4] = 24
        struct.pack_into('>H', rel, row('pokemon', i) + 10, 288)
    species = prep.BASE + 0x121A04 + 288 * 0x11C
    rel[species + 2] = 127
    rel[species + 0x32] = 53
    struct.pack_into('>H', rel, species + 8, 70)
    struct.pack_into('>6H', rel, species + 0x84, *([50] * 6))
    rel[prep.BASE + 0x13E430:prep.BASE + 0x13E432] = bytes((100, 100))
    for i in range(1, 358):
        rel[prep.BASE + 0x11C7C4 + i * 0x38 + 1] = 20
    return bytes(rel)


class InitializerPreparationTests(unittest.TestCase):
    def admitted_fixture(self, data):
        # Admit this explicitly synthetic input only within its unit test.
        return mock.patch.object(prep, 'COMMON_SHA1', hashlib.sha1(data).hexdigest())

    def test_canonical_hash_is_required_before_table_access(self):
        for value in (b'', bytes(prep.COMMON_BYTES - 1), fixture()):
            with self.assertRaisesRegex(ValueError, 'canonical'):
                prep.make_pack(value)

    def test_layout_integrity_and_determinism(self):
        data = fixture()
        with self.admitted_fixture(data):
            pack, report = prep.make_pack(data)
            again, report_again = prep.make_pack(data)
        self.assertEqual(pack, again)
        self.assertEqual(report, report_again)
        self.assertEqual(len(pack), 768)
        self.assertEqual(pack[:4], b'C3I1')
        self.assertEqual(pack[56:76], hashlib.sha1(pack[128:]).digest())
        self.assertEqual(pack[76:96].hex(), report['battle_payload_sha1'])
        self.assertEqual(pack[96:128], bytes(32))
        self.assertEqual(report['sha256'], hashlib.sha256(pack).hexdigest())
        self.assertTrue(report['source_spans'])

    def test_rejects_changed_source_counts_and_bounds(self):
        for offset in (12, 0x100 + 32, prep.BASE + prep.TABLES['trainer'][1],
                       prep.BASE + prep.TABLES['encounter'][0] + 10 * 56 + 24):
            data = bytearray(fixture())
            data[offset] ^= 0x40
            data = bytes(data)
            with self.admitted_fixture(data), self.assertRaises(ValueError):
                prep.make_pack(data)

    def test_invalid_nature_index_or_move_pp(self):
        for offset, value in ((prep.BASE + 0x121614 + 5, 255),
                              (prep.BASE + 0x11C7C4 + 0x38 + 1, 255)):
            data = bytearray(fixture()); data[offset] = value; data = bytes(data)
            with self.admitted_fixture(data), self.assertRaises(ValueError):
                prep.make_pack(data)

    def test_private_output_and_existing_files_are_preserved(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / 'common_rel.rel'; data = fixture(); source.write_bytes(data)
            output = root / 'private' / 'init.c3i'
            with self.admitted_fixture(data):
                prep.write_pack(source, output)
                saved = output.read_bytes()
                with self.assertRaisesRegex(ValueError, 'overwrite'):
                    prep.write_pack(source, output)
                self.assertEqual(output.read_bytes(), saved)
                with self.assertRaisesRegex(ValueError, 'outside'):
                    prep.write_pack(source, ROOT / 'tests' / 'do-not-create.c3i')
                metadata_only = root / 'other.c3i.json'; metadata_only.write_text('preserve')
                with self.assertRaisesRegex(ValueError, 'overwrite'):
                    prep.write_pack(source, root / 'other.c3i')
                self.assertEqual(metadata_only.read_text(), 'preserve')
                self.assertFalse((root / 'other.c3i').exists())

    def test_symlink_source_rejected(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); source = root / 'original'; source.write_bytes(fixture())
            link = root / 'linked'
            try:
                link.symlink_to(source)
            except (OSError, NotImplementedError):
                self.skipTest('Creating symlinks unavailable on this host')
            with self.assertRaisesRegex(ValueError, 'regular'):
                prep.write_pack(link, root / 'init.c3i')

    def test_reference_rejects_source_drift(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary); source = root / reference.SOURCE
            source.parent.mkdir(parents=True); source.write_text('/* not the pinned source */\n')
            target = root / 'generated.inc'; target.write_text('preserve')
            with self.assertRaisesRegex(ValueError, 'source drift'):
                reference.prepare(root, target)
            self.assertEqual(target.read_text(), 'preserve')

    def test_reference_extraction_rejects_missing_or_unbalanced_function(self):
        for source in ('', 'u32 example(void);', 'u32 example(void) {'):
            with self.assertRaises(ValueError):
                reference.extract_function(source, 'example')
        self.assertEqual(reference.extract_function('u32 example(void) { return 1; }', 'example'),
                         'u32 example(void) { return 1; }')

    def test_reference_extracts_real_pinned_bodies_when_available(self):
        donor = ROOT / 'vendor' / 'colosseum'
        if not (donor / reference.SOURCE).is_file():
            self.skipTest('Full pinned upstream checkout required; make target enforces it')
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / 'reference.inc'
            reference.prepare(donor, target)
            text = target.read_text()
            for name in reference.FUNCTIONS:
                self.assertIn(name, text)
            self.assertIn('u32 high = fn_800E0C54();', text)
            self.assertIn('goto retry;', text)


if __name__ == '__main__':
    unittest.main()
