"""Actual private Tackle assets through production stage and particle geometry."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]

class TackleStageTests(unittest.TestCase):
    def test_actual_source_timeline_geometry_audio_and_lifetime(self):
        fixture=Path(os.environ.get('C3_TACKLE_STAGE_FIXTURE',str(ROOT.parent/'tackle-stage-v1')))
        if not (fixture/'tackle.c3w').is_file():
            self.skipTest('Private Tackle stage fixture required')
        cc=shlex.split(os.environ.get('CC','cc'))
        if not shutil.which(cc[0]):self.skipTest('Host C compiler required')
        sources=['tests/test_wzx_tackle_stage.c','src/title.c','src/battle_presentation.c',
                 'src/battle_audio.c','src/wzx_bite.c','src/wzx_damage.c','src/wzx_reflect.c',
                 'src/wzx_confusion.c','src/wzx_confusion_sequence.c','src/wzx_tail_whip.c',
                 'src/wzx_growl.c','src/wzx_stat_down.c','src/wzx_tackle.c','src/particle.c',
                 'src/particle_pack.c','src/particle_line.c','src/battle_camera.c','build/save_codec.c']
        subprocess.run([sys.executable,'tools/prepare_save_codec.py'],cwd=ROOT,check=True,capture_output=True,text=True,timeout=120)
        flags=shlex.split(os.environ.get('CFLAGS','-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror'))
        with tempfile.TemporaryDirectory() as tmp:
            exe=str(Path(tmp)/'tackle-stage')
            result=subprocess.run([*cc,*flags,'-Iinclude','-Isrc','-Iplatform/3ds',*sources,'-lm','-o',exe],cwd=ROOT,capture_output=True,text=True,timeout=120)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([exe,str(fixture)],cwd=ROOT,capture_output=True,text=True,timeout=180)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('Actual Tackle stage contract passed',result.stdout)
            print(result.stdout,end='')

if __name__=='__main__':unittest.main()
