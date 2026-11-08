"""Optional actual-resource GREEN stage tests; original packs remain private."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class StatDownStageTests(unittest.TestCase):
    def test_actual_targets_source_color_clock_audio_and_cleanup(self):
        fixture = Path(os.environ.get('C3_STAT_DOWN_STAGE_FIXTURE', str(ROOT.parent / 'stat-down-stage-v1')))
        if not (fixture / 'stat_down.c3w').is_file():
            self.skipTest('Private GREEN stat-down stage fixture required')
        cc = shlex.split(os.environ.get('CC', 'cc'))
        if not shutil.which(cc[0]):
            self.skipTest('Host C compiler required')
        sources = ['tests/test_wzx_stat_down_stage.c', 'src/title.c',
                   'src/battle_presentation.c', 'src/battle_audio.c',
                   'src/wzx_bite.c', 'src/wzx_damage.c', 'src/wzx_reflect.c',
                   'src/wzx_confusion.c', 'src/wzx_confusion_sequence.c',
                   'src/wzx_tail_whip.c', 'src/wzx_growl.c', 'src/wzx_stat_down.c', 'src/wzx_tackle.c',
                   'src/particle.c', 'src/particle_pack.c', 'src/battle_camera.c',
                   'build/save_codec.c']
        subprocess.run([sys.executable, str(ROOT / 'tools/prepare_save_codec.py')], cwd=ROOT, check=True, capture_output=True, text=True, timeout=120)
        flags = shlex.split(os.environ.get('CFLAGS', '-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror'))
        with tempfile.TemporaryDirectory() as temporary:
            exe = str(Path(temporary) / 'stage')
            result = subprocess.run([*cc, *flags, '-Iinclude', '-Isrc', '-Iplatform/3ds', *sources, '-lm', '-o', exe], cwd=ROOT, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            dual = Path(os.environ.get('C3_STAT_DOWN_RED_STAGE_FIXTURE', str(ROOT.parent / 'stat-down-red-stage-v1')))
            red_only = Path(os.environ.get('C3_STAT_DOWN_RED_ONLY_STAGE_FIXTURE', str(ROOT.parent / 'stat-down-red-only-stage-v1')))
            arguments = [exe, str(fixture)]
            if (dual / 'attack_down.c3w').is_file() and (red_only / 'attack_down.c3w').is_file():
                arguments += [str(dual), str(red_only)]
            result = subprocess.run(arguments, cwd=ROOT, capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
if __name__ == '__main__':
    unittest.main()
