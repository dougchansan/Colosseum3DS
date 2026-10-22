#!/usr/bin/env python3
"""Prepare the genuine GC6E01 New Game opening movie as a private C3V1 stream.

Only openingdemo.thp from the admitted original revision is accepted. Conversion
streams through FFmpeg with bounded memory. The THP's embedded audio is kept;
the original event's separate BGM and sound-effect cues are recorded explicitly
and are not silently replaced by the embedded track.
"""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

import prepare_boot_media as media

SOURCE_COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
SOURCE = {
    "sha256": "092d180eec49a002fb2a99d4d25e967c616a4ce8d8f06c1f95b160b1897d2526",
    "bytes": 64046624, "frames": 1926, "audio": True,
}
LIMITS = {
    "max_input_bytes": SOURCE["bytes"],
    "max_frames": SOURCE["frames"],
    "max_audio_bytes": 65 * 32000 * 4,
}


def prepare(disc, output, ffmpeg):
    disc = media.verify_disc(disc)
    output = Path(output).absolute()
    if output.exists() or output.is_symlink():
        raise ValueError("Choose a new output directory; existing media is never overwritten")
    resolved = output.resolve()
    if media.ROOT == resolved or media.ROOT in resolved.parents:
        if media.ROOT / "private" not in resolved.parents:
            raise ValueError("Repository media outputs must be under ignored private/")
    source = disc / "files/movie/openingdemo.thp"
    media.admit_movie(source, SOURCE, max_input_bytes=LIMITS["max_input_bytes"])
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".c3-intro-", dir=output.parent) as temporary:
        stage = Path(temporary) / "intro"
        stage.mkdir()
        record = media.convert_movie(source, stage / "openingdemo.c3v", SOURCE,
                                     ffmpeg, **LIMITS)
        manifest = {
            "format": "C3V1", "disc": "GC6E01", "revision": 0,
            "main_dol_sha1": media.MAIN_DOL_SHA1,
            "source_commit": SOURCE_COMMIT,
            "sequence": ["New Game", "openingdemo.c3v", "floor 1, entrance 0"],
            "conversion": "Lanczos 640x480 to 320x240, RGB565LE, nominal NTSC 30000/1001; embedded PCM16LE stereo",
            "movie": record,
            "event": {
                "source": "src/game/movie.c: fn_80035EE4 / fn_80035E04",
                "ordinary_skip": False,
                "finish_set_event_flag": "0x08D0",
                "finish_floor": 1, "finish_entrance": 0,
                "fade_script": "0x05960008",
                "external_bgm": "0x0495",
                "external_sound_cues": [
                    {"movie_frame": 0x13D, "sound": "0x01D6"},
                    {"movie_frame": 0x576, "sound": "0x0449"},
                    {"movie_frame": 0x5C3, "sound": "0x01D6"},
                    {"movie_frame": 0x5D4, "sound": "0x0449"},
                ],
                "external_audio_included": False,
            },
        }
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
        stage.rename(output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path,
                        help="new private output directory; about 304 MB")
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    args = parser.parse_args()
    if not args.ffmpeg:
        parser.error("FFmpeg is required on the host")
    try:
        result = prepare(args.disc, args.output, args.ffmpeg)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("Intro preparation failed: " + str(error), file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
