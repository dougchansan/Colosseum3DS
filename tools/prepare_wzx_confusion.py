#!/usr/bin/env python3
"""Qualify Confusion's actual type12 captured-scene indirect effect CPU slice.

The output is private. It does not substitute an ordinary billboard for the
original dependent-texture lookup, nor claim to implement POINT+TRAIL particles.
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
from prepare_battle_scene import admit,load_pkx,clip_mapping
from prepare_wzx_bite import link_times
ROOT=Path(__file__).resolve().parents[1]
ROOTS=[dict(name='wzx_nenriki_attack.fsys',member=0x07532000,bytes=72832,archive_sha256='ce7df34845f8cc5520242dec14538ee2c1c662e9b4217cc1002393eff3c0885d',member_sha256='c4c0c0c2a678a891ed71b92aa2212977a1e24adb985bc6a5c155fe2184b9f8bb',flags=0x200848,entry=1,nodes=[0xA0,0x11C,0x1521C,0x152C8,0x193E0],type12=0x152C8,texture=0x15360,owner=[0,130,210,250],starts=[130,44,44,44,44],shift=0,duration=90,part=62,radius=.15,pivot=0,indirect=.25,end=145),
 dict(name='wzx_nenriki_damage.fsys',member=0x05292000,bytes=72960,archive_sha256='b4bc2e6b2c5332cf2afc7a8cbfa26d86f9b37b69625be4d0de2143c64b1c3920',member_sha256='a1a0a4114c68a1b4c92d7952fb8bed60503b041f8eee85f033ef32f64d696d04',flags=0x48,entry=8,nodes=[0xA0,0x151A0,0x15220,0x152A0,0x1531C,0x1539C,0x15458,0x19580,0x195FC,0x1967C],type12=0x15458,texture=0x15500,owner=[0,190,358],starts=[44,44,12,12,0,0,0,0,44,44],shift=44,duration=100,part=53,radius=.3,pivot=1,indirect=.25,end=402)]
MASK_SHA256='d2553537786142248f0b400b750c6e9e86a662495fe6729f62634360b883b6d2'
SOURCE_FILES={'src/game/gs_range_801DE698.c':'e4594cc503d7dd0c4acd06b5df8aa216d04c10ea3698b6e20285cf5f50e2b69d','src/game/wazaSequence.c': '975afb18f19c6da6f7132fe3772d8ab3ab9546f528d8381073b59e614459444d', 'src/game/wazaSequenceEntry.c': '6500a4db7253ce776686b86d470a6f9587cfa3db10cf9fcfb9cca50328d0855f', 'src/game/effect/tracefx.c': '2bf2283cad5bc36624e516d698395125874582b5b2279aeeec49b96f2eb50372', 'src/game/effect/effect_visual.c': '8f0dc333353b606fc000b20103ec451f220b7d9b548ae04e187b899a4d883144', 'src/game/gs_math_mtx.c': 'b99174b1537a461840f293dfb235e6b57359a9bec6ccbe34bfe46e734802b6d7', 'src/game/gs_gfx_range_800D2B90.c': '12a319f6de7f98bc6e3b78b506dd5de87553d848cf9be8bd61b4b8189886b097', 'src/game/gs_gfx_range_800DB890.c': '00a84c000672e8f53a40f131f7265593a7ba18f84180297eba9a7595ee441617', 'src/game/gs_texture.c': 'ece4b77c03a5b472ce97cfae2b8a32700ddffbfa0a469ec3d3d2b8514ed0a04a'}
def read32(data,off):
    if not 0<=off<=len(data)-4:raise ValueError('Truncated Confusion field')
    return struct.unpack_from('>I',data,off)[0]
def node(data,off):
    if off<0 or off+112>len(data):raise ValueError('Truncated Confusion node')
    w=struct.unpack_from('>28i',data,off)
    if w[26]!=2 or not 1<=w[0]<=16 or not 1<=w[1]<=5 or not 0<=w[5]<16 or not 0<=w[6]<16 or not 1<=w[7]<16:raise ValueError('Unknown Confusion node profile')
    return dict(offset=off,key=w[0],kind=w[1],attachment=w[2],position_type=w[3],link=w[4],source=w[5],target=w[6],timing_index=w[7],timing=list(w[8:24]),flags=w[24],part=w[25],state=w[27])
def linear_i8(texture):
    if len(texture)!=0x4080 or struct.unpack_from('>HH',texture)!=(128,128) or read32(texture,8)!=0x42 or read32(texture,40)!=128:raise ValueError('Unqualified type12 GX I8 mask')
    data=texture[128:];out=bytearray(128*128)
    for y in range(128):
        for x in range(128):out[y*128+x]=data[((y//4)*16+x//8)*32+(y%4)*8+x%8]
    return bytes(out)
def parse(data,phase):
    r=ROOTS[phase]
    if len(data)<160 or read32(data,104)!=2 or struct.unpack_from('>6I',data,112)!=(r['entry'],len(r['nodes'])+1,r['flags'],2,5,0):raise ValueError('Confusion WZX root drift')
    nodes=[node(data,o) for o in r['nodes']]
    effect=next(n for n in nodes if n['offset']==r['type12']);desc=effect['offset']+112;params=desc+12
    if [read32(data,desc+i*4) for i in range(3)]!=[12,r['duration'],2]:raise ValueError('Confusion type12 source ABI drift')
    follow0,follow1=struct.unpack_from('>2I',data,params);radius,indirect=struct.unpack_from('>2f',data,params+8)
    size,mode=struct.unpack_from('>2I',data,params+16);pivot=struct.unpack_from('>f',data,params+24)[0]
    if follow0 or follow1 or radius!=struct.unpack('>f',struct.pack('>f',r['radius']))[0] or pivot!=r['pivot'] or size!=0x4080 or mode!=2 or indirect!=r['indirect']:raise ValueError('Unsupported type12 following/curve/texture branch')
    if effect['timing'][effect['timing_index']-1]==0:effect['timing'][effect['timing_index']-1]=r['duration']
    shift=link_times(nodes,r['owner'])
    if shift!=r['shift'] or [n['start'] for n in nodes]!=r['starts']:raise ValueError('Confusion implicit-point0/source-link timeline drift')
    end=nodes[0]['start']+read32(data,nodes[0]['offset']+116) if phase==0 else r['owner'][-1]+shift
    if end!=r['end'] or (phase==0 and struct.unpack_from('>3I',data,nodes[0]['offset']+112)!=(0,15,3)):raise ValueError('Confusion source owner/control completion changed')
    bank_offset=0x19C if phase==0 else 0x120;bank=data[bank_offset:bank_offset+86132]
    if hashlib.sha256(bank).hexdigest()!='5a8f38eaf12eff12c78b1e61e91ab2f001cc0f9c0c9e23fe650781368585ced2':raise ValueError('Confusion GPT1 bank dependency changed')
    texture=data[r['texture']:r['texture']+0x4080]
    if hashlib.sha256(texture).hexdigest()!=MASK_SHA256:raise ValueError('Original Confusion indirect mask changed')
    for n in nodes:
        count=3 if n['kind'] in (1,5) else 4
        if n['kind']==4 and read32(data,n['offset']+112)==0:count=7+4*read32(data,n['offset']+128)
        n['resource_words']=[read32(data,n['offset']+112+4*k) for k in range(count)]
    return dict(phase=phase,start_tick=effect['start'],duration=r['duration'],owner_start=shift,owner_end=r['end'],part=r['part'],radius_per_tick=radius,pivot=pivot,indirect_base=indirect,follow=0,source_body_map_index=2,nodes=nodes,gpt1_sha256=hashlib.sha256(bank).hexdigest(),particle_roots=[read32(data,n['offset']+112) for n in nodes if n['kind']==3],sound_ids=[read32(data,n['offset']+112) for n in nodes if n['kind']==5],texture_offset=r['texture'],texture_sha256=MASK_SHA256,mask_format='GS0x42 -> GX_I8, original8x4tiles'),linear_i8(texture)
def encode(profiles,mask):
    if len(profiles)!=2 or len(mask)!=16384:raise ValueError('Confusion descriptor bound')
    b=bytearray(256);struct.pack_into('<4s7I',b,0,b'C3CF',1,16640,93,12,128,128,60);b[32:52]=bytes.fromhex(SOURCE_COMMIT)
    for phase,p in enumerate(profiles):
        expected=ROOTS[phase]
        if (p['phase'],p['start_tick'],p['duration'],p['owner_start'],p['owner_end'],p['part'])!=(phase,44 if not phase else 0,expected['duration'],expected['shift'],expected['end'],expected['part']):raise ValueError('Unqualified Confusion source timeline/attachment')
        if not all(math.isfinite(p[k]) for k in ('radius_per_tick','pivot','indirect_base')) or abs(p['radius_per_tick']-expected['radius'])>1e-7 or p['pivot']!=expected['pivot'] or p['indirect_base']!=expected['indirect']:raise ValueError('Unqualified Confusion source curves')
        struct.pack_into('<6I3f',b,64+phase*64,p['phase'],p['start_tick'],p['duration'],p['owner_start'],p['owner_end'],p['part'],p['radius_per_tick'],p['pivot'],p['indirect_base'])
    b.extend(mask);b[224:244]=hashlib.sha1(b).digest();return bytes(b)
def encode_sequence(disc,parser,profiles,mask_pack,packs):
    # Source joints are kept separately from compact C3T actor envelopes.
    import prepare_field_geometry as geometry
    geometry.import_parser(parser)
    points=[];tracks=[]
    for species,name,part,entry in ((196,'pkx_eifie.fsys',62,0),(196,'pkx_eifie.fsys',62,1),(288,'pkx_ziguzaguma.fsys',53,0),(288,'pkx_ziguzaguma.fsys',53,8)):
        archive=admit(disc/'files'/name)
        data,raw,scene,header=load_pkx(archive,name)
        model=geometry.Model(scene.models[0],raw.models[0],allow_image_swaps=True)
        mapping=clip_mapping(header,model.ends);idle=mapping[0];motion=mapping[entry]
        if len(motion['clips'])!=1 or idle['body_map_bones'][2]!=part:raise ValueError('Confusion idle source part/clip drift')
        clip=motion['clips'][0];end=model.ends[clip]
        frames=geometry.sample_sequence(end)
        positions=[tuple(map(float,model.world(clip,frame)[0][part,:3,3])) for frame in frames]
        if len(frames)>4096 or any(not all(math.isfinite(v) and abs(v)<1000 for v in pos) for pos in positions):raise ValueError('Confusion live part bounds')
        points.append(b''.join(struct.pack('<3f',*pos) for pos in positions))
        tracks.append(dict(species=species,part=part,entry=entry,clip=clip,frames=len(frames),end=end,pkx_sha1=hashlib.sha1(data).hexdigest()))
    nodes=[dict(n,phase=p['phase']) for p in profiles for n in p['nodes']]
    # Every source entry is retained, including resource-sharing state1 and
    # damage root8: state is a GPT resource key, not an emitter-stop command.
    b=bytearray(448+128*len(nodes));offset=len(b)
    struct.pack_into('<4s7I',b,0,b'C3CS',1,offset+sum(map(len,points)),93,len(nodes),120,128,448)
    b[32:52]=bytes.fromhex(SOURCE_COMMIT);b[52:72]=hashlib.sha1(mask_pack).digest();struct.pack_into('<f',b,92,.5)
    struct.pack_into('<4I',b,352,0,145,44,402)
    for phase,t in enumerate(tracks):
        struct.pack_into('<8I',b,96+phase*64,t['species'],t['part'],t['clip'],t['frames'],offset,len(points[phase]),t['entry'],0)
        b[128+phase*64:148+phase*64]=bytes.fromhex(t['pkx_sha1'])
        offset+=len(points[phase])
    for i,n in enumerate(nodes):
        values=[n[k] for k in ('phase','key','kind','start','attachment','position_type','link','source','target','timing_index','flags','part','state')]+n['resource_words']+[0]*(19-len(n['resource_words']))
        struct.pack_into('<32I',b,448+128*i,*values)
    for phase,pack in enumerate(packs):b[368+phase*20:388+phase*20]=hashlib.sha1(pack).digest()
    b[408:428]=hashlib.sha1(b[448:]).digest()
    b.extend(b''.join(points));b[72:92]=hashlib.sha1(b).digest()
    return bytes(b),tracks

def prepare(disc,output,parser=None):
    verify_disc(disc);check_output(output);d=Dol((disc/'sys/main.dol').read_bytes())
    for address,value in ((0x8047E348,.5),(0x8047E3C8,.5),(0x8047E354,.75),(0x8047D300,0.),(0x8047D304,-1.),(0x8047D308,1.)):
        if struct.unpack('>f',d.read(address,4))[0]!=value:raise ValueError('Original type12 scalar constant drift')
    for name,digest in SOURCE_FILES.items():
        p=ROOT/'vendor/colosseum'/name
        if p.is_symlink() or hashlib.sha256(p.read_bytes()).hexdigest()!=digest:raise ValueError('Type12 source drift: '+name)
    # sequenceLoad copies the PKX raw values at0x0C, including implicitpoint0.
    # EffectStart type12 reads IDLEtable body_map[2] at table+0x54, not a
    # C3T envelope and not the currently selected motion's arbitrary part.
    for phase,(name,identity,entry) in enumerate((('pkx_eifie.fsys',0x03B71E00,1),('pkx_ziguzaguma.fsys',0x04C51E00,8))):
        pkx=member(admit(disc/'files'/name),identity)
        main,aux,count,selector=struct.unpack_from('>3Ii',pkx)
        if count!=17 or selector!=-1:raise ValueError('Confusion owner PKX size selector/count changed')
        table=64+((main+31)&~31)+((aux+31)&~31)
        row=table+entry*0xD0;kind=read32(pkx,row)
        values=list(struct.unpack_from('>'+str(kind)+'I',pkx,row+12))
        part=read32(pkx,table+0x54)
        if values!=ROOTS[phase]['owner'] or part!=ROOTS[phase]['part']:raise ValueError('Confusion original implicit timing/idlebody-map binding changed')
    profiles=[];masks=[]
    for phase,r in enumerate(ROOTS):
        p=disc/'files'/r['name']
        if p.is_symlink() or p.stat().st_size!=r['bytes']:raise ValueError('Unsafe Confusion archive')
        archive=p.read_bytes()
        if hashlib.sha256(archive).hexdigest()!=r['archive_sha256']:raise ValueError('Confusion archive hash drift')
        data=member(archive,r['member'])
        if hashlib.sha256(data).hexdigest()!=r['member_sha256']:raise ValueError('Confusion member hash drift')
        profile,mask=parse(data,phase);profiles.append(profile);masks.append(mask)
    if masks[0]!=masks[1]:raise ValueError('Confusion phases unexpectedly need different indirect masks')
    packed=encode(profiles,masks[0]);output.mkdir(parents=True,exist_ok=False);(output/'confusion_type12.c3f').write_bytes(packed)
    report=dict(source_commit=SOURCE_COMMIT,source_files=SOURCE_FILES,roots=ROOTS,profiles=profiles,descriptor_sha256=hashlib.sha256(packed).hexdigest(),mask_linear_sha256=hashlib.sha256(masks[0]).hexdigest(),scope='Qualified source type12 curves and optional complete entry/body-part contract; native renderer and stage implementation present, fresh GPU qualification pending',source_callbacks=dict(load='fn801364A8 case12',allocate='fn8013F000',init='fn8013F114',start='distortionEffectStart@8013F344',update='fn8013F80C',render='fn8013F410',matrices='_distortionEffectUpdateMatrices@8013F97C'),qualification_pending=[dict(dependency='visible-model capture + GX dependent-texture lookup',source='fn8013F410',details='Capture sharedRGAB8 target; rerender allvisiblemodels flags0x3010; bindcapturetex0 andoriginalI8masktex1; indirectstage1 coord1/map1 matrix1 exponent1. Native capture+128x128indirect grid implementation now exists; fresh source/hardware comparison remains required.'),dict(dependency='Confusiondamage generator4 POINT+TRAIL raster',source='psDispParticles -> psDispSubPointTrail',details='Actualflags0x48500402 aftertexture/E8: GX_LINES screenpixelwidth min(6*size,255), endpoints current/previous, trailalpha; TEV100400 texture*RASTER. Native mixed POINT+TRAIL adapter now exists; fresh GPU path and original-reference comparison remain required.')])
    if parser is not None:
        import prepare_particle as particle
        packs=[]
        for phase,r in enumerate(ROOTS):
            original=member((disc/'files'/r['name']).read_bytes(),r['member']);bank_offset=0x19C if not phase else 0x120;bank=original[bank_offset:bank_offset+86132]
            closure=particle.bank(bank,1 if not phase else 7,() if not phase else (4,3,2,8))
            pack=particle.encode(bank,closure,r['member_sha256']);packs.append(pack)
            (output/('confusion_attack.c3p' if not phase else 'confusion_damage.c3p')).write_bytes(pack)
        sequence,tracks=encode_sequence(disc,parser,profiles,packed,packs)
        (output/'confusion_sequence.c3s').write_bytes(sequence)
        report['sequence_sha256']=hashlib.sha256(sequence).hexdigest();report['source_part_tracks']=tracks
        report['owner_rate_qualification']={'rate':.5,'start':'wazaSequencePokemonMotionStart uses GSmodelSetAnimRate(lbl_8047E348), direct-motion helper fn801DF070 uses lbl_8047E3C8; both pinned DOL floats .5','update':'wazaSequenceUpdate consumes fn800D3088 ticks independently; joint source seconds = owner elapsed seconds*.5','source_hashes':{name:hashlib.sha256((ROOT/'vendor/colosseum'/name).read_bytes()).hexdigest() for name in ('src/game/wazaSequenceEntry.c','src/game/gs_range_801DE698.c')}}
    (output/'confusion_type12.json').write_text(json.dumps(report,indent=2)+'\n');return report
if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--disc',type=Path,required=True);p.add_argument('--output',type=Path,required=True);p.add_argument('--parser',type=Path,help='Prepare checked original idle source-part tracks and full entry contract');a=p.parse_args();print(json.dumps(prepare(a.disc,a.output,a.parser),indent=2))
