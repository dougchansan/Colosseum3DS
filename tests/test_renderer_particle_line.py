"""Run the production mixed particle renderer against bounded GPU stubs."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class ParticleLineRendererTests(unittest.TestCase):
    def test_order_material_shader_rollback_and_segment(self):
        cc = shlex.split(os.environ.get('CC', 'cc'))
        if not shutil.which(cc[0]):
            self.skipTest('Host C compiler required')
        with tempfile.TemporaryDirectory() as tmp:
            directory = Path(tmp)
            (directory / 'particle_line_shader_shbin.h').write_text(
                'static const unsigned char particle_line_shader_shbin[4]={0};\n'
                'static const unsigned particle_line_shader_shbin_size=4;\n')
            flags = shlex.split(os.environ.get('CFLAGS',
                '-std=c11 -O2 -Wall -Wextra -Wpedantic -Werror'))
            exe = directory / 'line-renderer'
            result = subprocess.run(cc + flags + [
                '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'platform/3ds'),
                '-I' + str(directory), str(ROOT / 'tests/renderer_particle_line.c'),
                str(ROOT / 'src/particle_line.c'), str(ROOT / 'src/particle.c'),
                '-lm', '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('source order quad/line/quad/line', result.stdout)


if __name__ == '__main__':
    unittest.main()
