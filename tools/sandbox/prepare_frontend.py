#!/usr/bin/env python3
"""Extract only the uploaded core and public build headers; no network/install."""
import argparse
import hashlib
import json
from pathlib import Path
from zipfile import ZipFile

HEADERS = ("EGL/egl.h", "EGL/eglext.h", "EGL/eglplatform.h", "KHR/khrplatform.h",
           "GL/gl.h", "GL/glext.h", "GL/glcorearb.h")

def extract_one(archive, member, destination, maximum):
    with ZipFile(archive) as source:
        entry = source.getinfo(member)
        if entry.is_dir() or entry.file_size > maximum or (entry.external_attr >> 16) & 0o170000 == 0o120000:
            raise ValueError(f"Unexpected archive entry: {member}")
        content = source.read(entry)
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.exists():
        if destination.read_bytes() != content:
            raise FileExistsError(f"Preserving different existing file: {destination}")
    else:
        with destination.open("xb") as target:
            target.write(content)
    return {"member": member, "destination": str(destination), "bytes": len(content),
            "sha256": hashlib.sha256(content).hexdigest()}

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--azahar", type=Path, required=True)
    parser.add_argument("--retroarch", type=Path, required=True)
    parser.add_argument("--output", type=Path, default=Path(__file__).resolve().parent)
    args = parser.parse_args()
    root = args.output.resolve()
    records = [extract_one(args.azahar, "azahar_libretro.so", root / "core/azahar_libretro.so", 64 << 20),
               extract_one(args.retroarch, "RetroArch-1.22.2/libretro-common/include/libretro.h", root / "include/libretro.h", 2 << 20)]
    for name in HEADERS:
        records.append(extract_one(args.retroarch, "RetroArch-1.22.2/gfx/include/MESA/" + name, root / "include" / name, 2 << 20))
    (root / "dependencies.json").write_text(json.dumps(records, indent=2) + "\n")
    print(f"Prepared {len(records)} bounded entries in {root}")

if __name__ == "__main__":
    main()
