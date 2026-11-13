#!/usr/bin/env python3
"""Prepare the original Outskirt interior and its field NPC geometry.

This admitted-area extension reuses the reviewed HSD/CCD pipeline. Outputs
contain original game data and must remain in a private SD preparation folder.
It does not manufacture dialogue, battle outcomes, or later-area assets.

--floor NAME prepares a later floor from its floor_profiles.py row instead:
every original person record, the CCD event surfaces, one NPC model pack per
distinct model resource and the party partner's pack (<prefix>_partner.c3t,
lit like that floor's Wes pack). The Outskirt modes are unchanged.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import struct
import tempfile

import prepare_title as title
import prepare_field_geometry as geometry
import prepare_outskirt as field
from floor_profiles import PARTNER, story_floors
from prepare_boot_media import verify_disc
from prepare_model import check_output

PROFILES = {**field.PROFILES,
    "S1_shop_1F.fsys": (2947200, "7d74dcb75c7f8aebc7479fee695e87a02ab58112d110dada15e88353d7215329")}
INITIAL_EXTERIOR_ACTORS = (0,1,2,3)


def admit_archive(path):
    size, digest = PROFILES[path.name]
    if path.is_symlink() or not path.is_file() or path.stat().st_size != size:
        raise ValueError("Unexpected original archive path or size: " + path.name)
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError("Archive differs from admitted GC6E01 revision 0: " + path.name)
    return data


def common_rel(archive):
    table = field.u32(archive,field.u32(archive,24))
    identities = [field.u32(archive,field.u32(archive,table+i*4))
                  for i in range(field.u32(archive,12))]
    candidates = [(i,field.member(archive,i)) for i in identities if (i>>8)&255 == 0x1C]
    candidates = [(i,data) for i,data in candidates if field.u32(data,0)==125]
    if len(candidates)!=1:
        raise ValueError("Expected one original common REL125")
    return candidates[0]


def actor_records(map_rel, module, common_rel, offset, count, prefix, included=None):
    local, common = field.rel_data(map_rel, module), field.rel_data(common_rel, 125)
    if field.u32(common, 0x14A6E8) != 150:
        raise ValueError("Original PeopleInfo count changed")
    result = []
    for index in range(count):
        raw = field.extent(local, offset + index * 36, 36)
        words = struct.unpack_from(">6I", raw)
        info_index = words[1] & 0xFFFF
        if info_index >= 150:
            raise ValueError("Invalid original PeopleInfo index")
        info = field.extent(common, 0x13E440 + info_index * 44, 44)
        position = struct.unpack_from(">3f", raw, 24)
        if not all(math.isfinite(v) for v in position):
            raise ValueError("Invalid original NPC position")
        result.append({"index":index,"people_info":info_index,
                       "model_resource":field.u32(info,12),
                       "position":[v*field.SCALE for v in position],
                       "yaw_degrees":struct.unpack_from(">h",raw,4)[0],
                       "move_script":words[4],"talk_script":words[5],
                       "visible_initial":bool(raw[0]&0x80),"load_initial":bool(raw[0]&0x40),
                       "talk_start_type":(raw[1]>>6)&3,"talk_end_type":(raw[1]>>4)&3,
                       "move_type":raw[0]&7,
                       "raw_words":list(words),"people_info_raw":info.hex(),
                       "motion_indices":list(info[1:10]),
                       "default_idle_clip":info[1],"default_idle_loop":1,
                       "walk_distance":struct.unpack_from(">f",info,16)[0]*field.SCALE,
                       "run_distance":struct.unpack_from(">f",info,20)[0]*field.SCALE,
                       "radius":struct.unpack_from(">f",info,24)[0]*field.SCALE,
                       "file":f"{prefix}_npc{index}.c3t" if included is None or index in included else None})
    return result


def source_collision_tables(data, field_offset):
    """Preserve original CCD transition/interaction surfaces for GSVM glue.

    Triangle fields are not guessed as destination floors. Their original
    object/source IDs must be resolved through the game's event tables.
    """
    objects, count = field.u32(data, 0), field.u32(data, 4)
    field.extent(data, objects, count * 64)
    if field_offset not in (44, 48):
        raise ValueError("Expected CCD transition or interaction table")
    result = []
    for index in range(count):
        block = field.u32(data, objects + index * 64 + field_offset)
        if not block:
            continue
        first, triangles = field.u32(data, block), field.u32(data, block + 4)
        if triangles > 4096:
            raise ValueError("CCD event surface bound exceeded")
        field.extent(data, first, triangles * 52)
        for n in range(triangles):
            values = struct.unpack_from(">12fHH", data, first+n*52)
            if not all(math.isfinite(x) for x in values[:12]):
                raise ValueError("Invalid CCD event coordinates")
            result.append({"object":index,"vertices":[v*field.SCALE for v in values[:9]],
                           "normal":list(values[9:12]),"flags":values[12] if field_offset==44 else 0,
                           "source_id":values[13] if field_offset==44 else values[12],
                           "raw_halfwords":list(values[12:14])})
    return result


def npc_retained_parts(model, resource):
    if resource==0x010B0400:
        # Native46's reviewed vehicle part1 has only an identity source root
        # above it. Its matrix therefore retains the original Euler tuple;
        # arbitrary ancestor rotations cannot reconstruct GSpart's Euler sum.
        if (len(model.joints)<2 or model.model.bones[1].parent_index!=0 or
            not geometry.np.allclose(model.rest[0],geometry.np.eye(4),rtol=0,atol=1e-8) or
            any(tracks[0] or set(tracks[1])-{1,2,3,5,6,7} for tracks in model.tracks) or
            tuple(model.model.bones[1].scale)!=(1.,1.,1.)):
            raise ValueError("Original vehicle attachment requires the reviewed identity-ancestor SRT")
        # Euler tuples are not unique. The reviewed source is close to zero;
        # keep every stored angle on that branch before runtime decomposition.
        for tracks,end in zip(model.tracks,model.ends):
            for frame in geometry.sample_sequence(end) if end else [0.]:
                for axis in range(3):
                    angle=title.curve_value(tracks[1].get(1+axis),frame,
                                            model.model.bones[1].rotation[axis])
                    if not math.isfinite(angle) or abs(angle)>.25:
                        raise ValueError("Vehicle attachment Euler angles leave the reviewed branch")
        return ((model,1,1),)
    return ()


def npc_pack(archive, record, fixed, lights, archive_hash, output, format_version=4):
    data, raw, scene = geometry.load_scene(archive, record["model_resource"])
    if len(scene.models) != 1:
        raise ValueError("NPC must have exactly one original model")
    model = geometry.Model(scene.models[0], raw.models[0])
    if not 1 <= len(model.ends) <= title.LIMITS[4]:
        raise ValueError("NPC motion count exceeds native bound")
    meshes = [geometry.build_mesh(model,i) for i in range(len(model.meshes))]
    retained_parts=npc_retained_parts(model,record["model_resource"])
    # Source clip IDs remain exact. peoplePlayMotion selects loop at runtime;
    # a one-shot motion therefore cannot wrap merely because it is in a pack.
    schedules = [(i,geometry.sample_sequence(end) if end else [0.],False)
                 for i,end in enumerate(model.ends)]
    samples = sum(len(sequence) for _,sequence,_ in schedules)
    reports = geometry.prepare_pack_set(meshes,schedules,60,[fixed]*samples,lights,
                archive_hash,output,Path(record["file"]).stem,expected_count=1,format_version=format_version,
                retained_parts=retained_parts,gpu_textures=True)
    record["source_clip_ends"] = model.ends
    record["default_idle_clip"] = record["motion_indices"][0]
    record["particle_callbacks_unrendered"] = model.particle_events
    record["source_part_bones"] = {str(part["source_part"]):part["sampled_bone"]
                                   for part in reports[0]["retained_parts"]}
    return reports


def sampled_lights(raw, data, archive_hash):
    """Original LObjUpdateFunc RGB curves, independently clocked from actors."""
    root = copy.copy(raw)
    root.lights, tracks = [], []
    for light_set in raw.lights:
        curves = {}
        if len(light_set.animations or []) > 1:
            raise ValueError("Multiple field light animation sets are unreviewed")
        for animation in light_set.animations or []:
            if animation.next or animation.eye_position_animation or animation.interest_animation:
                raise ValueError("Animated field light position is unreviewed")
            if animation.animation:
                if animation.animation.flags != 0x20000000 or animation.animation.end_frame != 300:
                    raise ValueError("Original interior light period changed")
                curves = title.channels(animation.animation,{9,10,11,12})
        tracks.append(curves)
        item=copy.copy(light_set);item.animations=[];root.lights.append(item)
    # C3T4 retains the original GXSpotFn/cutoff in extended light records.
    # Other descriptors follow the existing fixed-light conversion exactly.
    lights=[]
    for light_set in root.lights:
        light=light_set.light
        if light.flags&3 == 3:
            if light.link or light.attn_flags or not light.position or not light.interest:
                raise ValueError("Unsupported chained/raw/positionless field spotlight")
            color=[getattr(light.color,k)/255. for k in ("red","green","blue","alpha")]
            pos=[v*field.SCALE for v in light.position.position]
            interest=[v*field.SCALE for v in light.interest.position]
            prop=light.property
            lights.append((light.flags,light.attn_flags,*color,*pos,*interest,
                           prop.reference_br,prop.reference_distance*field.SCALE,
                           float(prop.distance_attn_flags),0.,prop.spot_flags,prop.cutoff,0,0))
        else:
            one=copy.copy(root);one.lights=[light_set]
            lights.append((*title.light_values(one,data,field.SCALE)[0],0,0.,0,0))
    colors=bytearray()
    sequence=geometry.sample_sequence(300)
    for frame in sequence:
        for light,curves in zip(lights,tracks):
            for component in range(4):
                value=title.curve_value(curves.get(component+9),frame,light[component+2])
                # lobj.c:LObjUpdateFunc clamps then multiplies by float32 255.
                colors.append(round(title.tev_byte(value)*255))
    header=bytearray(64)
    struct.pack_into("<4s5IfI",header,0,b"C3L1",1,64+len(colors),64,len(lights),len(sequence),60.,1)
    header[32:64]=bytes.fromhex(archive_hash)
    return lights,bytes(header)+colors


def sampled_camera(camera, archive_hash, resource=0):
    """Bounded original camera track, with its independent authored clock.

    resource is the original camera member ID stored in the header word at
    offset 28; zero keeps the first shop pack's layout byte for byte.
    """
    if len(camera.animations) != 1:
        raise ValueError("Expected one original area camera animation")
    animation = camera.animations[0]
    sequence = geometry.sample_sequence(animation.end_frame) if animation.end_frame else [0.]
    if len(sequence) > 4096:
        raise ValueError("Area camera sample bound exceeded")
    frames = b"".join(struct.pack("<11f",*title.camera_values(camera,frame)[:11]) for frame in sequence)
    header = bytearray(64)
    struct.pack_into("<4s4IfII",header,0,b"C3C1",1,64+len(frames),64,len(sequence),60.,int(animation.loop),resource)
    header[32:64] = bytes.fromhex(archive_hash)
    return bytes(header)+frames


def prepare_television(archive, output):
    """Fresh shop field-camera texture entry 0: a rendered animated model."""
    archive_hash = PROFILES["S1_shop_1F.fsys"][1]
    data,raw,scene = geometry.load_scene(archive,0x0D560400)
    _camera_data,_camera_raw,camera_scene = geometry.load_scene(archive,0x0D571800)
    if len(scene.models)!=1 or len(camera_scene.cameras)!=1 or scene.fogs:
        raise ValueError("Original television scene topology changed")
    camera = camera_scene.cameras[0]
    fixed = title.camera_values(camera,0)
    model = geometry.Model(scene.models[0],raw.models[0],allow_image_swaps=True)
    if model.ends != [96.] or any(n.animation.flags!=0x20000000
        for n in title.tree_nodes(raw.models[0].animated_joints[0]) if n.animation):
        raise ValueError("Original television clip timing changed")
    meshes = geometry.expand_image_swaps([geometry.build_mesh(model,i) for i in range(len(model.meshes))])
    if len(meshes)!=21:
        raise ValueError("Original television image-variant topology changed")
    sequence = geometry.sample_sequence(model.ends[0])
    reports = geometry.prepare_pack_set(meshes,[(0,sequence,True)],60,[fixed]*len(sequence),
        geometry.fixed_lights(raw,data),archive_hash,output,"shop_tv",expected_count=1,format_version=3)
    _room_data,_room_raw,room_scene = geometry.load_scene(archive,0x00E51800)
    if len(room_scene.cameras)!=1 or room_scene.cameras[0].animations[0].end_frame!=300:
        raise ValueError("Original fresh shop room camera changed")
    track = sampled_camera(room_scene.cameras[0],archive_hash)
    (output/"shop_camera.c3c").write_bytes(track)
    return reports,{"file":"shop_camera.c3c","bytes":len(track),
                   "sha256":hashlib.sha256(track).hexdigest(),"source_resource":"00e51800",
                   "samples":601,"sample_fps":60,"duration_seconds":10,"loop":False}


def prepare(disc, output, parser):
    disc, output = verify_disc(disc), check_output(output)
    if output.exists():
        raise ValueError("Choose a new private output directory")
    geometry.import_parser(title.verify_parser(parser))
    archives = {name:admit_archive(disc/"files"/name) for name in PROFILES}
    common_id, common = common_rel(archives["common.fsys"])
    interior = archives["S1_shop_1F.fsys"]
    interior_rel = field.member(interior,0x02581C00)
    exterior_rel = field.member(archives["S1_out.fsys"],0x02571C00)
    actors = {"2":actor_records(interior_rel,5,common,0x180,5,"shop"),
              "1":actor_records(exterior_rel,131,common,0x30,7,"outskirt",INITIAL_EXTERIOR_ACTORS)}
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".area-",dir=output.parent) as temporary:
        stage = Path(temporary)/"stage"
        stage.mkdir()
        data,raw,scene = geometry.load_scene(interior,0x00E60200)
        if len(scene.models)!=3 or len(scene.cameras)!=1 or scene.fogs:
            raise ValueError("Original interior scene topology changed")
        camera = scene.cameras[0]
        fixed = (*camera.position,*camera.target_position,camera.roll,camera.field_of_view,
                 camera.near,camera.far,camera.aspect,0.)
        lights, light_pack = sampled_lights(raw,data,PROFILES["S1_shop_1F.fsys"][1])
        (stage/"shop.c3l").write_bytes(light_pack)
        models = [geometry.Model(m,r) for m,r in zip(scene.models,raw.models)]
        if [m.ends for m in models] != [[300],[],[]]:
            raise ValueError("Original interior map animations changed")
        meshes = [geometry.build_mesh(model,i) for model in models for i in range(len(model.meshes))]
        sequence = geometry.sample_sequence(300)
        reports = geometry.prepare_pack_set(meshes,[(0,sequence,True)],60,[fixed]*len(sequence),lights,
                    PROFILES["S1_shop_1F.fsys"][1],stage,"shop_map",expected_count=2,format_version=4)
        screen = [(report["file"],i,mesh["texture_index"]) for report in reports
                  for i,mesh in enumerate(report["source_meshes"])
                  if mesh["hsd_root_address"]==models[2].raw.root_joint.address]
        if len(screen)!=1:
            raise ValueError("Original TV screen must select exactly one map material")
        tv_reports,camera_report = prepare_television(interior,stage)
        reports += tv_reports
        wes_reports,actor_vertices = geometry.prepare_wes(archives["field_common.fsys"],stage,
                                                         "shop_wes",fixed,lights,format_version=4)
        reports += wes_reports
        for actor in actors["2"]:
            reports += npc_pack(interior,actor,fixed,lights,PROFILES["S1_shop_1F.fsys"][1],stage)
        outside_data,outside_raw,outside_scene = geometry.load_scene(archives["S1_out.fsys"],0x00E20200)
        outside_camera = outside_scene.cameras[0]
        outside_fixed = (*outside_camera.position,*outside_camera.target_position,outside_camera.roll,
                         outside_camera.field_of_view,outside_camera.near,outside_camera.far,outside_camera.aspect,0.)
        outside_lights = geometry.fixed_lights(outside_raw,outside_data)
        for index in INITIAL_EXTERIOR_ACTORS:
            reports += npc_pack(archives["S1_out.fsys"],actors["1"][index],outside_fixed,outside_lights,
                                PROFILES["S1_out.fsys"][1],stage,format_version=3)
        ccd = field.member(interior,0x00E70600)
        c3f,metadata = field.field_metadata(interior_rel,common,ccd,fixed[:11],
            actor_vertices,module=5,spawn_table=0x234,
            spawn_count=2,floor=2,map_resource=0x00E60200,map_pack_count=2,arrival_pack_count=0)
        (stage/"shop.c3f").write_bytes(c3f)
        areas = []
        for floor,archive,resource in ((1,"S1_out.fsys",0x00E30600),(2,"S1_shop_1F.fsys",0x00E70600)):
            collision = field.member(archives[archive],resource)
            areas.append({"floor":floor,"actors":actors[str(floor)],
                          "transition_surfaces":source_collision_tables(collision,44),
                          "interaction_surfaces":source_collision_tables(collision,48)})
        (stage/"area-actors.json").write_text(json.dumps(areas,indent=2)+"\n")
        source_members = []
        ids = {"S1_shop_1F.fsys":(0x02581C00,0x00E60200,0x00E70600,0x01460E00,
                     0x00E51800,0x0D560400,0x0D571800,
                     *(a["model_resource"] for a in actors["2"])),
               "S1_out.fsys":(0x02571C00,0x00E20200,0x00E30600,0x06B10400,0x06B00400,0x010B0400,0x01040400),
               "field_common.fsys":(0x00F70400,),"common.fsys":(common_id,)}
        for archive,identities in ids.items():
            for identity in identities:
                source = field.member(archives[archive],identity)
                source_members.append({"archive":archive,"resource_id":f"{identity:08x}",
                    "bytes":len(source),"sha256":hashlib.sha256(source).hexdigest()})
        manifest = {"source_commit":title.SOURCE_COMMIT,"parser_commit":title.PARSER_COMMIT,
                    "archives":{name:{"bytes":size,"sha256":digest} for name,(size,digest) in PROFILES.items()},
                    "source_members":source_members,"packs":reports,
                    "field":{"file":"shop.c3f","bytes":len(c3f),"sha256":hashlib.sha256(c3f).hexdigest(),**metadata},
                    "light_track":{"file":"shop.c3l","bytes":len(light_pack),
                                   "sha256":hashlib.sha256(light_pack).hexdigest(),
                                   "lights":len(lights),"samples":601,"sample_fps":60,"loop_seconds":10},
                    "room_camera":camera_report,
                    "television":{"table_id":0,"width":256,"height":256,
                                  "model_resource":"0d561000","model_member":"0d560400",
                                  "camera_resource":"0d571800","screen_resource":"00e61002",
                                  "file":"shop_tv.c3t","source_clip":0,"loop_seconds":3.2,
                                  "screen_file":screen[0][0],"screen_batch":screen[0][1],
                                  "screen_texture":screen[0][2],"image_count":3},
                    "actors":"area-actors.json",
                    "limitations":["The original geometry, authored motion clips and CCD tables do not implement the GSVM native calls; runtime story integration is separate.",
                       "Animated neck targeting, per-part animation blending, original shadows and particle callbacks need their separate runtime systems.",
                       "Only the first interior's five NPCs and exterior actors zero through three are prepared here; later-area NPC packs are not generated.",
                       "Original BGM, event SFX and battle geometry are not embedded in these field packs."]}
        (stage/"area-manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
        stage.rename(output)
    return manifest


def prepare_wes_only(disc, output, parser):
    """Atomically prepare both Wes bundles without rebuilding or editing maps."""
    disc, output = verify_disc(disc), check_output(output)
    if output.exists():
        raise ValueError("Choose a new private output directory")
    geometry.import_parser(title.verify_parser(parser))
    names=("field_common.fsys","S1_out.fsys","S1_shop_1F.fsys")
    archives={name:admit_archive(disc/"files"/name) for name in names}
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".wes-",dir=output.parent) as temporary:
        stage=Path(temporary)/"stage"
        stage.mkdir()
        reports=[]
        for archive,resource,prefix,version in (
            ("S1_out.fsys",0x00E20200,"outskirt_wes",2),
            ("S1_shop_1F.fsys",0x00E60200,"shop_wes",4)):
            data,raw,scene=geometry.load_scene(archives[archive],resource)
            if len(scene.cameras)!=1:
                raise ValueError("Original area camera count changed")
            camera=scene.cameras[0]
            fixed=(*camera.position,*camera.target_position,camera.roll,camera.field_of_view,
                   camera.near,camera.far,camera.aspect,0.)
            lights=(geometry.fixed_lights(raw,data) if version==2 else
                    sampled_lights(raw,data,PROFILES[archive][1])[0])
            prepared,vertices=geometry.prepare_wes(archives["field_common.fsys"],stage,
                                                   prefix,fixed,lights,version)
            if vertices!=2445:
                raise ValueError("Original Wes vertex count changed")
            reports+=prepared
        member=field.member(archives["field_common.fsys"],0x00F70400)
        manifest={"source_commit":title.SOURCE_COMMIT,"parser_commit":title.PARSER_COMMIT,
            "archives":{name:{"bytes":PROFILES[name][0],"sha256":PROFILES[name][1]} for name in names},
            "source_member":{"archive":"field_common.fsys","resource_id":"00f70400",
                             "bytes":len(member),"sha256":hashlib.sha256(member).hexdigest()},
            "source_to_pack_clip":{"1":0,"2":1,"3":2,"0":3},"packs":reports}
        (stage/"wes-manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
        stage.rename(output)
    return manifest


def prepare_exterior_actors(disc, output, parser, indices=None):
    """Prepare the four actors used by the initial exterior script and CCD metadata."""
    disc, output = verify_disc(disc), check_output(output)
    if output.exists():
        raise ValueError("Choose a new private output directory")
    geometry.import_parser(title.verify_parser(parser))
    archives={name:admit_archive(disc/"files"/name) for name in PROFILES}
    indices=tuple(INITIAL_EXTERIOR_ACTORS if indices is None else indices)
    if not indices or len(set(indices))!=len(indices) or any(i not in INITIAL_EXTERIOR_ACTORS for i in indices):
        raise ValueError("Select distinct initial exterior actor indices 0..3")
    common_id,common=common_rel(archives["common.fsys"])
    exterior=archives["S1_out.fsys"]
    interior=archives["S1_shop_1F.fsys"]
    actors={1:actor_records(field.member(exterior,0x02571C00),131,common,0x30,7,
                            "outskirt",INITIAL_EXTERIOR_ACTORS),
            2:actor_records(field.member(interior,0x02581C00),5,common,0x180,5,"shop")}
    data,raw,scene=geometry.load_scene(exterior,0x00E20200)
    if len(scene.cameras)!=1:
        raise ValueError("Original exterior camera count changed")
    camera=scene.cameras[0]
    fixed=(*camera.position,*camera.target_position,camera.roll,camera.field_of_view,
           camera.near,camera.far,camera.aspect,0.)
    lights=geometry.fixed_lights(raw,data)
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".actors-",dir=output.parent) as temporary:
        stage=Path(temporary)/"stage"
        stage.mkdir()
        reports=[]
        for index in indices:
            reports+=npc_pack(exterior,actors[1][index],fixed,lights,
                              PROFILES["S1_out.fsys"][1],stage,format_version=3)
        areas=[]
        for floor,archive,resource in ((1,exterior,0x00E30600),(2,interior,0x00E70600)):
            collision=field.member(archive,resource)
            areas.append({"floor":floor,"actors":actors[floor],
                          "transition_surfaces":source_collision_tables(collision,44),
                          "interaction_surfaces":source_collision_tables(collision,48)})
        (stage/"area-actors.json").write_text(json.dumps(areas,indent=2)+"\n")
        source_members=[]
        for archive,identities in (
            ("S1_out.fsys",(0x02571C00,0x00E20200,0x00E30600,
                            *(actors[1][i]["model_resource"] for i in INITIAL_EXTERIOR_ACTORS))),
            ("S1_shop_1F.fsys",(0x02581C00,0x00E70600)),("common.fsys",(common_id,))):
            for identity in identities:
                member=field.member(archives[archive],identity)
                source_members.append({"archive":archive,"resource_id":f"{identity:08x}",
                    "bytes":len(member),"sha256":hashlib.sha256(member).hexdigest()})
        manifest={"source_commit":title.SOURCE_COMMIT,"parser_commit":title.PARSER_COMMIT,
            "archives":{name:{"bytes":size,"sha256":digest} for name,(size,digest) in PROFILES.items()},
            "source_members":source_members,"packs":reports,"actors":"area-actors.json"}
        (stage/"exterior-actors-manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
        stage.rename(output)
    return manifest


def profile_floors():
    """Story floors whose actors come from the profile alone (they name a map)."""
    return {name:row for name,row in story_floors().items() if "map" in row}


def admit_profile_archive(disc, profile):
    path = Path(disc)/"files"/profile["archive"]
    if path.is_symlink() or not path.is_file() or path.stat().st_size != profile["bytes"]:
        raise ValueError("Unexpected original archive path or size: " + profile["archive"])
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != profile["sha256"]:
        raise ValueError("Archive differs from admitted GC6E01 revision 0: " + profile["archive"])
    return data


def share_models(actors):
    """Group person records by model resource, in original record order.

    Returns [(owner, [sharers...])]. The owner is the first record using a
    resource and names its pack; later records reuse that file, because the
    converted pack depends only on the model, the floor camera and lights.
    """
    groups = {}
    for actor in actors:
        groups.setdefault(actor["model_resource"],[]).append(actor)
    return [(rows[0],rows[1:]) for rows in groups.values()]


def adopt_shared_pack(actor, owner):
    """Point a later record at the pack already prepared for its resource."""
    if actor["model_resource"] != owner["model_resource"] or actor is owner:
        raise ValueError("Shared NPC pack requires the same original model resource")
    actor["file"] = owner["file"]
    actor["shares_model_with"] = owner["index"]
    for key in ("source_clip_ends","particle_callbacks_unrendered","source_part_bones"):
        actor[key] = copy.deepcopy(owner[key])
    actor["default_idle_clip"] = actor["motion_indices"][0]


def actor_bounds(actors, reports):
    """Exact counts the native area/scene must hold; nothing is trimmed."""
    packs = {report["file"]:report for report in reports}
    used = list(dict.fromkeys(actor["file"] for actor in actors if actor["file"]))
    if any(name not in packs for name in used):
        raise ValueError("Actor names an NPC pack that was not prepared")
    missing_idle = [actor["index"] for actor in actors if actor["file"] and
                    actor["motion_indices"][0] >= len(actor["source_clip_ends"])]
    return {"actors":len(actors),
            "actors_with_model":sum(bool(actor["file"]) for actor in actors),
            "actors_without_model":[actor["index"] for actor in actors if not actor["file"]],
            "model_packs":len(used),
            "model_pack_bytes":sum(packs[name]["bytes"] for name in used),
            "model_pack_vertices":sum(packs[name]["vertices"] for name in used),
            "model_pack_textures":sum(packs[name]["textures"] for name in used),
            "largest_pack_bytes":max((packs[name]["bytes"] for name in used),default=0),
            "max_sampled_bones":max((packs[name]["sampled_bones"] for name in used),default=0),
            "max_clips":max((len(packs[name]["clips"]) for name in used),default=0),
            "max_samples":max((sum(clip["samples"] for clip in packs[name]["clips"])
                               for name in used),default=0),
            "initially_loaded":[actor["index"] for actor in actors if actor["load_initial"]],
            "initially_visible":[actor["index"] for actor in actors if actor["visible_initial"]],
            "idle_motion_outside_pack":missing_idle}


def partner_schedules(ends, partner=None):
    """(source clip, authored frames, loop) in pack order for the partner.

    The pack keeps floor_profiles.PARTNER's clip list: PeopleInfo idle, walk
    and run first (pack clips 0, 1, 2, looping, as in the Wes pack), then the
    other listed source motions, one-shot until the runtime's motion native
    selects looping.
    """
    partner = PARTNER if partner is None else partner
    clips, looping = partner["clips"], set(partner["locomotion"])
    if any(clip >= len(ends) for clip in clips):
        raise ValueError("Partner model lacks a listed source motion")
    return [(clip,geometry.sample_sequence(ends[clip]) if ends[clip] else [0.],clip in looping)
            for clip in clips]


def prepare_partner(field_common, common, output, prefix, camera, lights, format_version=2):
    """The second party member's field model, lit and framed like Wes's pack."""
    partner = PARTNER
    info = field.extent(field.rel_data(common,125),0x13E440+partner["people_info"]*44,44)
    if (field.u32(info,12)!=partner["resource"] or
            tuple(info[1:4])!=tuple(partner["locomotion"]) or info[4]!=partner["clips"][4]):
        raise ValueError("Partner PeopleInfo row differs from the reviewed source")
    walk,run,radius = struct.unpack_from(">3f",info,16)
    if not all(math.isfinite(v) and v>0 for v in (walk,run,radius)):
        raise ValueError("Partner movement distances changed")
    _data,raw,scene = geometry.load_scene(field_common,partner["resource"])
    if len(scene.models)!=1:
        raise ValueError("Partner must have exactly one original model")
    model = geometry.Model(scene.models[0],raw.models[0])
    meshes = [geometry.build_mesh(model,i) for i in range(len(model.meshes))]
    schedules = partner_schedules(model.ends)
    samples = sum(len(sequence) for _,sequence,_ in schedules)
    reports = geometry.prepare_pack_set(meshes,schedules,60,[camera]*samples,lights,
                PROFILES["field_common.fsys"][1],output,prefix,expected_count=1,
                format_version=format_version,gpu_textures=True)
    member = field.member(field_common,partner["resource"])
    summary = {"file":reports[0]["file"],"archive":partner["archive"],
               "resource_id":"%08x" % partner["resource"],"member_id":partner["member_id"],
               "people_info":partner["people_info"],"people_info_motions":list(info[1:10]),
               "source_bytes":len(member),"source_sha256":hashlib.sha256(member).hexdigest(),
               "source_clip_count":len(model.ends),"source_clip_ends":model.ends,
               "source_to_pack_clip":{str(clip):index for index,clip in enumerate(partner["clips"])},
               "looping_source_clips":list(partner["locomotion"]),
               "walk_distance":walk*field.SCALE,"run_distance":run*field.SCALE,"radius":radius*field.SCALE,
               "particle_callbacks_unrendered":model.particle_events,
               "pack":reports[0]}
    return summary


def prepare_floor_actors(disc, output, parser, floor):
    """Profile-driven actors, CCD event surfaces and NPC packs of one floor."""
    profile = profile_floors()[floor]
    disc, output = verify_disc(disc), check_output(output)
    if output.exists():
        raise ValueError("Choose a new private output directory")
    geometry.import_parser(title.verify_parser(parser))
    archive = admit_profile_archive(disc,profile)
    common_archive = admit_archive(disc/"files"/"common.fsys")
    field_common = admit_archive(disc/"files"/"field_common.fsys")
    common_id,common = common_rel(common_archive)
    floor_rel = field.member(archive,profile["rel"])
    actors = actor_records(floor_rel,profile["module"],common,profile["people_table"],
                           profile["people_count"],profile["prefix"])
    data,raw,scene = geometry.load_scene(archive,profile["map"])
    if len(scene.cameras)!=1:
        raise ValueError("Original floor camera count changed")
    camera = scene.cameras[0]
    fixed = (*camera.position,*camera.target_position,camera.roll,camera.field_of_view,
             camera.near,camera.far,camera.aspect,0.)
    lights = geometry.fixed_lights(raw,data)
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".floor-actors-",dir=output.parent) as temporary:
        stage = Path(temporary)/"stage"
        stage.mkdir()
        reports,shared = [],[]
        for owner,sharers in share_models(actors):
            try:
                field.member(archive,owner["model_resource"])
            except ValueError as error:
                raise ValueError("Person %d model %08x is not in %s: %s" % (
                    owner["index"],owner["model_resource"],profile["archive"],error)) from error
            # Exterior actors use C3T3 like the first exterior's NPC packs.
            reports += npc_pack(archive,owner,fixed,lights,profile["sha256"],stage,format_version=3)
            for actor in sharers:
                adopt_shared_pack(actor,owner)
            if sharers:
                shared.append({"resource_id":"%08x" % owner["model_resource"],"file":owner["file"],
                               "actors":[owner["index"],*(actor["index"] for actor in sharers)]})
        partner = prepare_partner(field_common,common,stage,profile["prefix"]+"_partner",fixed,lights,
                                  profile.get("pack_format",2))
        camera_reports = []
        for index,identity in enumerate(profile.get("event_cameras",())):
            # Type-0x18 camera members played by the field bank (native 2A).
            _data,_raw,camera_scene = geometry.load_scene(archive,identity)
            if len(camera_scene.cameras)!=1:
                raise ValueError("Original event camera %08x changed" % identity)
            track = sampled_camera(camera_scene.cameras[0],profile["sha256"],identity)
            name = "%s_camera%s.c3c" % (profile["prefix"],"" if index==0 else str(index))
            (stage/name).write_bytes(track)
            camera_reports.append({"file":name,"resource_id":"%08x" % identity,"bytes":len(track),
                                   "samples":(len(track)-64)//44,"sha256":hashlib.sha256(track).hexdigest()})
        collision = field.member(archive,profile["collision"])
        areas = [{"floor":profile["floor"],"actors":actors,
                  "transition_surfaces":source_collision_tables(collision,44),
                  "interaction_surfaces":source_collision_tables(collision,48)}]
        (stage/"area-actors.json").write_bytes((json.dumps(areas,indent=2)+"\n").encode())
        source_members = []
        resources = dict.fromkeys(actor["model_resource"] for actor in actors)
        for container,blob,identities in (
                (profile["archive"],archive,(profile["rel"],profile["map"],profile["collision"],*resources)),
                ("common.fsys",common_archive,(common_id,)),
                ("field_common.fsys",field_common,(PARTNER["resource"],))):
            for identity in identities:
                source = field.member(blob,identity)
                source_members.append({"archive":container,"resource_id":"%08x" % identity,
                    "bytes":len(source),"sha256":hashlib.sha256(source).hexdigest()})
        manifest = {"floor_profile":floor,"floor":profile["floor"],
                    "lifecycle_row":profile["lifecycle_row"],"resource_group":profile["resource_group"],
                    "source_commit":title.SOURCE_COMMIT,"parser_commit":title.PARSER_COMMIT,
                    "archives":{profile["archive"]:{"bytes":profile["bytes"],"sha256":profile["sha256"]},
                                "common.fsys":{"bytes":PROFILES["common.fsys"][0],
                                               "sha256":PROFILES["common.fsys"][1]},
                                "field_common.fsys":{"bytes":PROFILES["field_common.fsys"][0],
                                                     "sha256":PROFILES["field_common.fsys"][1]}},
                    "source_members":source_members,"packs":reports,"partner":partner,
                    "actors":"area-actors.json",
                    "shared_models":shared,"event_cameras":camera_reports,"bounds":actor_bounds(actors,reports),
                    "event_surfaces":{"transition":len(areas[0]["transition_surfaces"]),
                                      "interaction":len(areas[0]["interaction_surfaces"])},
                    "limitations":[
                        "Actor geometry, authored motion clips and CCD event tables only; GSVM natives, dialogue and battles are separate runtime work.",
                        "Records sharing one model resource name the same pack file; the pack is written once.",
                        "Neck targeting, per-part animation blending, shadows and particle callbacks are not rendered by these packs.",
                        "NPC packs carry the floor map's fixed camera and lights; BGM and event SFX are not embedded."]}
        (stage/"area-manifest.json").write_bytes((json.dumps(manifest,indent=2)+"\n").encode())
        stage.rename(output)
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    parser.add_argument("--parser",type=Path,default=title.ROOT/"vendor/hsd-importer")
    mode=parser.add_mutually_exclusive_group()
    mode.add_argument("--wes-only",action="store_true",
                      help="Prepare only exterior/interior Wes bundles, including source motion zero")
    mode.add_argument("--exterior-actors-only",nargs="*",type=int,choices=INITIAL_EXTERIOR_ACTORS,default=None,
                      help="Prepare selected initial exterior actors and area metadata (default all four)")
    mode.add_argument("--floor",choices=sorted(profile_floors()),
                      help="Prepare one profile floor's actors, event surfaces and NPC packs")
    args = parser.parse_args()
    try:
        if args.floor:
            result=prepare_floor_actors(args.disc,args.output,args.parser,args.floor)
            result={key:value for key,value in result.items() if key!="packs"}
        elif args.exterior_actors_only is not None:
            result=prepare_exterior_actors(args.disc,args.output,args.parser,args.exterior_actors_only or None)
        else:
            result=(prepare_wes_only if args.wes_only else prepare)(args.disc,args.output,args.parser)
    except (OSError,ValueError,KeyError) as error:
        parser.exit(1,"Area preparation failed: %s\n"%error)
    print(json.dumps(result,indent=2))


if __name__ == "__main__":
    main()
