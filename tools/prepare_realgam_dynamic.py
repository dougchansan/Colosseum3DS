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
RESOURCE = 0x115A0400

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
    if count >= 48 or any(struct.unpack_from("<I",blob,128+i*160+28)[0] == MARKER for i in range(count)):
        raise ValueError("Dynamic registry already present or actor table full")
    used = {struct.unpack_from("<I",blob,128+i*160)[0] for i in range(count)}
    index = next(i for i in range(255,-1,-1) if i not in used)
    output = area.check_output(output)
    if output.exists():
        raise ValueError("Choose a new private output directory")
    output.mkdir(parents=True)
    data, raw, scene = geometry.load_scene(archive, profile["map"])
    camera = scene.cameras[0]
    fixed = (*camera.position,*camera.target_position,camera.roll,camera.field_of_view,camera.near,camera.far,camera.aspect,0.)
    lights = geometry.fixed_lights(raw,data)
    # peopleOpenSub finds no PeopleInfo for this model, so it keeps source
    # model defaults at the origin. Native73 immediately selects clip0.
    record = {"model_resource":RESOURCE,"file":"f076_dynamic115a.c3t","motion_indices":[0]*9}
    packs = area.npc_pack(archive,record,fixed,lights,profile["sha256"],output,format_version=3)
    row = bytearray(160)
    struct.pack_into("<8I7f",row,0,index,RESOURCE,0,0x40000000,0,0,0,MARKER,0.,0.,0.,0.,0.01,0.,0.)
    filename = record["file"].encode("ascii")
    row[72:72+len(filename)] = filename
    result = insert_actor(blob,row)
    (output/metadata.name).write_bytes(result)
    manifest = {"resource":"115a0400","registry_marker":"43334459","reserved_index":index,"source_archive":profile["archive"],"source_archive_sha256":profile["sha256"],"source_model_sha256":hashlib.sha256(area.field.member(archive,RESOURCE)).hexdigest(),"packs":packs,"metadata":metadata.name}
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
