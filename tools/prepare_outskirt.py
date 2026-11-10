#!/usr/bin/env python3
"""Prepare admitted Outskirt Stand geometry, Wes animation and collision data.

This exporter reads the user's original GC6E01 revision 0 files. Its outputs
contain original game data and belong in a private SD preparation directory.
The stage runtime supports a bounded field subset, not the complete GS VM.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import tempfile

import prepare_title as title
from prepare_boot_media import verify_disc
from prepare_model import check_output

SOURCE_COMMIT = title.SOURCE_COMMIT
PROFILES = {
    "S1_out.fsys": (1468000, "37900ccab1353ab835c7e4acc40cd5e1349237589a53b441c78ce91a891f8034"),
    "field_common.fsys": (211424, "83bc8cf05026263cd0c1d3dfb7cc448445922b34213ef973ab3c94090c669fb5"),
    "common.fsys": (743136, "f83b73c524498c1ec3ea1c04d06d391bcd6ca7e1ddd318fa33bcca0d6cf5b19a"),
}
SCALE = 0.1
MAX_MEMBER_BYTES = 16 * 1024 * 1024


def u32(data, offset):
    if offset < 0 or offset + 4 > len(data):
        raise ValueError("Input word exceeds file bounds")
    return struct.unpack_from(">I", data, offset)[0]


def extent(data, offset, size):
    if offset < 0 or size < 0 or offset + size > len(data):
        raise ValueError("Input section exceeds file bounds")
    return data[offset:offset + size]


def admit_archive(path):
    path = Path(path)
    size, digest = PROFILES[path.name]
    if path.is_symlink() or path.stat().st_size != size:
        raise ValueError("Unexpected original archive path or size: " + path.name)
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError("Original archive differs from admitted GC6E01 revision 0: " + path.name)
    return data


def member(archive, identity):
    """The exact FSYS directory layout used by fsys_file.c:fn_8017E30C."""
    if len(archive) < 64 or archive[:4] != b"FSYS" or u32(archive, 32) != len(archive):
        raise ValueError("Invalid FSYS header")
    count = u32(archive, 12)
    if not 1 <= count <= 4096:
        raise ValueError("Invalid FSYS entry count")
    table = u32(archive, u32(archive, 24))
    extent(archive, table, count * 4)
    selected = None
    for index in range(count):
        entry = u32(archive, table + index * 4)
        extent(archive, entry, 40)
        if u32(archive, entry) == identity:
            if selected is not None:
                raise ValueError("Duplicate FSYS resource identity")
            selected = entry
    if selected is None:
        raise ValueError("Required FSYS member is missing: %08x" % identity)
    unpacked = u32(archive, selected + 8)
    if not 1 <= unpacked <= MAX_MEMBER_BYTES:
        raise ValueError("FSYS member exceeds allocation limit")
    packed = extent(archive, u32(archive, selected + 4), u32(archive, selected + 20))
    if not u32(archive, selected + 12) & 0x80000000:
        if len(packed) != unpacked:
            raise ValueError("Raw FSYS member length mismatch")
        return packed
    if len(packed) < 16 or packed[:4] != b"LZSS" or u32(packed, 4) != unpacked:
        raise ValueError("Invalid LZSS member header")
    end = u32(packed, 8)
    if not 16 <= end <= len(packed) or u32(packed, 12):
        raise ValueError("Invalid LZSS stream extent")
    output, history = bytearray(), bytearray(4096)
    position, cursor = 0xFEE, 16
    while len(output) < unpacked:
        if cursor >= end:
            raise ValueError("Truncated LZSS flag byte")
        flags = packed[cursor]
        cursor += 1
        for bit in range(8):
            if len(output) == unpacked:
                break
            if flags & (1 << bit):
                if cursor >= end:
                    raise ValueError("Truncated LZSS literal")
                values = (packed[cursor],)
                cursor += 1
            else:
                if cursor + 2 > end:
                    raise ValueError("Truncated LZSS reference")
                low, high = packed[cursor:cursor+2]
                cursor += 2
                reference, length = low | ((high & 0xF0) << 4), (high & 15) + 3
                if len(output) + length > unpacked:
                    raise ValueError("LZSS reference exceeds declared output")
                # Read each history byte after the previous byte was written:
                # the source codec permits an overlapping reference.
                values = (history[(reference + i) & 4095] for i in range(length))
            for value in values:
                output.append(value)
                history[position] = value
                position = (position + 1) & 4095
    return bytes(output)


def rel_data(data, module):
    # REL15 is M1_out (Phenac City); it uses REL131's section layout.
    section, count = {5: (3,10), 15: (3,10), 125: (4,11), 131: (3,10)}[module]
    if u32(data, 0) != module or u32(data, 12) != count:
        raise ValueError("Unexpected original REL identity or section count")
    table = u32(data, 16)
    return extent(data, u32(data, table + section * 8) & ~3, u32(data, table + section * 8 + 4))


def collision(data):
    objects, count = u32(data, 0), u32(data, 4)
    if not 1 <= count <= 64:
        raise ValueError("CCD object count exceeds reviewed bound")
    extent(data, objects, count * 64)
    walks, walls = [], []
    for index in range(count):
        at = objects + index * 64
        srt = struct.unpack(">9f", extent(data, at, 36))
        if srt != (0,0,0,0,0,0,1,1,1) or struct.unpack(">H", extent(data, at+60, 2))[0]:
            raise ValueError("Outskirt CCD must contain fixed identity objects")
        # GScolsys2HitCollision selects +0x28; +0x34 is the separate NPC
        # collision query and is deliberately not merged into player walls.
        for field, output in ((36, walks), (40, walls)):
            block = u32(data, at + field)
            if not block:
                continue
            first, triangles = u32(data, block), u32(data, block + 4)
            if triangles > 4096 - len(output):
                raise ValueError("CCD triangle count exceeds runtime bound")
            extent(data, first, triangles * 52)
            for n in range(triangles):
                values = struct.unpack_from(">12fHH", data, first + n * 52)
                if any(not math.isfinite(x) or abs(x) > 1000000 for x in values[:12]):
                    raise ValueError("Invalid CCD floating point data")
                if not .99 <= sum(x*x for x in values[9:12]) <= 1.01:
                    raise ValueError("CCD triangle normal is not normalized")
                output.append((*[x*SCALE for x in values[:9]], *values[9:], index, 0))
    if not walks:
        raise ValueError("Outskirt CCD contains no walk surfaces")
    return walks, walls


def field_metadata(map_rel, common_rel, collision_data, camera, actor_vertices=0,
                   *, module=131, spawn_table=0x12C, spawn_count=5, floor=1,
                   map_resource=0x00E20200, map_pack_count=4, arrival_pack_count=2):
    local, common = rel_data(map_rel, module), rel_data(common_rel, 125)
    # _prolog in exact REL131 publishes five FieldPointRecords at data+12C.
    spawns = []
    for index in range(spawn_count):
        at = spawn_table + index * 16
        direction, zero, x, y, z = struct.unpack(">hH3f", extent(local, at, 16))
        if zero or not all(math.isfinite(v) for v in (x,y,z)):
            raise ValueError("Invalid original floor entrance")
        spawns.append((x*SCALE,y*SCALE,z*SCALE,direction*(math.pi/180),index,0))
    # common_rel._prolog publishes the PeopleInfo table and its 150 count.
    count = u32(common, 0x14A6E8)
    if count != 150:
        raise ValueError("Original PeopleInfo table count changed")
    rows = [extent(common, 0x13E440+i*44, 44) for i in range(count)]
    info = [row for row in rows if u32(row,12)==0x00F70400]
    if len(info)!=1 or info[0][1:4]!=bytes((1,2,3)):
        raise ValueError("Wes original motion table differs from reviewed source")
    walk, run, radius = struct.unpack_from(">3f",info[0],16)
    if (walk,run,radius)!=(9,13,4):
        raise ValueError("Wes original movement distances changed")
    walks,walls=collision(collision_data)
    spawn_offset=256
    walk_offset=spawn_offset+len(spawns)*24
    wall_offset=walk_offset+len(walks)*56
    total=wall_offset+len(walls)*56
    header=bytearray(256)
    struct.pack_into("<4s15I",header,0,b"C3F1",1,total,256,floor,map_resource,
                     len(spawns),len(walks),len(walls),spawn_offset,walk_offset,
                     wall_offset,56,24,0,0)
    # GSmodelGetFrameCount returns end_frame + 1, proved by the original DOL.
    struct.pack_into("<7fI",header,64,SCALE,radius*SCALE,walk*SCALE,run*SCALE,31,18,31,1)
    struct.pack_into("<11f",header,96,*camera)
    struct.pack_into("<4f",header,140,camera[1]-camera[4],camera[2]-camera[5],0,1.4)
    struct.pack_into("<6I",header,156,0,actor_vertices,0xFFFFFFFF,0,1,2)
    struct.pack_into("<2I",header,180,map_pack_count,arrival_pack_count)
    out=bytes(header)+b"".join(struct.pack("<4f2I",*row) for row in spawns)
    out+=b"".join(struct.pack("<12f4H",*row) for row in walks+walls)
    if len(out)!=total or total>512*1024:
        raise ValueError("C3F extent exceeds runtime bound")
    return out, {"spawns":spawns,"walk_triangles":len(walks),"wall_triangles":len(walls),
                 "source_units_to_pack":SCALE,"wes_motion_indices":[1,2,3],
                 "wes_frame_counts":[31,31,18],"wes_walk_distance":walk*SCALE,
                 "wes_run_distance":run*SCALE,"wes_radius":radius*SCALE,
                 "map_pack_count":map_pack_count,"arrival_pack_count":arrival_pack_count}


def prepare(disc, output, parser):
    from prepare_field_geometry import prepare_geometry, import_parser
    disc=verify_disc(disc)
    output=check_output(output)
    if output.exists():
        raise ValueError("Choose a new private output directory")
    parser=title.verify_parser(parser)
    import_parser(parser)
    archives={name:admit_archive(disc/"files"/name) for name in PROFILES}
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".outskirt-",dir=output.parent) as temporary:
        stage=Path(temporary)/"stage"
        stage.mkdir()
        packs,camera,actor_vertices=prepare_geometry(archives,stage)
        # The FSYS type1C REL identity is fixed by the pinned module manifest.
        local_rel=member(archives["S1_out.fsys"],0x02571C00)
        common_rel_id=None
        a=archives["common.fsys"]
        table=u32(a,u32(a,24))
        for i in range(u32(a,12)):
            identity=u32(a,u32(a,table+i*4))
            if (identity>>8)&255==0x1C and u32(member(a,identity),0)==125:
                common_rel_id=identity
        if common_rel_id is None:
            raise ValueError("Required common REL is missing")
        field,metadata=field_metadata(local_rel,member(a,common_rel_id),
                                     member(archives["S1_out.fsys"],0x00E30600),camera,actor_vertices)
        (stage/"outskirt.c3f").write_bytes(field)
        source_members=[]
        identities={"S1_out.fsys":(0x00E20200,0x00E30600,0x02571C00,0x01430E00,
                                    0x0C6C0400,0x105D0400,0x105C1800),
                    "field_common.fsys":(0x00F70400,),"common.fsys":(common_rel_id,)}
        for archive,ids in identities.items():
            for identity in ids:
                data=member(archives[archive],identity)
                source_members.append({"archive":archive,"resource_id":"%08x"%identity,
                                       "bytes":len(data),"sha256":hashlib.sha256(data).hexdigest()})
        manifest={"source_commit":SOURCE_COMMIT,"parser_commit":title.PARSER_COMMIT,
                  "archives":{name:{"bytes":size,"sha256":digest} for name,(size,digest) in PROFILES.items()},
                  "source_members":source_members,
                  "field":{"file":"outskirt.c3f","sha256":hashlib.sha256(field).hexdigest(),
                           "bytes":len(field),**metadata},"packs":packs,
                  "limitations":["The field pack carries original geometry, animation and collision; story progression requires the separately implemented GSVM entry sequence.",
                                 "Only the map, Wes and the original bike arrival actors are included; the general NPC, dialogue, battle and field-event systems are not included in this pack.",
                                 "Source particle generators and recorded type40 particle callbacks are not synthesized by this geometry pack.",
                                 "Wes exports source idle, walk, run and motion zero at pack clips 0,1,2,3; runtime transitions are separate from the original general animation-blend system.",
                                 "Geometry and camera curves are sampled at 60 Hz; the slow sky uses 1001 samples across its original 29999/30-second period.",
                                 "The source separate BGM and event SFX are not embedded in geometry packs.",
                                 "GX filtering, lighting and blending use the native PICA material implementation; the separate specular pass has a clamp-order approximation at fractionally filtered alpha edges of the bike's otherwise binary-alpha texture."]}
        (stage/"outskirt-manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
        stage.rename(output)
    return manifest


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    parser.add_argument("--parser",type=Path,default=title.ROOT/"vendor/hsd-importer")
    args=parser.parse_args()
    try:
        result=prepare(args.disc,args.output,args.parser)
    except (OSError,ValueError,KeyError) as error:
        parser.exit(1,"Outskirt preparation failed: %s\n"%error)
    print(json.dumps(result,indent=2))


if __name__=="__main__":
    main()
