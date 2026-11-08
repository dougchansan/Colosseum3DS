#!/usr/bin/env python3
"""Prepare the authentic GC6E01 title as native geometry and sampled HSD state.

This bounded exporter admits one reviewed archive, retains all 43 rigid meshes,
and evaluates the original joint, camera, material and texture curves. It needs
Python, NumPy and the pinned parser; Blender is not required. No game assets are
distributed with the tool. See docs/title-format.md for the supported subset.
"""
import argparse
import bisect
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import tempfile
import types

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from prepare_boot_media import verify_disc
from prepare_model import check_output, rgba_top_down, resize_nearest

PARSER_COMMIT = "dc71de837978a6a2fe8c5082f2840cfafb135235"
SOURCE_COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
ARCHIVE_SHA256 = "8ae138e5f73cd024a60fe42da2a08220f42a8ce90424c3649cc67e7f27b4797a"
ARCHIVE_BYTES = 1374336
MAX_BYTES = 24 * 1024 * 1024
FPS = 60.0
LIMITS = (64, 32768, 64, 32, 32, 4096, 8)
CULL_BACK, CULL_FRONT, TRANSLUCENT, ADDITIVE = 1, 2, 4, 8
LIT, VERTEX_COLOR, VERTEX_ALPHA, SPECULAR = 16, 32, 64, 128
TEV_LERP, NO_DEPTH_WRITE = 256, 512


def verify_parser(parser):
    """Verify the parser dependency closure by the pinned Git blob identities."""
    parser = Path(parser).resolve(strict=True)
    manifest = json.loads((ROOT / "config/title-parser-files.json").read_text())
    if manifest["commit"] != PARSER_COMMIT:
        raise ValueError("Parser manifest pin differs from the exporter pin")
    for name, expected in manifest["files"].items():
        path = parser / name
        if path.is_symlink():
            raise ValueError("Parser source cannot be a symbolic link: " + name)
        data = path.read_bytes()
        actual = hashlib.sha1(b"blob " + str(len(data)).encode() + b"\0" + data).hexdigest()
        if actual != expected:
            raise ValueError("Parser file differs from the pinned source: " + name)
    return parser


def admit_archive(path):
    path = Path(path)
    if path.is_symlink() or path.stat().st_size != ARCHIVE_BYTES:
        raise ValueError("Expected the reviewed title.fsys size and regular path")
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != ARCHIVE_SHA256:
        raise ValueError("title.fsys differs from the reviewed GC6E01 revision 0 archive")
    return data


def import_parser(parser):
    # Avoid the Blender registration entry point; these phases are pure data.
    sys.path.insert(0, str(parser))
    for name in ("importer", "importer.phases"):
        package = types.ModuleType(name)
        package.__path__ = [str(parser / name.replace(".", "/"))]
        sys.modules[name] = package


def load_title(parser, archive):
    import_parser(parser)
    from importer.phases.extract.helpers.fsys import parse_fsys
    from importer.phases.route.route import route_sections
    from importer.phases.parse.parse import parse_sections
    from importer.phases.describe.describe import describe_scene
    options = {"filepath": "title.fsys", "game": "COLO_XD", "max_frame": 1000000000,
               "section_names": [], "ik_hack": False}
    entries = {Path(name).stem: data for data, _extension, name in parse_fsys(archive, "title.fsys")}
    result = []
    for name in ("logo_demo", "cam_logo_demo_start", "cam_logo_demo_stop"):
        data = entries[name]
        sections = parse_sections(data, route_sections(data), options)
        if len(sections) != 1 or sections[0].section_name != "scene_data":
            raise ValueError("Expected one HSD scene_data root: " + name)
        result.append((data, sections[0].root_node, describe_scene(sections, options)))
    return result


def curve_value(keys, frame, default=0.0):
    """HSD CON/LIN/SPL interpolation, including asymmetric SLP tangents.

    The right key stores the two tangents for the interval ending at that key:
    slope_in is the left endpoint's outgoing slope; slope_out is the right
    endpoint's incoming slope. The left key selects the interval opcode.
    """
    if not keys:
        return default
    index = bisect.bisect_right([key.frame for key in keys], frame) - 1
    if index < 0:
        return keys[0].value
    left = keys[index]
    if index + 1 == len(keys):
        return left.value
    right = keys[index + 1]
    mode = getattr(left.interpolation, "value", left.interpolation)
    if mode == "CONSTANT":
        return left.value
    span = right.frame - left.frame
    if span <= 0:
        return right.value
    t = (frame - left.frame) / span
    if mode == "LINEAR":
        return left.value + (right.value - left.value) * t
    if mode != "BEZIER":
        raise ValueError("Unsupported HSD interpolation mode")
    t2, t3 = t * t, t * t * t
    return ((2*t3 - 3*t2 + 1) * left.value + (t3 - 2*t2 + t) * span * (right.slope_in or 0)
            + (-2*t3 + 3*t2) * right.value + (t3 - t2) * span * (right.slope_out or 0))


def clamp01(value):
    return max(0.0, min(1.0, value))


def material_alpha(value):
    # MObjUpdateFunc HSD_A_M_ALPHA stores transparency, not opacity.
    return clamp01(1.0 - value)


def tev_byte(value):
    # TObjUpdateFunc truncates after a float32 multiplication by 255.
    value = struct.unpack("<f", struct.pack("<f", clamp01(value)))[0]
    value = struct.unpack("<f", struct.pack("<f", value * 255.0))[0]
    return int(value) / 255.0


def texture_transform(scale, translation, repeat):
    if any(abs(value) < 1e-9 for value in scale[:2]):
        raise ValueError("Zero texture scale")
    factors = tuple(repeat[i] / scale[i] for i in range(2))
    return (*factors, *(-translation[i] * factors[i] for i in range(2)))


def tree_nodes(root):
    result = []
    def walk(node):
        if node is None:
            return
        result.append(node)
        walk(node.child)
        walk(node.next)
    walk(root)
    return result


def channels(aobj, allowed):
    from importer.phases.describe.helpers.keyframe_decoder import decode_fobjdesc
    result = {}
    if aobj is None or aobj.flags & (1 << 30):
        return result
    node = aobj.frame
    while node:
        if node.type not in allowed or node.type in result:
            raise ValueError("Unsupported or duplicate HSD animation channel: " + str(node.type))
        keys = decode_fobjdesc(node)
        if not keys or any(not math.isfinite(key.frame) or not math.isfinite(key.value) for key in keys):
            raise ValueError("Empty or nonfinite HSD animation curve")
        if any(keys[i].frame > keys[i+1].frame for i in range(len(keys)-1)):
            raise ValueError("Unordered animation keys")
        result[node.type] = keys
        node = node.next
    return result


def visibility_at(bones, tracks, frame):
    visible = [not bone.is_hidden for bone in bones]
    # Branch changes alter each descendant flag; NODE changes only its own.
    # Process pre-order so a later descendant NODE can override its branch.
    for index, track in enumerate(tracks):
        if 12 in track:
            value = curve_value(track[12], frame) > 0.5
            visible[index] = value
            for child in range(index + 1, len(bones)):
                parent = bones[child].parent_index
                while parent is not None and parent != index:
                    parent = bones[parent].parent_index
                if parent == index:
                    visible[child] = value
        if 11 in track:
            visible[index] = curve_value(track[11], frame) > 0.5
    return visible


def collect_raw(model, raw_model):
    joints = tree_nodes(raw_model.root_joint)
    if len(joints) != len(model.bones):
        raise ValueError("Raw/IR joint count differs")
    raw_meshes = []
    for bone_index, joint in enumerate(joints):
        if joint.flags & (1 << 12) or joint.reference is not None:
            raise ValueError("Instanced or constrained joints are outside the title subset")
        dobj = joint.property
        while dobj is not None:
            pobj = dobj.pobject
            while pobj:
                raw_meshes.append((bone_index, dobj, pobj))
                pobj = pobj.next
            dobj = dobj.next
    if len(raw_meshes) != len(model.meshes):
        raise ValueError("Raw/IR mesh count differs")
    joint_sets, material_sets, ends = [], [], []
    for animation_root, material_root in zip(raw_model.animated_joints, raw_model.animated_material_joints):
        anodes, mnodes = tree_nodes(animation_root), tree_nodes(material_root)
        if len(anodes) != len(joints) or len(mnodes) != len(joints):
            raise ValueError("Animation trees must match every joint")
        joint_sets.append([channels(node.animation, {1,2,3,5,6,7,11,12}) for node in anodes])
        ends.append(max(node.animation.end_frame for node in anodes if node.animation))
        material_set = {}
        for joint, mnode in zip(joints, mnodes):
            dobj, anim = joint.property, mnode.animation
            while anim:
                if dobj is None or anim.render_animation is not None:
                    raise ValueError("Material animation topology is unsupported")
                mat = channels(anim.animation, {10})
                tex = {}
                ta = anim.texture_animation
                if ta:
                    if ta.next or ta.id != 0 or ta.image_table_count or ta.palette_table_count:
                        raise ValueError("Texture image swaps/multiple texture animation unsupported")
                    tex = channels(ta.animation, {2,3,12,13,14,16,17,18})
                material_set[dobj.address] = (mat, tex)
                anim, dobj = anim.next, dobj.next
        material_sets.append(material_set)
    if ends != [199.0, 20.0, 1499.0] or len(joint_sets) != 3:
        raise ValueError("Reviewed title animation durations changed")
    return joints, raw_meshes, joint_sets, material_sets, ends


def geometry(model, raw_meshes):
    import numpy as np
    vertices, batches, textures, tex_ids, provenance = [], [], [], {}, []
    for index, (mesh, (bone_index, dobj, _pobj)) in enumerate(zip(model.meshes, raw_meshes)):
        if mesh.parent_bone_index != bone_index or mesh.bone_weights.type.value != "RIGID" or mesh.shape_keys:
            raise ValueError("Title geometry must be rigid and unmorphed")
        mob, mat = dobj.mobject, dobj.mobject.material
        tex = mob.texture
        if tex and (tex.next or tex.source != 4 or any(tex.rotation) or tex.wrap_t == 2):
            raise ValueError("Title texture generator exceeds the reviewed affine UV subset")
        if mob.render_data or mob.render_mode & 8:
            raise ValueError("Custom material renders/specular exceed the reviewed title subset")
        mode = mob.render_mode
        flags = (CULL_BACK if mesh.cull_back else 0) | (CULL_FRONT if mesh.cull_front else 0)
        flags |= TRANSLUCENT if mode & (1 << 30) else 0
        flags |= NO_DEPTH_WRITE if mode & (1 << 29) else 0
        flags |= LIT if mode & 4 else 0
        flags |= VERTEX_COLOR if mode & 3 in (2, 3) else 0
        flags |= VERTEX_ALPHA if (mode >> 13) & 3 in (2, 3) else 0
        if tex and tex.tev and tex.tev.active:
            tev = tex.tev
            expected = {"color_op":0,"alpha_op":0,"color_bias":0,"alpha_bias":0,
                        "color_scale":0,"alpha_scale":0,"color_clamp":1,"alpha_clamp":1,
                        "color_a":133,"color_b":128,"color_c":8,"color_d":15,
                        "alpha_a":7,"alpha_b":7,"alpha_c":7,"alpha_d":7,"active":0x40000077}
            if any(getattr(tev, name) != value for name, value in expected.items()):
                raise ValueError("Unsupported title TEV expression")
            flags |= TEV_LERP
        if tex:
            image = mesh.material.texture_layers[0].image
            pixels = rgba_top_down(image.pixels, image.width, image.height)
            width, height, pixels = resize_nearest(pixels, image.width, image.height)
            wraps = (tex.wrap_s, tex.wrap_t)
            key = (width, height, hashlib.sha256(pixels).digest(), *wraps)
            source_dimensions = [image.width, image.height]
        else:
            width, height, pixels, wraps = 8, 8, bytes([255]) * 256, (0, 0)
            key = (8, 8, hashlib.sha256(pixels).digest(), 0, 0)
            source_dimensions = [1, 1]
        if key not in tex_ids:
            tex_ids[key] = len(textures)
            textures.append((width, height, pixels, *wraps))
            provenance.append({"source_dimensions":source_dimensions,"packed_dimensions":[width,height],
                               "wrap":list(wraps),"sha256":hashlib.sha256(pixels).hexdigest()})
        texture_index = tex_ids[key]
        pe = mob.pixel_engine_data
        alpha = [0, 0, 7, 7, 0]
        depth = 7 if mode & (1 << 27) else 3
        if pe:
            if (pe.flags, pe.type, pe.source_factor, pe.destination_factor, pe.z_comp) != (25,1,4,1,3):
                raise ValueError("Unsupported custom pixel-engine state")
            flags |= ADDITIVE | NO_DEPTH_WRITE | TRANSLUCENT
            alpha = [pe.reference_0, pe.reference_1, pe.alpha_component_0, pe.alpha_component_1, pe.alpha_op]
            depth = pe.z_comp
        elif mode & (1 << 30) and not mode & (1 << 29):
            alpha = [0,0,4,4,0]
        first = len(vertices)
        loop_count = sum(map(len, mesh.faces))
        if len(mesh.uv_layers) > 1 or (mesh.uv_layers and len(mesh.uv_layers[0].uvs) != loop_count):
            raise ValueError("Unexpected UV geometry")
        uvs = mesh.uv_layers[0].uvs if mesh.uv_layers else [(0,0)] * loop_count
        colors = next((layer.colors for layer in mesh.color_layers if layer.name == "color_0"), None)
        alphas = next((layer.colors for layer in mesh.color_layers if layer.name == "alpha_0"), None)
        if colors is not None and len(colors) != loop_count or alphas is not None and len(alphas) != loop_count:
            raise ValueError("Vertex color loop count mismatch")
        if flags & LIT and (not mesh.normals or len(mesh.normals) != loop_count):
            raise ValueError("Lit title mesh has no original normals")
        normal_matrix = np.linalg.inv(np.asarray(model.bones[bone_index].world_matrix)[:3,:3]).T
        offset = 0
        for face in mesh.faces:
            if len(face) not in (3, 4):
                raise ValueError("Reviewed title contains unsupported polygons")
            for corner in (corner for triangle in range(1, len(face)-1) for corner in (0, triangle, triangle+1)):
                vertex = face[corner]
                loop = offset + corner
                normal = normal_matrix @ np.asarray(mesh.normals[loop]) if mesh.normals else np.array([0.,1.,0.])
                normal /= max(float(np.linalg.norm(normal)), 1e-30)
                color = list(colors[loop] if colors else (1,1,1,1))
                if alphas:
                    color[3] = alphas[loop][0]
                rgba = [round(clamp01(value) * 255) for value in color]
                u,v = uvs[loop]
                vertices.append((*mesh.vertices[vertex], *normal, u, 1-v, *rgba, bone_index))
            offset += len(face)
        values = (*mat.diffuse.asRGBList(), *mat.ambient.asRGBList(), *mat.specular.asRGBList(),
                  mat.shininess, mat.alpha, tex.blending if tex else 1.0)
        batches.append((first, len(vertices)-first, texture_index, flags,
                        (tex.flags >> 16) & 15 if tex else 0,
                        (tex.flags >> 20) & 15 if tex else 0, mode, tex.flags if tex else 0,
                        *values, *alpha, depth, 0, 0))
    return vertices, batches, textures, provenance


def camera_values(camera, frame):
    if camera.projection.value != "PERSPECTIVE" or len(camera.animations) != 1:
        raise ValueError("Unsupported title camera")
    anim = camera.animations[0]
    fields = ("eye_x","eye_y","eye_z","target_x","target_y","target_z","roll","fov","near","far")
    defaults = (*camera.position, *camera.target_position, camera.roll, camera.field_of_view, camera.near, camera.far)
    return (*[curve_value(getattr(anim,name),frame,default) for name,default in zip(fields,defaults)], camera.aspect, 0.0)


def light_values(root, dat, units):
    result = []
    for light_set in root.lights:
        light = light_set.light
        if light.link or light_set.animations or light.attn_flags or light.flags & 3 not in (0,1,2):
            raise ValueError("Unsupported animated/chained/spot title light")
        # Light.Color is raw u8, unlike Material.Color (normalized at parse).
        color = [getattr(light.color,k) / 255.0 for k in ("red","green","blue","alpha")]
        pos = [v * units for v in light.position.position] if light.position else [0,0,0]
        interest = [v * units for v in light.interest.position] if light.interest else [0,0,0]
        brightness, distance, function = 1.0, 1.0, 0.0
        if light.flags & 3 == 2:
            brightness = light.property.reference_br
            distance = light.property.reference_distance * units
            # The pinned parser omits the third field; canonical LOBJ descriptor
            # specifies GXDistAttnFn at offset8 of HSD_LightPointDesc.
            function = float(struct.unpack_from(">I", dat, 32 + light.property.address + 8)[0])
        result.append((light.flags,light.attn_flags,*color,*pos,*interest,brightness,distance,function,0.0))
    return result


def sample_frames(model, raw_meshes, joint_sets, material_sets, ends, cameras):
    import numpy as np
    from shared.helpers.math_shim import compile_srt_matrix, matrix_to_list
    from shared.helpers.scale import GC_TO_METERS
    bones = model.bones
    rest_inverse = [np.linalg.inv(np.asarray(bone.world_matrix)) for bone in bones]
    static_local = [np.asarray(bone.local_matrix) for bone in bones]
    frames, clips = bytearray(), []
    for clip, (tracks, material_tracks, end) in enumerate(zip(joint_sets,material_sets,ends)):
        count = int(end * 2) + 1
        clips.append((sum(c[1] for c in clips),count,int(clip == 2),0))
        for sample in range(count):
            frame = sample * 0.5
            # The endpoint of the looping clip represents its reset state.
            model_frame = 0.0 if clip == 2 and sample == count-1 else frame
            world = []
            for index, (bone, track) in enumerate(zip(bones, tracks)):
                animated = any(kind in track for kind in (1,2,3,5,6,7))
                local = static_local[index]
                if animated:
                    rotation = [curve_value(track.get(1+i),model_frame,bone.rotation[i]) for i in range(3)]
                    position = [curve_value(track.get(5+i),model_frame,bone.position[i]/GC_TO_METERS)*GC_TO_METERS for i in range(3)]
                    parent_scale = bones[bone.parent_index].accumulated_scale if bone.parent_index is not None else None
                    local = np.asarray(matrix_to_list(compile_srt_matrix(bone.scale,rotation,position,parent_scale)))
                current = world[bone.parent_index] @ local if bone.parent_index is not None else local
                world.append(current)
                matrix = (current @ rest_inverse[index])[:3,:].reshape(-1)
                frames.extend(struct.pack("<12f", *matrix))
            visible = visibility_at(bones,tracks,model_frame)
            for bone_index,dobj,_pobj in raw_meshes:
                mat,tex = material_tracks.get(dobj.address,({},{}))
                texture = dobj.mobject.texture
                if texture:
                    translation = [curve_value(tex.get(2+i),model_frame,texture.translation[i]) for i in range(2)]
                    uv = texture_transform(texture.scale,translation,(texture.repeat_s,texture.repeat_t))
                    tev0 = texture.tev.tev0.asRGBList() if texture.tev else [0,0,0]
                    konst = texture.tev.konst.asRGBList() if texture.tev else [1,1,1]
                else:
                    uv,tev0,konst = (1,1,0,0),[0,0,0],[1,1,1]
                for component in range(3):
                    if 16+component in tex:
                        tev0[component] = tev_byte(curve_value(tex[16+component],model_frame))
                    if 12+component in tex:
                        konst[component] = tev_byte(curve_value(tex[12+component],model_frame))
                alpha = material_alpha(curve_value(mat[10],model_frame)) if 10 in mat else dobj.mobject.material.alpha
                frames.extend(struct.pack("<12f",*uv,alpha,float(visible[bone_index]),*tev0,*konst))
            camera_frame = frame + (ends[0] if clip == 1 else 0)
            frames.extend(struct.pack("<12f",*camera_values(cameras[0 if clip < 2 else 1],camera_frame)))
        print("Sampled title clip %d: %d states at 60 Hz" % (clip,count), flush=True)
    return frames, clips


def encode_pack(bones, vertices, batches, textures, clips, lights, frames, fps=FPS,
                format_version=1, archive_sha256=ARCHIVE_SHA256):
    if format_version not in (1,2,3,4):
        raise ValueError("Unsupported title pack version")
    counts = (bones,len(vertices),len(batches),len(textures),len(clips),sum(clip[1] for clip in clips),len(lights))
    if any(not isinstance(n,int) or not 0 < n <= limit for n,limit in zip(counts,LIMITS)):
        raise ValueError("Title exceeds section bounds")
    if not math.isfinite(fps) or not 1 <= fps <= 120 or len(vertices) % 3:
        raise ValueError("Invalid title timing or triangle count")
    frame_bytes = (bones + 1) * 48 + len(batches) * {1:48,2:64,3:96,4:96}[format_version]
    if len(frames) != frame_bytes * counts[5]:
        raise ValueError("Title frame extent mismatch")
    if any(not math.isfinite(value) or abs(value)>1000000 for (value,) in struct.iter_unpack("<f",frames)):
        raise ValueError("Nonfinite or excessive title frame value")
    for vertex in vertices:
        if len(vertex)!=(15 if format_version>=2 else 13) or not 0<=vertex[12]<bones or any(not math.isfinite(v) or abs(v)>1000000 for v in vertex[:8]):
            raise ValueError("Invalid title vertex")
        if format_version>=2 and any(not math.isfinite(v) or abs(v)>1000000 for v in vertex[13:15]):
            raise ValueError("Invalid second texture coordinate")
        if any(not isinstance(v,int) or not 0<=v<=255 for v in vertex[8:12]):
            raise ValueError("Invalid title vertex color")
    next_vertex = 0
    for batch in batches:
        if len(batch)!=28 or batch[0]!=next_vertex or batch[1]<=0 or batch[1]%3 or not 0<=batch[2]<len(textures) or batch[3]&~{1:1023,2:8191,3:131071,4:131071}[format_version]:
            raise ValueError("Invalid or noncanonical title batch")
        if any(not math.isfinite(v) for v in batch[8:20]) or (format_version<3 and batch[27]):
            raise ValueError("Invalid title batch material or reserved word")
        if (batch[3]&16384 and not batch[3]&8192) or (batch[3]&32768 and (batch[3]&24576!=24576 or batch[3]&65536)) or (batch[3]&65536 and (not batch[3]&8192 or batch[3]&16384)):
            raise ValueError("Conflicting source material expressions")
        if batch[3]&(1024|8192):
            if not 0<=batch[26]<len(textures) or tuple(batch[4:6])!=(4,0):
                raise ValueError("Invalid dual texture material")
            if batch[3]&1024 and (not batch[3]&TEV_LERP or batch[3]&8192):
                raise ValueError("Conflicting dual texture expressions")
        elif batch[26]:
            raise ValueError("Single texture batch has an extra texture")
        if format_version>=3 and (not math.isfinite(batch[27]) or not 0<=batch[27]<=1 or (not batch[3]&8192 and batch[27])):
            raise ValueError("Invalid reflection blend factor")
        next_vertex += batch[1]
    if next_vertex != len(vertices):
        raise ValueError("Title batches do not cover vertices")
    next_frame = 0
    for clip in clips:
        if len(clip)!=4 or clip[0]!=next_frame or clip[1]<1 or clip[2] not in (0,1) or clip[3]:
            raise ValueError("Invalid or noncanonical title clip")
        next_frame += clip[1]
    for light in lights:
        if len(light)!=(20 if format_version==4 else 16):
            raise ValueError("Invalid title light record width")
        flags,attenuation=light[:2]
        if (not isinstance(flags,int) or flags&~63 or attenuation!=0 or
            (flags&3==3 and format_version<4) or
            any(not math.isfinite(v) or not 0<=v<=1 for v in light[2:6]) or
            any(not math.isfinite(v) or abs(v)>1000000 for v in light[6:12]) or
            not math.isfinite(light[12]) or not 0<=light[12]<=1 or
            not math.isfinite(light[13]) or not 0<light[13]<=1000000 or
            not math.isfinite(light[14]) or light[14] not in (0,1,2,3) or light[15]!=0):
            raise ValueError("Unsupported title light color, position or attenuation")
        if format_version==4 and (not isinstance(light[16],int) or not 0<=light[16]<=6 or
            not math.isfinite(light[17]) or abs(light[17])>360 or light[18] or light[19] or
            (flags&3!=3 and (light[16] or light[17]))):
            raise ValueError("Invalid title spotlight parameters")
    vdata = b"".join(struct.pack("<8f4BI2f" if format_version>=2 else "<8f4BI",*vertex) for vertex in vertices)
    bdata = b"".join(struct.pack("<8I12f7If" if format_version>=3 else "<8I12f8I",*batch) for batch in batches)
    cdata = b"".join(struct.pack("<4I",*clip) for clip in clips)
    ldata = b"".join(struct.pack("<2I14fIf2I" if format_version==4 else "<2I14f",*light) for light in lights)
    offsets = [128]
    for size in (len(vdata),len(bdata),len(textures)*24,len(cdata),len(ldata),len(frames)):
        offsets.append(offsets[-1]+size)
    pixels, descriptors = bytearray(),bytearray()
    for width,height,rgba,wrap_s,wrap_t in textures:
        if any(d<8 or d>1024 or d&(d-1) for d in (width,height)) or len(rgba)!=width*height*4 or wrap_s not in (0,1,2) or wrap_t not in (0,1,2):
            raise ValueError("Invalid title texture")
        descriptors.extend(struct.pack("<6I",width,height,offsets[-1]+len(pixels),len(rgba),wrap_s,wrap_t))
        pixels.extend(rgba)
    total = offsets[-1] + len(pixels)
    if total > MAX_BYTES:
        raise ValueError("Title pack exceeds 24 MiB")
    header = bytearray(128)
    struct.pack_into("<4s9If9I",header,0, {1:b"C3T1",2:b"C3T2",3:b"C3T3",4:b"C3T4"}[format_version],format_version,total,*counts,fps,*offsets,frame_bytes,len(pixels))
    header[80:112] = bytes.fromhex(archive_sha256)
    return bytes(header+vdata+bdata+descriptors+cdata+ldata+frames+pixels)


def prepare(disc, output, parser):
    disc = verify_disc(disc)
    output = check_output(output)
    parser = verify_parser(parser)
    sources = load_title(parser,admit_archive(disc/"files/title.fsys"))
    (dat,root,scene),(_,_,start),(_,_,stop) = sources
    if len(scene.models)!=1 or len(scene.models[0].bones)!=31 or len(scene.models[0].meshes)!=43 or scene.fogs:
        raise ValueError("Reviewed title model inventory changed")
    model = scene.models[0]
    if model.shape_animations or model.ik_constraints or model.copy_location_constraints or model.track_to_constraints or model.copy_rotation_constraints or model.limit_rotation_constraints or model.limit_location_constraints:
        raise ValueError("Unsupported title constraint/morph animation")
    _joints,raw_meshes,joint_sets,material_sets,ends = collect_raw(model,root.models[0])
    vertices,batches,textures,texture_report = geometry(model,raw_meshes)
    frames,clips = sample_frames(model,raw_meshes,joint_sets,material_sets,ends,[start.cameras[0],stop.cameras[0]])
    from shared.helpers.scale import GC_TO_METERS
    lights = light_values(root,dat,GC_TO_METERS)
    pack = encode_pack(len(model.bones),vertices,batches,textures,clips,lights,frames)
    report = {"format":"C3T1","source_commit":SOURCE_COMMIT,"parser_commit":PARSER_COMMIT,
              "archive_sha256":ARCHIVE_SHA256,"bytes":len(pack),"sha256":hashlib.sha256(pack).hexdigest(),
              "bones":len(model.bones),"batches":len(batches),"vertices":len(vertices),"triangles":len(vertices)//3,
              "textures":texture_report,"fps":FPS,"clips":[{"first":c[0],"count":c[1],"loop":bool(c[2]),"authored_end":ends[i]} for i,c in enumerate(clips)],
              "source_units_to_pack":GC_TO_METERS,"member_sha256":[hashlib.sha256(item[0]).hexdigest() for item in sources],
              "limitations":["Static and sampled native HSD subset; no title menus or gameplay in this pack.",
                             "Source curves sampled at the NTSC 60 Hz title tick; float32 affine interpolation between ticks.",
                             "Non-power-of-two images resampled by nearest pixel to the next power of two; original UV repeat domain retained.",
                             "Stop-camera channels are constant; sharing the model-loop time does not alter this camera."]}
    output.parent.mkdir(parents=True,exist_ok=True)
    # Publish only after the full pack has been generated successfully.
    with tempfile.TemporaryDirectory(prefix=".title-",dir=output.parent) as temporary:
        staged = Path(temporary)/"title.c3t"
        staged.write_bytes(pack)
        sidecar = Path(temporary)/"title.c3t.json"
        sidecar.write_text(json.dumps(report,indent=2)+"\n")
        if output.exists() or output.with_suffix(output.suffix+".json").exists():
            raise ValueError("Output appeared during conversion; refusing to overwrite")
        staged.rename(output)
        sidecar.rename(output.with_suffix(output.suffix+".json"))
    print("Prepared %s (%d bytes, %d triangles)" % (output,len(pack),len(vertices)//3),flush=True)
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc",required=True,type=Path)
    parser.add_argument("--output",required=True,type=Path)
    parser.add_argument("--parser",type=Path,default=ROOT/"vendor/hsd-importer")
    args = parser.parse_args()
    try:
        prepare(args.disc,args.output,args.parser)
    except (ValueError,OSError,KeyError) as error:
        parser.exit(1,"Title preparation failed: %s\n" % error)


if __name__ == "__main__":
    main()
