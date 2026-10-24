#!/usr/bin/env python3
"""Prepare checked Bite/Confusion/Tail Whip/Growl camera profiles and original actor bounds.

No original bytes are distributed. The camera code samples the retail procedural
branches; it does not invent a camera DAT when WZX group/member fields are zero.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from prepare_boot_media import verify_disc
from prepare_game_state import Dol, SOURCE_COMMIT
from prepare_battle_scene import admit, load_pkx
from prepare_outskirt import member
from prepare_model import check_output
ROOT=Path(__file__).resolve().parents[1]
SOURCE_FILES={
'src/game/wazaSequenceEntry.c':'6500a4db7253ce776686b86d470a6f9587cfa3db10cf9fcfb9cca50328d0855f',
'src/game/gs_range_801DE698.c':'e4594cc503d7dd0c4acd06b5df8aa216d04c10ea3698b6e20285cf5f50e2b69d',
'src/game/gs_math_range_800E09E8.c': 'f1b57465550055f817be359aef3e56fca930f06cda9eae883b875899adccc498',
'src/game/wazaSequence.c': '975afb18f19c6da6f7132fe3772d8ab3ab9546f528d8381073b59e614459444d',
'src/game/gs_range_exact_801DF160.c': 'c3bba97a3efe2cefcc79ab2db7a0c5d3a98b313d52c9237ba137ec85c97e9370',
'src/game/wazaSequenceCamera.c': 'f59ed730b4f94c842e81c2316537c7ca847a44de7d0e5a6e21eada4179ef93f4',
'src/game/wazaSequenceCamera_exact_801D2B4C.c': 'a4cbaca482b2c3fb26c0f1f4379de6a7be447075117580bf16783d4c5e020fff',
'src/game/sequence.c': '5197f33a164dd2698f04c01272f0deaa7f02629f010856b535684277f5daa1dd',
'src/game/camera.c': '200b7ec04193420a38966aefea4e5cf5824f0d7dc3dac33b2658c9b945f85924',
'src/game/gs_model_bound.c': '834f966ce186f00a347eaecd1a04ab48bc84778c7e03b50f718cfb86a07b2df2',
'src/hsd/hsd_object.c': 'de07f85afc03cdf6d8ce6b46ec38c372b85c20e2d61f3ff46170883ac948820b',
'src/game/battle/battle_grid.c': 'ddff1286599be59bfc134b3b25326f41e53de5de3ccbde27804020d1ae7df3b2',
}
WZX=(('wzx_kamituku_attack.fsys',0x034B2000,'0cb9912dcbe7c4626d63789a34cfe1a36e869a0878454b8cde878434814216f7',0x201048,6,4,2),
     ('wzx_kamituku_damage.fsys',0x04E32000,'4ed0fd1739bb6fcb2caac0c49e4a53db1e0d39680cfd1495653e146a8aacd9ed',0x200848,5,2,8))
ACTORS=((197,'pkx_blacky.fsys',0x03381E00),(196,'pkx_eifie.fsys',0x03B71E00),(288,'pkx_ziguzaguma.fsys',0x04C51E00))
def profile(data,flags,pattern,mask,entry,node_count=5):
    if len(data)<160 or len(data)>1024*1024:raise ValueError('Truncated/oversized camera WZX')
    kind,count,actual,target_class,unknown,target_part=struct.unpack_from('>6I',data,112)
    if (kind,count,actual,target_class,unknown,target_part)!=(entry,node_count,flags,2,5,0):raise ValueError('Unsupported Bite camera WZX profile')
    if any(data[136:160]):raise ValueError('Unexpected external camera bank/track; resolve explicitly')
    return dict(flags=flags,pattern=pattern,fov_mask=mask,entry=entry,target_body_map_index=2,owner_start=0 if entry==2 else 22)
def encode(profiles,actors,confusion=False,patterns=None):
    if len(profiles)!=2 or len(actors)!=3:raise ValueError('Camera descriptor count bound')
    base=1500 if confusion else 512
    b=bytearray(base);struct.pack_into('<4s6I',b,0,b'C3BC',3,512,44,60,3,2);b[32:52]=bytes.fromhex(SOURCE_COMMIT)
    if confusion:
        struct.pack_into('<4s2I',b,0,b'C3CC',1,base);struct.pack_into('<I',b,12,93)
        if patterns is None or len(patterns)!=988 or hashlib.sha1(patterns).hexdigest()!='176a55770152bab923808f8f20af6a6066b4d794':raise ValueError('Confusion pattern table extent')
        b[512:1500]=patterns
    for i,p in enumerate(profiles):
        if (p['flags'],p['pattern'],p['fov_mask'],p['entry'])!=(((0x200848,5,2,1),(0x48,0xffffffff,0,8)) if confusion else ((0x201048,6,4,2),(0x200848,5,2,8)))[i]:raise ValueError('Unsupported procedural camera profile')
        if p['owner_start']!=((0,44) if confusion else (0,22))[i]:raise ValueError('Unqualified camera owner animation normalization')
        struct.pack_into('<5I',b,64+i*64,p['flags'],p['pattern'],p['fov_mask'],p['entry'],p['owner_start'])
    for i,a in enumerate(actors):
        if a['species']!=ACTORS[i][0] or a['selector']!=-1:raise ValueError('Unknown source camera size selector')
        lo,hi=a['minimum'],a['maximum']
        if len(lo)!=3 or len(hi)!=3 or not all(math.isfinite(x) and abs(x)<10000 for x in lo+hi) or any(x>y for x,y in zip(lo,hi)) or lo==hi:raise ValueError('Invalid source actor bounds')
        off=192+96*i;struct.pack_into('<Ii6f',b,off,a['species'],a['selector'],*lo,*hi)
        for phase in range(2):
            timing=a['timing'][phase]
            if len(timing)!=4 or not timing[0] or timing[0]>timing[1] or any(not isinstance(t,int) or not 0<=t<=1800 for t in timing):raise ValueError('Invalid source camera timing')
            if a['part'][phase]!=(42,62,53)[i] or a['kind'][phase]!=(4,3)[phase]:raise ValueError('Unqualified camera body-map part/type')
            struct.pack_into('<4I',b,off+32+phase*16,*timing)
            struct.pack_into('<I',b,off+64+phase*4,a['part'][phase]);struct.pack_into('<I',b,off+72+phase*4,a['kind'][phase])
    count=0
    for i,a in enumerate(actors):
        tracks=a['tracks']
        if len(tracks)!=2:raise ValueError('Camera part phase count')
        for phase,track in enumerate(tracks):
            if not 1<=len(track)<=1025 or any(len(v)!=3 or not all(math.isfinite(x) and abs(x)<1000 for x in v) for v in track):raise ValueError('Invalid source camera-part track')
            struct.pack_into('<2I',b,192+96*i+80+phase*8,len(b),len(track))
            for point in track:b.extend(struct.pack('<3f',*point))
            count+=len(track)
    struct.pack_into('<2I',b,8,len(b),93 if confusion else 44);struct.pack_into('<I',b,28,count)
    b[480:500]=hashlib.sha1(b).digest();return bytes(b)
def source_bounds(model,meshes,clip):
    """GSmodelRecalculateBound / _modelBoundVertex: expand transformed vertices at frame0.

    Exporter vertices are native0.1GC units. Reverse only that established unit
    conversion; use the same checked skin/IK matrices as the existing packs.
    """
    import numpy as np
    world,_=model.world(clip,0);points=[];matrices={}
    for mesh in meshes:
        for vertex,descriptor in zip(mesh['vertices'],mesh['descriptors']):
            if descriptor not in matrices:
                matrices[descriptor]=model.deform(descriptor,world) @ np.linalg.inv(model.deform(descriptor,model.rest))
            points.append((matrices[descriptor] @ np.array([*vertex[:3],1]))[:3]/.1)
    points=np.asarray(points)
    if not len(points) or not np.isfinite(points).all():raise ValueError('Invalid original camera bound vertices')
    return list(map(float,points.min(axis=0))),list(map(float,points.max(axis=0)))
def prepare(disc,parser,output,confusion=False):
    import prepare_field_geometry as geometry
    verify_disc(disc);check_output(output)
    for name,digest in SOURCE_FILES.items():
        p=ROOT/'vendor/colosseum'/name
        if p.is_symlink() or hashlib.sha256(p.read_bytes()).hexdigest()!=digest:raise ValueError('Camera source drift: '+name)
    geometry.import_parser(parser)
    dol=Dol((disc/'sys/main.dol').read_bytes());profiles=[];resources=[];damage_data=None
    for address in (0x8047E348,0x8047E3C8):
        if struct.unpack('>f',dol.read(address,4))[0]!=.5:raise ValueError('Unqualified source owner motion rate')
    roots=WZX
    if confusion:
        roots=(('wzx_nenriki_attack.fsys',0x07532000,'c4c0c0c2a678a891ed71b92aa2212977a1e24adb985bc6a5c155fe2184b9f8bb',0x200848,5,2,1),('wzx_nenriki_damage.fsys',0x05292000,'a1a0a4114c68a1b4c92d7952fb8bed60503b041f8eee85f033ef32f64d696d04',0x48,0xffffffff,0,8))
    for name,identity,digest,flags,index,mask,entry in roots:
        path=disc/'files'/name
        expected=(25632,'3a5a0667e61bed6508a0bff68cec95316a08804288b82f80a1761c7afa4ce193') if entry==2 else (117856,'9626a36b5b609302940e8c74a147d0e590fd0b08c06726999a0489ce98dc41ef')
        if confusion:expected=(72832,'ce7df34845f8cc5520242dec14538ee2c1c662e9b4217cc1002393eff3c0885d') if entry==1 else (72960,'b4bc2e6b2c5332cf2afc7a8cbfa26d86f9b37b69625be4d0de2143c64b1c3920')
        if path.is_symlink() or path.stat().st_size!=expected[0]:raise ValueError('Unsafe camera WZX archive')
        archive=path.read_bytes()
        if hashlib.sha256(archive).hexdigest()!=expected[1]:raise ValueError('Camera WZX archive hash mismatch')
        data=member(archive,identity);actual=hashlib.sha256(data).hexdigest()
        if digest and actual!=digest:raise ValueError('Camera WZX member hash mismatch')
        p=profile(data,flags,index,mask,entry,(6 if entry==1 else 11) if confusion else 5)
        if confusion:p['owner_start']=0 if entry==1 else 44
        if entry==8:damage_data=data
        # GetPattern uses short8entry table; flags0x40/0x80 select5/6.
        table=dol.read(0x803721C0 if index==0xffffffff else 0x80371F60+index*76,988 if index==0xffffffff else 76);words=struct.unpack('>19I',table[:76])
        if index!=0xffffffff and (words[1]!=1 or words[7]!=mask or any(words[k] for k in range(2,19) if k!=7)):raise ValueError('Source Bite FOV pattern changed')
        profiles.append(p);resources.append(dict(archive=name,member=identity,member_sha256=actual,source_pattern_address=hex(0x803721C0 if index==0xffffffff else 0x80371F60+index*76),pattern_sha256=hashlib.sha256(table).hexdigest()))
    actors=[]
    for species,name,identity in ACTORS:
        archive=admit(disc/'files'/name);pkx=member(archive,identity)
        selector=struct.unpack_from('>i',pkx,12)[0]
        dat,raw,scene,header=load_pkx(archive,name)
        model=geometry.Model(scene.models[0],raw.models[0],allow_image_swaps=True)
        idle=header.anim_entries[0].sub_anims[0].anim_index
        meshes=[geometry.build_mesh(model,i) for i in range(len(model.meshes))]
        lo,hi=source_bounds(model,meshes,idle)
        entries=[header.anim_entries[i] for i in ((1,8) if confusion else (2,8))]
        # fn801DC5F0 uses WZX header[3]=2 for sequencebyte0x17;
        # battleCameraStartWaza indexes the owning PKX body_map[2]. Original
        # JObj IDs are baked into independent immutable source60Hz tracks,
        # never reinterpreted as C3T envelope indices. GSmodel anim rate=.5.
        tracks=[]
        for e in entries:
            clip=e.sub_anims[0].anim_index;part=e.body_map_bones[2]
            if not 0<=part<len(model.joints):raise ValueError('Source camera body-map JObj outside model')
            frames=geometry.sample_sequence(model.ends[clip])
            tracks.append([list(map(float,model.world(clip,frame)[0][part,:3,3])) for frame in frames])
        a=dict(species=species,selector=selector,minimum=lo,maximum=hi,timing=[[round(t*60) for t in e.timing] for e in entries],part=[e.body_map_bones[2] for e in entries],kind=[e.anim_type for e in entries],archive=name,member=identity,member_sha256=hashlib.sha256(pkx).hexdigest(),idle_clip=idle,tracks=tracks,camera_part_qualification='OriginalJObj body_map[2] sampled at .5animationframe per original60Hz tick')
        if any(e.sub_anim_count!=1 for e in entries):raise ValueError('Unsupported camera owner multiple subanimations')
        actors.append(a)
    instruction_profile={}
    if not confusion:
        from prepare_wzx_damage import parse as parse_damage, qualify_timeline
        instruction_profile=qualify_timeline(dol)
        defender=actors[2]['timing'][1]
        damage_profile,_,_=parse_damage(damage_data,[0,*defender[:2]])
        if damage_profile['owner_start']!=22:raise ValueError('Damage camera actor normalization changed')
        profiles[1]['owner_start']=damage_profile['owner_start']
    # The camera DoPosition duration itself is not the WZX normalized owner end:
    # DOL0x801D3820/3A50 loads count, shifts2, loads+4 (or+8 fallback).
    for address,word in ((0x801D3820,0x80070000),(0x801D382C,0x83A30004),(0x801D3A50,0x80070000),(0x801D3A5C,0x83A30004)):
        if struct.unpack('>I',dol.read(address,4))[0]!=word:raise ValueError('Source camera duration instruction changed')
    patterns=None
    if confusion:
        raw_patterns=dol.read(0x803721C0,988)
        patterns=struct.pack('<247I',*struct.unpack('>247I',raw_patterns))
    descriptor=encode(profiles,actors,confusion,patterns);output.mkdir(parents=True,exist_ok=False);(output/('confusion_camera.c3c' if confusion else 'bite_camera.c3c')).write_bytes(descriptor)
    report=dict(source_commit=SOURCE_COMMIT,source_files=SOURCE_FILES,resources=resources,actors=actors,descriptor_sha256=hashlib.sha256(descriptor).hexdigest(),normalization=instruction_profile,owner_start=[0,22],scope='Source procedural Bite attack/damage motion2/3 and short-pattern5/6 FOV; separate cosmetic stream, not original shared RNG parity',projection='Retain original inherited near/far/aspect; source computed FOV; native unit conversion0.1',timing='Source60ticks; action duration timing1, damage timing0; owneranimationactive1 shift0; target-offset move0.2seconds; damage actortrack holds tick0..22 then(.5frame/tick), camera motion duration stays190ticks forZig (DOLcount3+4)',camera_target='fn801DC5F0 header[3]2 ->body_map[2] ->originalJObj42/62/53; independent source .5frame/tick localtracks',limitations='Default variant0 only; NumPy/native scalar arithmetic is not PPC bit parity; original shared cosmetic RNG differs; source visual comparison remains required')

    if confusion:
        report.update(move=93,owner_start=[0,44],scope='Original procedural Confusion motion2/3, attack short5 FOV and defender weighted long13 FOV with two source segments',timing='Source60ticks; ownerentry1/8; camera duration timing1/action and timing0/hit; defender actor track delayed44ticks; FOV uses implicitzero then timing0/1',pattern_address='0x803721C0',pattern_sha256=hashlib.sha256(raw_patterns).hexdigest())
    (output/('confusion_camera.json' if confusion else 'bite_camera.json')).write_text(json.dumps(report,indent=2)+'\n');return report
def encode_single_camera(actors,patterns,move):
    if move not in (39,45,0x10029,0x10027):raise ValueError("Unqualified single-phase camera move")
    growl=move==45;red=move==0x10027;stat=move in (0x10029,0x10027)
    if len(actors)!=(2 if stat else 3) or len(patterns)!=608 or hashlib.sha1(patterns).hexdigest()!='cbded7a9ed874d8b68a9e20c3fe082d2c75def47':raise ValueError('Unqualified TailWhip short8 camera table')
    b=bytearray(1120);struct.pack_into('<4s6I',b,0,b'C3SC' if stat else b'C3GC' if growl else b'C3TC',1,1120,0x27 if red else 0x29 if stat else move,60,2 if stat else 3,1);b[32:52]=bytes.fromhex(SOURCE_COMMIT)
    struct.pack_into('<5I',b,64,0x200858 if growl else 0 if stat and not red else 0x78,5 if growl else 0xffffffff,2 if growl else 0,1 if growl else 0,0);b[512:1120]=patterns
    count=0
    for i,a in enumerate(actors):
        if a['species']!=ACTORS[i][0] or a['selector']!=-1 or a['kind']!=(4 if growl else 2) or a['part']!=(42,62,53)[i]:raise ValueError('Unsupported TailWhip camera actor binding')
        timing=a['timing'];lo=a['minimum'];hi=a['maximum'];track=a['track']
        if len(timing)!=4 or not 1<=timing[0]<=1800 or (not growl and any(timing[1:])) or (growl and (not timing[0]<=timing[1]<=timing[2]<=1800 or timing[3])):raise ValueError('Unqualified TailWhip idle camera timing')
        if len(lo)!=3 or len(hi)!=3 or not all(math.isfinite(v) and abs(v)<10000 for v in lo+hi) or any(x>y for x,y in zip(lo,hi)) or lo==hi:raise ValueError('Invalid TailWhip source bounds')
        if not 1<=len(track)<=1025 or any(len(v)!=3 or not all(math.isfinite(x) and abs(x)<1000 for x in v) for v in track):raise ValueError('Invalid TailWhip source part track')
        if stat and (timing!=[(118,180)[i],0,0,0] or len(track)!=(119,181)[i]):raise ValueError('Unqualified GREEN target idle duration/sample count')
        off=192+96*i;struct.pack_into('<Ii6f',b,off,a['species'],-1,*lo,*hi);struct.pack_into('<4I',b,off+32,*timing);struct.pack_into('<I',b,off+64,a['part']);struct.pack_into('<I',b,off+72,a['kind'])
        struct.pack_into('<2I',b,off+80,len(b),len(track))
        for point in track:b.extend(struct.pack('<3f',*point))
        struct.pack_into('<2I',b,off+88,len(b),0);count+=len(track)
    struct.pack_into('<I',b,8,len(b));struct.pack_into('<I',b,28,count);b[480:500]=hashlib.sha1(b).digest();return bytes(b)

def encode_tailwhip(actors,patterns):
    return encode_single_camera(actors,patterns,39)

def encode_growl(actors,patterns):
    return encode_single_camera(actors,patterns,45)

def encode_attack_down(actors,patterns):
    return encode_single_camera(actors,patterns,0x10027)

def encode_stat_down(actors,patterns):
    return encode_single_camera(actors,patterns,0x10029)

def prepare_single_camera(disc,parser,output,move):
    if move not in (39,45,0x10029,0x10027):raise ValueError("Unqualified single-phase camera preparation")
    growl=move==45;red=move==0x10027;stat=move in (0x10029,0x10027)
    import prepare_field_geometry as geometry
    verify_disc(disc);check_output(output)
    for name,digest in SOURCE_FILES.items():
        path=ROOT/'vendor/colosseum'/name
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise ValueError('Camera source drift: '+name)
    path=disc/'files'/('wzx_reddown_status.fsys' if red else 'wzx_greendown_status.fsys' if stat else 'wzx_nakigoe_attack.fsys' if growl else 'wzx_shippowofuru_attack.fsys')
    archive_size=1376 if stat else 248960 if growl else 51616
    archive_sha='a05747fe283ce15e197cef32c61a1394de84ae66e5293fcf5bc2201095b1399c' if red else '8947e10d57f4d17f426debe1ddb713c8856cf889f3d7cf6c5a8ced0e1b602dd8' if stat else '3f171e90d3725c2e9172f980ee0e5ae234ce00dbf9e6ab4cf251df676ae63af8' if growl else '821c575cf7a6c12d67238f80fb139e7d810052ed721aaefabab3350f08255ea1'
    wzx_identity=0x0B6E2000 if red else 0x0B6F2000 if stat else 0x07352000 if growl else 0x07332000
    member_sha='0e0103bdcf6f2dca317e8ae86cfbc5294de2bcd2d7f1af4bf42c04a01bdf08de' if red else '5e08695ee116a4a5aaded4deef4fd1af71e5f4178992e187610a7e92a63e7c1e' if stat else '85862ce50519fc694b179761e5038bab74f4bb2ce9c15b7102a0b0937ef8f82e' if growl else 'f3dabf6dfd082f2c10f333dcc1afed34f1ea54c454e850f04fe8429a0f6030dc'
    if path.is_symlink() or path.stat().st_size!=archive_size or hashlib.sha256(path.read_bytes()).hexdigest()!=archive_sha:raise ValueError('TailWhip camera source archive drift')
    original=member(path.read_bytes(),wzx_identity)
    if hashlib.sha256(original).hexdigest()!=member_sha:raise ValueError('TailWhip member drift')
    profile(original,0x200858 if growl else 0 if stat and not red else 0x78,5 if growl else 0xffffffff,2 if growl else 0,1 if growl else 0,6 if growl else 4)
    geometry.import_parser(parser);actors=[]
    for species,name,identity in (ACTORS[:2] if stat else ACTORS):
        archive=admit(disc/'files'/name);dat,raw,scene,header=load_pkx(archive,name);model=geometry.Model(scene.models[0],raw.models[0],allow_image_swaps=True);entry=header.anim_entries[1 if growl else 0]
        if stat and (entry.sub_anim_count!=1 or entry.sub_anims[0].motion_type!=0):raise ValueError('Stat-down requires direct idle frame0 reset')
        if entry.sub_anim_count!=1:raise ValueError('Unsupported TailWhip idle subanimations')
        clip=entry.sub_anims[0].anim_index;part=entry.body_map_bones[2];meshes=[geometry.build_mesh(model,i) for i in range(len(model.meshes))];lo,hi=source_bounds(model,meshes,header.anim_entries[0].sub_anims[0].anim_index)
        track=[list(map(float,model.world(clip,frame)[0][part,:3,3])) for frame in geometry.sample_sequence(model.ends[clip])]
        actors.append(dict(species=species,selector=struct.unpack_from('>i',member(archive,identity),12)[0],kind=entry.anim_type,part=part,timing=[round(t*60) for t in entry.timing],minimum=lo,maximum=hi,track=track,archive=name,member=identity,source_clip=clip))
    dol=Dol((disc/'sys/main.dol').read_bytes())
    if red and struct.unpack('>2I',dol.read(0x803727C8+0x27*12,8))!=(0x49B,0x0B6E2000):raise ValueError('RED special0x27 resource namespace drift')
    for address in (0x8047E348,0x8047E3C8):
        if struct.unpack('>f',dol.read(address,4))[0]!=.5:raise ValueError('Source single-phase owner rate drift')
    raw_patterns=dol.read(0x80371F60,608);patterns=struct.pack('<152I',*struct.unpack('>152I',raw_patterns));packed=encode_single_camera(actors,patterns,move)
    output.mkdir(parents=True,exist_ok=False);(output/('attack_down_camera.c3c' if red else 'stat_down_camera.c3c' if stat else 'growl_camera.c3c' if growl else 'tailwhip_camera.c3c')).write_bytes(packed)
    report=dict(move=move,source_commit=SOURCE_COMMIT,source_files=SOURCE_FILES,archive=path.name,member=wzx_identity,member_sha256=hashlib.sha256(original).hexdigest(),header=[1,6,0x200858,2,5,0] if growl else [0,4,0 if stat and not red else 0x78,2,5,0],actors=actors,descriptor_sha256=hashlib.sha256(packed).hexdigest(),short_table_address='0x80371F60',short_table_sha256=hashlib.sha256(raw_patterns).hexdigest(),scope='Checked TailWhip camera only: four source motion choices, idle owner entry0, short8 initial FOV; no stage integration or hardware qualification',owner_transform='Origin captured at start; target worldPart-currentOwnerRoot followed at0.2s with current source yaw; translation world X ignores owner yaw',rng='Separate native cosmetic stream; bounded source helper consumes two16bit draws; no original shared RNG parity',motion_selection='Bits3/4/5/6 select3/0/1/2 with ordered fallthrough previous-motion exclusion',duration='Idle count2: implicit point0=0 then fallback timing0; no doubled trainer shift for admitted Pokemon',fov='Motion0 static middle mask2; motions1/2/3 weighted short8 initial flags, count2 returns before segment interpolation',limitations=['PPC floating-point bit parity not claimed','Controller must supply actual current owner yaw for source part look-at','Root translation cancels in relative target offset; source camera origin stays captured while offset move is renewed each frame','No retail or native hardware visual acceptance yet'])
    if growl:
        report.update(scope='Checked Growl45 camera only; source3/0/2 motions, fixed short5 mask2, no stage/hardware claim',motion_selection='Raw200858 bits3/4/6 =>ordered3/0/2; bit5 translation absent',duration='Kind4: runtimecount*4+4=offset20 =>timing1 (Zig244), no ownerstart shift',fov='Dolly directly usesmask2; orbit/still consumeGetPattern draw andforce shortrow5/mask2, both sourceFOVsegments hold',owner_entry=1,owner_rate=.5,owner_start=0,owner_start_frame=0,owner_transform='Captured owner root; current entry1 bodymap2 local track follows current source yaw; no translation camera mode',zig_raw_timing=[0,*actors[2]['timing'][:3]],zig_camera_duration=actors[2]['timing'][1])
        if report['zig_raw_timing']!=[0,130,244,366] or report['zig_camera_duration']!=244:raise ValueError('Growl Zig source duration drift')
    if stat:
        report.pop('move');report.update(profile_kind='special',special=0x27 if red else 0x29,special_hex='0x27' if red else '0x29',scope=('RED0x27' if red else 'GREEN0x29')+' camera-only checked affected actors0/1, no stage/hardware claim',owner_entry=0,owner_start=0,owner_start_frame=0,owner_rate=.5,targets=[0,1],duration='Idle count2 implicit0 then timing0 fallback:118Umbreon/180Espeon',motion_selection='Rawflags78 =>bits3/4/5/6 explicitly enable ordered3/0/1/2' if red else 'Rawflags0 =>CountSheep0 fallback enables ordered3/0/1/2',fov='Derivedflags0, Dollydirectmask2; othermotions weightedshort8 initialonly',target_part='WZX targetclass2 ->ownerentry0 body_map2 ->42Umbreon/62Espeon',idle_reset='Sequencecopy inserts runtimekind1/value0 at8C/90, originalmotion_type0 at94 and animindex0/1 at98. DD100 sets90normalization0; MotionStart takesDF070 frame0/rate.5, notDEF0C preserveidle.')
        if [a['timing'] for a in actors]!=[[118,0,0,0],[180,0,0,0]]:raise ValueError('Stat-down idle timing drift')
    (output/('attack_down_camera.json' if red else 'stat_down_camera.json' if stat else 'growl_camera.json' if growl else 'tailwhip_camera.json')).write_text(json.dumps(report,indent=2)+'\n');return report

def prepare_tailwhip(disc,parser,output):
    return prepare_single_camera(disc,parser,output,39)

def prepare_growl(disc,parser,output):
    return prepare_single_camera(disc,parser,output,45)

def prepare_attack_down(disc,parser,output):
    return prepare_single_camera(disc,parser,output,0x10027)

def prepare_stat_down(disc,parser,output):
    return prepare_single_camera(disc,parser,output,0x10029)

def encode_tackle(actors,patterns):
    """Separate C3KCv1: exact short8 followed by long13, no legacy changes."""
    if len(patterns)!=1596 or hashlib.sha1(patterns[:608]).hexdigest()!='cbded7a9ed874d8b68a9e20c3fe082d2c75def47' or hashlib.sha1(patterns[608:]).hexdigest()!='176a55770152bab923808f8f20af6a6066b4d794':raise ValueError('Tackle source FOV table drift')
    if len(actors)!=3 or actors[2]['timing'][0]!=[62,130,240,0] or actors[0]['timing'][1]!=[170,298,0,0] or actors[1]['timing'][1]!=[100,176,0,0]:raise ValueError('Tackle owner camera timing drift')
    profiles=[dict(flags=f,pattern=i,fov_mask=m,entry=e,owner_start=0 if e==2 else 22) for _,_,_,f,i,m,e in WZX]
    original=encode(profiles,actors)
    b=bytearray(original[:512]+patterns+original[512:])
    struct.pack_into('<4s3I',b,0,b'C3KC',1,len(b),33)
    for phase in range(2):struct.pack_into('<5I',b,64+phase*64,0x78,0xffffffff,0,2 if phase==0 else 8,0)
    for i in range(3):
        for phase in range(2):
            off=192+96*i+80+8*phase;struct.pack_into('<I',b,off,struct.unpack_from('<I',b,off)[0]+1596)
    b[480:500]=bytes(20);b[480:500]=hashlib.sha1(b).digest();return bytes(b)

def prepare_tackle(disc,parser,output):
    import prepare_field_geometry as geometry
    verify_disc(disc);check_output(output)
    for name,digest in SOURCE_FILES.items():
        path=ROOT/'vendor/colosseum'/name
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise ValueError('Camera source drift: '+name)
    dol=Dol((disc/'sys/main.dol').read_bytes());resources=[]
    roots=[('wzx_taiatari_attack.fsys',0x072e2000,352,'88581c56940803646d4a15b1e83b56b3b9cdf56d22308e870297d55b9da291b4','5754c2ac8e4e3caef0bad6486d7cb6a06f91869daae4e2e336a969fb65318439',2),('wzx_taiatari_damage.fsys',0x03a42000,67008,'181cc398a3dddde92b3b60eccc1b7d904eef2cb2479d9f2c54aad292892974cc','c144e8784d610c38960b67b5dcb4c1776ff25ec0195df0e0b3c4b2ea06951f97',8)]
    for name,identity,size,digest,member_sha,entry in roots:
        path=disc/'files'/name
        if path.is_symlink() or path.stat().st_size!=size or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise ValueError('Tackle camera archive drift')
        data=member(path.read_bytes(),identity)
        if hashlib.sha256(data).hexdigest()!=member_sha:raise ValueError('Tackle camera member drift')
        profile(data,0x78,0xffffffff,0,entry,3)
        resources.append(dict(archive=name,member=identity,archive_sha256=digest,member_sha256=member_sha))
    for address,value in ((0x8047E348,.5),(0x8047E3C8,.5)):
        if struct.unpack('>f',dol.read(address,4))[0]!=value:raise ValueError('Tackle source motion rate drift')
    for address,word in ((0x801D3820,0x80070000),(0x801D382C,0x83A30004),(0x801D3A50,0x80070000),(0x801D3A5C,0x83A30004)):
        if struct.unpack('>I',dol.read(address,4))[0]!=word:raise ValueError('Tackle source duration instruction drift')
    geometry.import_parser(parser);actors=[]
    for species,name,identity in ACTORS:
        archive=admit(disc/'files'/name);pkx=member(archive,identity)
        selector=struct.unpack_from('>i',pkx,12)[0]
        dat,raw,scene,header=load_pkx(archive,name)
        model=geometry.Model(scene.models[0],raw.models[0],allow_image_swaps=True)
        idle=header.anim_entries[0].sub_anims[0].anim_index
        meshes=[geometry.build_mesh(model,i) for i in range(len(model.meshes))]
        lo,hi=source_bounds(model,meshes,idle)
        entries=[header.anim_entries[i] for i in (2,8)]
        # fn801DC5F0 uses WZX header[3]=2 for sequencebyte0x17;
        # battleCameraStartWaza indexes the owning PKX body_map[2]. Original
        # JObj IDs are baked into independent immutable source60Hz tracks,
        # never reinterpreted as C3T envelope indices. GSmodel anim rate=.5.
        tracks=[]
        for e in entries:
            clip=e.sub_anims[0].anim_index;part=e.body_map_bones[2]
            if not 0<=part<len(model.joints):raise ValueError('Source camera body-map JObj outside model')
            frames=geometry.sample_sequence(model.ends[clip])
            tracks.append([list(map(float,model.world(clip,frame)[0][part,:3,3])) for frame in frames])
        a=dict(clips=[e.sub_anims[0].anim_index for e in entries],motion_types=[e.sub_anims[0].motion_type for e in entries],species=species,selector=selector,minimum=lo,maximum=hi,timing=[[round(t*60) for t in e.timing] for e in entries],part=[e.body_map_bones[2] for e in entries],kind=[e.anim_type for e in entries],archive=name,member=identity,member_sha256=hashlib.sha256(pkx).hexdigest(),idle_clip=idle,tracks=tracks,camera_part_qualification='OriginalJObj body_map[2] sampled at .5animationframe per original60Hz tick')
        if any(e.sub_anim_count!=1 for e in entries):raise ValueError('Unsupported camera owner multiple subanimations')
        actors.append(a)
    if actors[2]['clips'][0]!=4 or [a['clips'][1] for a in actors[:2]]!=[3,2] or any(any(a['motion_types']) for a in actors):raise ValueError('Tackle source clip/start mode drift')
    patterns=struct.pack('<399I',*struct.unpack('>399I',dol.read(0x80371f60,608)+dol.read(0x803721c0,988)))
    descriptor=encode_tackle(actors,patterns)
    output.mkdir(parents=True,exist_ok=False);(output/'tackle_camera.c3c').write_bytes(descriptor)
    report=dict(source_commit=SOURCE_COMMIT,source_files=SOURCE_FILES,move=33,resources=resources,actors=actors,descriptor_sha256=hashlib.sha256(descriptor).hexdigest(),owner_start=[0,0],motion_choices=[3,0,1,2],raw_flags=0x78,params_flags=0,normalization_shift=0,attack_duration=130,damage_duration={'197':170,'196':100},fov='Dolly mask2; other motions weighted short8 attack / long13 damage, two dynamic segments at implicit0 then timing0/1',source_pattern_addresses=['0x80371f60','0x803721c0'],pattern_sha256=hashlib.sha256(patterns).hexdigest(),scope='Explicit bounded Tackle camera profile; attacker Zig288 entry2 clip4, defender Umbreon197 entry8 clip3 or Espeon196 entry8 clip2; source bodymap2 tracks at0/.5',limitations='Independent cosmetic RNG, no original global RNG or PPC float/frame parity; original hardware visual comparison remains required')
    (output/'tackle_camera.json').write_text(json.dumps(report,indent=2)+'\n');return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--disc',type=Path,required=True);p.add_argument('--parser',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--confusion',action='store_true');p.add_argument('--tailwhip',action='store_true');p.add_argument('--growl',action='store_true');p.add_argument('--stat-down',action='store_true');p.add_argument('--attack-down',action='store_true');p.add_argument('--tackle',action='store_true');a=p.parse_args();
    if sum((a.confusion,a.tailwhip,a.growl,a.stat_down,a.attack_down,a.tackle))>1:p.error('Choose one exact camera profile')
    print(json.dumps(prepare_tackle(a.disc,a.parser,a.output) if a.tackle else prepare_attack_down(a.disc,a.parser,a.output) if a.attack_down else prepare_stat_down(a.disc,a.parser,a.output) if a.stat_down else prepare_growl(a.disc,a.parser,a.output) if a.growl else prepare_tailwhip(a.disc,a.parser,a.output) if a.tailwhip else prepare(a.disc,a.parser,a.output,a.confusion),indent=2))
