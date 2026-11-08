#!/usr/bin/env python3
"""Prepare the bounded original Tackle descriptor and particles privately.

No HSD/model packs are generated. Body-map2 refers to original source parts,
never compact native bone indices. Camera/audio packs are prepared separately.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
from prepare_boot_media import verify_disc
from prepare_model import check_output
from prepare_outskirt import member
from prepare_battle_scene import admit, load_pkx
from prepare_wzx_bite import link_times
from prepare_game_state import Dol, SOURCE_COMMIT
import prepare_particle as particle

ROOT=Path(__file__).resolve().parents[1]
BYTES=1024
ATTACK_SHA='5754c2ac8e4e3caef0bad6486d7cb6a06f91869daae4e2e336a969fb65318439'
DAMAGE_SHA='c144e8784d610c38960b67b5dcb4c1776ff25ec0195df0e0b3c4b2ea06951f97'
BANK_SHA='40269fb5a7111267999a9e9d9fd72f18c24a4b54a9b452267c782b7123f8bdef'
PACK_SHA1='34e7da926099fcdcfdee31a56d0be2e354f9ad27'
RESOURCES=(('wzx_taiatari_attack.fsys',0x072E2000,352,'88581c56940803646d4a15b1e83b56b3b9cdf56d22308e870297d55b9da291b4',408,ATTACK_SHA),
 ('wzx_taiatari_damage.fsys',0x03A42000,67008,'181cc398a3dddde92b3b60eccc1b7d904eef2cb2479d9f2c54aad292892974cc',92092,DAMAGE_SHA))
OWNERS=((197,0,'pkx_blacky.fsys',0x03381E00,8,3,149,170,298,42),
        (196,1,'pkx_eifie.fsys',0x03B71E00,8,2,88,100,176,62))

def node_words(index):
    words=[0]*28
    words[0]=(index&1)+1;words[1]=(1,5,3,5)[index]
    words[2]=2 if index==2 else 0;words[3]=1 if index==2 else 0
    words[4]=index&1;words[6]=2 if index==0 else 0
    words[7]=words[26]=2;words[24]=1 if index&1 else 2 if index==2 else 0
    return words

def parse(attack,damage):
    nodes=[]
    for phase,data,header,offsets,payloads,owner in (
      ('attack',attack,(2,3,120,2,5,0),(160,284),((0,3,3),(0x4A2,3,0)),[0,62,130,240]),
      ('damage',damage,(8,3,120,2,5,0),(160,91968),((0,1,91680,4),(0xDE,3,0)),[0,170,298])):
        if len(data)!=(408 if phase=='attack' else 92092) or particle.word(data,104)!=2 or struct.unpack_from('>6I',data,112)!=header or any(data[136:160]):
            raise ValueError('Tackle WZX header/extent drift')
        entries=[]
        for offset,payload in zip(offsets,payloads):
            index=len(nodes);words=list(struct.unpack_from('>28I',data,offset))
            if words!=node_words(index) or struct.unpack_from('>%dI'%len(payload),data,offset+112)!=payload:
                raise ValueError('Tackle typed node/payload drift')
            n=dict(key=words[0],kind=words[1],attachment=words[2],position_type=words[3],link=words[4],source=words[5],target=words[6],timing_index=words[7],timing=words[8:24],flags=words[24],part=words[25],state=words[27],words=words,payload=list(payload),phase=phase)
            entries.append(n);nodes.append(n)
        if link_times(entries,owner)!=0 or [n['start'] for n in entries]!=([130,130] if phase=='attack' else [0,0]):
            raise ValueError('Tackle source node links changed')
    bank=damage[288:91968]
    if hashlib.sha256(bank).hexdigest()!=BANK_SHA:raise ValueError('Tackle original GPT1 hash drift')
    return nodes,bank

def encode(nodes,owners,caster,pack):
    if hashlib.sha1(pack).hexdigest()!=PACK_SHA1:raise ValueError('Tackle converted particle identity changed')
    out=bytearray(BYTES)
    struct.pack_into('<4s7I',out,0,b'C3TK',1,BYTES,33,130,133,0x4A2,0xDE)
    out[32:52]=bytes.fromhex(SOURCE_COMMIT);out[52:84]=bytes.fromhex(ATTACK_SHA)
    out[84:116]=bytes.fromhex(DAMAGE_SHA);out[116:148]=bytes.fromhex(BANK_SHA)
    out[148:168]=hashlib.sha1(pack).digest()
    struct.pack_into('<10I2f',out,168,2,0,91680,0,60,2,4,120,240,53,.5,.75)
    for i,o in enumerate(owners):
        at=256+i*96
        struct.pack_into('<8I',out,at,o['species'],o['slot'],o['owner_clip'],o['owner_normal'],o['owner_minimum'],o['body_part'],o['owner_end'],8)
        out[at+32:at+52]=bytes.fromhex(o['pkx_sha1']);out[at+52:at+72]=bytes.fromhex(o['hsd_sha1'])
        struct.pack_into('<3f',out,at+72,*o['anchor'])
    out[448:468]=bytes.fromhex(caster['pkx_sha1']);out[468:488]=bytes.fromhex(caster['hsd_sha1']);struct.pack_into('<I',out,488,1)
    for i,n in enumerate(nodes):struct.pack_into('<28I',out,512+i*112,*n['words'])
    out[1004:1024]=hashlib.sha1(out[:1004]).digest()
    return bytes(out)

def prepare(disc,parser,output):
    import prepare_field_geometry as geometry
    import prepare_title as title
    disc=verify_disc(Path(disc));output=check_output(Path(output))
    sources=json.loads((ROOT/'config/wzx-tackle-source-files.json').read_text())
    if sources['commit']!=SOURCE_COMMIT:raise ValueError('Tackle source commit drift')
    for name,digest in sources['files'].items():
        path=ROOT/'vendor/colosseum'/name
        if path.is_symlink() or hashlib.sha256(path.read_bytes()).hexdigest()!=digest:raise ValueError('Tackle source drift: '+name)
    dol=Dol((disc/'sys/main.dol').read_bytes())
    for proof in sources['dol_slices']:
        if hashlib.sha256(dol.read(proof['address'],proof['bytes'])).hexdigest()!=proof['sha256']:raise ValueError('Tackle DOL dispatch/lifecycle drift')
    data=[]
    for name,identity,size,archive_hash,member_size,member_hash in RESOURCES:
        path=disc/'files'/name
        if path.is_symlink() or path.stat().st_size!=size:raise ValueError('Unsafe Tackle archive')
        archive=path.read_bytes();raw=member(archive,identity)
        if hashlib.sha256(archive).hexdigest()!=archive_hash or len(raw)!=member_size or hashlib.sha256(raw).hexdigest()!=member_hash:raise ValueError('Tackle original resource drift')
        data.append(raw)
    nodes,raw_bank=parse(*data)
    bank_report=particle.bank(raw_bank,0,profile='tackle');pack=particle.encode(raw_bank,bank_report,DAMAGE_SHA)
    geometry.import_parser(title.verify_parser(Path(parser)))
    owners=[];caster=None
    for species,slot,name,identity,entry,clip,end,normal,minimum,part in (*OWNERS,(288,2,'pkx_ziguzaguma.fsys',0x04C51E00,2,4,120,130,240,53)):
        archive=admit(disc/'files'/name);dat,raw,scene,header=load_pkx(archive,name)
        e=header.anim_entries[entry];model=geometry.Model(scene.models[0],raw.models[0],allow_image_swaps=True)
        ticks=[round(t*60) for t in e.timing]
        expected=[normal,minimum,0,0] if species!=288 else [62,130,240,0]
        if e.anim_type!=(4 if species==288 else 3) or e.sub_anim_count!=1 or e.sub_anims[0].anim_index!=clip or e.sub_anims[0].motion_type!=0 or model.ends[clip]!=end or ticks!=expected or e.body_map_bones[2]!=part:raise ValueError('Tackle original actor profile drift')
        # Model.world uses the complete original joint traversal, before compact
        # native pack pruning; bodymap2 therefore indexes this original tree.
        matrix=model.world(clip,0)[0][part]
        o=dict(species=species,slot=slot,entry=entry,owner_clip=clip,owner_end=end,owner_normal=normal,owner_minimum=minimum,body_part=part,anchor=[float(matrix[i,3]) for i in range(3)],pkx_sha1=hashlib.sha1(member(archive,identity)).hexdigest(),hsd_sha1=hashlib.sha1(dat).hexdigest(),raw_timing=[0,*ticks[:3 if species==288 else 2]])
        if species==288:caster=o
        else:owners.append(o)
    descriptor=encode(nodes,owners,caster,pack)
    output.mkdir(parents=True,exist_ok=True)
    outputs={'tackle.c3w':descriptor,'tackle.c3p':pack,'tackle.gpt':raw_bank}
    for name,payload in outputs.items():(output/name).write_bytes(payload)
    report=dict(source_commit=SOURCE_COMMIT,source_files=sources,nodes=nodes,owners=owners,caster=caster,particles=particle.report_json(bank_report),
      attack=dict(control_start=130,stop=133,sound=0x4A2),damage=dict(start=0,sound=0xDE,hp_gate=0,bodymap=2,flags=2,bank_flags=1),
      completion='B988 type2 minimum298U/176E plus pending PS and direct SE. DA9E8 clears owner16/17. External early branch only !owner16 && owner17 && type1normal<state; normal170U/100E is not terminal. Camera durations independently normal170/100. ApplyStop kills whole owned bank family and keyoffs SE; no DSP silence claim.',
      reset='Successful defender mode2 start DBCCC calls DB864 ResetAnimationExcept before DD100/MotionStart; request idle on other active owners unless owner16/flags18bit8 guard rejects. Caster authoredend240 does not continue to240.',
      miss='Special57 bypass is separate and unsupported; no defender bank/DE/reset request on miss.',
      outputs=[dict(file=n,bytes=len(v),sha1=hashlib.sha1(v).hexdigest(),sha256=hashlib.sha256(v).hexdigest()) for n,v in outputs.items()])
    (output/'tackle.json').write_text(json.dumps(report,indent=2)+'\n')
    return report

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disc',type=Path,required=True);p.add_argument('--parser',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();print(json.dumps(prepare(a.disc,a.parser,a.output)['outputs'],indent=2))
