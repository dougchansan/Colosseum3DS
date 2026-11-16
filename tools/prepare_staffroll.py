#!/usr/bin/env python3
"""Prepare the admitted credits THP and original clear-save messages privately.

Video is streamed through the existing C3V1 encoder. The source has no audio
track: its external BGM 0x4C9 is played by the runtime music service.
"""
import argparse
import json
from pathlib import Path
import shutil
import uuid

import prepare_boot_media as media
from prepare_model import check_output
from prepare_outskirt import u32
from prepare_story import admit_common_rel, encode_pack, inspect_bank, be16

SOURCE = {
    "sha256": "3b867d6d447ad9fe6d00ff6a9b1821e5c0f9b32cd13b511bf92c4cd1bce8fd2c",
    "bytes": 109163344, "frames": 5250, "audio": False,
}
LIMITS = {"max_input_bytes": SOURCE["bytes"], "max_frames": SOURCE["frames"],
          "max_audio_bytes": 0}


def message_pack(disc):
    rel = admit_common_rel(disc / "files")
    base = u32(rel, u32(rel, 16) + 4 * 8) & ~3
    common = rel[base + 0x89CFC:base + 0x8BCB4]
    messages = rel[base + 0x647B4:base + 0x647B4 + 0x124DD]
    names = rel[base + 0x58044:base + 0x647B4]
    inspect_bank(common)
    if be16(common, 0) != 0x596 or be16(messages, 4) != 684 or be16(names, 4) != 2018:
        raise ValueError("Unexpected original common banks")
    return encode_pack(0x39F, [common], [messages, names])


def prepare(disc, output, ffmpeg):
    disc = media.verify_disc(disc)
    output = check_output(Path(output))
    source = disc / "files/movie/staffroll.thp"
    media.admit_movie(source, SOURCE, max_input_bytes=LIMITS["max_input_bytes"])
    messages = message_pack(disc)
    output.parent.mkdir(parents=True, exist_ok=True)
    stage = output.with_name(".staffroll-" + uuid.uuid4().hex)
    stage.mkdir()
    try:
        record = media.convert_movie(source, stage / "staffroll.c3v", SOURCE, ffmpeg, **LIMITS)
        (stage / "staffroll.c3s").write_bytes(messages)
        manifest = {
            "format": "C3V1", "disc": "GC6E01", "revision": 0,
            "main_dol_sha1": media.MAIN_DOL_SHA1,
            "source_commit": "51bfe4314a218dfaf9592fc80723c50498d0ee55",
            "movie": record,
            "messages": {"file": "staffroll.c3s", "bytes": len(messages),
                         "sha256": media.file_digest(stage / "staffroll.c3s")},
            "event": {"source": "movie.c fn_800361C0 / fn_80035F64",
                      "floor": 0x39F, "previous_floor_guard": 0x76,
                      "clear_flag_guard": "0x476 == 1", "prepared_flag": "0x478",
                      "save_checkpoint": [2, 1, 1], "return_floor": 2, "return_entrance": 1,
                      "fade_script": "0x05960008", "ordinary_skip": False,
                      "external_bgm": "0x04C9", "embedded_audio": False,
                      "external_audio_included": False}}
        (stage / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        if output.exists():
            raise FileExistsError(output)
        stage.rename(output)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path, help="new private directory; about 806 MB")
    parser.add_argument("--ffmpeg", default=shutil.which("ffmpeg"))
    args = parser.parse_args()
    if not args.ffmpeg:
        parser.error("FFmpeg is required")
    print(json.dumps(prepare(args.disc, args.output, args.ffmpeg), indent=2))


if __name__ == "__main__":
    main()
