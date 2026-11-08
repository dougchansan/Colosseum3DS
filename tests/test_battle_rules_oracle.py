"""Replay independently observed original battle leaves without private DOL bytes."""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/qualification'))
import prepare_battle_rules_oracle as PREPARE
import run_battle_rules_oracle as RUN
import check_battle_rules_oracle as CHECK

def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

VECTORS = json.loads((ROOT / 'docs/battle-rules-original-vectors.json').read_text())

class OracleInputTests(unittest.TestCase):
    def test_pinned_sidecar_retains_interpreter_and_integrity_guards(self):
        data = PREPARE.BASE.read_bytes()
        source = PREPARE.specialize(data)
        self.assertEqual(source.count('system.GetInterpreter().SingleStepInner();'), 1)
        for anchor in ('request.code_base', '!admitted', 'ppc.Exceptions != 0', 's_alert_seen',
                       '"code_modified"', '"ram_changes_truncated"', 'draw_trace.emplace_back'):
            self.assertIn(anchor, source)
        with self.assertRaisesRegex(ValueError, 'reviewed pin'):
            PREPARE.specialize(data + b'\n')

    def test_numeric_corpus_has_all_admitted_boundaries(self):
        rows = CHECK.validate_vectors(VECTORS)
        self.assertEqual(len(rows), 482)
        floor = [r for r in rows if r['function'] == 'combine_result']
        self.assertEqual(len(floor), 146)
        self.assertEqual({(r['current'], r['requested'], r['draw_flag']) for r in floor if r['draw_flag'] <= 1},
                         {(c, r, d) for c in range(8) for r in range(8) for d in (0, 1)})
        self.assertEqual(len([r for r in rows if r['function'] == 'ai_argmax']), 336)

    def test_incomplete_or_mismatched_numeric_observations_fail_closed(self):
        for mutation in ('identity', 'missing_case', 'case', 'draw', 'seed', 'path'):
            changed = copy.deepcopy(VECTORS)
            ai = next(row for row in changed['vectors'] if row['draws'])
            if mutation == 'identity': changed['dol_sha1'] = '0' * 40
            elif mutation == 'missing_case': changed['vectors'].pop()
            elif mutation == 'case': changed['vectors'][0]['requested'] = 6
            elif mutation == 'draw': ai['draw_trace'] = []
            elif mutation == 'seed': ai['draw_trace'][0]['seed_after'] = '0x00000000'
            else: ai['instruction_trace_sha256'] = ''
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                CHECK.validate_vectors(changed)

    def test_raw_observation_guards_registers_memory_and_paths(self):
        case = {'function': 'combine_result', 'current': 0, 'requested': 0, 'draw_flag': 0}
        result = {'id': 'fixture', 'status': 'returned', 'instructions': 1,
                  'instruction_pcs': ['0x801f3984'], 'draw_trace': [],
                  'ram_changes_truncated': False, 'ram_changed_bytes': 0, 'ram_changes': [],
                  'final': {'pc': hex(RUN.RETURN), 'lr': hex(RUN.RETURN),
                            'gpr': {'1': hex(RUN.STACK), '13': hex(RUN.SDA), '3': '0',
                                    **{key: hex(value) for key, value in RUN.CALLEE.items()}},
                            'memory': [{'data_hex': '00000000'}, {'data_hex': '0000'},
                                       {'data_hex': f'{RUN.SEED:08x}'}]}}
        RUN.validate(result, case, 'fixture')
        for mutation in ('id', 'status', 'register', 'path', 'write', 'truncated'):
            changed = copy.deepcopy(result)
            if mutation == 'id': changed['id'] = 'other'
            elif mutation == 'status': changed['status'] = 'alert'
            elif mutation == 'register': changed['final']['gpr']['14'] = '0'
            elif mutation == 'path': changed['instruction_pcs'] = ['0x80000000']
            elif mutation == 'write': changed['ram_changes'] = [{'address': '0x80400000', 'after_hex': '01'}]
            else: changed['ram_changes_truncated'] = True
            with self.subTest(mutation=mutation), self.assertRaises(ValueError):
                RUN.validate(changed, case, 'fixture')

class NativeReplayTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which('cc') or shutil.which('gcc')
        if not compiler or sys.platform == 'win32':
            raise unittest.SkipTest('POSIX host C compiler unavailable; native replay not executed')
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        folder = Path(cls.directory.name)
        rules = load('rules_prepare_for_oracle', ROOT / 'tools/prepare_battle_rules.py')
        rng = load('rng_prepare_for_rules_oracle', ROOT / 'tools/prepare_battle_rng.py')
        (folder / 'battle_rules_original.inc').write_text(rules.generate(ROOT / 'vendor/colosseum'))
        (folder / 'battle_rng_source.h').write_text(rng.generate(ROOT / 'vendor/colosseum'))
        cls.library = folder / ('libbattle_rules.dylib' if sys.platform == 'darwin' else 'libbattle_rules.so')
        command = [compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-Werror', '-shared', '-fPIC',
                   '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'compat/include'),
                   '-I' + str(ROOT / 'vendor/colosseum/include'), '-I' + str(folder),
                   str(ROOT / 'src/battle_rules.c'), str(ROOT / 'src/battle_rng.c'), '-o', str(cls.library)]
        subprocess.run(command, check=True, capture_output=True, text=True)

    def test_current_native_api_matches_every_original_observation(self):
        self.assertEqual(CHECK.check(VECTORS, self.library), 482)

if __name__ == '__main__':
    unittest.main()
