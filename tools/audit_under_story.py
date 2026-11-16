#!/usr/bin/env python3
"""Inventory Under source/boot dependencies without exporting original bytes.

This is an all-function static inventory, not a story route or rendering proof.
Output must stay in the ignored private/ directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from floor_profiles import FLOORS, auto_profiles
from prepare_outskirt import member
from prepare_story import admit, be16, inspect_bank
from prepare_story_index import bank_index, reject_overlap

ROOT = Path(__file__).resolve().parents[1]
WALKABLE = (*range(0x2f, 0x38), *range(0x39, 0x3f), 0x79, 0x7a, 0x7d, 0x7f)
SPECIAL = (0x3f, 0x40, 0x8a, 0x8b, 0xd3, 0xd4, 0xd5)
FLOORS_UNDER = tuple(sorted((*WALKABLE, *SPECIAL)))


def digest(data):
    return hashlib.sha256(data).hexdigest()


def pack_entries(data, floor):
    """Validate C3S1 directory extents before inspecting its original banks."""
    if len(data) < 64 or len(data) > 1048576:
        raise ValueError("Invalid C3S1 extent")
    magic, version, size, actual_floor, nb, ng, scripts, messages = struct.unpack_from('<4s7I', data)
    if (magic != b'C3S1' or version != 1 or size != len(data) or actual_floor != floor
            or not 1 <= nb <= 8 or ng > 16 or scripts != 64 or messages != 64 + 16*nb):
        raise ValueError("Invalid C3S1 header")
    end = 64 + 16*(nb+ng)
    if end > len(data):
        raise ValueError("Truncated C3S1 directory")
    result, spans = [], [(0, end)]
    for i in range(nb+ng):
        identity, offset, length, reserved = struct.unpack_from('<4I', data, 64+16*i)
        if reserved or length < 2 or offset < end or offset+length > len(data):
            raise ValueError("Invalid C3S1 entry")
        payload = data[offset:offset+length]
        if be16(payload, 0) != identity:
            raise ValueError("C3S1 entry identity mismatch")
        spans.append((offset, offset+length))
        result.append(("script" if i < nb else "messages", payload))
    reject_overlap(spans, "C3S1 entries")
    return result


def message_inventory(data):
    """Report control IDs/locations; never retain dialogue strings."""
    if len(data) < 16:
        raise ValueError("Truncated message group")
    count, controls, issues, stopped = be16(data, 4), {}, [], []
    supported_controls = source_support()[1]
    if count > 8192 or 16+8*count > len(data):
        raise ValueError("Invalid message group directory")
    entries = [struct.unpack_from('>II', data, 16+i*8) for i in range(count)]
    if any(key > 0xfffff or off < 16+8*count or off+2 > len(data) for key, off in entries):
        raise ValueError("Invalid message entry")
    if any(a[0] >= b[0] for a, b in zip(entries, entries[1:])):
        raise ValueError("Unsorted message IDs")
    offsets = sorted({off for _, off in entries} | {len(data)})
    for key, start in entries:
        end = next(off for off in offsets if off > start)
        at, terminated, unsupported = start, False, False
        while at+2 <= end:
            glyph = be16(data, at)
            at += 2
            if not glyph:
                terminated = True
                break
            if glyph == 0xffff:
                if at >= end:
                    break
                control = data[at]
                at += 1
                controls.setdefault(f'{control:02x}', []).append(f'{(be16(data, 0)<<20)|key:08x}')
                if control not in supported_controls:
                    # Unknown controls can carry payload bytes. Do not turn
                    # those bytes into invented later controls or dialogue.
                    stopped.append(f'{key:05x}: {control:02x}')
                    unsupported = True
                    break
                # src/story.c's control_kind/parameters table. Parameters
                # are byte payloads, never UTF-16 glyphs or later controls.
                lengths = {7: 1, 8: 4, 9: 1, 0x38: 1, 0x53: 1,
                           0x52: 1, 0x54: 1, 0x5b: 1, 0x5c: 1}
                at += lengths.get(control, 0)
                if at > end:
                    break
        if not terminated and not unsupported:
            issues.append(f'{key:05x}: unterminated/truncated')
    return {"id": f'{be16(data, 0):04x}', "bytes": len(data), "sha256": digest(data),
            "messages": count, "controls": controls, "structural_issues": issues,
            "scan_stopped_at_unsupported_control": stopped}


def source_support():
    """Source observations are deliberately weaker than an execution claim."""
    source = (ROOT/'src/field_story.c').read_text()
    native_body = source.split('static int run_native(', 1)[1].split('default:return fail(error,"Unported native', 1)[0]
    natives = set(int(n, 16) for n in re.findall(r'case 0x([0-9a-f]+):', native_body))
    header = (ROOT/'include/colosseum3ds/story.h').read_text()
    bound = re.search(r'#define C3_STORY_CONTROLS (0x[0-9A-Fa-f]+)u', header)
    if not bound:
        raise ValueError('Cannot identify the decoder control range')
    # All IDs below this bound have an original table entry. This is only
    # decoder-source coverage: a value still needs a live runtime binding.
    controls = set(range(int(bound.group(1), 16)))
    return natives, controls


def describe_bank(data, supported):
    result = bank_index(data)
    try:
        inspect_bank(data, allow_null_entries=True)
        result['runtime_frame_inspector'] = 'passed'
    except ValueError as exc:
        result['runtime_frame_inspector'] = str(exc)
    result['native_ids_without_dispatch_case'] = [f'{n:02x}' for n in result['required_natives'] if n not in supported]
    result['source_guard_observations'] = {
        'globals': result['globals'], 'runtime_max_globals': 1024,
        'global_extent_admitted': result['globals'] <= 1024,
        'native_calls_exceeding_8_arguments': [s['offset'] for s in result['native_calls'] if len(s['arguments']) > 8]}
    return result


def audit(survey_path, boot, files=None):
    survey = json.loads(Path(survey_path).read_text())
    rows = {r['floor']: r for r in survey['floors']}
    profiles = {r['floor']: (name, r) for name, r in {**auto_profiles(survey_path), **FLOORS}.items()}
    natives, controls = source_support()
    result = []
    for floor in FLOORS_UNDER:
        row = rows.get(floor, {})
        name, profile = profiles.get(floor, (None, {}))
        prefix = profile.get('prefix', f'f{floor:03x}')
        item = {'floor': f'{floor:03x}', 'classification': 'walkable' if floor in WALKABLE else 'special',
                'archive': row.get('archive'), 'survey_status': row.get('status'),
                'profile': name, 'profile_kind': 'survey' if profile.get('auto') else 'reviewed' if profile else 'absent',
                'banks': [], 'message_groups': [], 'errors': []}
        item['boot_files'] = {suffix: (Path(boot)/(prefix+suffix)).is_file()
                              for suffix in ('.c3s', '.c3f', '.c3a', '_map0.c3t', '_camera.c3c', '.c3d')}
        entries, local_payloads = [], set()
        item['source_event_camera_count'] = len(row.get('event_cameras', []))
        pack = Path(boot)/profile.get('story_pack', prefix+'.c3s')
        if pack.is_file():
            raw = pack.read_bytes()
            item['boot_story_sha256'] = digest(raw)
            try:
                entries = pack_entries(raw, floor)
            except ValueError as exc:
                item['errors'].append(str(exc))
        if files and row.get('archive'):
            try:
                archive = admit(Path(files)/row['archive'], {row['archive']: (row['bytes'], row['sha256'])})
                original = []
                for kind, key in (('script', '0e'), ('messages', '0a')):
                    for identity in row.get('members', {}).get(key, []):
                        payload = member(archive, int(identity, 16))
                        original.append((kind, payload))
                item['original_archive_admitted'] = True
                local_payloads = {digest(o) for _, o in original}
                item['boot_payloads_in_original_archive'] = [digest(b) for _, b in entries
                    if digest(b) in {digest(o) for _, o in original}]
                # Inspect original local banks even when the boot pack is absent.
                seen = {digest(b) for _, b in entries}
                entries += [(kind, b) for kind, b in original if digest(b) not in seen]
            except (ValueError, OSError) as exc:
                item['errors'].append(f'original: {exc}')
        for kind, data in entries:
            try:
                if kind == 'script':
                    bank = describe_bank(data, natives)
                    bank['origin'] = 'admitted_floor_archive' if digest(data) in local_payloads else 'boot_pack_dependency'
                    item['banks'].append(bank)
                else:
                    group = message_inventory(data)
                    group['origin'] = 'admitted_floor_archive' if digest(data) in local_payloads else 'boot_pack_dependency'
                    group['controls_without_decoder_source'] = sorted(c for c in group['controls'] if int(c, 16) not in controls)
                    item['message_groups'].append(group)
            except ValueError as exc:
                item['errors'].append(f'{kind} {be16(data, 0):04x}: {exc}')
        result.append(item)
    scene_source = (ROOT/'src/field_story.c').read_text()
    return {'scope': 'Under original all-function static dependencies; no execution/rendering claim',
            'floor_count': len(result), 'walkable_count': len(WALKABLE), 'special_count': len(SPECIAL),
            'source_dispatch_note': 'A case only establishes static dispatch presence; argument modes and presentation can remain unported.',
            'shared_presentation_limitations': sorted(set(re.findall(r'note_presentation\([^;]*?"([^"]+)"', scene_source))),
            'runtime_source_sha256': {name: digest((ROOT/name).read_bytes())
                                      for name in ('src/story.c', 'src/field_story.c')},
            'boot': str(Path(boot).resolve()), 'survey_sha256': digest(Path(survey_path).read_bytes()),
            'floors': result}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--survey', required=True, type=Path)
    parser.add_argument('--boot', required=True, type=Path)
    parser.add_argument('--files', type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to((ROOT/'private').resolve()):
        parser.error('Output must be within this worktree private/ directory')
    report = audit(args.survey, args.boot, args.files)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2)+'\n')
    print(f"Inventoried {report['floor_count']} floors ({report['walkable_count']} walkable, {report['special_count']} special): {output}")


if __name__ == '__main__':
    main()
