#!/usr/bin/env python3
"""Bound an isolated libretro run and bind captures to content/input hashes."""
import argparse
import csv
import datetime
import hashlib
import io
import json
import os
from pathlib import Path
import re
import signal
import stat
import subprocess
import time

HERE = Path(__file__).resolve().parent
LIVE_PACKET_BYTES = 256
LIVE_HEADER = ["sequence", "start", "count", "mask", "left_x", "left_y", "right_x", "right_y"]

# Azahar can log an invalid guest read and continue rendering instead of
# raising the ARM11 abort a real console would take. A completed frame budget
# therefore does not establish a valid native run on its own. Match only
# explicit guest execution/memory faults, not absent optional firmware or
# unimplemented platform services that the application can handle.
GUEST_FAULT = re.compile(
    r"(?:HW\.Memory\s+<(?:Error|Critical)>.*(?:[Uu]nmapped|[Ii]nvalid|[Oo]ut.of.bounds)"
    r"|(?:Core|Kernel|HW\.GPU)[^\n]*<(?:Error|Critical)>[^\n]*"
    r"(?:[Uu]ndefined instruction|[Pp]refetch abort|[Dd]ata abort|[Aa]ssertion failed|[Ff]atal exception))"
)

def guest_faults(log):
    """Return bounded fault lines; input is the complete emitted core log."""
    lines = [line for line in log.splitlines() if GUEST_FAULT.search(line)]
    return {"count": len(lines), "lines": lines[:32], "truncated": len(lines) > 32}

def application_failures(log):
    """The app can return an error while the emulator keeps producing frames."""
    lines = [line for line in log.splitlines()
             if re.match(r"(?:boot|field)_end result=-\d+(?:\s|$)", line)
             or line.startswith("game_save status=failed ")]
    return {"count": len(lines), "lines": lines[:32], "truncated": len(lines) > 32}

def digest(path):
    value = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            value.update(block)
    return value.hexdigest()

def live_packet_snapshot(path, output, name):
    """Archive a bounded observation, never treat a mutable packet as an ack."""
    try:
        descriptor = os.open(path, os.O_RDONLY | os.O_NONBLOCK | os.O_CLOEXEC | os.O_NOFOLLOW)
    except FileNotFoundError:
        return {"exists": False}
    except OSError as error:
        return {"snapshot_error": str(error)}
    try:
        entry = os.fstat(descriptor)
        if not stat.S_ISREG(entry.st_mode) or not 1 <= entry.st_size <= LIVE_PACKET_BYTES:
            return {"snapshot_error": "Packet is not a regular file of 1..256 bytes"}
        data = os.read(descriptor, LIVE_PACKET_BYTES + 1)
        if len(data) != entry.st_size or len(data) > LIVE_PACKET_BYTES:
            return {"snapshot_error": "Packet changed during bounded snapshot"}
        (output / name).write_bytes(data)
        return {"exists": True, "file": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    except OSError as error:
        return {"snapshot_error": str(error)}
    finally:
        os.close(descriptor)

def live_input_evidence(path, frame_limit):
    """Validate and hash what the frontend actually accepted, not proposals."""
    with path.open("rb") as source:
        data = source.read(4 * 1024 * 1024 + 1)
    if len(data) > 4 * 1024 * 1024:
        raise ValueError("Live input evidence exceeds its bounded size")
    rows = csv.reader(io.StringIO(data.decode("ascii")))
    if next(rows, []) != LIVE_HEADER:
        raise ValueError("Live input evidence has an invalid header")
    accepted, last_sequence, last_frame = 0, 0, -1
    for row in rows:
        if len(row) != 8:
            raise ValueError("Live input evidence has an invalid row")
        sequence, start, count = (int(value, 10) for value in row[:3])
        mask, axes = int(row[3], 0), [int(value, 10) for value in row[4:]]
        if not (last_sequence < sequence <= (1 << 64)-1 and last_frame < start < frame_limit
                and 1 <= count <= 36000 and 0 <= mask <= 65535
                and all(-32768 <= axis <= 32767 for axis in axes)):
            raise ValueError("Live input evidence violates packet or accepted-frame bounds")
        accepted += 1
        if accepted > frame_limit:
            raise ValueError("Too many accepted live packets")
        last_sequence, last_frame = sequence, start
    return {"file": path.name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(),
            "accepted_packets": accepted, "last_sequence": last_sequence,
            "last_start_frame": last_frame if accepted else None}

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
    parser.add_argument("--live-input", type=Path,
                        help="Atomic controller packet file outside --out; each newer sequence expires to neutral")
    parser.add_argument("--sd-file", type=Path, action="append", default=[],
                        help="SD-relative asset/save to hash before and after the run; repeatable")
    parser.add_argument("--app-trace", type=Path,
                        help="SD-relative boot trace to archive and check for application errors")
    parser.add_argument("--gl", action="store_true", help="Optional Mesa EGL; default is Azahar software PICA")
    args = parser.parse_args()
    if not 1 <= args.frames <= 36000 or not 1 <= args.seconds <= 300 or args.capture_every < 1:
        parser.error("Invalid run bounds")
    content, core, state, output = (p.resolve() for p in (args.content, args.core, args.state, args.out))
    if not content.is_file() or not core.is_file():
        parser.error("Content/core must exist")
    live_input = None
    if args.live_input:
        if args.live_input.is_symlink():
            parser.error("Live input must not be a symbolic link")
        live_input = args.live_input.resolve()
        if live_input == output or live_input.is_relative_to(output):
            parser.error("Live input packet must be outside the evidence output directory")
    sdmc = (state / "save/Azahar/sdmc").resolve()
    try:
        before = sd_records(sdmc, args.sd_file)
        trace_before = sd_records(sdmc, [args.app_trace])[0] if args.app_trace else None
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
    recording_pipe = os.environ.get("C3_SANDBOX_RECORD_PIPE")
    if recording_pipe:
        command += ["--record-rgb", recording_pipe, "--record-every", "2"]
        record["recording"] = {"format": "RGB24", "width": 400, "height": 480,
                               "frontend_frames_per_sample": 2, "playback_fps": 30,
                               "audio": False,
                               "scope": "Sampled at the emulator frontend clock; not measured hardware performance"}
    if args.events:
        events = args.events.resolve()
        (output / "input.csv").write_bytes(events.read_bytes())
        record["input_sha256"] = digest(events)
        command += ["--events", str(output / "input.csv")]
    if live_input:
        command += ["--live-input", str(live_input)]
        record["live_input"] = {
            "packet_path": str(live_input), "packet_max_bytes": LIVE_PACKET_BYTES,
            "semantics": "CSV until first accepted live packet; then complete live override, expiring to neutral",
            "sequence": "Strictly increasing positive uint64; repeated/stale packets never renew duration",
            "clock": "Accepted at the next frontend frame before retro_run; duration counts frontend frames",
            "initial_packet": live_packet_snapshot(live_input, output, "live-input-initial.txt")}
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
    core_log = (output / "core.log").read_text(errors="replace")
    match = re.search(r"RUN END frames=(\d+) video=(\d+) audio=(\d+) elapsed=([\d.]+) interrupted=(\d+) shutdown=(\d+)",
                      core_log)
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
    record["guest_faults"] = guest_faults(core_log)
    if record["guest_faults"]["count"]:
        exit_code = exit_code or 1
    if live_input:
        record["live_input"]["final_packet"] = live_packet_snapshot(live_input, output, "live-input-final.txt")
        try:
            accepted = live_input_evidence(output / "live-input.csv", args.frames)
            logged_count = len(re.findall(r"^LIVE INPUT accepted sequence=", core_log, re.MULTILINE))
            if accepted["accepted_packets"] != logged_count:
                raise ValueError("Accepted live input CSV and core log disagree")
            record["live_input"]["accepted"] = accepted
            live_end = re.search(r"LIVE INPUT END accepted=(\d+) expired=(\d+) rejected_polls=(\d+) last_sequence=(\d+)", core_log)
            if live_end:
                if int(live_end[1]) != accepted["accepted_packets"] or int(live_end[4]) != accepted["last_sequence"]:
                    raise ValueError("Final live input counters disagree with accepted evidence")
                record["live_input"].update(expired_packets=int(live_end[2]), rejected_polls=int(live_end[3]))
        except (OSError, ValueError, csv.Error) as error:
            record["live_input"]["evidence_error"] = str(error)
            exit_code = exit_code or 1
    if args.app_trace:
        try:
            trace_after = sd_records(sdmc, [args.app_trace])[0]
            if not trace_after["exists"] or trace_after.get("sha256") == trace_before.get("sha256"):
                raise ValueError("Application trace was not created or updated by this run")
            trace_bytes = (sdmc / args.app_trace).read_bytes()
            (output / "application-trace.log").write_bytes(trace_bytes)
            record["application_trace"] = trace_after
            record["application_failures"] = application_failures(trace_bytes.decode(errors="replace"))
            if record["application_failures"]["count"]:
                exit_code = exit_code or 1
        except (OSError, ValueError) as error:
            record["application_trace_error"] = str(error)
            exit_code = exit_code or 1
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
