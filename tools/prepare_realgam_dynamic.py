#!/usr/bin/env python3
"""Prepare Realgam's original transient ending model into a copied C3A pack.

C3DY in actor aux3 reserves a hidden streamed model; native70 allocates the
source person ID and native71 releases it. Extracted data stays private.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

MARKER = 0x43334459
RESOURCES = (0x115A0400,0x115E0400,0x115F0400,0x11600400,0x11610400)

def insert_actor(blob, row):
    """Insert a reserved row without changing any existing section bytes."""
    count = struct.unpack_from("<I", blob, 20)[0]
    cut = 128+count*160
    result = bytearray(blob[:cut]+row+blob[cut:])
    struct.pack_into("<I",result,20,count+1)
    for offset in (36,40,60):
        struct.pack_into("<I",result,offset,struct.unpack_from("<I",blob,offset)[0]+160)
    struct.pack_into("<I",result,8,len(result))
    return result


def prepare(disc, metadata, output, parser):
    import prepare_area as area
    import prepare_field_geometry as geometry
    import prepare_title as title
    profile = next(p for p in area.story_floors().values() if p["floor"] == 0x76)
    archive = area.admit_profile_archive(disc, profile)
    geometry.import_parser(title.verify_parser(parser))
    blob = metadata.read_bytes()
    if blob[:4] != b"C3A1" or struct.unpack_from("<I", blob, 12)[0] != 0x76:
        raise ValueError("Expected Realgam Colosseum C3A1")
    count = struct.unpack_from("<I", blob, 20)[0]
    if count+len(RESOURCES)>48 or any(struct.unpack_from("<I",blob,128+i*160+28)[0] == MARKER for i in range(count)):
        raise ValueError("Dynamic registry already present or actor table full")
    used = {struct.unpack_from("<I",blob,128+i*160)[0] for i in range(count)}
    output = area.check_output(output)
    if output.exists():
        raise ValueError("Choose a new private output directory")
    output.mkdir(parents=True)
    data, raw, scene = geometry.load_scene(archive, profile["map"])
    camera = scene.cameras[0]
    fixed = (*camera.position,*camera.target_position,camera.roll,camera.field_of_view,camera.near,camera.far,camera.aspect,0.)
    lights = geometry.fixed_lights(raw,data)
    result,packs,resources = blob,[],[]
    for resource in RESOURCES:
        index = next(i for i in range(255,-1,-1) if i not in used)
        used.add(index)
        model_data,model_raw,model_scene = geometry.load_scene(archive,resource)
        if len(model_scene.models)!=1:
            raise ValueError("Dynamic resource needs exactly one original model")
        model = geometry.Model(model_scene.models[0],model_raw.models[0])
        meshes = [geometry.build_mesh(model,i) for i in range(len(model.meshes))]
        schedules = [(i,geometry.sample_sequence(end) if end else [0.],False) for i,end in enumerate(model.ends)]
        samples = sum(len(sequence) for _,sequence,_ in schedules)
        stem = f"f076_dynamic{resource>>16:04x}"
        # Retain the original root part0 for nativeA6's original attachment.
        if meshes:
            prepared = geometry.prepare_pack_set(meshes,schedules,60,[fixed]*samples,lights,
                profile["sha256"],output,stem,expected_count=1,format_version=3,
                retained_parts=((model,0,0),),gpu_textures=True)
        else:
            frames,clips,first = bytearray(),[],0
            for clip,sequence,loop in schedules:
                clips.append((first,len(sequence),int(loop),0));first+=len(sequence)
                for frame in sequence:
                    world,_ = model.world(clip,frame)
                    frames.extend(geometry.np.asarray(world[0][:3,:],dtype="<f4").tobytes())
                    frames.extend(struct.pack("<12f",*fixed))
            pack = title.encode_pack(1,[],[],[],clips,lights,frames,fps=60,format_version=3,archive_sha256=profile["sha256"])
            filename = stem+".c3t";(output/filename).write_bytes(pack)
            prepared = [{"file":filename,"bytes":len(pack),"sha256":hashlib.sha256(pack).hexdigest(),
                "vertices":0,"batches":0,"textures":0,"sampled_bones":1,"frames":samples,
                "retained_parts":[{"source_part":0,"source_joint":0,"sampled_bone":0}],"transform_only":True}]
        packs.extend(prepared)
        root_bone = prepared[0]["retained_parts"][0]["sampled_bone"]
        row = bytearray(160)
        struct.pack_into("<8I7f",row,0,index,resource,0,0x40000000,0,0,root_bone+1,MARKER,0.,0.,0.,0.,0.01,0.,0.)
        filename = prepared[0]["file"].encode("ascii")
        row[72:72+len(filename)] = filename
        result = insert_actor(result,row)
        resources.append({"resource":f"{resource:08x}","reserved_index":index,"source_model_sha256":hashlib.sha256(model_data).hexdigest(),"root_bone_plus_one":root_bone+1})
    (output/metadata.name).write_bytes(result)
    manifest = {"resources":resources,"registry_marker":"43334459","source_archive":profile["archive"],"source_archive_sha256":profile["sha256"],"packs":packs,"metadata":metadata.name}
    (output/"dynamic-manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    print(json.dumps(manifest))

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc",type=Path,required=True)
    parser.add_argument("--metadata",type=Path,required=True)
    parser.add_argument("--output",type=Path,required=True)
    parser.add_argument("--parser",type=Path,default=Path(__file__).resolve().parents[1]/"vendor/hsd-importer")
    args = parser.parse_args()
    prepare(args.disc,args.metadata,args.output,args.parser)

if __name__ == "__main__":
    main()
