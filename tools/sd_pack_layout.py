#!/usr/bin/env python3
"""Move per-floor packs out of the SD card's flat boot/ directory.

On the console every path lookup in sdmc:/3ds/Colosseum3DS/boot/ scans that
FAT directory from the start (about 1900 entries): opening a file late in it
costs ~0.4 s and a missing file ~0.85 s (hardware traces, 2026-11-20). The
game resolves every boot/<name> lookup through an index of boot/ and of the
grouped tree /3ds/Colosseum3DS/boot/packs/<group>/ (platform/3ds/boot_index.c), so
the same files can live in small per-floor directories instead, where a
lookup scans a few dozen entries. A file still in boot/ wins over a grouped
copy; builds before the index need everything back in boot/ (--undo).

Moves are renames on the same card: no file data is copied or changed.

  tools/sd_pack_layout.py --local <copy of the SD card's 3ds/Colosseum3DS>   (plan)
  tools/sd_pack_layout.py --ftp 192.168.50.154:5000 --apply                 (on the console)
  tools/sd_pack_layout.py --ftp 192.168.50.154:5000 --undo --apply

Groups are the floor prefixes of src/floor_profile.c and surveyed floors
fNNN (the longest prefix followed by "_" or "."); movies (.c3v) and shared
files (pda.c3pd, ui.c3ui, ...) stay in boot/.
"""
import argparse
import ftplib
import os
import re
import sys
from pathlib import Path

ROOT = "/3ds/Colosseum3DS"
BOOT = ROOT + "/boot"
GROUPS = "boot/packs"  # relative to ROOT: inside boot/, whose freed early slots keep this entry near the top
PROFILES = Path(__file__).resolve().parent.parent / "src" / "floor_profile.c"
SURVEYED = re.compile(r"^f[0-9a-f]{3}(?=[_.])")


def floor_prefixes(path=PROFILES):
    text = path.read_text(encoding="utf-8")
    prefixes = set(re.findall(r'\{\s*(?:0x[0-9A-Fa-f]+u|C3_FLOOR_WORLD_MAP_ID)\s*,\s*"([a-z0-9_]+)"', text))
    if not prefixes:
        raise SystemExit(f"no floor prefixes found in {path}")
    return sorted(prefixes, key=len, reverse=True)


def group_of(name, prefixes):
    """The group directory a boot file moves to, or None to keep it in boot/."""
    lower = name.lower()
    if lower.endswith(".c3v"):
        return None
    for prefix in prefixes:
        if lower.startswith(prefix) and len(lower) > len(prefix) and lower[len(prefix)] in "_.":
            return prefix
    match = SURVEYED.match(lower)
    return match.group(0) if match else None


def plan(boot_files, grouped, prefixes, undo):
    """(source, destination) renames, relative to the card's 3ds/Colosseum3DS."""
    moves = []
    if undo:
        present = {name.lower() for name in boot_files}
        for group, names in sorted(grouped.items()):
            for name in sorted(names):
                if name.lower() not in present:
                    moves.append((f"{GROUPS}/{group}/{name}", f"boot/{name}"))
        return moves
    taken = {(g, n.lower()) for g, names in grouped.items() for n in names}
    for name in sorted(boot_files):
        group = group_of(name, prefixes)
        if group and (group, name.lower()) not in taken:
            moves.append((f"boot/{name}", f"{GROUPS}/{group}/{name}"))
    return moves


class Local:
    def __init__(self, root):
        self.root = Path(root)

    def files(self, relative):
        path = self.root / relative
        if not path.is_dir():
            return [], []
        entries = list(path.iterdir())
        return [e.name for e in entries if e.is_file()], [e.name for e in entries if e.is_dir()]

    def mkdir(self, relative):
        (self.root / relative).mkdir(parents=True, exist_ok=True)

    def rename(self, source, destination):
        os.rename(self.root / source, self.root / destination)


class Remote:
    def __init__(self, address):
        host, _, port = address.partition(":")
        self.ftp = ftplib.FTP()
        self.ftp.connect(host, int(port or 5000), timeout=60)
        self.ftp.login()
        self.ftp.set_pasv(True)

    def files(self, relative):
        lines = []
        try:
            self.ftp.retrlines(f"LIST {ROOT}/{relative}", lines.append)
        except ftplib.error_perm:
            return [], []
        files, directories = [], []
        for line in lines:
            parts = line.split(None, 8)
            if len(parts) < 9 or parts[8] in (".", ".."):
                continue
            (directories if line.startswith("d") else files).append(parts[8])
        return files, directories

    def mkdir(self, relative):
        try:
            self.ftp.mkd(f"{ROOT}/{relative}")
        except ftplib.error_perm:
            pass  # already there

    def rename(self, source, destination):
        self.ftp.rename(f"{ROOT}/{source}", f"{ROOT}/{destination}")


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    where = parser.add_mutually_exclusive_group(required=True)
    where.add_argument("--local", help="local copy of the card's 3ds/Colosseum3DS directory")
    where.add_argument("--ftp", help="console FTP server host[:port]")
    parser.add_argument("--undo", action="store_true", help="move grouped files back into boot/")
    parser.add_argument("--apply", action="store_true", help="perform the renames (default: print the plan)")
    args = parser.parse_args(argv)

    card = Local(args.local) if args.local else Remote(args.ftp)
    prefixes = floor_prefixes()
    boot_files, _ = card.files("boot")
    _, groups = card.files(GROUPS)
    grouped = {group: card.files(f"{GROUPS}/{group}")[0] for group in groups}
    moves = plan(boot_files, grouped, prefixes, args.undo)
    kept = len(boot_files) - (0 if args.undo else len(moves))
    print(f"boot files={len(boot_files)} grouped={sum(map(len, grouped.values()))} "
          f"moves={len(moves)} boot_after={kept if not args.undo else len(boot_files) + len(moves)}")
    for source, destination in moves:
        print(f"{source} -> {destination}")
    if not args.apply:
        return 0
    if moves and not args.undo:
        card.mkdir(GROUPS)
        for group in sorted({destination[len(GROUPS) + 1:].split("/")[0] for _, destination in moves}):
            card.mkdir(f"{GROUPS}/{group}")
    for index, (source, destination) in enumerate(moves, 1):
        card.rename(source, destination)
        if index % 100 == 0:
            print(f"moved {index}/{len(moves)}", file=sys.stderr)
    print(f"moved {len(moves)}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
