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
        validate = source[source.index("static bool valid_batch("):
                          source.index("/* HSD reserves a ninth")]
        draw = source[source.index("int c3_stage_renderer_draw("):
                      source.index("int c3_title_renderer_draw(")]
        shutdown = source[source.index("void c3_title_renderer_shutdown("):]
        (cls.directory / "title_renderer_draw.inc").write_text(validate + draw + shutdown)
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
        self.assertEqual(result.stdout, "")

    def test_static_staging_keeps_draws_and_invalidates_camera_state(self):
        result = subprocess.run([str(self.executable), "0", "1"],
            env={**os.environ,"C3_TEST_STATIC":"1"},capture_output=True,text=True,timeout=10)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn("Static staging reuse",result.stdout)

    def test_busy_returns_and_retains_ownership_until_idle_teardown(self):
        result = subprocess.run([str(self.executable), "0", "1"],
            env={**os.environ, "C3_TEST_GPU_BUSY": "1"},
            capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout,
            "BUSY returned; ownership retained; idle teardown verified\n")

    def test_particle_command_overload_closes_frame(self):
        result = subprocess.run([str(self.executable), "0", "1"],
            env={**os.environ, "C3_TEST_PARTICLE_OVERLOAD": "1"},
            capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout+result.stderr)
        self.assertIn("overload closed acquired frame safely", result.stdout)

    def test_single_texture(self):
        self.check_draw(0, 1)

    def test_tv_pass_has_no_feedback_and_never_enters_main_scene(self):
        result = subprocess.run([str(self.executable), "0", "1"],
            env={**os.environ, "C3_TEST_TV": "1"},
            capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("disabled TV stays hidden", result.stdout)

    def test_dual_texture(self):
        self.check_draw(1024, 1)

    def test_reflection_texture(self):
        self.check_draw(8192, 1)

    def test_original_npc_alpha_reference(self):
        result = subprocess.run([str(self.executable), "0", "0.5"],
            env={**os.environ, "C3_TEST_ALPHA_REF": "127"},
            capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_fade_preserves_valid_texture_bindings(self):
        for flags in (0, 1024, 8192):
            for fade in (0, 0.5):
                with self.subTest(flags=flags, fade=fade):
                    self.check_draw(flags, fade)


if __name__ == "__main__":
    unittest.main()
