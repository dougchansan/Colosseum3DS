#!/usr/bin/env python3
"""Prepare only original GREEN special0x29 Defense-down resources (private).

No cast-root substitute, red profile, combat mutation or native playback is
implemented here. Original GPT1 bytes are never rewritten.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from prepare_boot_media import verify_disc
from prepare_game_state import Dol,SOURCE_COMMIT
from prepare_model import check_output
from prepare_outskirt import member,rel_data
from prepare_battle_init import COMMON_SHA1
from prepare_battle_scene import admit,load_pkx
import prepare_particle as particle

ROOT=Path(__file__).resolve().parents[1]
ARCHIVE='wzx_greendown_status.fsys'
ARCHIVE_SHA256='8947e10d57f4d17f426debe1ddb713c8856cf889f3d7cf6c5a8ced0e1b602dd8'
MEMBER=0x0B6F2000
MEMBER_SHA256='5e08695ee116a4a5aaded4deef4fd1af71e5f4178992e187610a7e92a63e7c1e'
BANK_SHA256='7f42a1c5a723ed7563e290ede2feb2b1828f90498553feaea96ec1e650bdefeb'
COLOR=[0,50,2,2,3,3,0,0x80808080,0x80437A3D,10,1,0x80437A3D,0x80437A3D,10,1,0x80437A3D,0x80808080,30,1]
PAYLOADS=([0,0xA1,1644,4],[0x19B,3,0],COLOR)
OFFSETS=(0xA0,0x7A0,0x81C)
OWNERS=((197,0,'pkx_blacky.fsys',0x03381E00,0,59,118,42),(196,1,'pkx_eifie.fsys',0x03B71E00,1,90,180,62))


def parse(data):
    if len(data)!=2264 or struct.unpack_from('>I',data,104)[0]!=2 or struct.unpack_from('>6I',data,112)!=(0,4,0,2,5,0) or any(data[136:160]):
        raise ValueError('Unsupported GREEN special29 header/extent')
    nodes=[]
    for i,(kind,link,flags) in enumerate(((3,0,2),(5,1,1),(4,0,1))):
        offset=OFFSETS[i];expected=[i+1,kind,0,1 if i==0 else 0,link,0,0,2]+[0]*16+[flags,0,2,0]
        if list(struct.unpack_from('>28i',data,offset))!=expected:
            raise ValueError('GREEN typed node/attachment/timing drift')
        payload=list(struct.unpack_from('>%dI'%len(PAYLOADS[i]),data,offset+112))
        if payload!=PAYLOADS[i]:raise ValueError('GREEN source payload drift')
        nodes.append(dict(key=i+1,kind=kind,start=0,attachment=0,position_type=1 if i==0 else 0,link=link,source=0,target=0,timing_index=2,flags=flags,part=0,state=0,timing=[0]*16,resource_words=payload))
    if any(data[0x78C:0x7A0]):raise ValueError('GREEN bank alignment padding drift')
    return nodes


def inspect_program(raw,index):
    """Reuse checked decoding around the one source ADDXYZ instruction.

    This changes only the inspection view, never the exported script/bank.
    Exact bank admission and fixed source instruction offsets precede it.
    """
    if index==0:return particle.program(raw)
    at=2 if index==1 else 3
    if index not in (1,2) or len(raw)<at+13 or raw[:at]!=(b'\x40\0' if index==1 else b'\xbc\0\1') or raw[at]!=0x8F or struct.unpack_from('>3f',raw,at+1)!=(0.,22.,0.):
        raise ValueError('GREEN ADDXYZ profile changed')
    instructions,children,frames=particle.program(raw[:at]+raw[at+13:])
    for instruction in instructions:
        if instruction['offset']>=at:instruction['offset']+=13
    instructions.append(dict(offset=at,opcode=0x8F,name='ADD_POSITION_XYZ',bytes=13,args={'values':[0.,22.,0.],'source_offset_rotation':False,'source_offset_transform_bit':8,'height_scaled':False}))
    instructions.sort(key=lambda x:x['offset'])
    return instructions,children,frames


def bank(data):
    if len(data)!=1644 or hashlib.sha256(data).hexdigest()!=BANK_SHA256:
        raise ValueError('GREEN exact original GPT1 binding mismatch')
    ptl,txg,tex,refs=struct.unpack_from('>4I',data,4)
    if data[:4]!=b'GPT1' or particle.word(data,ptl+8)!=3 or refs+12!=len(data) or txg+tex!=refs:
        raise ValueError('GREEN GPT1 section bounds')
    reference=[particle.word(data,refs+4*i) for i in range(3)]
    if reference!=[0,1,2]:raise ValueError('GREEN source reference table changed')
    offsets=[ptl+particle.word(data,ptl+12+4*i) for i in range(3)]
    generators=[]
    for i,at in enumerate(offsets):
        end=offsets[i+1] if i<2 else txg
        h=struct.unpack_from('>4HI12f',data,at)
        if (h[0],h[1],h[2],h[3],h[4])!=((0,0,1,1000,0x1000000),(7,0,32,16,0x400000),(7,1,28,26,0x400000))[i]:
            raise ValueError('GREEN source generator shape/lifetime/kind drift')
        raw=particle.extent(data,at+60,end-at-60)
        ops,children,poses=inspect_program(raw,i)
        if children!=([1,2] if i==0 else []):raise ValueError('GREEN transitive closure changed')
        generators.append(dict(index=i,bank_offset=at,bytes=end-at,type=h[0],texture_group=h[1],generator_life=h[2],particle_life=h[3],source_kind=h[4],params=list(h[5:]),instructions=ops,child_generators=children,texture_poses=poses,program_sha256=hashlib.sha256(raw).hexdigest()))
    textures=[]
    if particle.word(data,txg)!=2:raise ValueError('GREEN texture group count')
    for group in range(2):
        at=txg+particle.word(data,txg+4+4*group)
        count,fmt,palette,width,height,palnum=struct.unpack_from('>6I',data,at)
        if (count,fmt,palette,width,height,palnum)!=((1,1,0,32,32,0),(2,0,0,8,8,0))[group]:
            raise ValueError('GREEN source texture profile changed')
        for pose in range(count):
            offset=txg+particle.word(data,at+24+4*pose);size=width*height//(2 if fmt==0 else 1)
            pixels=particle.extent(data,offset,size)
            if offset+size>refs:raise ValueError('GREEN texture outside original section')
            rgba=(particle.decode_i4 if fmt==0 else particle.decode_i8)(pixels,width,height)
            textures.append(dict(group=group,pose=pose,width=width,height=height,source_format=fmt,bank_offset=offset,source_bytes=size,source_sha256=hashlib.sha256(pixels).hexdigest(),rgba_sha256=hashlib.sha256(rgba).hexdigest(),rgba=rgba))
    return dict(root_generator=0,root_generators=[0],bank_bytes=len(data),bank_sha256=BANK_SHA256,generator_count=3,references=reference,closure=[0,1,2],generators=generators,textures=textures)


def encode(nodes,owners,pack):
    if len(nodes)!=3 or len(owners)!=2:raise ValueError('GREEN descriptor count')
    b=bytearray(1024)
    struct.pack_into('<4s7I',b,0,b'C3SG',1,1024,0x29,4,2,2,3)
    b[32:52]=bytes.fromhex(SOURCE_COMMIT);b[52:84]=bytes.fromhex(MEMBER_SHA256);b[84:116]=bytes.fromhex(BANK_SHA256);b[116:136]=hashlib.sha1(pack).digest()
    struct.pack_into('<6I2f9I',b,136,0x19B,0,0,60,50,2,.75,.5,0,0x803763CE,448,256,1644,0,1,2,0xA1)
    for i,o in enumerate(owners):
        expected=OWNERS[i]
        if (o['species'],o['slot'],o['entry'],o['clip'],o['end'],o['gate'],o['root_part'],o['camera_part'],o['selector'])!=(expected[0],expected[1],0,expected[4],expected[5],expected[6],0,expected[7],-1) or o['root_translation']!=[0.,0.,0.]:
            raise ValueError('GREEN owner idle/source-root binding changed')
        if any(len(bytes.fromhex(o[k]))!=20 for k in ('hsd_sha1','pkx_sha1')):
            raise ValueError('GREEN owner source hash length')
        at=256+i*96
        struct.pack_into('<8I',b,at,o['species'],o['slot'],0,o['clip'],o['end'],o['gate'],0,o['camera_part'])
        b[at+32:at+52]=bytes.fromhex(o['hsd_sha1']);b[at+52:at+72]=bytes.fromhex(o['pkx_sha1'])
        struct.pack_into('<3fi',b,at+72,0.,0.,0.,-1)
    for i,n in enumerate(nodes):
        expected=(i+1,(3,5,4)[i],0,0,1 if i==0 else 0,(0,1,0)[i],0,0,2,(2,1,1)[i],0,0)
        v=tuple(n[k] for k in ('key','kind','start','attachment','position_type','link','source','target','timing_index','flags','part','state'))
        if v!=expected or n['timing']!=[0]*16 or n['resource_words']!=PAYLOADS[i]:raise ValueError('GREEN descriptor node mismatch')
        struct.pack_into('<48I',b,448+i*192,*v,*n['timing'],*n['resource_words'],*([0]*(19-len(n['resource_words']))),0)
    b[236:256]=hashlib.sha1(b).digest()
    return bytes(b)


def prepare(disc,parser,output):
    import prepare_field_geometry as geometry
    import prepare_title as title
    disc=verify_disc(Path(disc));output=check_output(Path(output))
    sources=json.loads((ROOT/'config/wzx-stat-down-source-files.json').read_text())
    if sources['commit']!=SOURCE_COMMIT:raise ValueError('GREEN source commit drift')
    for name,digest in sources['files'].items():
        p=ROOT/'vendor/colosseum'/name
        if p.is_symlink() or hashlib.sha256(p.read_bytes()).hexdigest()!=digest:raise ValueError('GREEN source drift: '+name)
    parser=title.verify_parser(parser);geometry.import_parser(parser)
    path=disc/'files'/ARCHIVE
    if path.is_symlink() or path.stat().st_size!=1376:raise ValueError('Unsafe GREEN archive')
    archive=path.read_bytes()
    if hashlib.sha256(archive).hexdigest()!=ARCHIVE_SHA256:raise ValueError('GREEN archive hash mismatch')
    data=member(archive,MEMBER)
    if hashlib.sha256(data).hexdigest()!=MEMBER_SHA256:raise ValueError('GREEN member hash mismatch')
    nodes=parse(data);raw=data[0x120:0x78C];closure=bank(raw);pack=particle.encode(raw,closure,MEMBER_SHA256)
    owners=[]
    for species,slot,name,identity,clip,end,gate,camera_part in OWNERS:
        arc=admit(disc/'files'/name);pkx=member(arc,identity)
        dat,r,scene,header=load_pkx(arc,name);entry=header.anim_entries[0]
        model=geometry.Model(scene.models[0],r.models[0],allow_image_swaps=True)
        if entry.anim_type!=2 or entry.sub_anim_count!=1 or entry.sub_anims[0].motion_type!=0 or entry.sub_anims[0].anim_index!=clip or model.ends[clip]!=end or [round(t*60) for t in entry.timing]!=[gate,0,0,0] or entry.body_map_bones[0]!=0 or entry.body_map_bones[2]!=camera_part or struct.unpack_from('>i',pkx,12)[0]!=-1:
            raise ValueError('GREEN original target idle/profile drift')
        matrix=model.world(clip,0)[0][0]
        if any(abs(float(matrix[y,x])-(1. if y==x else 0.))>1e-7 for y in range(4) for x in range(4)):
            raise ValueError('GREEN root-part zero anchor is not identity')
        owners.append(dict(species=species,slot=slot,entry=0,clip=clip,end=end,gate=gate,root_part=0,camera_part=camera_part,selector=-1,root_translation=[0.,0.,0.],hsd_sha1=hashlib.sha1(dat).hexdigest(),pkx_sha1=hashlib.sha1(pkx).hexdigest()))
    dol=Dol((disc/'sys/main.dol').read_bytes())
    if struct.unpack('>2I',dol.read(0x803727C8+0x29*12,8))!=(0x49C,MEMBER):raise ValueError('GREEN special-table binding drift')
    for at,value in ((0x8047E3C8,.5),(0x8047E3CC,0.)):
        if struct.unpack('>f',dol.read(at,4))[0]!=value:raise ValueError('GREEN idle start/rate drift')
    common=member((disc/'files/common.fsys').read_bytes(),0x01C71C00)
    if hashlib.sha1(common).hexdigest()!=COMMON_SHA1 or rel_data(common,125)[0x13FE08+0x19B*12:0x13FE08+0x19B*12+12]!=bytes.fromhex('8005010a020e000000000000'):
        raise ValueError('GREEN original sound request mapping drift')
    descriptor=encode(nodes,owners,pack)
    output.mkdir(parents=True,exist_ok=False)
    outputs={'stat_down.c3w':descriptor,'stat_down.c3p':pack,'stat_down.gpt':raw}
    for name,payload in outputs.items():(output/name).write_bytes(payload)
    report=dict(source_commit=SOURCE_COMMIT,source_files=sources,archive=ARCHIVE,group=0x49C,member=MEMBER,archive_sha256=ARCHIVE_SHA256,member_sha256=MEMBER_SHA256,owners=owners,nodes=nodes,particles=particle.report_json(closure),
        trigger='Only actual committed successful DEF decrease; sourcefn249B8→B910 minimum/failure earlyreturn suppresses sharedscript. No combat/save/RNG recalculation.',
        owner_start='Reservedtrack0kind1/value0 andnormalization0; firstoriginaltrackkind0 takes directfnDF070 clip0U/1E frame0/rate.5.',
        completion='DA9E8 resets owner16/17=0. Mode4point0 normal/minimum118U/180E; await ownergate,color50,PSnaturalfinish anddirectvoicefinish. No fixed50/118/180terminal ifresourcespending. Sourceearlyowner17 branch disabled absentexternalinterrupt.',
        teardown='Normalfullstop/cancel: prioractorcolor/camera restore, ownedbankstate0 KillFamily, SEkeyoff. Payloadselector3 maps runtimeFlags0/directfn66AB8; runningstatuses2/3, noGSthread. NaturalFINISHED and explicitSTOPPED are distinct; originalDSPreleasephase unqualified.',
        color=dict(duration=50,segments=[10,10,30],target='actor model',packed_to='80437A3D',program_rgb=[61,122,67],merge='Per-component clamp(previousRGB-128+programRGB), sourcefn8013B268; preserve/restore priorRGB.'),
        sound=dict(id=0x19B,payload_selector=3,runtime_flags=0,fx=0x20E,macro=0x22D,source_common_sha1=COMMON_SHA1,scope='Selected dry cue prepared separately; no PCM included'),
        camera=dict(special=0x29,entry=0,flags=0,body_map_index=2,external_bytes=0,scope='Separate special camera profile; no move-ID alias'),
        scope='GREENspecial29 preparation only; red27 explicitly unsupported. Original rawbank unchanged, no new HSD/model or actor/arena regeneration. Native playback/GPUpresentation require separate verification.',
        outputs=[dict(file=n,bytes=len(v),sha1=hashlib.sha1(v).hexdigest(),sha256=hashlib.sha256(v).hexdigest()) for n,v in outputs.items()])
    (output/'stat_down.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disc',type=Path,required=True);p.add_argument('--parser',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(prepare(a.disc,a.parser,a.output)['outputs'],indent=2))
