#!/usr/bin/env python3
"""Read-only verification of the recorded pre-Willie WES shop checkpoint.

Uses the independent original-slot decoder and source initializer from
verify_game_state_probe. No GCI or prepared pack is written. Checks every
party byte against the fresh initializer seed, not opponent/battle state.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import verify_game_state_probe as source


def be16(data, offset):
    return struct.unpack_from('>H', data, offset)[0]


def be32(data, offset):
    return struct.unpack_from('>I', data, offset)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gci', type=Path, required=True)
    parser.add_argument('--pack', type=Path, required=True)
    parser.add_argument('--seed', type=lambda x: int(x, 0), required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    source.require(0 <= args.seed <= 0xffffffff, 'Seed must fit uint32')
    for original in (args.gci, args.pack):
        source.require(args.out.resolve() != original.resolve(), 'Report must not replace an input')
        if args.out.exists() and original.exists():
            source.require(not args.out.samefile(original), 'Report must not replace an input alias')

    image = args.gci.read_bytes()
    generation, body = source.newest_body(image)
    pack = args.pack.read_bytes()
    expected = source.source_body(pack, 1, args.seed, 'WES', 0, True)
    name = body[0x70:0x70+22].decode('utf-16-be').split('\0')[0]
    # Flag definitions and packed-body bases match the admitted source layout.
    flag_base = 128 + 122832 + 0x6040 + 2*64 + 25*10
    flags = {}
    for flag in (0x0af8, 0x03da, 0x03db, 0x03dc, 0x03dd, 0x03de, 0x0a05, 0x0ace):
        definition = pack[flag_base+flag*8:flag_base+(flag+1)*8]
        kind, width, bit = definition[0] >> 6, definition[0] & 63, be16(definition, 4)
        source.require(kind and 1 <= width <= 32, 'Unsupported checkpoint flag definition')
        offset = (0, 0x1bdbc, 0x1bddc, 0x1be5c)[kind] + bit//32*4
        shift = bit % 32
        bits = be32(body, offset)
        if width+shift > 32:
            bits |= be32(body, offset+4) << 32
        flags[f'{flag:04x}'] = (bits >> shift) & ((1 << width)-1)
    party_bytes = body[0xa0:0xa0+6*0x138]
    party = []
    for index in range(6):
        pokemon = party_bytes[index*0x138:(index+1)*0x138]
        if be16(pokemon, 0):
            party.append({'species': be16(pokemon, 0), 'level': pokemon[0x60],
                          'hp': be16(pokemon, 0x8a), 'max_hp': be16(pokemon, 0x8c),
                          'personality': f'{be32(pokemon, 4):08x}'})
    source.require(name == 'WES' and be32(body, 0x10) == 1 and generation == 1,
                   'Expected fresh WES save count and generation one')
    source.require(be32(body, 0x1c) == 2 and be32(body, 0x24) == 1 and body[0x28] == 0,
                   'Expected shop checkpoint floor2/previous1/spawn0')
    source.require(party_bytes == expected[0xa0:0xa0+6*0x138],
                   'Party differs from independent fresh-seed calculation')
    source.require(be32(body, 0x9c) == be32(expected, 0x9c), 'Trainer ID differs from fresh seed')
    source.require(be32(body, 0xaf4) == be32(expected, 0xaf4), 'Money differs from fresh state')
    source.require(flags['0af8'] == flags['03da'] == flags['03db'] == flags['0a05'] == flags['0ace'] == 1,
                   'Arrival, departure or TV-completion flag missing')
    source.require(flags['03dc'] == flags['03dd'] == flags['03de'] == 0,
                   'Willie/encounter/victory checkpoint advanced')
    report = {'gci_sha256': hashlib.sha256(image).hexdigest(), 'bytes': len(image),
              'outer_generation': generation, 'save_count': be32(body, 0x10), 'name': name,
              'trainer_id': f'{be32(body, 0x9c):08x}', 'money': be32(body, 0xaf4),
              'floor': be32(body, 0x1c),
              'previous_floor': be32(body, 0x24), 'spawn': body[0x28], 'party': party,
              'party_sha256': hashlib.sha256(party_bytes).hexdigest(),
              'party_matches_independent_fresh_seed': True, 'new_game_seed': f'{args.seed:08x}',
              'flags': flags, 'body_sha256': hashlib.sha256(body).hexdigest(),
              'slot_payload_checksums_verified': True}
    args.out.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
