"""Build and execute a private Under route with one persistent imported game.

Returns nonzero for incomplete coverage. Reports stay private and distinguish
synthetic battle wins and declared probe placements from interactive play.
No console access, pack preparation, asset copying, or save writes occur.
"""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
import sys

REPO = Path(__file__).resolve().parents[1]
SOURCES = [
    "tests/test_under_chapter.c", "src/inventory.c", "src/pda.c", "src/field_story.c", "src/story.c", "src/area.c",
    "src/area_camera.c", "src/field.c", "src/title.c", "src/game_state.c",
    "src/save.c", "src/decomp_pokemon.c", "src/battle.c", "src/relive.c",
    "build/save_codec.c",
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--boot", type=Path, required=True)
    parser.add_argument("--saves", type=Path, required=True,
                        help="one GCI or directory of checkpoint GCIs")
    parser.add_argument("--output", type=Path, default=REPO / "private/under/chapter")
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--recipe", type=Path, required=True, help="private original-event route recipe")
    parser.add_argument("--expect-save-sha256", help="require this exact single checkpoint identity")
    args = parser.parse_args()
    if not args.output.resolve().is_relative_to((REPO / "private").resolve()):
        parser.error("Output must stay inside this worktree private/ directory")
    args.output.mkdir(parents=True, exist_ok=True)
    (REPO / "build").mkdir(exist_ok=True)
    subprocess.run([sys.executable, "tools/prepare_save_codec.py"], cwd=REPO, check=True)
    executable = REPO / "build" / ("test_under_chapter.exe" if os.name == "nt" else "test_under_chapter")
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
    if args.expect_save_sha256 and len(saves) != 1:
        parser.error("--expect-save-sha256 requires one save")
    copies = args.output / "save-copies"
    copies.mkdir(exist_ok=True)
    results = []
    for save in saves:
        original_hash = hashlib.sha256(save.read_bytes()).hexdigest()
        if args.expect_save_sha256 and original_hash != args.expect_save_sha256.lower():
            parser.error("chapter checkpoint differs from the required SHA-256")
        copied_save = copies / save.name
        if save.resolve() == copied_save.resolve():
            parser.error("input save must be outside output/save-copies")
        shutil.copyfile(save, copied_save)
        report = args.output / (save.stem + ".txt")
        trace = args.output / (save.stem + ".trace")
        run_command = [str(executable), str(args.boot), str(copied_save), str(args.recipe), str(report), str(trace)]
        result = subprocess.run(run_command, cwd=REPO, env=environment)
        source_unchanged = hashlib.sha256(save.read_bytes()).hexdigest() == original_hash
        copy_unchanged = hashlib.sha256(copied_save.read_bytes()).hexdigest() == original_hash
        text = report.read_text() if report.exists() else ""
        summary = next((line for line in text.splitlines() if line.startswith("chapter_summary ")), None)
        results.append({"save": save.name, "sha256": original_hash, "source_unchanged": source_unchanged, "copy_unchanged": copy_unchanged,
                        "returncode": result.returncode if source_unchanged and copy_unchanged and summary else 2, "summary": summary,
                        "report": str(report), "trace": str(trace),
                        "flag_checks": [line for line in text.splitlines() if line.startswith("chapter_flag ")]})
        print(save.name, summary or "audit did not finish", flush=True)
    (args.output / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
    return int(any(result["returncode"] for result in results))


if __name__ == "__main__":
    raise SystemExit(main())
