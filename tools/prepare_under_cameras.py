#!/usr/bin/env python3
"""Prepare Under event cameras from admitted disc archives into private storage."""
import argparse
import hashlib
import json
from pathlib import Path

import prepare_title as title
from audit_under_story import WALKABLE
from floor_profiles import FLOORS
from prepare_area import sampled_camera
from prepare_field_geometry import import_parser, load_scene
from prepare_story import admit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--survey', type=Path, help='Legacy survey argument; pinned profiles are used')
    parser.add_argument('--files', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--parser', type=Path, default=title.ROOT/'vendor/hsd-importer')
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to((title.ROOT/'private').resolve()):
        parser.error('Output must be inside this worktree private/ directory')
    import_parser(title.verify_parser(args.parser))
    output.mkdir(parents=True, exist_ok=True)
    reports = []
    for row in FLOORS.values():
        if row['floor'] not in WALKABLE or not row.get('event_cameras'):
            continue
        archive = admit(args.files/row['archive'], {row['archive']: (row['bytes'], row['sha256'])})
        for index, resource in enumerate(row['event_cameras']):
            _, _, scene = load_scene(archive, resource)
            if len(scene.cameras) != 1:
                raise ValueError(f'Expected one camera in {resource:08x}')
            data = sampled_camera(scene.cameras[0], row['sha256'], resource)
            name = row['prefix'] + '_camera' + (str(index) if index else '') + '.c3c'
            (output/name).write_bytes(data)
            reports.append({'file': name, 'floor': row['floor'], 'resource': f'{resource:08x}',
                            'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
    (output/'under-cameras.json').write_text(json.dumps(reports, indent=2)+'\n', newline='\n')
    print(json.dumps(reports, indent=2))


if __name__ == '__main__':
    main()
