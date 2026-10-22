"""Execute the production draw path against a host Citro3D binding contract."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RendererTextureBindingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = shlex.split(os.environ.get("CC", "cc"))
        cls.cflags = shlex.split(os.environ.get("CFLAGS",
            "-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror"))
        if not shutil.which(cls.compiler[0]):
            raise unittest.SkipTest("A host C compiler is required for renderer execution")
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.directory = Path(cls.temporary.name)
        source = (ROOT / "platform/3ds/title_renderer.c").read_text()
        diagnostic = source[source.index("static unsigned diagnostic_frame;"):
                            source.index("static int fail(")]
        draw = source[source.index("int c3_stage_renderer_draw("):
                      source.index("int c3_title_renderer_draw(")]
        (cls.directory / "title_renderer_draw.inc").write_text(diagnostic + draw)
        cls.executable = cls.directory / "binding-test"
        result = subprocess.run(cls.compiler + cls.cflags + [
            "-I", str(ROOT / "include"), "-I", str(ROOT / "platform/3ds"),
            "-I", str(cls.directory), str(ROOT / "tests/renderer_texture_binding.c"),
            "-lm", "-o", str(cls.executable)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def check_draw(self, flags, fade):
        result = subprocess.run([str(self.executable), str(flags), str(fade)],
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.count("before_begin\n"), 4)
        self.assertEqual(result.stdout.count("after_end\n"), 4)
        self.assertEqual(result.stdout.count("cmd_usage 0.250"), 4)
        self.assertEqual(result.stdout.count("counters_before "), 4)
        self.assertEqual(result.stdout.count("counters_after "), 4)
        self.assertIn("title_gpu 0 clear after_end", result.stdout)
        self.assertIn("title_gpu 1 geometry after_end", result.stdout)
        self.assertIn("title_gpu 2 geometry_overlay after_immediate", result.stdout)
        self.assertIn("title_gpu 3 normal after_end", result.stdout)
        self.assertNotIn("title_gpu 4 ", result.stdout)

    def test_busy_queue_times_out_and_parks_without_submitting(self):
        result = subprocess.run([str(self.executable), "0", "1"],
            env={**os.environ, "C3_TEST_GPU_BUSY": "1"},
            capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 91, result.stdout + result.stderr)
        self.assertIn("GPU WAIT TIMEOUT frame 0", result.stdout)
        self.assertIn("Mode: clear", result.stdout)
        self.assertIn("Manual reset required.", result.stdout)
        self.assertIn("HOST_PARK_SENTINEL", result.stdout)
        self.assertNotIn("after_begin", result.stdout)

    def test_single_texture(self):
        self.check_draw(0, 1)

    def test_dual_texture(self):
        self.check_draw(1024, 1)

    def test_reflection_texture(self):
        self.check_draw(8192, 1)

    def test_fade_preserves_valid_texture_bindings(self):
        for flags in (0, 1024, 8192):
            for fade in (0, 0.5):
                with self.subTest(flags=flags, fade=fade):
                    self.check_draw(flags, fade)


if __name__ == "__main__":
    unittest.main()
