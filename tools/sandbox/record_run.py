#!/usr/bin/env python3
"""Record actual native-emulator screens while retaining run.py evidence.

Example: record_run.py --video /absolute/progress.mp4 -- app.3dsx
         --state /absolute/private-state --out /absolute/new-run --gl
         --frames 9000 --seconds 90 --events route.csv

The MP4 contains one unmodified combined-screen image per two 60Hz frontend
frames, played at 30fps. It has no audio. It is not a hardware benchmark.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--video", type=Path, required=True)
    parser.add_argument("run_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    run_args = args.run_args[1:] if args.run_args[:1] == ["--"] else args.run_args
    if not run_args or "--out" not in run_args:
        parser.error("Pass run.py arguments after --, including a new --out directory")
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        parser.error("FFmpeg is required for an MP4 recording")
    output = args.video.resolve()
    if output.suffix.lower() != ".mp4" or output.exists():
        parser.error("--video must name a new .mp4 file")
    run_dir = Path(run_args[run_args.index("--out") + 1]).resolve()
    if run_dir.exists():
        parser.error("--out must be a new run directory")
    output.parent.mkdir(parents=True, exist_ok=True)
    encoder_log = output.with_suffix(".ffmpeg.log")
    if encoder_log.exists() or output.with_suffix(".recording.json").exists():
        parser.error("Recording evidence paths already exist")
    with tempfile.TemporaryDirectory(prefix="c3-record-") as temporary:
        fifo = Path(temporary) / "screens.rgb"
        os.mkfifo(fifo, 0o600)
        command = [ffmpeg, "-nostdin", "-n", "-hide_banner", "-loglevel", "warning",
                   "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", "400x480",
                   "-framerate", "30", "-i", str(fifo), "-an", "-c:v", "libx264",
                   "-preset", "veryfast", "-crf", "18", "-pix_fmt", "yuv420p",
                   "-movflags", "+faststart", str(output)]
        environment = os.environ.copy()
        environment["C3_SANDBOX_RECORD_PIPE"] = str(fifo)
        with encoder_log.open("xb") as log:
            encoder = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            runner = subprocess.Popen([sys.executable, str(HERE / "run.py"), *run_args],
                                      env=environment)
            try:
                run_code = runner.wait(timeout=330)
                try:
                    encoder_code = encoder.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    encoder.terminate()
                    encoder_code = encoder.wait(timeout=5)
            except (KeyboardInterrupt, subprocess.TimeoutExpired):
                runner.send_signal(signal.SIGINT)
                try:
                    runner.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    runner.kill()
                    runner.wait()
                encoder.terminate()
                try:
                    encoder.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    encoder.kill()
                    encoder.wait()
                raise
    run = json.loads((run_dir / "run.json").read_text()) if (run_dir / "run.json").exists() else {}
    text = (run_dir / "core.log").read_text(errors="replace") if (run_dir / "core.log").exists() else ""
    match = re.search(r"RECORD END frames=(\d+) interval=(\d+) width=400 height=480 initial_black=(\d+)", text)
    frame_count = int(match[1]) if match else None
    expected = (int(run.get("completed_frames") or 0) + 1) // 2
    complete = bool(run.get("completed") and not run_code and not encoder_code and match and
                    int(match[2]) == 2 and frame_count == expected and output.is_file())
    record = {"video": str(output), "run": str(run_dir / "run.json"),
              "run_exit_code": run_code, "encoder_exit_code": encoder_code,
              "completed": complete, "frames": frame_count, "playback_fps": 30,
              "seconds": frame_count / 30 if frame_count is not None else None,
              "audio": False, "width": 400, "height": 480,
              "initial_black_frames": int(match[3]) if match else None,
              "scope": "Actual native New 3DS executable in Azahar; sampled every two frontend frames. "
                       "No hardware performance or physical-console success is implied.",
              "source_sha256": run.get("content_sha256")}
    if output.is_file():
        record["bytes"] = output.stat().st_size
        digest = hashlib.sha256()
        with output.open("rb") as source:
            for block in iter(lambda: source.read(1 << 20), b""):
                digest.update(block)
        record["sha256"] = digest.hexdigest()
    output.with_suffix(".recording.json").write_text(json.dumps(record, indent=2) + "\n")
    print(json.dumps(record, indent=2))
    return 0 if complete else run_code or encoder_code or 1


if __name__ == "__main__":
    raise SystemExit(main())
