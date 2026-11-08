#!/usr/bin/env python3
"""Prepare private, pinned tables for the explicitly nonpersistent battle preview."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from prepare_battle import make_pack as encounter_pack
from prepare_game_state import Dol, COMMON_SHA1, SOURCE_COMMIT, u16
from prepare_boot_media import verify_disc, MAIN_DOL_SHA1
from prepare_model import check_output

MOVE_IDS = (33, 39, 44, 45, 93, 115, 165, 216, 269, 270, 289, 290)
SPECIES = (197, 196, 288)
SIZE = 1024


def make_pack(disc):
    disc = verify_disc(disc)
    encounter, _ = encounter_pack(disc)
    rel = (disc / 'files/common_rel.rel').read_bytes()
    dol = Dol((disc / 'sys/main.dol').read_bytes())
    b = bytearray(SIZE)
    struct.pack_into('>4sIII', b, 0, b'C3P1', 1, SIZE, 10)
    b[16:36] = bytes.fromhex(SOURCE_COMMIT)
    b[36:56] = bytes.fromhex(COMMON_SHA1)
    b[76:96] = hashlib.sha1(encounter[128:]).digest()
    b[96:116] = bytes.fromhex(MAIN_DOL_SHA1)
    for i, move in enumerate(MOVE_IDS):
        off = 128 + i * 60
        struct.pack_into('>H', b, off, move)
        source = 0x184c + 0x11c7c4 + move * 56
        b[off+4:off+60] = rel[source:source+56]
    for i, species in enumerate(SPECIES):
        off = 848 + i * 12
        row = 0x184c + 0x121a04 + species * 284
        struct.pack_into('>H', b, off, species)
        b[off+2:off+6] = rel[row+0x30:row+0x34]
    b[884:910] = dol.read(0x80375d10, 26)
    b[910:936] = dol.read(0x80375dd0, 26)
    b[936] = dol.read(0x80478d58, 1)[0]
    terrain = encounter[128+56]
    effect = u16(dol.read(0x80363c00 + terrain * 12, 12), 2)
    if effect != 27:
        raise ValueError('Unsupported first-arena Secret Power effect')
    b[937] = effect
    b[940:974] = dol.read(0x80279ed0, 34)
    b[56:76] = hashlib.sha1(b[128:]).digest()
    return bytes(b), {'format': 'C3P1', 'bytes': len(b),
        'source_commit': SOURCE_COMMIT, 'main_dol_sha1': MAIN_DOL_SHA1,
        'common_rel_sha1': COMMON_SHA1, 'moves': MOVE_IDS, 'species': SPECIES,
        'terrain_effect': effect, 'sha256': hashlib.sha256(b).hexdigest(),
        'scope': 'Nonpersistent combat preview tables; no retail AI, outcome or save authority'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = check_output(args.output)
    data, report = make_pack(args.disc)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open('xb') as f:
        f.write(data)
    output.with_suffix(output.suffix+'.json').write_text(json.dumps(report, indent=2)+'\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
