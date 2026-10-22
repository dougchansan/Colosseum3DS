import importlib.util
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("prepare_gekko_backend", ROOT / "tools/prepare_gekko_backend.py")
backend = importlib.util.module_from_spec(spec)
spec.loader.exec_module(backend)


class GekkoBackendTests(unittest.TestCase):
    def test_generated_shader_lifecycle(self):
        compiler = shutil.which("cc") or shutil.which("gcc")
        if not compiler:
            self.skipTest("host C compiler required for generated lifecycle test")
        with tempfile.TemporaryDirectory() as folder:
            backend.prepare(ROOT / "vendor/gekkoctr", folder)
            source = (Path(folder) / "ctr_gx_citro3d.c").read_text()
            # Execute the real generated ownership paths, up through both arena
            # allocations. Only subsequent fixed GPU state setup is omitted.
            initialization = source.split("static bool ctr_gx_init_common(", 1)[1]
            initialization = "static bool ctr_gx_init_common(" + initialization.split(
                "    /*\n     * Fixed fragment setup:", 1)[0]
            initialization += "    g_state.ready = true;\n    return true;\n}\n"
            shutdown = "void gctr_ctr_gx_shutdown(void) {" + source.split(
                "void gctr_ctr_gx_shutdown(void) {", 1)[1].split(
                    "\nconst char *gctr_ctr_gx_error", 1)[0]
            state = source.split("typedef struct ctr_gx_state {", 1)[1].split(
                "} ctr_gx_state_t;", 1)[0]
            storage = "\n".join(line for line in state.splitlines() if "void *shader_blob;" in line)
            harness = (ROOT / "tests/gekko_shader_lifecycle.c.in").read_text()
            harness = harness.replace("@SHADER_STORAGE@", storage).replace(
                "@INITIALIZATION@", initialization).replace("@SHUTDOWN@", shutdown)
            path = Path(folder) / "shader_lifecycle.c"
            executable = Path(folder) / "shader_lifecycle"
            path.write_text(harness)
            # make's sanitizer CFLAGS propagate into this generated C regression.
            flags = shlex.split(os.environ.get("CFLAGS", "-std=c11 -Wall -Wextra -Werror"))
            subprocess.run([compiler, *flags, str(path), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)
            # Restore the donor's premature free to prove the harness detects
            # the reviewed behavior, rather than merely accepting new strings.
            premature = harness.replace("    shaderProgramInit(&g_state.program);",
                                        "    free(g_state.shader_blob);\n"
                                        "    shaderProgramInit(&g_state.program);", 1)
            self.assertNotEqual(premature, harness)
            path.write_text(premature)
            subprocess.run([compiler, *flags, str(path), "-o", str(executable)], check=True)
            rejected = subprocess.run([str(executable)], capture_output=True, text=True)
            self.assertNotEqual(rejected.returncode, 0, "premature shader free must fail")
            if any("-fsanitize=" in flag and "address" in flag for flag in flags):
                self.assertIn("heap-use-after-free", rejected.stderr)

    def test_pinned_adaptations_and_cleanup_order(self):
        with tempfile.TemporaryDirectory() as folder:
            backend.prepare(ROOT / "vendor/gekkoctr", folder)
            source = (Path(folder) / "ctr_gx_citro3d.c").read_text()
            header = (Path(folder) / "include/gekkoctr/gx_texture.h").read_text()
            self.assertIn("#define CTR_MAX_VERTICES 8192u", source)
            self.assertIn("#define CTR_MAX_TEXTURES 16u", source)
            self.assertIn("#define GCTR_GX_MAX_TEXTURES 16u", header)
            shutdown = source.split("void gctr_ctr_gx_shutdown(void) {", 1)[1].split("\nconst char *gctr_ctr_gx_error", 1)[0]
            self.assertNotIn("C3D_FrameSync", shutdown)
            self.assertLess(shutdown.index("C3D_RenderTargetDelete"), shutdown.index("linearFree"))
            self.assertIn("if (g_state.c3d_initialized)", shutdown)
            self.assertIn('C3D_TexDelete(tex);\n        g_state.error = "texture staging allocation failed";', source)

    def test_exact_match_refuses_donor_drift(self):
        with self.assertRaises(ValueError):
            backend.replace_once("changed source", "original source", "adapted")
        with self.assertRaises(ValueError):
            backend.replace_once("duplicate duplicate", "duplicate", "adapted")

    def test_chunks_keep_triangles_and_reject_frame_exhaustion(self):
        compiler = shutil.which("cc") or shutil.which("gcc")
        if not compiler:
            self.skipTest("host C compiler required for adapter boundary test")
        source = '''#include "model_renderer.h"
#include <assert.h>
int main(void) {
    unsigned remaining = 132, calls = 0, submitted = 0;
    while (remaining) {
        unsigned count = c3_model_renderer_chunk_size(remaining);
        assert(count <= 64 && count % 3 == 0);
        assert(c3_model_renderer_reserve(submitted, count));
        submitted += count; remaining -= count; ++calls;
    }
    assert(calls == 3 && submitted == 132);
    assert(c3_model_renderer_chunk_size(63) == 63);
    assert(c3_model_renderer_chunk_size(66) == 63);
    assert(c3_model_renderer_reserve(0, 8190));
    assert(!c3_model_renderer_reserve(8190, 3));
    assert(!c3_model_renderer_reserve(8193, 3));
    assert(!c3_model_renderer_reserve(0, 64));
    return 0;
}'''
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "chunks.c"
            executable = Path(folder) / "chunks"
            path.write_text(source)
            subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-I" + str(ROOT / "include"), "-I" + str(ROOT / "platform/3ds"),
                            str(path), "-o", str(executable)], check=True)
            subprocess.run([str(executable)], check=True)


if __name__ == "__main__":
    unittest.main()
