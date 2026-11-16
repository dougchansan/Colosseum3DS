#!/usr/bin/env python3
"""Prepare the original DNA-key special screen; private assets only."""
import argparse
import json
from pathlib import Path
from prepare_model import check_output
from prepare_boot_media import verify_disc
from prepare_floor import admit_floor
from realgam_profiles import REALGAM_FLOORS as REALGAM
import prepare_field_geometry as geometry
import prepare_title as title
from prepare_area import sampled_camera

def prepare(disc,output,parser):
    output=check_output(Path(output))
    disc=verify_disc(Path(disc))
    geometry.import_parser(title.verify_parser(parser))
    profile=REALGAM['auto_387']
    archive=admit_floor(disc,profile)
    data,raw,scene=geometry.load_scene(archive,0x107e0200)
    if len(scene.models)!=12 or len(scene.cameras)!=1 or scene.fogs:
        raise ValueError('DNA source topology changed')
    output.mkdir(parents=True,exist_ok=False)
    fixed=title.camera_values(scene.cameras[0],0)
    lights=geometry.fixed_lights(raw,data)
    reports=[]
    expected=[[1.,1.]]*9+[[1.]*9,[],[1.]*4]
    for i,(model,original) in enumerate(zip(scene.models,raw.models)):
        own=geometry.Model(model,original,allow_instances=True,allow_paths=True,frozen_material_colors=True)
        if own.ends!=expected[i]:raise ValueError('DNA source clips changed')
        meshes=[geometry.build_mesh(own,j) for j in range(len(own.meshes))]
        meshes+=geometry.instance_meshes(own,meshes)
        schedules=[(clip,geometry.sample_sequence(end) if end else [0.],False) for clip,end in enumerate(own.ends)] or [(0,[0.],False)]
        samples=sum(len(sequence) for _,sequence,_ in schedules)
        result=geometry.prepare_pack_set(meshes,schedules,60,[fixed]*samples,lights,profile['sha256'],output,'f387_obj%d'%i,expected_count=1,format_version=3,gpu_textures=True,crop_textures=True)
        reports+=result
    for ident in (0x107c1800,0x107d1800):
        _,_,track=geometry.load_scene(archive,ident)
        if len(track.cameras)!=1:raise ValueError('DNA camera topology changed')
        (output/('f387_%08x.c3c'%ident)).write_bytes(sampled_camera(track.cameras[0],profile['sha256'],ident))
    manifest={'floor':0x387,'resource_group':0x62d,'source':profile,'objects':reports,'limitations':['Material colors retain their first source sample; model and camera clips retain their authored samples.']}
    (output/'dna-key.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
    return manifest

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--disc',type=Path,required=True);p.add_argument('--output',type=Path,required=True)
    p.add_argument('--parser',type=Path,default=Path(__file__).resolve().parents[1]/'vendor/hsd-importer')
    a=p.parse_args();print(json.dumps(prepare(a.disc,a.output,a.parser),indent=2))
if __name__=='__main__':main()
