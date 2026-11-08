#!/usr/bin/env python3
"""Prepare the admitted original Growl model, sparse-ID particle bank and links.

This does not implement Growl's native application-SRT renderer or stage.
Original assets and converted packs are private.
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
from prepare_outskirt import member
from prepare_battle_scene import admit,load_pkx
from prepare_wzx_bite import link_times
from prepare_wzx_tail_whip import read_model
import prepare_particle as particle

ROOT=Path(__file__).resolve().parents[1]
ARCHIVE='wzx_nakigoe_attack.fsys'
ARCHIVE_SHA256='3f171e90d3725c2e9172f980ee0e5ae234ce00dbf9e6ab4cf251df676ae63af8'
MEMBER=0x07352000
MEMBER_SHA256='85862ce50519fc694b179761e5038bab74f4bb2ce9c15b7102a0b0937ef8f82e'
MODEL_SHA256='c2c07e0d6cc2a93505386592dd416f0bf3730c926ec49bb31fc4600610019ac8'
BANK_SHA256='64867627ce7029307e3064e1af883cafb52995b7e2834980d7722d328ef043da'
OFFSETS=(0xA0,0xFEA0,0x69400,0x6947C,0x694F8)
FLOOR=[0,120,2,1,3,3,0,0x80808080,0x802D2D2D,20,1,0x802D2D2D,0x802D2D2D,50,1,0x802D2D2D,0x80808080,50,1]
PAYLOADS=([0,0,0,0,0,0,1,64851],[18,0x10A1,365764,4],[0x210,3,0],[0,100,3],FLOOR)


def verify_source(source_root,dol):
    p=json.loads((ROOT/'config/wzx-growl-source-files.json').read_text())
    if p['commit']!=SOURCE_COMMIT:raise ValueError('Growl source commit drift')
    for name,digest in p['files'].items():
        path=Path(source_root)/name
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:
            raise ValueError('Growl source drift: '+name)
    for entry in p['dol_slices']:
        if hashlib.sha256(dol.read(entry['address'],entry['bytes'])).hexdigest()!=entry['sha256']:
            raise ValueError('Growl DOL timing/update instruction drift')
    return p


def parse(data):
    if len(data)!=431540 or struct.unpack_from('>I',data,104)[0]!=2 or struct.unpack_from('>6I',data,112)!=(1,6,0x200858,2,5,0) or any(data[136:160]):
        raise ValueError('Growl root/header/external-camera profile changed')
    nodes=[]
    for i,(offset,kind,selector,pos,link,source,target,flags,part) in enumerate(zip(OFFSETS,(2,3,5,1,4),(1,0,0,0,0),(1,1,0,0,0),(0,1,1,0,0),(0,0,0,0,1),(1,0,0,1,1),(2,3,1,0,0),(0,9,0,0,0))):
        timing=[0]*16
        if i==4:timing[1:3]=[40,220]
        expected=[i+1,kind,selector,pos,link,source,target,3 if i==4 else 2,*timing,flags,part,2,0]
        if list(struct.unpack_from('>28i',data,offset))!=expected:
            raise ValueError('Growl typed node profile changed at %x' % offset)
        payload=list(struct.unpack_from('>%dI'%len(PAYLOADS[i]),data,offset+112))
        if payload!=PAYLOADS[i]:raise ValueError('Growl source payload changed')
        nodes.append(dict(key=i+1,kind=kind,attachment=selector,position_type=pos,link=link,source=source,target=target,timing_index=expected[7],timing=timing,flags=flags,part=part,state=0,offset=offset,resource_words=payload))
    nodes[0]['timing'][1]=39  # source model loader's zero-slot fallback
    if link_times(nodes,[0,130,244,366]) or [n['start'] for n in nodes]!=[130,130,130,130,90]:
        raise ValueError('Growl source implicit-zero/link timing changed')
    if any(data[0x693E4:0x69400]):raise ValueError('Growl GPT1 alignment padding changed')
    return nodes


def encode(nodes,model,particles,owner_sha1,retained_bone,owner_anchor,emitter_anchor):
    if not 0<=retained_bone<64 or any(len(p)!=3 or any(not math.isfinite(v) or abs(v)>1000 for v in p) for p in (owner_anchor,emitter_anchor)):
        raise ValueError('Growl source attachment bounds')
    if len(nodes)!=5:raise ValueError('Growl node count')
    out=bytearray(1280)
    struct.pack_into('<4s7I',out,0,b'C3WG',1,1280,45,288,1,1,60)
    out[32:52]=bytes.fromhex(SOURCE_COMMIT)
    out[52:84]=bytes.fromhex(MEMBER_SHA256)
    out[84:104]=hashlib.sha1(model).digest()
    out[104:124]=hashlib.sha1(particles).digest()
    out[124:144]=bytes.fromhex(owner_sha1)
    out[144:176]=bytes.fromhex(BANK_SHA256)
    struct.pack_into('<2f4I6f8I',out,176,.5,.75,58,9,retained_bone,0,*owner_anchor,*emitter_anchor,244,366,230,5,320,39,183,79)
    struct.pack_into('<8I',out,256,90,120,130,130,130,130,100,18)
    for i,n in enumerate(nodes):
        expected=(i+1,(2,3,5,1,4)[i],(130,130,130,130,90)[i],(1,0,0,0,0)[i],(1,1,0,0,0)[i],(0,1,1,0,0)[i],(0,0,0,0,1)[i],(1,0,0,1,1)[i],3 if i==4 else 2,(2,3,1,0,0)[i],(0,9,0,0,0)[i],0)
        values=tuple(n[k] for k in ('key','kind','start','attachment','position_type','link','source','target','timing_index','flags','part','state'))
        timing=[0]*16
        if i==0:timing[1]=39
        if i==4:timing[1:3]=[40,220]
        if values!=expected or n['timing']!=timing or n['resource_words']!=PAYLOADS[i] or n['offset']!=OFFSETS[i]:
            raise ValueError('Unqualified Growl descriptor node/timing/payload')
        words=[*values,*timing,*n['resource_words'],*([0]*(19-len(n['resource_words']))),0]
        struct.pack_into('<48I',out,320+i*192,*words)
    out[300:320]=hashlib.sha1(out).digest()
    return bytes(out)


def prepare(disc,parser,output,source_root=None):
    import prepare_field_geometry as geometry
    import prepare_title as title
    disc=verify_disc(Path(disc));output=check_output(Path(output))
    dol=Dol((disc/'sys/main.dol').read_bytes())
    sources=verify_source(source_root or ROOT/'vendor/colosseum',dol)
    parser=title.verify_parser(parser);geometry.import_parser(parser)
    path=disc/'files'/ARCHIVE
    if path.is_symlink() or path.stat().st_size!=248960:raise ValueError('Unsafe Growl archive')
    archive=path.read_bytes()
    if hashlib.sha256(archive).hexdigest()!=ARCHIVE_SHA256:raise ValueError('Growl archive hash mismatch')
    data=member(archive,MEMBER)
    if hashlib.sha256(data).hexdigest()!=MEMBER_SHA256:raise ValueError('Growl WZX hash mismatch')
    nodes=parse(data);dat=data[0x140:0x140+64851];bank=data[0xFF20:0x693E4]
    if hashlib.sha256(dat).hexdigest()!=MODEL_SHA256 or hashlib.sha256(bank).hexdigest()!=BANK_SHA256:
        raise ValueError('Growl embedded dependency hash mismatch')
    model,raw=read_model(dat,geometry)
    if (model.ends,len(model.joints),len(model.meshes),len(model.materials))!=([39.],25,22,1):
        raise ValueError('Growl original HSD topology/clip changed')
    meshes=[geometry.build_mesh(model,i) for i in range(len(model.meshes))]
    if any(m['batch'][3]&(title.LIT|title.SPECULAR) for m in meshes):raise ValueError('Growl requires unknown effect lighting')
    owner_dat,owner_raw,owner_scene,header=load_pkx(admit(disc/'files/pkx_ziguzaguma.fsys'),'pkx_ziguzaguma.fsys')
    owner=geometry.Model(owner_scene.models[0],owner_raw.models[0],allow_image_swaps=True)
    entry=header.anim_entries[1]
    if entry.anim_type!=4 or entry.sub_anim_count!=1 or entry.sub_anims[0].anim_index!=1 or owner.ends[1]!=183 or [round(v*60) for v in entry.timing]!=[130,244,366,0] or entry.body_map_bones[1]!=58:
        raise ValueError('Growl owner source entry/clip/timing changed')
    owner_anchor=list(map(float,owner.world(1,130*.5)[0][58,:3,3]))
    emitter_anchor=list(map(float,model.world(0,0)[0][9,:3,3]))
    if struct.unpack('>2I',dol.read(0x8036E150+45*28+4,8))!=(0x3AB,MEMBER) or struct.unpack('>2I',dol.read(0x8035C430+45*20+12,8))!=(0x8020FAC0,0x8020FB30):
        raise ValueError('Growl resource/dispatcher binding drift')
    for at,value in ((0x8047E348,.5),(0x8047E3C8,.5)):
        if struct.unpack('>f',dol.read(at,4))[0]!=value:raise ValueError('Growl source animation rate drift')
    closure=particle.bank(bank,18,profile='growl')
    particle_pack=particle.encode(bank,closure,MEMBER_SHA256)
    frames=geometry.sample_sequence(39);camera=(0.,2.,10.,0.,0.,0.,0.,45.,.1,1000.,4/3,0.)
    output.mkdir(parents=True,exist_ok=False)
    packs=geometry.prepare_pack_set(meshes,[(0,frames,False)],120,[camera]*len(frames),geometry.fixed_lights(raw,dat),ARCHIVE_SHA256,output,'growl_model',expected_count=1,format_version=3,retained_parts=((model,9,9),))
    model_pack=(output/'growl_model.c3t').read_bytes();retained=packs[0]['retained_parts']
    if len(retained)!=1 or retained[0]['source_part']!=9:raise ValueError('Growl original retained part changed')
    packed=encode(nodes,model_pack,particle_pack,hashlib.sha1(owner_dat).hexdigest(),retained[0]['sampled_bone'],owner_anchor,emitter_anchor)
    outputs={'growl.c3w':packed,'growl.c3p':particle_pack,'growl.gpt':bank}
    for name,payload in outputs.items():(output/name).write_bytes(payload)
    outputs['growl_model.c3t']=model_pack
    report=dict(source_commit=SOURCE_COMMIT,source_files=sources,archive=ARCHIVE,group=0x3AB,member=MEMBER,archive_sha256=ARCHIVE_SHA256,member_sha256=MEMBER_SHA256,nodes=nodes,packs=packs,
        owner=dict(species=288,entry=1,clip=1,authored_end=183,initial_frame=0,rate=.5,raw_timing=[0,130,244,366],normal_end=244,minimum_end=366,source_part=58,anchor_tick=130,anchor_frame=65,anchor_native=owner_anchor,extracted_hsd_sha1=hashlib.sha1(owner_dat).hexdigest()),
        timing=dict(normalization=0,starts=[130,130,130,130,90],floor_duration=120,control_delay=100,stop_threshold=230,qualification='SysGetWazaTime801DB3F8 mode1/nonzero non-hit point: timeType1 reads raw+14=244, type2 reads raw+18=366, plus field10=0. EntryUpdate801D81CC control0 crosses130+100, sets stopping1 or-1 for state3. SequenceUpdate801DB988 returns stopping status even when owner minimum366 remains pending. No invented366-tick terminal wait.'),
        model=dict(offset=0x140,bytes=64851,sha256=MODEL_SHA256,end=39,samples=79,rate=.5,source_part=9,retained_sampled_bone=retained[0]['sampled_bone'],emitter_anchor_native=emitter_anchor),
        attachment=dict(model='Zigzagoon entry1 bodymap1/sourcepart58, POSITION-only one-shot; retain owner base yaw and class scale.75.',particle='Linked original effect-model part9, POSITION-only one-shot; not actorbone9. Rawkind20000 in93/111 requires original PSAppSRT family/render behavior; retained basis includes scale but POSITION alone inherits only translation.',composition='POSITION-only uses retained part9 translation, not its rotation/nonuniform basis. Effectroot is actorroot applied to body58 anchor; emitter adds effect base-yaw/class.75 applied to part9 localnative translation. Raw particles remain GCunits for separate AppSRT adapter; do not scale native translation twice.'),
        stop=dict(scope='WholeWZX stop: ApplyStop801DBB10 ->EntryStopall ->fn80118874(bank,1) for state0 ->destroyFieldParticleInstance notify1 ->psKillFamily801695FC releases matchingfamily particles/AppSRT ->psKillGeneratorID. F0children inherit familyID; no survivingGrowlfamily required after this fullteardown.',sound='EntryStop ->fn80166B18 ->fn80167070(id,1) ->fn801669E4 ->_sndStopSE ->sndFXKeyOff; wrapperwork released. OriginalDSP keyoff/release differs from unqualified instantaneous dryPCMcut.',owner_start='Reserved track0 kind1/value0; fn801DD100 sets entry90=normalization0; MotionStart duration0 ->direct fn801DF070 clip1 explicitframe0/rate.5; normal timing244 is separate.'),
        particles=particle.report_json(closure),sound=dict(id=0x210,flags=3,start=130,fx=0x283,macro=0x39C,conversion='Separate selected dry-audio profile; original control sends keyoff before dry cue naturally ends; release phase is not an instantaneous-PCM-cut claim.'),
        camera=dict(entry=1,flags=0x200858,derived_flags=0x42,motions=[3,0,2],part=53,duration=244,external_bytes=0,qualification='Motion0 directmask2, motion2/3 forcedshort5mask2; separate camera opt-in and RNG qualification required.'),
        outputs=[dict(file=n,bytes=len(v),sha1=hashlib.sha1(v).hexdigest(),sha256=hashlib.sha256(v).hexdigest()) for n,v in outputs.items()],
        scope='Preparation only. One original HSD model, exact113-ID rawbank with4admittedgenerators/25textures, and typed source descriptor; not proof of native Growl execution or hardware presentation.',
        unsupported=['Native Growl sequence/stage execution and checked attachment lifecycle','Growl application-SRT particle family/raster semantics pending native foundation','Selected Growl PCM/voice-keyoff backend is prepared separately; original DSP release-tail parity remains unqualified','Growl camera profile integration and original visual comparison','Unused generator/texturegroup2 format6 is retained raw but neither executed nor rendered'])
    (output/'growl.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disc',type=Path,required=True);p.add_argument('--parser',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--source-root',type=Path)
    a=p.parse_args();print(json.dumps(prepare(a.disc,a.parser,a.output,a.source_root)['outputs'],indent=2))
