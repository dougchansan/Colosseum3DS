#!/usr/bin/env python3
"""Prepare source Bite procedural camera profiles and original actor bounds.

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
def profile(data,flags,pattern,mask,entry):
    if len(data)<160 or len(data)>1024*1024:raise ValueError('Truncated/oversized camera WZX')
    kind,version,actual,target_class,unknown,target_part=struct.unpack_from('>6I',data,112)
    if (kind,version,actual,target_class,unknown,target_part)!=(entry,5,flags,2,5,0):raise ValueError('Unsupported Bite camera WZX profile')
    if any(data[136:160]):raise ValueError('Unexpected external camera bank/track; resolve explicitly')
    return dict(flags=flags,pattern=pattern,fov_mask=mask,entry=entry,target_body_map_index=2,owner_start=0 if entry==2 else 22)
def encode(profiles,actors):
    if len(profiles)!=2 or len(actors)!=3:raise ValueError('Camera descriptor count bound')
    b=bytearray(512);struct.pack_into('<4s6I',b,0,b'C3BC',3,512,44,60,3,2);b[32:52]=bytes.fromhex(SOURCE_COMMIT)
    for i,p in enumerate(profiles):
        if (p['flags'],p['pattern'],p['fov_mask'],p['entry'])!=((0x201048,6,4,2),(0x200848,5,2,8))[i]:raise ValueError('Unsupported procedural camera profile')
        if p['owner_start']!=(0,22)[i]:raise ValueError('Unqualified camera owner animation normalization')
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
    struct.pack_into('<2I',b,8,len(b),44);struct.pack_into('<I',b,28,count)
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
def prepare(disc,parser,output):
    import prepare_field_geometry as geometry
    verify_disc(disc);check_output(output)
    for name,digest in SOURCE_FILES.items():
        p=ROOT/'vendor/colosseum'/name
        if p.is_symlink() or hashlib.sha256(p.read_bytes()).hexdigest()!=digest:raise ValueError('Camera source drift: '+name)
    geometry.import_parser(parser)
    dol=Dol((disc/'sys/main.dol').read_bytes());profiles=[];resources=[];damage_data=None
    for address in (0x8047E348,0x8047E3C8):
        if struct.unpack('>f',dol.read(address,4))[0]!=.5:raise ValueError('Unqualified source owner motion rate')
    for name,identity,digest,flags,index,mask,entry in WZX:
        path=disc/'files'/name
        expected=(25632,'3a5a0667e61bed6508a0bff68cec95316a08804288b82f80a1761c7afa4ce193') if entry==2 else (117856,'9626a36b5b609302940e8c74a147d0e590fd0b08c06726999a0489ce98dc41ef')
        if path.is_symlink() or path.stat().st_size!=expected[0]:raise ValueError('Unsafe camera WZX archive')
        archive=path.read_bytes()
        if hashlib.sha256(archive).hexdigest()!=expected[1]:raise ValueError('Camera WZX archive hash mismatch')
        data=member(archive,identity);actual=hashlib.sha256(data).hexdigest()
        if digest and actual!=digest:raise ValueError('Camera WZX member hash mismatch')
        p=profile(data,flags,index,mask,entry)
        if entry==8:damage_data=data
        # GetPattern uses short8entry table; flags0x40/0x80 select5/6.
        table=dol.read(0x80371F60+index*76,76);words=struct.unpack('>19I',table)
        if words[1]!=1 or words[7]!=mask or any(words[k] for k in range(2,19) if k!=7):raise ValueError('Source Bite FOV pattern changed')
        profiles.append(p);resources.append(dict(archive=name,member=identity,member_sha256=actual,source_pattern_address=hex(0x80371F60+index*76),pattern_sha256=hashlib.sha256(table).hexdigest()))
    actors=[]
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
        a=dict(species=species,selector=selector,minimum=lo,maximum=hi,timing=[[round(t*60) for t in e.timing] for e in entries],part=[e.body_map_bones[2] for e in entries],kind=[e.anim_type for e in entries],archive=name,member=identity,member_sha256=hashlib.sha256(pkx).hexdigest(),idle_clip=idle,tracks=tracks,camera_part_qualification='OriginalJObj body_map[2] sampled at .5animationframe per original60Hz tick')
        if any(e.sub_anim_count!=1 for e in entries):raise ValueError('Unsupported camera owner multiple subanimations')
        actors.append(a)
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
    descriptor=encode(profiles,actors);output.mkdir(parents=True,exist_ok=False);(output/'bite_camera.c3c').write_bytes(descriptor)
    report=dict(source_commit=SOURCE_COMMIT,source_files=SOURCE_FILES,resources=resources,actors=actors,descriptor_sha256=hashlib.sha256(descriptor).hexdigest(),normalization=instruction_profile,owner_start=[0,22],scope='Source procedural Bite attack/damage motion2/3 and short-pattern5/6 FOV; separate cosmetic stream, not original shared RNG parity',projection='Retain original inherited near/far/aspect; source computed FOV; native unit conversion0.1',timing='Source60ticks; action duration timing1, damage timing0; owneranimationactive1 shift0; target-offset move0.2seconds; damage actortrack holds tick0..22 then(.5frame/tick), camera motion duration stays190ticks forZig (DOLcount3+4)',camera_target='fn801DC5F0 header[3]2 ->body_map[2] ->originalJObj42/62/53; independent source .5frame/tick localtracks',limitations='Default variant0 only; NumPy/native scalar arithmetic is not PPC bit parity; original shared cosmetic RNG differs; source visual comparison remains required')
    (output/'bite_camera.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--disc',type=Path,required=True);p.add_argument('--parser',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();print(json.dumps(prepare(a.disc,a.parser,a.output),indent=2))
