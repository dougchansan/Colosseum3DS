#!/usr/bin/env python3
"""Bound an isolated libretro run and bind captures to content/input hashes."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import re
import signal
import subprocess
import time

HERE = Path(__file__).resolve().parent

def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            value.update(block)
    return value.hexdigest()

def sd_records(root, names):
    records = []
    for name in names:
        path = (root / name).resolve()
        if name.is_absolute() or not path.is_relative_to(root):
            raise ValueError(f"SD evidence path must stay inside the emulated SD: {name}")
        record = {"path": str(name), "exists": path.exists()}
        if path.exists():
            if not path.is_file():
                raise ValueError(f"SD evidence path must name a file: {name}")
            record.update(bytes=path.stat().st_size, sha256=digest(path))
        records.append(record)
    return records

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("content", type=Path)
    parser.add_argument("--core", type=Path, default=HERE / "core/azahar_libretro.so")
    parser.add_argument("--state", type=Path, default=HERE / "state")
    parser.add_argument("--out", type=Path, required=True, help="New run directory")
    parser.add_argument("--frames", type=int, default=180)
    parser.add_argument("--seconds", type=int, default=45)
    parser.add_argument("--capture-every", type=int, default=30)
    parser.add_argument("--events", type=Path)
    parser.add_argument("--sd-file", type=Path, action="append", default=[],
                        help="SD-relative asset/save to hash before and after the run; repeatable")
    parser.add_argument("--gl", action="store_true", help="Optional Mesa EGL; default is Azahar software PICA")
    args = parser.parse_args()
    if not 1 <= args.frames <= 36000 or not 1 <= args.seconds <= 300 or args.capture_every < 1:
        parser.error("Invalid run bounds")
    content, core, state, output = (p.resolve() for p in (args.content, args.core, args.state, args.out))
    if not content.is_file() or not core.is_file():
        parser.error("Content/core must exist")
    sdmc = (state / "save/Azahar/sdmc").resolve()
    try:
        before = sd_records(sdmc, args.sd_file)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    state.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=False)
    command = [str(HERE / "harness"), str(content), "--core", str(core), "--state", str(state),
               "--out", str(output), "--frames", str(args.frames), "--seconds", str(args.seconds),
               "--capture-every", str(args.capture_every)]
    record = {"started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "content": str(content), "content_sha256": digest(content), "core_sha256": digest(core),
              "harness_sha256": digest(HERE / "harness"), "harness_source_sha256": digest(HERE / "harness.c"),
              "renderer": "OpenGL/llvmpipe" if args.gl else "Azahar software PICA", "system_model": "New 3DS",
              "state": str(state), "sdmc": str(sdmc), "frame_limit": args.frames,
              "wall_limit_seconds": args.seconds, "capture_every": args.capture_every,
              "sd_files_before": before,
              "scope": "Emulator integration evidence; not hardware performance or save-format verification"}
    if args.gl:
        command.append("--gl")
    if args.events:
        events = args.events.resolve()
        (output / "input.csv").write_bytes(events.read_bytes())
        record["input_sha256"] = digest(events)
        command += ["--events", str(output / "input.csv")]
    record["command"] = command
    (output / "run.json").write_text(json.dumps(record, indent=2) + "\n")
    started = time.monotonic()
    with (output / "core.log").open("xb") as log:
        process = subprocess.Popen(command, cwd=HERE, stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
        try:
            process.wait(timeout=args.seconds + 3)
        except (subprocess.TimeoutExpired, KeyboardInterrupt) as error:
            record["forced_stop"] = True
            record["stop_reason"] = "wall_timeout" if isinstance(error, subprocess.TimeoutExpired) else "keyboard_interrupt"
            os.killpg(process.pid, signal.SIGTERM)
            try:
                process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
    record["process_exit_code"] = process.returncode
    exit_code = process.returncode if process.returncode >= 0 else 128 - process.returncode
    record.update(completed_frames=None, interrupted=None, shutdown=None)
    match = re.search(r"RUN END frames=(\d+) video=(\d+) audio=(\d+) elapsed=([\d.]+) interrupted=(\d+) shutdown=(\d+)",
                      (output / "core.log").read_text(errors="replace"))
    if match:
        record.update(completed_frames=int(match[1]), video_callbacks=int(match[2]),
                      audio_frames=int(match[3]), run_loop_seconds=float(match[4]),
                      interrupted=bool(int(match[5])), shutdown=bool(int(match[6])))
        if not record["shutdown"] and record["completed_frames"] < args.frames and not exit_code:
            # Also detect incomplete runs from older harness builds that returned
            # success after their SIGALRM handler merely set a stop flag.
            exit_code = 124 if record["interrupted"] else 1
    elif not exit_code:
        exit_code = 1
    if record.get("forced_stop"):
        exit_code = 124 if record["stop_reason"] == "wall_timeout" else 130
    record["elapsed_seconds"] = time.monotonic() - started
    try:
        record["sd_files_after"] = sd_records(sdmc, args.sd_file)
        for initial, final in zip(before, record["sd_files_after"]):
            final["changed"] = any(initial.get(key) != final.get(key)
                                   for key in ("exists", "bytes", "sha256"))
    except (OSError, ValueError) as error:
        record["sd_evidence_error"] = str(error)
    record["captures"] = [{"file": p.name, "sha256": digest(p), "bytes": p.stat().st_size}
                          for p in sorted(output.glob("frame-*.ppm"))]
    if not record["captures"] or record.get("sd_evidence_error"):
        exit_code = exit_code or 1
    record["exit_code"] = exit_code
    record["completed"] = bool(not exit_code and (record["shutdown"] or record["completed_frames"] == args.frames))
    try:
        from PIL import Image
        for path in output.glob("*.ppm"):
            with Image.open(path) as image:
                image.save(path.with_suffix(".png"))
        record["png_conversion"] = True
    except ImportError:
        record["png_conversion"] = False
    (output / "run.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps({"exit_code": exit_code, "completed": record["completed"],
                      "completed_frames": record["completed_frames"],
                      "interrupted": record["interrupted"], "shutdown": record["shutdown"],
                      "captures": len(record["captures"]),
                      "elapsed_seconds": record["elapsed_seconds"], "output": str(output), "sdmc": record["sdmc"]}, indent=2))
    return exit_code

if __name__ == "__main__":
    raise SystemExit(main())
