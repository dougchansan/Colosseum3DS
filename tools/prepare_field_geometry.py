"""Reviewed GC6E01 Outskirt geometry and HSD animation conversion.

This module is used by prepare_outskirt.py after exact archive admission. It
evaluates the original SRT, aligned scale, two-bone IK and skin envelopes. A
skin envelope is an affine matrix, so each distinct envelope becomes a sampled
C3T bone; this retains weighted skinning without adding a GPU skinning ABI.
"""
import copy
import hashlib
import math
import struct

import numpy as np

import prepare_title as title

DUAL_MODULATE, SUBTRACT, ADDITIVE_ONE, REFLECTION_BLEND = 1024, 2048, 4096, 8192
UNITS = 0.1


def import_parser(parser):
    title.import_parser(parser)
    # The pinned pure-Python shim implements non-mutating versions, while its
    # envelope normal helper calls Blender's two mutating spellings. Adapt the
    # runtime class, leaving every verified vendor source byte unchanged.
    from shared.helpers.math_shim import Matrix
    if not hasattr(Matrix, "invert"):
        Matrix.invert = lambda self: setattr(self, "_rows", self.inverted()._rows)
    if not hasattr(Matrix, "transpose"):
        Matrix.transpose = lambda self: setattr(self, "_rows", self.transposed()._rows)


def load_scene(archive, identity):
    from prepare_outskirt import member
    from importer.phases.route.route import route_sections
    from importer.phases.parse.parse import parse_sections
    from importer.phases.describe.describe import describe_scene
    data = member(archive, identity)
    options = {"filepath": "%08x.dat" % identity, "game": "COLO_XD",
               "max_frame": 1000000000, "section_names": [], "ik_hack": False}
    sections = parse_sections(data, route_sections(data), options)
    if len(sections) != 1 or sections[0].section_name != "scene_data":
        raise ValueError("Expected one scene_data root for %08x" % identity)
    return data, sections[0].root_node, describe_scene(sections, options)


def raw_meshes(raw_model):
    result = []
    for index, joint in enumerate(title.tree_nodes(raw_model.root_joint)):
        dobj = joint.property
        while dobj is not None and hasattr(dobj, "pobject"):
            pobj = dobj.pobject
            while pobj:
                result.append((index, dobj, pobj))
                pobj = pobj.next
            dobj = dobj.next
    return result


def srt_matrix(scale, rotation, position, parent_scale=None):
    """HSD_MtxSRT's Euler XYZ and aligned parent-scale correction."""
    sx, sy, sz = (math.sin(x) for x in rotation)
    cx, cy, cz = (math.cos(x) for x in rotation)
    result = np.eye(4)
    result[:3, :3] = np.array(((cy*cz, sx*sy*cz-cx*sz, cx*sy*cz+sx*sz),
                               (cy*sz, sx*sy*sz+cx*cz, cx*sy*sz-sx*cz),
                               (-sy, sx*cy, cx*cy))) * np.asarray(scale)
    if parent_scale is not None:
        parent_scale = np.asarray(parent_scale)
        if min(abs(parent_scale)) < 1e-10:
            raise ValueError("Unsupported singular inherited scale")
        result[:3, :3] *= parent_scale[np.newaxis, :] / parent_scale[:, np.newaxis]
    result[:3, 3] = position
    return result


def normalized(value):
    length = float(np.linalg.norm(value))
    if length < 1e-10:
        raise ValueError("Degenerate HSD IK direction")
    return value / length


def solve_ik_upper(initial, origin, target, length1, length2, scale, flip):
    """Matched jobj.c:resolveIKJoint1, the reviewed no-pole-target case."""
    direction = target - origin
    distance2 = float(direction @ direction)
    if distance2 <= 1e-10:
        raise ValueError("Unsupported coincident HSD IK target")
    bend = normalized(np.cross(initial[:3, 2], direction))
    normal = normalized(np.cross(direction, bend))
    a, b = length1*length1, length2*length2
    height2 = max(0.0, .25 * (2*(a+b)-distance2-(a-b)**2/distance2))
    axial = math.sqrt(max(0.0, (a-height2)/distance2))
    if b-height2 >= distance2:
        axial = -axial
    height = math.sqrt(height2) * (-1 if flip else 1)
    x = normalized(direction*axial + bend*height)
    y = np.cross(normal, x)
    result = np.eye(4)
    result[:3, :3] = np.column_stack((x, y, normal)) * np.asarray(scale)
    result[:3, 3] = origin
    return result


def solve_ik_lower(upper, target, upper_length, scale):
    """Matched resolveIKJoint2 without unused angle-limit RObjs."""
    knee = upper[:3, 3] + normalized(upper[:3, 0]) * upper_length
    x = normalized(target-knee)
    y = normalized(np.cross(upper[:3, 2], x))
    z = np.cross(x, y)
    result = np.eye(4)
    result[:3, :3] = np.column_stack((x, y, z)) * np.asarray(scale)
    result[:3, 3] = knee
    return result


class Model:
    def __init__(self, model, raw):
        self.model, self.raw = model, raw
        self.joints = title.tree_nodes(raw.root_joint)
        self.meshes = raw_meshes(raw)
        if len(self.joints) != len(model.bones) or len(self.meshes) != len(model.meshes):
            raise ValueError("Raw and IR field topology differ")
        if any(b.flags & ((1 << 12) | (1 << 17)) for b in model.bones):
            raise ValueError("Instanced/quaternion field joints are unsupported")
        if any(getattr(model, name) for name in ("track_to_constraints", "copy_rotation_constraints",
                                                "limit_rotation_constraints", "limit_location_constraints")):
            raise ValueError("Unreviewed field constraint")
        self.names = {b.name: i for i, b in enumerate(model.bones)}
        self.rest = np.asarray([b.world_matrix for b in model.bones])
        self.rest_inverse = np.linalg.inv(self.rest)
        self.invbind = []
        for index in range(len(model.bones)):
            bone = model.bones[index]
            while bone.inverse_bind_matrix is None and bone.parent_index is not None:
                bone = model.bones[bone.parent_index]
            self.invbind.append(np.asarray(bone.inverse_bind_matrix) if bone.inverse_bind_matrix else np.eye(4))
        self.tracks, self.materials, self.ends, self.particle_events = [], [], [], []
        for root in raw.animated_joints or []:
            nodes = title.tree_nodes(root)
            if len(nodes) != len(model.bones):
                raise ValueError("Incomplete HSD joint animation tree")
            tracks = [title.channels(n.animation, {1,2,3,5,6,7,8,9,10,11,12,40}) for n in nodes]
            self.tracks.append(tracks)
            self.ends.append(max((n.animation.end_frame for n in nodes if n.animation), default=0))
            # Type40 is an integer-bit particle callback, not a transform. Its
            # exact calls are retained in the manifest, not rendered as meshes.
            self.particle_events.append([
                {"bone": i, "frame": k.frame, "argument": struct.unpack("<I", struct.pack("<f", k.value))[0]}
                for i, track in enumerate(tracks) for k in track.get(40, [])])
        for root in raw.animated_material_joints or []:
            nodes = title.tree_nodes(root)
            if len(nodes) != len(model.bones):
                raise ValueError("Incomplete HSD material animation tree")
            mapping = {}
            for joint, node in zip(self.joints, nodes):
                dobj, anim = joint.property, node.animation
                while anim:
                    if dobj is None or not hasattr(dobj, "mobject") or anim.render_animation:
                        raise ValueError("Unsupported animated field material topology")
                    tex_tracks, ta = {}, anim.texture_animation
                    while ta:
                        if ta.id in tex_tracks or ta.image_table_count or ta.palette_table_count:
                            raise ValueError("Texture image/palette swaps exceed field subset")
                        tex_tracks[ta.id] = title.channels(ta.animation, set(range(2,10)) | {12,13,14,16,17,18})
                        ta = ta.next
                    mapping[dobj.address] = (title.channels(anim.animation, {10}), tex_tracks)
                    dobj, anim = dobj.next, anim.next
            self.materials.append(mapping)
        self.upper, self.lower, self.effector = {}, {}, {}
        for constraint in model.ik_constraints:
            if constraint.chain_length != 3 or constraint.pole_target_bone or not constraint.target_bone:
                raise ValueError("Unreviewed HSD IK chain")
            end = self.names[constraint.bone_name]
            lower = model.bones[end].parent_index
            upper = model.bones[lower].parent_index
            target = self.names[constraint.target_bone]
            lengths = []
            for index in (upper, lower):
                refs = []
                reference = self.joints[index].reference
                while reference:
                    if reference.flags & 0x70000000 != 0x40000000:
                        raise ValueError("IK chain includes an unsupported RObj")
                    refs.append(reference.property.length * UNITS)
                    reference = reference.next
                if len(refs) != 1:
                    raise ValueError("Missing original HSD IK length")
                lengths.append(refs[0])
            desc = (upper, lower, end, target, *lengths, constraint.pole_flip)
            self.upper[upper] = self.lower[lower] = self.effector[end] = desc
        self.copy_position = {}
        for constraint in model.copy_location_constraints:
            if constraint.influence != 1:
                raise ValueError("Partial copy-location constraint is unsupported")
            self.copy_position[self.names[constraint.bone_name]] = self.names[constraint.target_bone]
        self._cache = {}

    def world(self, clip, frame):
        key = (clip, frame)
        if key in self._cache:
            return self._cache[key]
        bones = self.model.bones
        tracks = self.tracks[clip] if self.tracks else [{} for _ in bones]
        local, accumulated = [], []
        for bone, track in zip(bones, tracks):
            position = [title.curve_value(track.get(5+i), frame, bone.position[i]/UNITS)*UNITS for i in range(3)]
            rotation = [title.curve_value(track.get(1+i), frame, bone.rotation[i]) for i in range(3)]
            scale = np.asarray([title.curve_value(track.get(8+i), frame, bone.scale[i]) for i in range(3)])
            parent_scale = accumulated[bone.parent_index] if bone.parent_index is not None else None
            accumulated.append((parent_scale if bone.flags & 8 else scale*parent_scale)
                               if parent_scale is not None else scale)
            local.append(srt_matrix(scale, rotation, position, parent_scale))
        world, active = {}, set()
        def matrix(index):
            if index in world:
                return world[index]
            if index in active:
                raise ValueError("Cyclic HSD field constraint")
            active.add(index)
            parent = bones[index].parent_index
            value = matrix(parent) @ local[index] if parent is not None else local[index].copy()
            if index in self.upper:
                upper, lower, end, target, a, b, flip = self.upper[index]
                origin = matrix(parent)[:3,3] if parent is not None else np.zeros(3)
                # Joint2 length includes its own local X scale as in retail.
                track = tracks[lower]
                lower_scale = title.curve_value(track.get(8),frame,bones[lower].scale[0])
                value = solve_ik_upper(value, origin, matrix(target)[:3,3],
                                       a*accumulated[upper][0], b*lower_scale*accumulated[upper][0],
                                       accumulated[upper], flip)
            elif index in self.lower:
                upper, lower, end, target, a, b, flip = self.lower[index]
                value = solve_ik_lower(matrix(upper), matrix(target)[:3,3],
                                       a*accumulated[upper][0], accumulated[lower])
            elif index in self.effector:
                upper, lower, end, target, a, b, flip = self.effector[index]
                lower_world = matrix(lower)
                value[:3,3] = lower_world[:3,3] + normalized(lower_world[:3,0])*b*accumulated[lower][0]
            elif index in self.copy_position:
                value[:3,3] = matrix(self.copy_position[index])[:3,3]
            world[index] = value
            active.remove(index)
            return value
        result = (np.asarray([matrix(i) for i in range(len(bones))]), title.visibility_at(bones, tracks, frame))
        # At most the bounded set of exported frames is evaluated.
        self._cache[key] = result
        return result

    def deform(self, descriptor, world):
        if descriptor[0] == "rigid":
            return world[descriptor[1]]
        _, owner, weights = descriptor
        if not self.model.bones[owner].flags & 2:
            raise ValueError("Only reviewed skeleton-root envelopes are admitted")
        if weights[0][1] == 1:
            if len(weights) != 1:
                raise ValueError("Malformed single-weight envelope")
            return world[weights[0][0]]
        return sum(weight*(world[bone] @ self.invbind[bone]) for bone,weight in weights)


def _texture(texture, image):
    pixels = title.rgba_top_down(image.pixels, image.width, image.height)
    width, height, pixels = title.resize_nearest(pixels, image.width, image.height)
    return (width, height, pixels, texture.wrap_s, texture.wrap_t)


def validate_envelope_indices(model, mesh, pobj):
    """Reject per-corner envelope aliases the pinned IR cannot represent.

    The IR rest positions are indexed by original position index. If the same
    index were reused with different GX matrix indices, silently taking the
    last assignment would deform other corners incorrectly.
    """
    from shared.Constants.gx import GX_VA_POS, GX_VA_PNMTXIDX
    position_index=pobj.find_attribute_index(GX_VA_POS)
    matrix_index=pobj.find_attribute_index(GX_VA_PNMTXIDX)
    if position_index is None or matrix_index is None:
        raise ValueError("Weighted field mesh is missing its matrix index")
    positions=pobj.face_lists[position_index]
    matrices=pobj.face_lists[matrix_index]
    if len(positions)!=len(matrices):
        raise ValueError("Mismatched field vertex/matrix face lists")
    mapping={}
    address_indices={joint.address:i for i,joint in enumerate(model.joints)}
    for face,indices in zip(positions,matrices):
        if len(set(face))!=len(face):
            continue
        if len(face)!=len(indices):
            raise ValueError("Mismatched field vertex/matrix corners")
        for position,index in zip(face,indices):
            raw_index=pobj.sources[matrix_index][index]
            if raw_index%3 or raw_index//3>=len(pobj.property):
                raise ValueError("Original field envelope index is invalid")
            envelope=pobj.property[raw_index//3]
            weights=tuple((address_indices[e.joint.address],float(e.weight)) for e in envelope.envelopes)
            if position in mapping and mapping[position]!=weights:
                raise ValueError("Shared position index uses conflicting skin envelopes")
            mapping[position]=weights
    assignments={position:tuple((model.names[name],weight) for name,weight in weights)
                 for position,weights in mesh.bone_weights.assignments}
    if mapping!=assignments:
        raise ValueError("Pinned IR skin weights differ from original matrix indices")


def build_mesh(model, mesh_index):
    from shared.IR.enums import SkinType
    ir = model.model
    mesh = ir.meshes[mesh_index]
    bone, dobj, pobj = model.meshes[mesh_index]
    weighted = mesh.bone_weights.type.value == "WEIGHTED"
    if mesh.bone_weights.type.value not in ("WEIGHTED", "RIGID"):
        raise ValueError("Unexpected field skin type")
    if mesh.shape_keys:
        raise ValueError("Field morph geometry exceeds the reviewed subset")
    if weighted:
        validate_envelope_indices(model,mesh,pobj)
    textures, raw_textures, tex = [], [], dobj.mobject.texture
    while tex:
        raw_textures.append(tex)
        tex = tex.next
    if len(raw_textures) > 2:
        raise ValueError("Field material has more than two texture layers")
    dual = len(raw_textures) == 2 and raw_textures[1].flags == 0x40010
    reflection = len(raw_textures) == 2 and raw_textures[1].flags == 0x30081
    if len(raw_textures)==2 and not (dual or reflection):
        raise ValueError("Unreviewed second field texture expression")
    if dual:
        if len(mesh.uv_layers) != 2 or [t.source for t in raw_textures] != [4,5]:
            raise ValueError("Unsupported dual field texture generators")
        for tex in raw_textures:
            if tex.flags != 0x40010 or not tex.tev or tex.tev.active != 0x40000077:
                raise ValueError("Dual texture expression differs from reviewed material")
            if tex.tev.tev0.asRGBList() != [0,0,0] or tex.tev.konst.asRGBList() != [127/255]*3:
                raise ValueError("Dual texture gradient constants changed")
    if reflection:
        if (len(mesh.uv_layers)!=1 or raw_textures[0].flags!=0x40010 or
            raw_textures[0].tev or raw_textures[1].tev or
            [t.source for t in raw_textures]!=[4,5]):
            raise ValueError("Unreviewed normal-generated reflection material")
    # Reuse the independently tested title material/triangulation path, with
    # explicit adapters for already-deformed skin geometry and texture layer1.
    mesh_copy, ir_copy, dobj_copy = copy.copy(mesh), copy.copy(ir), copy.copy(dobj)
    mesh_copy.bone_weights = copy.copy(mesh.bone_weights)
    mesh_copy.bone_weights.type = SkinType.RIGID
    mesh_copy.uv_layers = mesh.uv_layers[:1]
    ir_copy.meshes = [mesh_copy]
    if weighted:
        ir_copy.bones = list(ir.bones)
        ir_copy.bones[bone] = copy.copy(ir.bones[bone])
        ir_copy.bones[bone].world_matrix = np.eye(4).tolist()
    dobj_copy.mobject = copy.copy(dobj.mobject)
    if raw_textures:
        dobj_copy.mobject.texture = copy.copy(raw_textures[0])
        dobj_copy.mobject.texture.next = None
    pe = dobj.mobject.pixel_engine_data
    dobj_copy.mobject.pixel_engine_data = None
    # C3T already carries the separate specular material. The title itself
    # never uses it, but the arrival's nine metal/gloss meshes do.
    dobj_copy.mobject.render_mode &= ~8
    vertices, batches, textures, _ = title.geometry(ir_copy, [(bone,dobj_copy,pobj)])
    batch = list(batches[0])
    batch[6] = dobj.mobject.render_mode
    if dobj.mobject.render_mode & 8:
        batch[3] |= title.SPECULAR
    if pe:
        state = (pe.flags,pe.type,pe.source_factor,pe.destination_factor,pe.z_comp)
        flags = {(25,1,4,1,3):title.ADDITIVE, (25,1,1,1,3):ADDITIVE_ONE,
                 (25,3,4,5,3):SUBTRACT}.get(state)
        if flags is None:
            raise ValueError("Unreviewed field pixel-engine state: " + str(state))
        batch[3] |= flags | title.NO_DEPTH_WRITE | title.TRANSLUCENT
        batch[20:26] = [pe.reference_0,pe.reference_1,pe.alpha_component_0,
                       pe.alpha_component_1,pe.alpha_op,pe.z_comp]
    if dual or reflection:
        textures.append(_texture(raw_textures[1],mesh.material.texture_layers[1].image))
        batch[3] |= DUAL_MODULATE if dual else REFLECTION_BLEND
        batch[26] = 1
        if reflection:
            batch[27] = raw_textures[1].blending
    assignments = dict(mesh.bone_weights.assignments) if weighted else {}
    descriptors, loops, offset = [], [], 0
    for face in mesh.faces:
        for triangle in range(1,len(face)-1):
            for corner in (0,triangle,triangle+1):
                position = face[corner]
                if weighted:
                    if position not in assignments:
                        raise ValueError("Skin vertex has no original envelope")
                    weights = tuple((model.names[name],float(weight)) for name,weight in assignments[position])
                    if not 1 <= len(weights) <= 3 or abs(sum(w for _,w in weights)-1)>1e-5:
                        raise ValueError("Invalid source field envelope weights")
                    descriptors.append(("skin",bone,weights))
                else:
                    descriptors.append(("rigid",bone))
                loops.append(offset+corner)
        offset += len(face)
    if len(vertices) != len(descriptors):
        raise ValueError("Triangulated field envelope mapping differs")
    vertices = [tuple(v) + ((mesh.uv_layers[1].uvs[loop][0],1-mesh.uv_layers[1].uvs[loop][1])
                            if dual else (0.,0.)) for v,loop in zip(vertices,loops)]
    return {"model":model,"index":mesh_index,"bone":bone,"dobj":dobj,"textures":textures,
            "raw_textures":raw_textures,"vertices":vertices,"batch":batch,"descriptors":descriptors}


def texture_affine(scale, translation, repeat, rotation):
    """HSD MakeTextureMtx applied to GX's UV input (s,t,1,1).

    Retail builds Scale * Rotation(rx,ry,-rz) * Translation(-u,-v,+z).
    GX_TG_MTX2x4 retains the first two result rows, so X/Y rotations remain
    an affine map of the original two UV coordinates, including the third
    input component's constant contribution.
    """
    if min(abs(value) for value in scale[:2])<1e-9:
        raise ValueError("Singular animated field UV scale")
    rotation_matrix=srt_matrix((1,1,1),(rotation[0],rotation[1],-rotation[2]),(0,0,0))[:3,:3]
    factors=np.asarray((repeat[0]/scale[0],repeat[1]/scale[1]))
    matrix=rotation_matrix[:2,:]*factors[:,None]
    offset=matrix @ np.asarray((-translation[0],-translation[1],translation[2]+1))
    return (matrix[0,0],matrix[1,1],*offset),(matrix[0,1],matrix[1,0])


def material_state(mesh, clip, frame, visible):
    model, dobj = mesh["model"], mesh["dobj"]
    mapping = model.materials[clip] if clip < len(model.materials) else {}
    mat_track, tex_tracks = mapping.get(dobj.address, ({},{}))
    transforms, crosses, tev0, konst = [], [], [0.,0.,0.], [1.,1.,1.]
    for index, tex in enumerate(mesh["raw_textures"]):
        # Node.id is the parser's address identity. HSD animation IDs refer to
        # the original texture_id field (0/1), not to that Python identity.
        tracks = tex_tracks.get(tex.texture_id,{})
        rotation = [title.curve_value(tracks.get(6+i),frame,tex.rotation[i]) for i in range(3)]
        blend = title.curve_value(tracks.get(9),frame,tex.blending)
        if abs(blend-tex.blending)>1e-7:
            raise ValueError("Animated texture blending is not represented by C3T2")
        scale = [title.curve_value(tracks.get(4+i),frame,tex.scale[i]) for i in range(2)]
        translation = [title.curve_value(tracks.get(2+i),frame,tex.translation[i]) for i in range(2)]+[tex.translation[2]]
        transform,cross=texture_affine(scale,translation,(tex.repeat_s,tex.repeat_t),rotation)
        transforms.append(transform)
        crosses.append(cross)
        if index == 0:
            tev0 = tex.tev.tev0.asRGBList() if tex.tev else [0.,0.,0.]
            konst = tex.tev.konst.asRGBList() if tex.tev else [1.,1.,1.]
            for component in range(3):
                if 16+component in tracks:
                    tev0[component] = title.tev_byte(title.curve_value(tracks[16+component],frame))
                if 12+component in tracks:
                    konst[component] = title.tev_byte(title.curve_value(tracks[12+component],frame))
    while len(transforms)<2:
        transforms.append((1.,1.,0.,0.))
        crosses.append((0.,0.))
    alpha = title.material_alpha(title.curve_value(mat_track[10],frame)) if 10 in mat_track else dobj.mobject.material.alpha
    return (*transforms[0],alpha,float(visible[mesh["bone"]]),*tev0,*konst,*transforms[1],*crosses[0],*crosses[1])


def _sample_descriptor(model, descriptor, states, attachment=None):
    rest_inverse = np.linalg.inv(model.deform(descriptor,model.rest))
    result = np.asarray([model.deform(descriptor,world) @ rest_inverse for world,_ in states])
    if attachment is not None:
        result = attachment @ result
    if not np.isfinite(result).all():
        raise ValueError("Nonfinite sampled field transform")
    if np.min(np.abs(np.linalg.det(result[:,:3,:3])))<1e-9:
        raise ValueError("Singular sampled field transform")
    if np.max(np.abs(result-np.eye(4)))<1e-7:
        result[:] = np.eye(4)
    return result[:,:3,:].astype("<f4")


def prepare_pack_set(meshes, schedules, fps, camera_frames, lights, archive_hash, output, prefix,
                     attachment=None, expected_count=None, format_version=2):
    """Sample once, compact equal affine tracks, then partition at native bounds.

    schedules is a list of (source_clip, authored_frame_sequence, loop). Every
    model in a map group uses the same clock; independent clocks get a new set.
    """
    frames = [(clip,frame) for clip,sequence,_loop in schedules for frame in sequence]
    models = list(dict.fromkeys(mesh["model"] for mesh in meshes))
    states = {model:[model.world(clip if model.tracks else 0,frame) for clip,frame in frames] for model in models}
    matrix_tracks, signatures, material_tracks = {}, {}, {}
    for mesh in meshes:
        model = mesh["model"]
        for descriptor in dict.fromkeys(mesh["descriptors"]):
            key = (model,descriptor)
            if key not in matrix_tracks:
                track = _sample_descriptor(model,descriptor,states[model],attachment)
                signature = hashlib.sha256(track.tobytes()).digest()
                matrix_tracks[key] = track
                signatures[key] = signature
        material_tracks[id(mesh)] = []
        for i,(clip,frame) in enumerate(frames):
            material = material_state(mesh,clip,frame,states[model][i][1])
            if format_version==3:
                source_joint = states[model][i][0][mesh["bone"]]
                if attachment is not None:
                    source_joint = attachment[i] @ source_joint
                material += (*source_joint[:3,3],0.)
            else:
                if any(abs(value)>1e-7 for value in material[16:]):
                    raise ValueError("Full UV affine rotation requires C3T3")
                material=material[:16]
            material_tracks[id(mesh)].append(material)
    chunks, current, keys, count = [], [], set(), 0
    for mesh in meshes:
        used = {signatures[(mesh["model"],d)] for d in mesh["descriptors"]}
        if len(used)>64:
            raise ValueError("Single field mesh exceeds sampled bone limit")
        if len(keys|used)>64 or count+len(mesh["vertices"])>32768 or len(current)==64:
            chunks.append(current)
            current,keys,count=[],set(),0
        current.append(mesh)
        keys |= used
        count += len(mesh["vertices"])
    if current:
        chunks.append(current)
    if expected_count is not None and len(chunks)!=expected_count:
        raise ValueError("Reviewed field pack partition changed: %d != %d" % (len(chunks),expected_count))
    reports=[]
    for number,chunk in enumerate(chunks):
        vertices,batches,textures,texture_ids,bone_ids,bone_tracks=[],[],[],{}, {},[]
        for mesh in chunk:
            texture_remap=[]
            for texture in mesh["textures"]:
                w,h,pixels,s,t=texture
                key=(w,h,hashlib.sha256(pixels).digest(),s,t)
                if key not in texture_ids:
                    texture_ids[key]=len(textures)
                    textures.append(texture)
                texture_remap.append(texture_ids[key])
            batch=list(mesh["batch"])
            batch[0]=len(vertices)
            batch[2]=texture_remap[0]
            batch[26]=texture_remap[1] if len(texture_remap)>1 else 0
            batches.append(tuple(batch))
            for vertex,descriptor in zip(mesh["vertices"],mesh["descriptors"]):
                key=(mesh["model"],descriptor)
                signature=signatures[key]
                if signature not in bone_ids:
                    bone_ids[signature]=len(bone_tracks)
                    bone_tracks.append(matrix_tracks[key])
                vertices.append((*vertex[:12],bone_ids[signature],*vertex[13:]))
        sampled=bytearray()
        for index in range(len(frames)):
            for track in bone_tracks:
                sampled.extend(track[index].tobytes())
            for mesh in chunk:
                sampled.extend(struct.pack("<24f" if format_version==3 else "<16f",*material_tracks[id(mesh)][index]))
            sampled.extend(struct.pack("<12f",*camera_frames[index]))
        clips,first=[],0
        for _clip,sequence,loop in schedules:
            clips.append((first,len(sequence),int(loop),0))
            first+=len(sequence)
        pack=title.encode_pack(len(bone_tracks),vertices,batches,textures,clips,lights,sampled,
                               fps=fps,format_version=format_version,archive_sha256=archive_hash)
        name=prefix + (str(number) if expected_count != 1 else "") + ".c3t"
        (output/name).write_bytes(pack)
        report={"file":name,"bytes":len(pack),"sha256":hashlib.sha256(pack).hexdigest(),
                "vertices":len(vertices),"batches":len(batches),"textures":len(textures),
                "texture_rgba_bytes":sum(len(texture[2]) for texture in textures),
                "format_version":format_version,
                "sampled_bones":len(bone_tracks),"sample_fps":fps,
                "source_meshes":[{"hsd_root_address":mesh["model"].raw.root_joint.address,
                                  "mesh_index":mesh["index"],"owner_joint":mesh["bone"],
                                  "source_render_mode":"%08x"%mesh["dobj"].mobject.render_mode}
                                 for mesh in chunk],
                "clips":[{"source_clip":c,"samples":len(s),"authored_end":s[-1],"loop":l}
                         for c,s,l in schedules]}
        reports.append(report)
        print("Prepared %s: %d vertices, %d batches, %d sampled bones, %d bytes" %
              (name,len(vertices),len(batches),len(bone_tracks),len(pack)),flush=True)
    return reports


def sample_sequence(end, samples_per_frame=2):
    count=int(round(end*samples_per_frame))
    if count<1 or abs(count-end*samples_per_frame)>1e-5:
        raise ValueError("Animation end does not align with the sampled clock")
    return [sample/samples_per_frame for sample in range(count+1)]


def fixed_lights(raw, data):
    # S1_out contains one empty LObjAnim record per light. They carry no AObj
    # or WObjAnim and do not animate anything; retain the original light data.
    root = copy.copy(raw)
    root.lights = []
    for light_set in raw.lights:
        for animation in light_set.animations or []:
            if animation.next or animation.animation or animation.eye_position_animation or animation.interest_animation:
                raise ValueError("Animated Outskirt light exceeds the reviewed subset")
        item = copy.copy(light_set)
        item.animations = []
        root.lights.append(item)
    return title.light_values(root,data,UNITS)


def prepare_geometry(archives, output):
    from prepare_outskirt import PROFILES
    data,raw,scene=load_scene(archives["S1_out.fsys"],0x00E20200)
    if len(scene.models)!=5 or len(scene.cameras)!=1 or scene.fogs:
        raise ValueError("Original Outskirt scene topology changed")
    models=[Model(model,raw_model) for model,raw_model in zip(scene.models,raw.models)]
    camera=scene.cameras[0]
    fixed=(*camera.position,*camera.target_position,camera.roll,camera.field_of_view,
           camera.near,camera.far,camera.aspect,0.)
    lights=fixed_lights(raw,data)
    source_hash=PROFILES["S1_out.fsys"][1]
    meshes=[build_mesh(model,i) for model in models[:4] for i in range(len(model.meshes))]
    short=sample_sequence(179)
    reports=prepare_pack_set(meshes,[(0,short,True)],60,[fixed]*len(short),lights,source_hash,
                             output,"outskirt_map",expected_count=3)
    sky=[build_mesh(models[4],i) for i in range(len(models[4].meshes))]
    # Sparse sampling of the slow linear sky rotation; its exact loop is 29999
    # source frames, not GSmodelGetFrameCount's inclusive query of 30000.
    sky_frames=[i*29999/1000 for i in range(1001)]
    sky_reports=prepare_pack_set(sky,[(0,sky_frames,True)],30000/29999,[fixed]*1001,lights,
                                 source_hash,output,"outskirt_map3",expected_count=1)
    reports+=sky_reports
    _,wes_raw,wes_scene=load_scene(archives["field_common.fsys"],0x00F70400)
    wes=Model(wes_scene.models[0],wes_raw.models[0])
    if wes.ends[1:4] != [30,30,17]:
        raise ValueError("Original Wes animation ends changed")
    wes_meshes=[build_mesh(wes,i) for i in range(len(wes.meshes))]
    schedules=[(i,sample_sequence(wes.ends[i]),True) for i in (1,2,3)]
    samples=sum(len(sequence) for _,sequence,_ in schedules)
    reports+=prepare_pack_set(wes_meshes,schedules,60,[fixed]*samples,lights,
                               PROFILES["field_common.fsys"][1],output,"outskirt_wes",expected_count=1)
    reports+=prepare_arrival(archives,output,lights,source_hash)
    return reports,fixed[:11],sum(len(mesh["vertices"]) for mesh in wes_meshes)


def prepare_arrival(archives,output,lights,source_hash):
    _,bike_raw,bike_scene=load_scene(archives["S1_out.fsys"],0x0C6C0400)
    _,parent_raw,parent_scene=load_scene(archives["S1_out.fsys"],0x105D0400)
    _,camera_raw,camera_scene=load_scene(archives["S1_out.fsys"],0x105C1800)
    bike=Model(bike_scene.models[0],bike_raw.models[0])
    parent=Model(parent_scene.models[0],parent_raw.models[0])
    if bike.ends[1]!=30 or parent.ends!=[150] or len(parent.model.bones)!=1:
        raise ValueError("Original arrival motion topology changed")
    arrival=sample_sequence(150)
    attachment=np.asarray([parent.world(0,frame)[0][0] for frame in arrival])
    camera_frames=[title.camera_values(camera_scene.cameras[0],frame) for frame in arrival]
    # Bike's animation loops independently while the parent motion and camera
    # continue once. Source frame150 is sampled at bike frame0 after five loops.
    bike_frames=[frame % 30 for frame in arrival]
    bike_meshes=[build_mesh(bike,i) for i in range(len(bike.meshes))]
    reports=prepare_pack_set(bike_meshes,[(1,bike_frames,False)],60,camera_frames,lights,
                               source_hash,output,"outskirt_arrival",attachment=attachment,expected_count=2,
                               format_version=3)
    for report in reports:
        if report["file"].startswith("outskirt_arrival"):
            report["clips"][0]["authored_end"]=150
            report["particle_callbacks_unrendered"]=bike.particle_events[1]
            report["parent_resource"]="105d0400"
            report["camera_resource"]="105c1800"
    return reports
