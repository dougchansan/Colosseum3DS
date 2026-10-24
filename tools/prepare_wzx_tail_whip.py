#!/usr/bin/env python3
"""Prepare original encounter-10 Tail Whip resources; no native runtime is implied.

The first HSD has no geometry. Its original joint matrices are a motion resource,
not a fabricated render mesh. The second HSD uses the existing C3T converter.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from prepare_boot_media import verify_disc
from prepare_game_state import Dol, SOURCE_COMMIT
from prepare_model import check_output
from prepare_outskirt import member, rel_data
from prepare_battle_init import COMMON_SHA1
from prepare_battle_scene import admit, load_pkx
from prepare_battle_camera import source_bounds
from prepare_wzx_bite import link_times

ROOT = Path(__file__).resolve().parents[1]
ARCHIVE = 'wzx_shippowofuru_attack.fsys'
ARCHIVE_BYTES = 51616
ARCHIVE_SHA256 = '821c575cf7a6c12d67238f80fb139e7d810052ed721aaefabab3350f08255ea1'
MEMBER = 0x07332000
MEMBER_BYTES = 139068
MEMBER_SHA256 = 'f3dabf6dfd082f2c10f333dcc1afed34f1ea54c454e850f04fe8429a0f6030dc'
MODEL_PROFILES = ((0xA0, 0x140, 683, '0582c1f2a2cb0441da2b9ba08634a7dd788aae0892fec21040cc02ed9ce068b1'),
                  (0x400, 0x4A0, 137747, '7b2b16b35d81b75f7aa6d59660703216bcbc33154c5013b8c08bf1aaef566ddf'))


def verify_source(source_root):
    manifest = json.loads((ROOT/'config/wzx-tail-whip-source-files.json').read_text())
    if manifest.get('commit') != SOURCE_COMMIT:
        raise ValueError('Tail Whip source commit drift')
    for name, digest in manifest['files'].items():
        path = Path(source_root)/name
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError('Tail Whip source drift: '+name)
    return manifest


def parse(data):
    """Exact structural profile. prepare() additionally admits every source byte."""
    if len(data) != MEMBER_BYTES or struct.unpack_from('>I', data, 104)[0] != 2:
        raise ValueError('Tail Whip root extent/version')
    if struct.unpack_from('>6I', data, 112) != (0, 4, 0x78, 2, 5, 0) or any(data[136:160]):
        raise ValueError('Tail Whip camera/header profile changed')
    nodes = []
    for key, kind, selector, position, flags, offset in ((1,2,0,4,0,0xA0), (2,2,16,1,0,0x400), (3,5,0,0,1,0x21EC0)):
        expected = [key,kind,selector,position,0,0,0,2]+[0]*16+[flags,0,2,0]
        if list(struct.unpack_from('>28i',data,offset)) != expected:
            raise ValueError('Tail Whip node profile changed at %x' % offset)
        words = list(struct.unpack_from('>8I', data, offset+112)) if kind==2 else [*struct.unpack_from('>3I',data,offset+112),0,0,0,0,0]
        nodes.append(dict(key=key,kind=kind,attachment=selector,position_type=position,
                          link=0,source=0,target=0,timing_index=2,timing=[0]*16,
                          flags=flags,part=0,state=0,offset=offset,resource_words=words))
    if nodes[0]['resource_words'] != [0,0,0,0,1,1,1,683] or nodes[1]['resource_words'] != [0,0,0,0,0,0,0,137747] or nodes[2]['resource_words'] != [0x1DB,3,0,0,0,0,0,0]:
        raise ValueError('Tail Whip model/sound payload changed')
    # Model loader fills a zero timing slot with the maximum source frame.
    # Both entries are one-shot, so their real completion is animation-ended,
    # not these link-table values interpreted as a global fixed duration.
    nodes[0]['timing'][1] = 31
    nodes[1]['timing'][1] = 45
    if link_times(nodes,[0,98]) != 0 or any(n['start'] for n in nodes):
        raise ValueError('Tail Whip requires source zero-start normalization')
    return nodes


def encode_motion(tracks, dat_digest):
    """C3TM1: two original JObj matrices, 63 source60Hz samples, native units."""
    if len(tracks)!=63 or any(len(frame)!=24 or any(not math.isfinite(v) or abs(v)>1000 for v in frame) for frame in tracks):
        raise ValueError('Tail Whip controller track bounds')
    for frame in tracks:
        identity=[1.,0.,0.,0.,0.,1.,0.,0.,0.,0.,1.,0.]
        if any(abs(a-b)>1e-6 for a,b in zip(frame[:12],identity)):
            raise ValueError('Tail Whip controller root is not identity')
        part=frame[12:]
        if any(abs(part[k])>1e-6 for k in (1,3,4,6,7,9,11)) or abs(part[5]-1)>1e-6 or abs(part[0]-part[10])>1e-6 or abs(part[2]+part[8])>1e-6 or abs(part[0]**2+part[2]**2-1)>1e-6:
            raise ValueError('Tail Whip controller requires unsupported non-yaw SRT')
        for at in (0,12):
            # No perspective, zero-scale or reflection branch is admitted.
            a,b,c,d,e,f,g,h,i = (frame[at+k] for k in (0,1,2,4,5,6,8,9,10))
            determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g)
            if determinant <= 0 or not math.isfinite(determinant):
                raise ValueError('Tail Whip controller singular/reflected basis')
    body=b''.join(struct.pack('<24f',*frame) for frame in tracks)
    out=bytearray(128)
    struct.pack_into('<4s7I',out,0,b'C3TM',1,128+len(body),39,2,63,60,1)
    out[32:52]=bytes.fromhex(SOURCE_COMMIT)
    out[52:84]=bytes.fromhex(dat_digest)
    out[84:104]=hashlib.sha1(body).digest()
    out.extend(body)
    out[104:124]=hashlib.sha1(out).digest()
    return bytes(out)


def encode_descriptor(nodes, controller, drawable, center, owner_sha1):
    if len(nodes)!=3 or len(center)!=3 or any(not math.isfinite(v) or abs(v)>1000 for v in center):
        raise ValueError('Tail Whip descriptor bounds')
    # Recheck the source node admission even for direct synthetic API callers.
    payloads=([0,0,0,0,1,1,1,683],[0,0,0,0,0,0,0,137747],[0x1DB,3,0,0,0,0,0,0])
    for n,key,kind,selector,pos,flags,offset,payload in zip(nodes,(1,2,3),(2,2,5),(0,16,0),(4,1,0),(0,0,1),(0xA0,0x400,0x21EC0),payloads):
        if (n['key'],n['kind'],n['attachment'],n['position_type'],n['flags'],n['start']) != (key,kind,selector,pos,flags,0):
            raise ValueError('Unqualified Tail Whip descriptor node')
        timing=[0]*16
        if key<3:timing[1]=(31,45)[key-1]
        if any(n[k] for k in ('link','source','target','part','state')) or n['timing_index']!=2 or n['timing']!=timing or n['offset']!=offset or n['resource_words']!=payload:
            raise ValueError('Unqualified Tail Whip descriptor payload/timing')
    out=bytearray(512)
    struct.pack_into('<4s7I',out,0,b'C3TW',1,512,39,288,0,60,3)
    out[32:52]=bytes.fromhex(SOURCE_COMMIT)
    out[52:84]=bytes.fromhex(MEMBER_SHA256)
    out[84:104]=hashlib.sha1(controller).digest()
    out[104:124]=hashlib.sha1(drawable).digest()
    out[124:144]=bytes.fromhex(owner_sha1)
    struct.pack_into('<5I3f',out,144,0x78,2,0x1DB,98,0,*center)
    struct.pack_into('<2f2I',out,176,.5,.75,0,0)  # original model rate/class scale; no fixed end
    for index,n in enumerate(nodes):
        values=[n[k] for k in ('key','kind','attachment','position_type','link','source','target','timing_index','flags','part','offset','start')]
        values += [31 if index==0 else 45 if index==1 else 0,index+1,0,0]
        struct.pack_into('<24I',out,192+96*index,*values,*n['resource_words'])
    out[480:500]=hashlib.sha1(out).digest()
    return bytes(out)


def read_model(dat, geometry):
    from importer.phases.route.route import route_sections
    from importer.phases.parse.parse import parse_sections
    from importer.phases.describe.describe import describe_scene
    opts=dict(filepath='tail-whip.dat',game='COLO_XD',max_frame=1000000000,section_names=[],ik_hack=False)
    sections=parse_sections(dat,route_sections(dat),opts)
    if len(sections)!=1 or sections[0].section_name!='scene_data':
        raise ValueError('Unknown Tail Whip HSD section')
    raw=sections[0].root_node
    scene=describe_scene(sections,opts)
    if len(scene.models)!=1:
        raise ValueError('Unknown Tail Whip HSD model count')
    return geometry.Model(scene.models[0],raw.models[0],constant_material_colors=True),raw


def prepare(disc, parser, output, source_root=None):
    import prepare_field_geometry as geometry
    import prepare_title as title
    disc=verify_disc(Path(disc))
    output=check_output(Path(output))
    manifest=verify_source(source_root or ROOT/'vendor/colosseum')
    parser=title.verify_parser(parser)
    geometry.import_parser(parser)
    path=disc/'files'/ARCHIVE
    if path.is_symlink() or path.stat().st_size!=ARCHIVE_BYTES:
        raise ValueError('Unsafe Tail Whip archive')
    archive=path.read_bytes()
    if hashlib.sha256(archive).hexdigest()!=ARCHIVE_SHA256:
        raise ValueError('Tail Whip archive hash mismatch')
    data=member(archive,MEMBER)
    if hashlib.sha256(data).hexdigest()!=MEMBER_SHA256:
        raise ValueError('Tail Whip member hash mismatch')
    nodes=parse(data)
    models=[]
    for _node,at,size,digest in MODEL_PROFILES:
        dat=data[at:at+size]
        if hashlib.sha256(dat).hexdigest()!=digest:
            raise ValueError('Tail Whip HSD hash mismatch')
        model,raw=read_model(dat,geometry)
        models.append((model,raw,dat))
    control,draw=models[0][0],models[1][0]
    if (control.ends,len(control.joints),len(control.meshes),len(control.materials)) != ([31.0],2,0,0) or (draw.ends,len(draw.joints),len(draw.meshes),len(draw.materials)) != ([45.0],6,2,1):
        raise ValueError('Tail Whip original geometry/clip profile changed')
    owner_archive=admit(disc/'files/pkx_ziguzaguma.fsys')
    owner_dat,owner_raw,owner_scene,header=load_pkx(owner_archive,'pkx_ziguzaguma.fsys')
    owner=geometry.Model(owner_scene.models[0],owner_raw.models[0],allow_image_swaps=True)
    idle=header.anim_entries[0]
    if idle.anim_type!=2 or idle.sub_anim_count!=1 or idle.sub_anims[0].anim_index!=0 or owner.ends[0]!=49.0 or [round(t*60) for t in idle.timing]!=[98,0,0,0] or idle.body_map_bones[2]!=53:
        raise ValueError('Tail Whip owner idle binding changed')
    owner_meshes=[geometry.build_mesh(owner,i) for i in range(len(owner.meshes))]
    lo,hi=source_bounds(owner,owner_meshes,0)
    center=[(a+b)*.05 for a,b in zip(lo,hi)]  # bounds are original GC units
    frames=geometry.sample_sequence(31)
    tracks=[list(map(float,control.world(0,f)[0][:,:3,:].reshape(-1))) for f in frames]
    motion=encode_motion(tracks,MODEL_PROFILES[0][3])
    meshes=[geometry.build_mesh(draw,i) for i in range(len(draw.meshes))]
    if any(m['batch'][3] & (title.LIT|title.SPECULAR) for m in meshes):
        raise ValueError('Tail Whip requires unsupported effect lighting')
    dol=Dol((disc/'sys/main.dol').read_bytes())
    if struct.unpack('>2I',dol.read(0x8036E150+39*28+4,8))!=(0x3A9,MEMBER) or struct.unpack('>2I',dol.read(0x8035C430+39*20+12,8))!=(0x8020FAC0,0x8020FB30):
        raise ValueError('Tail Whip original resource/dispatch binding drift')
    common=member((disc/'files/common.fsys').read_bytes(),0x01C71C00)
    if hashlib.sha1(common).hexdigest()!=COMMON_SHA1 or rel_data(common,125)[0x13FE08+0x1DB*12:0x13FE08+0x1DB*12+12]!=bytes.fromhex('8005010a024e000000000000'):
        raise ValueError('Tail Whip sound request mapping drift')
    for address,value in ((0x8047E348,.5),(0x8047E350,0.),(0x8047CB98,.5)):
        if struct.unpack('>f',dol.read(address,4))[0]!=value:
            raise ValueError('Tail Whip source rate/frame/center constant drift')
    attachment_words=struct.unpack('>77I',dol.read(0x800E4AC0,0x134))
    if any(word not in attachment_words for word in (0x7CBE2B78,0x7C7D1B78,0x93DD0114)):
        raise ValueError('Tail Whip source attachment-type store drift')
    output.mkdir(parents=True,exist_ok=False)
    draw_frames=geometry.sample_sequence(45)
    camera=(0.,2.,10.,0.,0.,0.,0.,45.,.1,1000.,4/3,0.)
    packs=geometry.prepare_pack_set(meshes,[(0,draw_frames,False)],120,[camera]*len(draw_frames),
        geometry.fixed_lights(models[1][1],models[1][2]),ARCHIVE_SHA256,output,'tail_whip_effect',expected_count=1,format_version=3)
    drawable=(output/'tail_whip_effect.c3t').read_bytes()
    descriptor=encode_descriptor(nodes,motion,drawable,center,hashlib.sha1(owner_dat).hexdigest())
    outputs={'tail_whip_controller.c3m':motion,'tail_whip.c3w':descriptor}
    for name,payload in outputs.items():
        (output/name).write_bytes(payload)
    outputs['tail_whip_effect.c3t']=drawable
    report=dict(source_commit=SOURCE_COMMIT,source_files=manifest['files'],archive=ARCHIVE,group=0x3A9,member=MEMBER,
        archive_sha256=ARCHIVE_SHA256,member_sha256=MEMBER_SHA256,nodes=nodes,models=[dict(offset=at,bytes=size,sha256=digest) for _,at,size,digest in MODEL_PROFILES],
        owner=dict(species=288,entry=0,clip=0,kind=2,timing=[0,98],authored_end=49,initial_frame=0,remaining_loops=0,initial_frame_qualification='Sequence loader inserts reserved track0 kind1/value0; fn801DD100 writes normalization0 to entry.field90. MotionStart801D9C1C duration0 takes direct fn801DF070 clip0, which explicitly sets frame0/rate.5.',center_native=center,bounds_minimum=lo,bounds_maximum=hi,extracted_hsd_sha1=hashlib.sha1(owner_dat).hexdigest()),
        attachment=dict(controller='Owner follows source controller joint1 POSITION+ROTATION continuously; preserve scale; reset/detach on node stop.',
            effect='Selector16 creates original bound-midpoint center-null; POSITION-only attachment, own owner-yaw/class-scale remain. It is not original bone16.',
            controller_root='Initial owner root position/yaw and source class-scale .75. Motion pack stores unbound local matrices; runtime composition is not implemented.',
            source='ModelStart801D91EC; Attach800E4AC0; modelUpdateAttachments800ED4D4; CenterNull800E6BC8; EntryStop801D7E58'),
        controller_srt=dict(translation=[0,0,0],scale=[1,1,1],pitch_roll=[0,0],yaw_min=-.2094116241166306,yaw_max=.26490944111351633,qualification='All63 original samples are checked for identity root, zero translation and a unit yaw-only joint1 basis. C3TM retains the full3x4 matrices.'),
        timing=dict(starts=[0,0,0],normalization=0,model_rate=.5,controller_end_frame=31,effect_end_frame=45,
            controller_samples=63,effect_samples=91,owner_gate=98,total_end=None,
            qualification='Animation-ended model checks plus owner timing and sound-running checks; no invented fixed90-tick total. GSmodel epsilon/update order still needs native qualification.'),
        sound=dict(id=0x1DB,flags=3,fx=0x24E,macro=0x402,start=0,common_sha1=COMMON_SHA1,request_row='8005010a024e000000000000',conversion='Separate selected dry-audio preparation; not contained in these model packs.'),
        camera=dict(entry=0,flags=0x78,body_map_index=2,external_camera_bytes=0,motions=[3,0,1,2],fov='Derivedflags0; original short8 weighted table80371F60. Native move39 camera prepared separately.'),
        packs=packs,outputs=[dict(file=name,bytes=len(payload),sha1=hashlib.sha1(payload).hexdigest(),sha256=hashlib.sha256(payload).hexdigest()) for name,payload in outputs.items()],
        scope='Preparation only: one original non-renderable HSD motion pack, one original drawable C3T and checked source binding descriptor. No Tail Whip native stage/runtime is implemented by this tool.',
        unsupported=['Native C3TW/C3TM decoding and execution','Owner-follow/center-null GPU integration and lifecycle','Original shared camera RNG/retail visual parity','Original wet MusyX DSP/mixer and exact audio completion phase','Actor default variants outside admitted Zigzagoon'])
    (output/'tail_whip.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disc',type=Path,required=True)
    p.add_argument('--parser',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--source-root',type=Path)
    a=p.parse_args()
    print(json.dumps(prepare(a.disc,a.parser,a.output,a.source_root)['outputs'],indent=2))
