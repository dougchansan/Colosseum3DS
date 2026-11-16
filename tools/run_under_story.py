"""Build and audit only The Under's private original field packs and checkpoints.

Returns nonzero for incomplete coverage. Reports stay private and distinguish
synthetic battle wins and checkpoint placement from continuous story proof.
No console access, pack preparation, asset copying, or save writes occur.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
SOURCES = [
    "tests/test_under_story.c", "src/field_story.c", "src/story.c", "src/area.c",
    "src/area_camera.c", "src/field.c", "src/title.c", "src/game_state.c",
    "src/save.c", "src/decomp_pokemon.c", "src/battle.c", "src/relive.c",
    "build/save_codec.c", "src/inventory.c", "src/pda.c",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot", type=Path, required=True)
    parser.add_argument("--saves", type=Path, required=True,
                        help="one GCI or directory of checkpoint GCIs")
    parser.add_argument("--output", type=Path, default=REPO / "private/under/host")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--walkable", action="store_true",
                        help="classify seven non-field sets as unqualified, audit 19 field floors")
    args = parser.parse_args()
    if not args.output.resolve().is_relative_to((REPO / "private").resolve()):
        parser.error("Output must stay inside this worktree private/ directory")
    args.output.mkdir(parents=True, exist_ok=True)
    (REPO / "build").mkdir(exist_ok=True)
    subprocess.run([sys.executable, "tools/prepare_save_codec.py"], cwd=REPO, check=True)
    executable = REPO / "build" / ("test_under_story.exe" if os.name == "nt" else "test_under_story")
    command = [args.cc, "-Iinclude", "-Icompat/include", "-Ivendor/colosseum/include",
               "-Isrc", "-std=c11", "-O2", "-g", "-Wall", "-Wextra", "-Wpedantic", "-Werror"]
    if os.name == "nt":
        command += ["-Dfsync=_commit", "-include", "io.h"]
    command += SOURCES + ["-lzstd", "-lz", "-lm", "-o", str(executable)]
    environment = dict(os.environ)
    environment["PATH"] = str(Path(args.cc).resolve().parent) + os.pathsep + environment["PATH"]
    subprocess.run(command, cwd=REPO, env=environment, check=True)
    saves = sorted(args.saves.glob("*.gci")) if args.saves.is_dir() else [args.saves]
    if not saves:
        parser.error("no checkpoint saves found")
    results = []
    copies = args.output / "save-copies"
    copies.mkdir(exist_ok=True)
    for save in saves:
        original_hash = hashlib.sha256(save.read_bytes()).hexdigest()
        copied_save = copies / save.name
        if save.resolve() == copied_save.resolve():
            parser.error("input save must be outside output/save-copies")
        shutil.copyfile(save, copied_save)
        report = args.output / (save.stem + ".txt")
        trace = args.output / (save.stem + ".trace")
        run_command = [str(executable), str(args.boot), str(args.boot), str(copied_save), str(report), str(trace)]
        if args.walkable:
            run_command.append("--walkable")
        result = subprocess.run(run_command, cwd=REPO, env=environment)
        text = report.read_text() if report.exists() else ""
        events = [line for line in text.splitlines() if line.startswith("under prefix=")]
        failures = [line for line in events if "status=failed" in line or "status=blocked" in line or "status=partial" in line]
        checkpoint = re.search(r"under_save floor=([0-9a-f]+)", text)
        summary = next((line for line in text.splitlines() if line.startswith("under_summary ")), None)
        source_unchanged = hashlib.sha256(save.read_bytes()).hexdigest() == original_hash
        copy_unchanged = hashlib.sha256(copied_save.read_bytes()).hexdigest() == original_hash
        results.append({"save": save.name, "sha256": original_hash, "saved_floor": checkpoint.group(1) if checkpoint else None,
                        "source_unchanged": source_unchanged, "copy_unchanged": copy_unchanged,
                        "returncode": result.returncode if source_unchanged and copy_unchanged and summary else 2, "summary": summary, "failures": failures,
                        "flag_changes": [line for line in text.splitlines() if line.startswith("under_flag ")],
                        "connectors": [line for line in events if "next=000" not in line],
                        "report": str(report), "trace": str(trace),
                        "saved_flags": [line for line in text.splitlines() if line.startswith("under_saved_flag ")]})
        print(save.name, summary or "audit did not finish", flush=True)
    (args.output / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
    return int(any(result["returncode"] for result in results))


if __name__ == "__main__":
    raise SystemExit(main())
