#!/usr/bin/env python3
"""Prepare the original message groups of the story menus (private packs).

Each C3M file is one original message group (big-endian GSmsg table, the
FSYS type-0A member as stored on the disc after LZSS), loaded at run time
with c3_story_add_messages. Outputs stay private.

  name_entry.c3m  name_entry_menu.fsys 02FD0A00  name entry (floor 0x390)
  pcbox.c3m       pcbox_menu.fsys      02E90A00  PC box / bag menus (0x391, 0x38F)
  pda.c3m         pda_menu.fsys        03750A00  P*DA and mail (0x392)
  worldmap.c3m    world_map.fsys       0C5A0A00  world-map place names and info (0x38E)
"""
import argparse
import hashlib
import json
from pathlib import Path

from prepare_message_sweep import group_valid
from prepare_outskirt import member

PACKS = {
    "name_entry.c3m": ("name_entry_menu.fsys", 0x02FD0A00),
    "pcbox.c3m": ("pcbox_menu.fsys", 0x02E90A00),
    "pda.c3m": ("pda_menu.fsys", 0x03750A00),
    "worldmap.c3m": ("world_map.fsys", 0x0C5A0A00),
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = {}
    for name, (archive, identity) in PACKS.items():
        data = member((args.disc / "files" / archive).read_bytes(), identity)
        if not group_valid(data):
            raise SystemExit("%s %08x is not a message group" % (archive, identity))
        (args.output / name).write_bytes(data)
        report[name] = {"archive": archive, "member": "%08x" % identity, "bytes": len(data),
                        "messages": int.from_bytes(data[4:6], "big"), "sha256": hashlib.sha256(data).hexdigest()}
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
