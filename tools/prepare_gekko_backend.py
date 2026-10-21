"""Generate the bounded native renderer adaptations from the pinned donor."""
import argparse
from pathlib import Path
import subprocess

PIN = "0f2032c6d641c78ebf4e6a418ecb51e04c0c62d8"


def replace_once(text, old, new):
    if text.count(old) != 1:
        raise ValueError(f"donor adaptation no longer matches: {old!r}")
    return text.replace(old, new, 1)


def prepare(donor, output):
    donor, output = Path(donor), Path(output)
    head = subprocess.check_output(["git", "-C", str(donor), "rev-parse", "HEAD"], text=True).strip()
    if head != PIN:
        raise ValueError(f"GekkoCTR must be pinned at {PIN}, found {head}")
    paths = ["platform/ctr/ctr_gx_citro3d.c", "include/gekkoctr/gx_texture.h"]
    sources = []
    for path in paths:
        canonical = subprocess.check_output(["git", "-C", str(donor), "show", f"{PIN}:{path}"])
        actual = (donor / path).read_bytes().replace(b"\r\n", b"\n")
        if actual != canonical.replace(b"\r\n", b"\n"):
            raise ValueError(f"donor source modified: {path}")
        sources.append(canonical.decode())
    source, header = sources
    source = replace_once(source, "#define CTR_MAX_VERTICES 256u", "#define CTR_MAX_VERTICES 8192u")
    source = replace_once(source, "#define CTR_MAX_TEXTURES 8u", "#define CTR_MAX_TEXTURES 16u")
    source = replace_once(source, "    bool ready;", "    bool c3d_initialized;\n    bool ready;")
    source = replace_once(source, "    g_state.target = C3D_RenderTargetCreate", "    g_state.c3d_initialized = true;\n    g_state.target = C3D_RenderTargetCreate")
    source = replace_once(source, "void gctr_ctr_gx_shutdown(void) {", """void gctr_ctr_gx_shutdown(void) {
    /* Target deletion drains the GPU queue without waiting on VBlank. */
    if (g_state.target != NULL) {
        C3D_RenderTargetDelete(g_state.target);
        g_state.target = NULL;
    }""")
    source = replace_once(source, "    if (g_state.ready) {\n        C3D_Fini();\n    }", "    if (g_state.c3d_initialized) {\n        C3D_Fini();\n        g_state.c3d_initialized = false;\n    }")
    source = replace_once(source, '        g_state.error = "texture staging allocation failed";', '        C3D_TexDelete(tex);\n        g_state.error = "texture staging allocation failed";')
    header = replace_once(header, "#define GCTR_GX_MAX_TEXTURES 8u", "#define GCTR_GX_MAX_TEXTURES 16u")
    outputs = {output / "ctr_gx_citro3d.c": source, output / "include/gekkoctr/gx_texture.h": header}
    for path, content in outputs.items():
        path.parent.mkdir(parents=True, exist_ok=True)
        if not path.exists() or path.read_text() != content:
            path.write_text(content, newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--donor", default="vendor/gekkoctr")
    parser.add_argument("--output", default="build-3ds/gekko")
    args = parser.parse_args()
    prepare(args.donor, args.output)
