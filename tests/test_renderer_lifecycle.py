"""Exercise native renderer resource ownership across repeated scene changes."""
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RendererLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shlex.split(os.environ.get("CC", "cc"))
        if not shutil.which(compiler[0]):
            raise unittest.SkipTest("A host C compiler is required")
        cls.temporary = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        source = (ROOT / "platform/3ds/title_renderer.c").read_text()
        init = source[source.index("int c3_stage_renderer_init("):
                      source.index("static float attenuation(")]
        shutdown = source[source.index("void c3_title_renderer_shutdown("):]
        (directory / "renderer_lifecycle.inc").write_text(init + shutdown)
        cls.executable = directory / "lifecycle-test"
        flags = shlex.split(os.environ.get("CFLAGS", "-std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror"))
        result = subprocess.run(compiler + flags + ["-I", str(ROOT / "include"),
            "-I", str(ROOT / "platform/3ds"), "-I", str(directory),
            str(ROOT / "tests/renderer_lifecycle.c"), "-o", str(cls.executable)],
            capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_multiple_scene_bundles_keep_sdk_program_valid(self):
        result = subprocess.run([str(self.executable)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Four scene replacements", result.stdout)

    def test_every_initialization_failure_releases_partial_resources(self):
        for failure in range(1, 10):
            with self.subTest(failure=failure):
                result = subprocess.run([str(self.executable), str(failure)],
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_model_capacity_keeps_existing_gpu_bounds(self):
        result = subprocess.run([str(self.executable), "capacity"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Maximum model count", result.stdout)

    def test_tv_target_allocation_failures_and_scene_ownership(self):
        result = subprocess.run([str(self.executable), "tv"],
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("TV allocation failures", result.stdout)


if __name__ == "__main__":
    unittest.main()
