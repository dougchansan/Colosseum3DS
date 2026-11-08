#!/usr/bin/env python3
"""Prepare the source-timed Bite attack model/floor-color slice (private assets).

This is not a general WZX interpreter or complete Bite presentation. The original
sound node is retained as unsupported. Damage GPT1/camera/audio are separate gates.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from prepare_boot_media import verify_disc
from prepare_game_state import SOURCE_COMMIT, Dol
from prepare_battle_scene import admit, load_pkx
from prepare_model import check_output
from prepare_outskirt import member

ROOT = Path(__file__).resolve().parents[1]
WZX_SHA256 = "0cb9912dcbe7c4626d63789a34cfe1a36e869a0878454b8cde878434814216f7"
MEMBER = 0x034B2000
BYTES = 256


def read32(data, offset, signed=False):
    if offset < 0 or offset + 4 > len(data):
        raise ValueError("Truncated WZX field")
    return struct.unpack_from(">i" if signed else ">I", data, offset)[0]


def entry(data, offset):
    if offset < 0 or offset + 112 > len(data) or read32(data, offset + 104) != 2:
        raise ValueError("Unsupported/truncated WZX entry version")
    words = [read32(data, offset + i * 4, signed=True) for i in range(28)]
    key, kind, attach, position, link, source, target, timing = words[:8]
    if key <= 0 or kind not in (1, 2, 4, 5) or not (0 <= source < 16 and 0 <= target < 16 and 0 <= timing < 16):
        raise ValueError("Unsupported WZX entry kind, key, or timing index")
    return dict(offset=offset, key=key, kind=kind, attachment=attach,
                position_type=position, link=link, source=source, target=target,
                timing_index=timing, timing=words[8:24], flags=words[24],
                part=words[25], state=words[27])


def link_times(entries, owner):
    """wazaSequenceEntryLink's original fallback/relative-time arithmetic."""
    if len(owner) > 16 or not owner or len(entries) > 16:
        raise ValueError("WZX link table bound")
    seen = {}
    for node in entries:
        if node['key'] in seen:
            raise ValueError("Duplicate WZX entry key")
        linked = seen.get(node['link'])
        index = node['target']
        values = linked['timing'] if linked else owner
        if not 0 <= index < len(values) or not 0 <= node['source'] < len(node['timing']):
            raise ValueError("WZX timing reference outside source table")
        start = (linked['start'] if linked else 0) + values[index] - node['timing'][node['source']]
        if abs(start) > 3600:
            raise ValueError("WZX timing exceeds bounded sequence")
        node['start'] = start
        seen[node['key']] = node
    shift = -min(0, min(node['start'] for node in entries))
    for node in entries:
        node['start'] += shift
    return shift


def parse(data, owner_times):
    """Decode just the reviewed four-entry attack; fail on unsupported changes."""
    if len(data) > 1024*1024 or len(data) < 160 or read32(data, 104) != 2:
        raise ValueError("Invalid Bite WZX root")
    header = [read32(data, 112+i*4) for i in range(6)]
    if header != [2, 5, 0x00201048, 2, 5, 0]:
        raise ValueError("Bite root profile changed")
    floor = entry(data, 160)
    if floor['kind'] != 4 or floor['flags'] != 1 or floor['link'] or floor['source'] or floor['target']:
        raise ValueError("Unsupported floor-color entry")
    p = floor['offset'] + 112
    color_header = [read32(data, p+4*i) for i in range(7)]
    if color_header != [0, 60, 2, 1, 2, 3, 0]:
        raise ValueError("Unsupported floor-color payload")
    colors = [read32(data, p+28+i*4, signed=(i % 4 == 2)) for i in range(8)]
    if colors[2:] != [60, 1, colors[1], colors[1], -1, 1]:
        raise ValueError("Unsupported floor-color segment closure")
    control = entry(data, p+60)
    c = control['offset']+112
    delay = read32(data, c+4)
    if control['kind'] != 1 or [read32(data,c), read32(data,c+8)] != [0,3] or not 1 <= delay <= 60:
        raise ValueError("Unsupported stop-control payload")
    model = entry(data, c+12)
    m = model['offset']+112
    params = [read32(data,m+i*4) for i in range(8)]
    if model['kind'] != 2 or params[:7] != [0,0,0,0,0,0,1] or \
       (model['attachment'],model['position_type'],model['flags'],model['part']) != (1,1,2,0):
        raise ValueError("Unsupported model/one-shot attachment profile")
    start = (m+0x43) & ~31
    length = params[7]
    if not 32 <= length <= 1024*1024 or start+length > len(data):
        raise ValueError("Truncated model DAT")
    sound = entry(data,start+((length+31)&~31))
    s = sound['offset']+112
    if sound['kind'] != 5 or sound['link'] != model['key'] or \
       [read32(data,s+i*4) for i in range(3)] != [0x85,3,0] or s+12 != len(data):
        raise ValueError("Unsupported/truncated sound or trailing entries")
    entries = [floor, control, model, sound]
    if link_times(entries, owner_times) != 0:
        raise ValueError("Bite requires a qualified zero-shift owner timeline")
    if sound['start'] != model['start'] or not floor['start'] <= model['start'] < control['start']:
        raise ValueError("Bite timing dependencies changed")
    return dict(entries=entries, floor_duration=colors[2], floor_from=colors[0],
                floor_to=colors[1], control_delay=delay, dat_offset=start,
                dat_bytes=length, sound_id=0x85, flags=header[2]), data[start:start+length]


def encode(report, anchor, owner_clip, model_frames, model_bytes):
    nodes = report['entries']
    if len(anchor) != 3 or not all(math.isfinite(x) and abs(x)<1000 for x in anchor):
        raise ValueError("Invalid source attachment position")
    out = bytearray(BYTES)
    struct.pack_into('<4s15I', out, 0, b'C3WX', 1, BYTES, 44, 197,
        owner_clip, 0, 60, nodes[0]['start'], report['floor_duration'],
        nodes[2]['start'], nodes[1]['start'], report['control_delay'],
        model_frames, report['floor_from'], report['floor_to'])
    struct.pack_into('<5f5I', out, 64, .5, .5, *anchor, 59, 1,
                     report['sound_id'], nodes[3]['start'], report['flags'])
    out[104:124] = bytes.fromhex(SOURCE_COMMIT)
    out[124:156] = bytes.fromhex(WZX_SHA256)
    out[156:176] = hashlib.sha1(model_bytes).digest()
    out[188:208] = hashlib.sha1(out[:188]).digest()
    return bytes(out)


def prepare(disc, parser, output):
    import prepare_field_geometry as geometry
    import prepare_title as title
    disc = verify_disc(disc)
    output = check_output(output)
    parser = title.verify_parser(parser)
    manifest = json.loads((ROOT/'config/wzx-bite-source-files.json').read_text())
    if manifest.get('commit') != SOURCE_COMMIT:
        raise ValueError('WZX source manifest pin differs from the admitted source')
    for name,digest in manifest['files'].items():
        path = ROOT/'vendor/colosseum'/name
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:
            raise ValueError('WZX source drift: '+name)
    geometry.import_parser(parser)
    from importer.phases.route.route import route_sections
    from importer.phases.parse.parse import parse_sections
    from importer.phases.describe.describe import describe_scene
    archive_path = disc/'files/wzx_kamituku_attack.fsys'
    if archive_path.is_symlink() or archive_path.stat().st_size > 1024*1024:
        raise ValueError('Unsafe WZX source path')
    archive = archive_path.read_bytes()
    data = member(archive,MEMBER)
    if hashlib.sha256(data).hexdigest()!=WZX_SHA256:
        raise ValueError('Bite member differs from admitted original')
    _dat, owner_raw, owner_scene, header = load_pkx(admit(disc/'files/pkx_blacky.fsys'),'pkx_blacky.fsys')
    owner = geometry.Model(owner_scene.models[0],owner_raw.models[0],allow_image_swaps=True)
    mapping = __import__('prepare_battle_scene').clip_mapping(header,owner.ends)[2]
    owner_times = [round(x*60) for x in mapping['timing_seconds']]
    if mapping['clips'] != [5] or owner_times != [72,100,160,0] or mapping['body_map_bones'][1] != 59:
        raise ValueError('Original Umbreon motion/attachment mapping changed')
    report,dat = parse(data,owner_times)
    dol=Dol((disc/'sys/main.dol').read_bytes())
    for address in (0x8047E348,0x8047E3C8):
        if struct.unpack('>f',dol.read(address,4))[0] != .5:
            raise ValueError('Source motion rate changed')
    opts=dict(filepath='bite-effect.dat',game='COLO_XD',max_frame=1000000000,
              section_names=[],ik_hack=False)
    sections=parse_sections(dat,route_sections(dat),opts)
    if len(sections)!=1 or sections[0].section_name!='scene_data':
        raise ValueError('Unexpected Bite DAT root')
    raw=sections[0].root_node
    scene=describe_scene(sections,opts)
    if len(scene.models)!=1 or len(scene.cameras)!=1 or len(scene.lights)!=2 or scene.fogs:
        raise ValueError('Unexpected Bite embedded scene')
    model=geometry.Model(scene.models[0],raw.models[0],constant_material_colors=True)
    if model.ends != [34.0] or len(model.materials)!=1:
        raise ValueError('Unexpected Bite clip/material schedule')
    meshes=[geometry.build_mesh(model,i) for i in range(len(model.meshes))]
    if any(mesh['batch'][3] & (title.LIT | title.SPECULAR) for mesh in meshes):
        raise ValueError('Bite effect requires unsupported runtime lighting')
    # GSmodelGetPart uses source JObj preorder; body-map entry 1 is part59.
    world,_=owner.world(5,report['entries'][2]['start']*.5)
    anchor=list(map(float,world[59,:3,3]))
    frames=geometry.sample_sequence(model.ends[0])
    # Retain embedded light data for C3T; all effect materials were checked
    # unlit, so the model-only WZX load does not use these authoring lights.
    camera=(0.,2.,10.,0.,0.,0.,0.,45.,.1,1000.,4/3,0.)
    output.mkdir(parents=True,exist_ok=False)
    try:
        packs=geometry.prepare_pack_set(meshes,[(0,frames,False)],120,
            [camera]*len(frames),geometry.fixed_lights(raw,dat),hashlib.sha256(archive).hexdigest(),output,
            'bite_attack',expected_count=1,format_version=3)
        model_bytes=(output/'bite_attack.c3t').read_bytes()
        descriptor=encode(report,anchor,5,34,model_bytes)
        (output/'bite.c3w').write_bytes(descriptor)
        report.update(source_commit=SOURCE_COMMIT,source_hashes=manifest['files'],
            archive_sha256=hashlib.sha256(archive).hexdigest(),member_sha256=WZX_SHA256,
            model_dat_sha256=hashlib.sha256(dat).hexdigest(),owner_timing_ticks=owner_times,
            source_part=59,anchor=anchor,owner_clip=5,owner_rate=.5,model_rate=.5,
            packs=packs,descriptor_sha256=hashlib.sha256(descriptor).hexdigest(),
            model_sha1=hashlib.sha1(model_bytes).hexdigest(),
            scope='Bite attack model/floor-color only; sound85 retained but not played; damage GPT1/cameras/audio unsupported')
        (output/'bite.json').write_text(json.dumps(report,indent=2)+'\n')
    except Exception:
        # Preserve partial output for diagnosis; never overwrite a previous run.
        raise
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disc',type=Path,required=True)
    p.add_argument('--parser',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    print(json.dumps(prepare(a.disc,a.parser,a.output),indent=2))

if __name__=='__main__': main()
