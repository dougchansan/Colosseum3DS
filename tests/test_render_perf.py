"""Old-versus-new equivalence of the two per-vertex render loops on synthetic packs.

Compiles tests/render_perf_native.c against the production c3_title_animate and
the production staging block sliced from platform/3ds/title_renderer.c. Run the
built harness with the argument `bench` for the host micro-benchmark, or with
`packs` and prepared C3T files to repeat the comparison on real packs and
report how many vertices a retained model rewrites per frame:

    python3 tests/test_render_perf.py bench
    python3 tests/test_render_perf.py packs /boot/outskirt_map*.c3t
"""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def build(directory):
    compiler = shlex.split(os.environ.get("CC", "cc"))
    if not shutil.which(compiler[0]):
        return None
    source = (ROOT / "platform/3ds/title_renderer.c").read_text()
    normalize = source[source.index("static float normalize("):source.index("static unsigned morton8(")]
    staging = source[source.index("static float attenuation("):
                     source.index("typedef struct { float position[3], half_direction[3], color[3]; } SpecularLight;")]
    (directory / "render_perf_staging.inc").write_text(normalize + staging)
    executable = directory / "render-perf"
    flags = shlex.split(os.environ.get("CFLAGS", "-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror"))
    result = subprocess.run(compiler + flags + ["-I", str(ROOT / "include"), "-I", str(ROOT / "platform/3ds"),
        "-I", str(directory),
        str(ROOT / "tests/render_perf_native.c"), str(ROOT / "src/title.c"), "-lm", "-o", str(executable)],
        capture_output=True, text=True)
    if result.returncode:
        raise AssertionError(result.stdout + result.stderr)
    return executable


class RenderPerfEquivalenceTests(unittest.TestCase):
    def test_animation_and_staging_match_the_previous_loops(self):
        with tempfile.TemporaryDirectory() as temporary:
            executable = build(Path(temporary))
            if executable is None:
                self.skipTest("A host C compiler is required")
            result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn("render perf equivalence passed", result.stdout)


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] in ("bench", "packs"):
        with tempfile.TemporaryDirectory() as temporary:
            executable = build(Path(temporary))
            if executable is None:
                sys.exit("A host C compiler is required")
            sys.exit(subprocess.run([str(executable)] + sys.argv[1:]).returncode)
    unittest.main()
