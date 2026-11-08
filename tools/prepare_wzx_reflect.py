#!/usr/bin/env python3
"""Qualify original Reflect sequence scheduling and extract private dependencies.

This is not a native Reflect player: the embedded model's texture animation,
owner attachments and overlapping all-ally instances still need integration.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from prepare_boot_media import verify_disc
from prepare_game_state import Dol
from prepare_model import check_output
from prepare_outskirt import member
from prepare_battle_scene import admit,load_pkx,clip_mapping
from prepare_wzx_damage import BANK_PROFILES, node
from prepare_wzx_bite import link_times
import prepare_particle as particle
try:
    import prepare_field_geometry as geometry
except ModuleNotFoundError as exc:
    if exc.name != 'numpy':
        raise
    geometry = None
import prepare_title as title

PROFILE = BANK_PROFILES['reflect']
NODES = (0xA0, 0x16FE0, 0x1705C, 0x17118)
MODEL_OFFSET = 0x171C0
MODEL_BYTES = 0x6817


def words(data, offset, count):
    return list(struct.unpack('>%dI' % count, particle.extent(data, offset, count * 4)))


def parse(data, owner_timing):
    if len(data) != PROFILE['member_bytes'] or words(data, 0x70, 6) != [0,5,0xA4A,2,5,0]:
        raise ValueError('Reflect WZX header/profile changed')
    if owner_timing != [0,180]:
        raise ValueError('Espeon Reflect idle timeline must include implicit point0')
    entries = [node(data, offset) for offset in NODES]
    signatures = [(n['key'],n['kind'],n['attachment'],n['position_type'],n['link'],n['source'],n['target'],n['timing_index'],n['flags'],n['part']) for n in entries]
    if signatures != [(1,3,2,1,0,0,0,2,0,0),(2,5,0,0,1,0,0,2,1,0),
                      (3,4,0,0,0,0,0,2,1,0),(4,2,2,1,0,0,0,2,0,0)]:
        raise ValueError('Reflect entry/attachment profile changed')
    if any(any(n['timing']) for n in entries):
        raise ValueError('Unexpected Reflect entry time links')
    if words(data,0x110,4) != [0,0x18A9,93864,4]:
        raise ValueError('Reflect GPT1 generator/flags changed')
    if words(data,0x17050,3) != [0x220,3,0]:
        raise ValueError('Reflect game sound ID is not hexadecimal0x220')
    floor = words(data,0x170CC,19)
    if floor != [0,190,2,1,3,3,0,0x80808080,0x802D2D2D,20,1,
                 0x802D2D2D,0x802D2D2D,110,1,0x802D2D2D,0x80808080,60,1]:
        raise ValueError('Reflect floor-color program changed')
    if words(data,0x17188,8) != [0xFFFFFFFF,0,0,0,0,0,1,MODEL_BYTES]:
        raise ValueError('Reflect embedded model/texture animation profile changed')
    shift=link_times(entries,owner_timing)
    if shift or any(n['start'] for n in entries):
        raise ValueError('Reflect source links do not all begin at local0')
    model=particle.extent(data,MODEL_OFFSET,MODEL_BYTES)
    bank=particle.extent(data,PROFILE['offset'],PROFILE['bytes'])
    return {'move':115,'resource_kind':3,'wzx_entry':0,'owner_internal_species':196,
        'owner_raw_timing':[0,180],'normalization_shift':0,'owner_start':0,
        'owner_minimum_end':180,'entries':entries,'sound_id':0x220,
        'floor_duration':190,'floor_segments':[
            {'ticks':20,'from':0x80808080,'to':0x802D2D2D},
            {'ticks':110,'from':0x802D2D2D,'to':0x802D2D2D},
            {'ticks':60,'from':0x802D2D2D,'to':0x80808080}],
        'particle':{'root':0,'flags':0x18A9,'animation_mode':4,'attachment':2,
                    'position_type':1,'part':0,'offset':PROFILE['offset'],'bytes':len(bank)},
        'model':{'offset':MODEL_OFFSET,'bytes':len(model),'joint_animation_index':-1,
                 'texture_animation_index':0,'joint_loop':False,'texture_loop':False,
                 'flags':1,'attachment':2,'position_type':1,'part':0,
                 'sha256':hashlib.sha256(model).hexdigest()},
        'completion':'Owner idle minimum180 is insufficient: floor190, surviving particles, sound status2/3, and texture-animation/model completion all participate.',
        'dispatch':'fightWazaWzxTypeFuncAllAlly loads kind3 for caster and valid ally; if ally exists, caster starts/waits then ally starts without wait. Preserve source ordering rather than duplicating a simultaneous single-target effect.',
        'scope':'Source-qualified dependencies/schedule only; not native model/texture/attachment/camera execution.'},model,bank


def encode(report, model, particles, parts):
    data=bytearray(288)
    fields=[2,288,115,196,0,1,0,3,180,190,95]
    struct.pack_into('<4s11I',data,0,b'C3WR',*fields)
    struct.pack_into('<2f18I',data,48,.5,.5,20,110,60,0x80808080,0x802D2D2D,
        0x220,0,0x18A9,2,1,2,62,42,0xFFFFFFFF,0,1,0,118)
    data[128:148]=bytes.fromhex(particle.SOURCE_COMMIT)
    data[148:180]=bytes.fromhex(PROFILE['member_sha256'])
    data[180:200]=hashlib.sha1(model).digest()
    data[200:220]=hashlib.sha1(particles).digest()
    data[220:240]=hashlib.sha1(parts).digest()
    data[268:288]=hashlib.sha1(data[:268]).digest()
    return bytes(data)


def texture_only_end(model,raw):
    if model.tracks or model.ends or len(model.materials)!=1 or len(model.joints)!=3 or len(model.meshes)!=1:
        raise ValueError('Reflect must retain static joints and one material timeline')
    ends=[]
    for root in raw.animated_material_joints or []:
        for node in title.tree_nodes(root):
            material=node.animation
            while material:
                if material.animation:
                    ends.append(material.animation.end_frame)
                texture=material.texture_animation
                while texture:
                    if texture.animation:ends.append(texture.animation.end_frame)
                    texture=texture.next
                material=material.next
    if ends!=[95.]:raise ValueError('Reflect texture animation0 duration changed')
    return ends[0]


def convert_model(data, parser, output):
    if geometry is None:
        raise ValueError('Reflect model preparation requires the existing NumPy geometry dependency')
    geometry.import_parser(parser)
    from importer.phases.route.route import route_sections
    from importer.phases.parse.parse import parse_sections
    from importer.phases.describe.describe import describe_scene
    options=dict(filepath='reflect_model.dat',game='COLO_XD',max_frame=1000000000,
                 section_names=[],ik_hack=False)
    sections=parse_sections(data,route_sections(data),options)
    if len(sections)!=1 or sections[0].section_name!='scene_data':
        raise ValueError('Unexpected Reflect embedded model root')
    raw=sections[0].root_node;scene=describe_scene(sections,options)
    if len(scene.models)!=1 or len(scene.cameras)!=1 or len(scene.lights)!=2 or scene.fogs:
        raise ValueError('Unexpected Reflect embedded HSD topology')
    # The source explicitly selects joint animation-1 and texture animation0.
    # There are no joint animation trees: geometry must stay at the rest pose.
    model=geometry.Model(scene.models[0],raw.models[0],constant_material_colors=True)
    texture_end=texture_only_end(model,raw.models[0])
    meshes=[geometry.build_mesh(model,i) for i in range(len(model.meshes))]
    if any(mesh['batch'][3]&(title.LIT|title.SPECULAR) for mesh in meshes):
        raise ValueError('Reflect effect requires unqualified runtime lighting')
    frames=geometry.sample_sequence(texture_end)
    camera=(0.,2.,10.,0.,0.,0.,0.,45.,.1,1000.,4/3,0.)
    packs=geometry.prepare_pack_set(meshes,[(0,frames,False)],120,[camera]*len(frames),
        geometry.fixed_lights(raw,data),PROFILE['archive_sha256'],output,'reflect',
        expected_count=1,format_version=3)
    packs[0]['timeline_semantics']='C3T clip0 samples material/texture animation0 only; original joint index-1, static rest matrices.'
    return packs


def prepare_parts(disc,parser):
    if geometry is None:
        raise ValueError('Reflect joint-track preparation requires the existing NumPy geometry dependency')
    """Only the required original idle source-part positions; no actor pack."""
    geometry.import_parser(parser)
    points=[];reports=[]
    for species,name,identity,part,expected_clip,end in [
        (196,'pkx_eifie.fsys',0x03B71E00,62,1,90.),
        (197,'pkx_blacky.fsys',0x03381E00,42,0,59.)]:
        archive=admit(disc/'files'/name)
        data,raw,scene,header=load_pkx(archive,name)
        model=geometry.Model(scene.models[0],raw.models[0],allow_image_swaps=True)
        entry=clip_mapping(header,model.ends)[0]
        if entry['clips']!=[expected_clip] or entry['body_map_bones'][2]!=part or model.ends[expected_clip]!=end:
            raise ValueError('Reflect idle part/clip qualification changed')
        frames=geometry.sample_sequence(end)
        positions=[tuple(map(float,model.world(expected_clip,frame)[0][part,:3,3])) for frame in frames]
        if any(not all(math.isfinite(v) and abs(v)<1000 for v in pos) for pos in positions):
            raise ValueError('Reflect source-part position bound')
        points.append(b''.join(struct.pack('<3f',*pos) for pos in positions))
        reports.append({'species':species,'source_part':part,'source_joint':part,
            'source_idle_joint_clip':expected_clip,'source_end_frame':end,
            'frames':len(frames),'sample_fps':120,'units':'native0.10 coordinate scale',
            'source_pkx_member_sha1':hashlib.sha1(member(archive,identity)).hexdigest(),
            'frame0':positions[0],'frame_mid':positions[len(positions)//2]})
    if [len(v) for v in points]!=[181*12,119*12]:raise ValueError('Reflect part-track counts changed')
    data=bytearray(128)
    struct.pack_into('<4s15I',data,0,b'C3RP',1,128+sum(map(len,points)),2,120,181,119,12,
                     62,42,128,128+len(points[0]),196,197,1,0)
    data[64:84]=bytes.fromhex(particle.SOURCE_COMMIT)
    data[84:104]=bytes.fromhex(reports[0]['source_pkx_member_sha1'])
    data[104:124]=bytes.fromhex(reports[1]['source_pkx_member_sha1'])
    return bytes(data)+b''.join(points),reports


def prepare(disc, output, parser=None):
    disc=verify_disc(disc);output=check_output(output)
    particle.source_manifest()
    path=disc/'files'/PROFILE['archive']
    if path.is_symlink() or path.stat().st_size!=PROFILE['archive_bytes']:
        raise ValueError('Reflect archive path/size changed')
    archive=path.read_bytes()
    if hashlib.sha256(archive).hexdigest()!=PROFILE['archive_sha256']:
        raise ValueError('Reflect archive source hash changed')
    data=member(archive,PROFILE['member'])
    if hashlib.sha256(data).hexdigest()!=PROFILE['member_sha256']:
        raise ValueError('Reflect member source hash changed')
    owner=member(admit(disc/'files/pkx_eifie.fsys'),0x03B71E00)
    main,aux=words(owner,0,2)
    offset=64+((main+31)&~31)+((aux+31)&~31)
    if words(owner,offset,5)!=[2,1,0,0,180]:
        raise ValueError('Espeon raw idle row no longer has source values[0,180]')
    dol=Dol((disc/'sys/main.dol').read_bytes())
    # Resource kind3 mode, PKX pointName0, source selectors +0C and +10.
    instructions={0x801DB718:0x801E000C,0x801DB720:0x801E0010,
        0x801D9D34:0xC022ACA8,0x801D9D3C:0x4BF12CA1,0x8047E348:0x3F000000,
        0x801DF124:0xC022AD28,0x801DF12C:0x4BF0D8B1,0x8047E3C8:0x3F000000}
    for address,instruction in instructions.items():
        if struct.unpack('>I',dol.read(address,4))[0]!=instruction:
            raise ValueError('Original Reflect owner time-query instruction changed')
    report,model,bank=parse(data,[0,180])
    if hashlib.sha256(bank).hexdigest()!=PROFILE['bank_sha256']:
        raise ValueError('Reflect particle bank differs from admitted original')
    closure=particle.bank(bank,0)
    if closure['closure']!=list(range(10)):
        raise ValueError('Reflect transitive generator closure changed')
    owner_rows={}
    for species,name,identity,metadata in [(196,'pkx_eifie.fsys',0x03B71E00,0x380E0),(197,'pkx_blacky.fsys',0x03381E00,0x3A5C0)]:
        owner_bytes=member(admit(disc/'files'/name),identity)
        point=words(owner_bytes,metadata+0x4C+2*4,1)[0]
        expected=62 if species==196 else 42
        if point!=expected:raise ValueError('Reflect source owner body-map selector2 changed')
        owner_rows[str(species)]={'entry':0,'attachment_selector':2,'body_map_index':2,'source_part':point,
            'follow':'Continuous position only via fn80118FB0 state1; flags bit1=0 updates field_06 every step; generator JObj linking bit1=0.',
            'runtime_requirement':'Caller must supply live source-part transform. Source-part ID is not the compact native bone index.'}
    report.update(source_commit=particle.SOURCE_COMMIT,source_profile=PROFILE,
        owner_metadata_offset=offset,source_instructions={hex(k):hex(v) for k,v in instructions.items()},
        particle_closure=closure['closure'],owner_attachments=owner_rows,
        target_instances={'maximum':2,'order':'caster complete, then valid same-side ally',
            'caster_mask':2,'alive_both_mask':3,'simultaneous_emitters_required':1})
    output.mkdir(parents=True)
    (output/'reflect_model.dat').write_bytes(model)
    packed=particle.encode(bank,closure,PROFILE['member_sha256'])
    (output/'reflect.c3p').write_bytes(packed)
    report['particle_pack_sha1']=hashlib.sha1(packed).hexdigest()
    if parser is not None:
        report['packs']=convert_model(model,parser,output)
        parts,tracks=prepare_parts(disc,parser)
        (output/'reflect_parts.c3r').write_bytes(parts)
        report['part_tracks']=tracks
        report['parts_sha1']=hashlib.sha1(parts).hexdigest()
        prepared=(output/'reflect.c3t').read_bytes()
        descriptor=encode(report,prepared,packed,parts)
        (output/'reflect.c3w').write_bytes(descriptor)
        report['descriptor_version']=2
        report['descriptor_sha256']=hashlib.sha256(descriptor).hexdigest()
        report['model_pack_sha1']=hashlib.sha1(prepared).hexdigest()
    (output/'reflect-sequence.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--disc',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--parser',type=Path,default=Path(__file__).resolve().parents[1]/'vendor/hsd-importer')
    args=parser.parse_args();print(json.dumps(prepare(args.disc,args.output,args.parser),indent=2))
