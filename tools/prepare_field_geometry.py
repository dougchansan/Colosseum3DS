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


JOBJ_INSTANCE = 1 << 12


def base_descriptor(descriptor):
    """The structural descriptor whose rest pose an instance copy reuses."""
    return descriptor[3] if descriptor[0] == "instance" else descriptor


def joint_nodes(root):
    """Joint order of HSD_JObjResolveRefsAll and the pinned importer.

    An instance joint's child pointer names a shared subtree that is already
    present elsewhere in the sibling chain; it is not a structural child.
    Without instances this is exactly prepare_title.tree_nodes.
    """
    result, stack = [], [root]
    while stack:
        node = stack.pop()
        if node is None:
            continue
        result.append(node)
        stack.append(node.next)
        if not node.flags & JOBJ_INSTANCE:
            stack.append(node.child)
    return result


def raw_meshes(raw_model):
    result = []
    for index, joint in enumerate(joint_nodes(raw_model.root_joint)):
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


def admitted_spline(node):
    """Reviewed HSD path spline: the cardinal type with its arc-length tables.

    spline.c names the fields type/numcv/tension/cv/totalLength/segLength/
    segPoly. A cardinal spline of n points stores n+2 control points, n
    cumulative normalised lengths and n-1 quartic arc-length polynomials.
    """
    if type(node).__name__ != "Spline":
        raise ValueError("HSD path track has no spline object")
    kind, count = node.flags >> 8, node.n
    cv, lengths, poly = node.s1, node.s2, node.s3
    if kind != 3:
        raise ValueError("Unreviewed HSD path spline type")
    if (not 2 <= count <= 64 or not isinstance(cv, list) or len(cv) != count + 2 or
            not isinstance(lengths, list) or len(lengths) != count or
            not isinstance(poly, list) or len(poly) != count - 1):
        raise ValueError("Malformed HSD cardinal path spline")
    values = [node.f0, node.f1, *lengths, *(v for row in cv for v in row), *(v for row in poly for v in row)]
    if any(not math.isfinite(v) for v in values) or any(len(row) != 3 for row in cv) or any(len(row) != 5 for row in poly):
        raise ValueError("Nonfinite HSD path spline")
    if lengths[0] != 0 or any(b < a for a, b in zip(lengths, lengths[1:])) or node.f1 <= 0:
        raise ValueError("HSD path spline lengths are not cumulative")
    return {"count": count, "tension": node.f0, "total": node.f1, "cv": [tuple(row) for row in cv],
            "lengths": list(lengths), "poly": [tuple(row) for row in poly]}


def _spline_speed(coefficients, t):
    """spline.c:splArcLengthPolynomial."""
    result = (((coefficients[0]*t + coefficients[1])*t + coefficients[2])*t + coefficients[3])*t + coefficients[4]
    if -0.001 < result < 0:
        result = 0.0
    if result < 0:
        raise ValueError("Negative HSD path arc-length integrand")
    return math.sqrt(result)


def _spline_simpson(coefficients, start, end):
    """spline.c:fn_801B1AD0, the eight-interval Simpson step."""
    dx = (end - start) / 8.0
    t, middle = start + dx, 0.0
    for i in range(2, 9):
        middle += (4.0 if not i & 1 else 2.0) * _spline_speed(coefficients, t)
        t += dx
    return dx * (middle + _spline_speed(coefficients, start) + _spline_speed(coefficients, end)) / 3.0


def spline_parameter(spline, distance):
    """spline.c:fn_801B18D8 (splArcLengthGetParameter) for a cardinal spline."""
    if distance <= 0:
        return 0.0
    if distance >= 1:
        return 1.0
    lengths, index = spline["lengths"], 0
    while lengths[index + 1] < distance:
        index += 1
    remaining = spline["total"] * (distance - lengths[index])
    start, end, result = 0.0, 1.0, 0.0
    while abs(start - end) >= 0.00001:
        result = (start + end) / 2.0
        step = _spline_simpson(spline["poly"][index], start, result)
        if remaining < 0.00001 + step:
            end = result
        else:
            start = result
            remaining -= step
    return (result + index) / (spline["count"] - 1.0)


def spline_point(spline, u):
    """spline.c:fn_801B2038 (splGetSplinePoint), cardinal case."""
    cv, count = spline["cv"], spline["count"]
    if not 0 <= u <= 1:
        raise ValueError("HSD path parameter outside the spline")
    if u >= 1:
        return cv[count]
    u *= count - 1
    index = int(u)
    u -= index
    t, u2 = spline["tension"], u * u
    u3 = u2 * u
    weights = (t * (-u3 + 2.0*u2 - u), (2.0 - t)*u3 + (t - 3.0)*u2 + 1.0,
               (t - 2.0)*u3 + (3.0 - 2.0*t)*u2 + t*u, t * (u3 - u2))
    return tuple(sum(cv[index + k][axis] * weights[k] for k in range(4)) for axis in range(3))


def path_point(spline, value):
    """JObjUpdateFunc HSD_A_J_PATH: clamp to 0..1, then splArcLengthPoint."""
    return spline_point(spline, spline_parameter(spline, min(1.0, max(0.0, value))))


def constant_material_byte(keys):
    if not keys or any(not math.isfinite(k.value) or k.value != keys[0].value or
                       (getattr(k, "slope_in", 0) or 0) != 0 or
                       (getattr(k, "slope_out", 0) or 0) != 0 for k in keys):
        raise ValueError("Only constant effect material RGB tracks are admitted")
    return int(255 * min(1.0, max(0.0, keys[0].value)))


class Model:
    def __init__(self, model, raw, allow_image_swaps=False, constant_material_colors=False,
                 allow_instances=False, allow_paths=False, frozen_material_colors=False):
        self.model, self.raw = model, raw
        self.joints = joint_nodes(raw.root_joint)
        self.meshes = raw_meshes(raw)
        if len(self.joints) != len(model.bones) or len(self.meshes) != len(model.meshes):
            raise ValueError("Raw and IR field topology differ")
        if any(b.flags & (1 << 17) for b in model.bones):
            raise ValueError("Quaternion field joints are unsupported")
        self.instances = []
        for index, (bone, joint) in enumerate(zip(model.bones, self.joints)):
            if not bone.flags & JOBJ_INSTANCE:
                continue
            if not allow_instances:
                raise ValueError("Instanced field joints are unsupported")
            if joint.flags != bone.flags or joint.property is not None or joint.child is None:
                raise ValueError("Unreviewed HSD instance joint")
            targets = [i for i, other in enumerate(self.joints) if other is joint.child]
            if len(targets) != 1:
                raise ValueError("HSD instance target is not a structural joint")
            self.instances.append((index, targets[0]))
        if any(target == instance or self.joints[target].flags & JOBJ_INSTANCE or
               self.in_subtree(instance, target) for instance, target in self.instances):
            raise ValueError("Nested or self-referencing HSD instances are unsupported")
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
        self.image_swaps = []
        self.constant_material_colors = {}
        # Opt-in interior subset. paths: per clip, joint -> admitted spline.
        # frozen_material_tracks: animated material RGB that the pack format
        # cannot sample; such a colour keeps its first-clip frame-zero value.
        self.paths, self.frozen_material_tracks = [], []
        joint_channels = {1,2,3,5,6,7,8,9,10,11,12,40} | ({4} if allow_paths else set())
        for root in raw.animated_joints or []:
            nodes = title.tree_nodes(root)
            if len(nodes) != len(model.bones):
                raise ValueError("Incomplete HSD joint animation tree")
            tracks = [title.channels(n.animation, joint_channels) for n in nodes]
            paths = {}
            for index, (node, track) in enumerate(zip(nodes, tracks)):
                if 4 in track:
                    # JObjUpdateFunc: the path track replaces the translation
                    # with a point of the spline held by the AObj's object.
                    if any(channel in track for channel in (5, 6, 7)):
                        raise ValueError("HSD path joint also animates its translation")
                    paths[index] = admitted_spline(getattr(getattr(node.animation, "joint", None), "property", None))
            self.paths.append(paths)
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
            mapping, swaps = {}, {}
            for joint, node in zip(self.joints, nodes):
                dobj, anim = joint.property, node.animation
                while anim:
                    if dobj is None or not hasattr(dobj, "mobject") or anim.render_animation:
                        raise ValueError("Unsupported animated field material topology")
                    tex_tracks, ta = {}, anim.texture_animation
                    while ta:
                        if ta.id in tex_tracks or ta.palette_table_count:
                            raise ValueError("Texture image/palette swaps exceed field subset")
                        if ta.image_table_count:
                            if (not allow_image_swaps or ta.id != 0 or
                                not 1 <= ta.image_table_count <= 32 or
                                len(ta.image_table or []) != ta.image_table_count or
                                any(image is None for image in ta.image_table)):
                                raise ValueError("Unreviewed field texture image table")
                            swaps[(dobj.address,ta.id)] = ta.image_table
                        channels = title.channels(ta.animation, set(range(2,10)) | set(range(12,24)) |
                                                  ({1} if ta.image_table_count else set()))
                        if ta.image_table_count and (1 not in channels or
                            any(key.value < 0 or key.value >= ta.image_table_count for key in channels[1])):
                            raise ValueError("Image animation references an invalid source image")
                        # Interior map exports include zero-valued curves for
                        # the unused TEV alpha/TEV1 registers. build_mesh still
                        # admits only the reviewed RGB expression (alpha A-D
                        # all ZERO, active 0x40000077). Do not silently admit
                        # a new animated combiner through these unused fields.
                        for channel in (15,19,20,21,22,23):
                            if channel in channels and any(key.value != 0 for key in channels[channel]):
                                raise ValueError("Nonzero unused TEV register animation")
                        tex_tracks[ta.id] = channels
                        ta = ta.next
                    material_tracks = title.channels(anim.animation, set(range(1, 11))
                        if constant_material_colors or frozen_material_colors else {10})
                    for channel, keys in material_tracks.items():
                        if channel == 10:
                            continue
                        # HSD MObjUpdateFunc clamps, multiplies by 255, then
                        # truncates to u8. Only constant, equal-across-clips RGB
                        # tracks may be folded into immutable batch colors.
                        identity = (dobj.address, channel)
                        try:
                            value = constant_material_byte(keys)
                            previous = self.constant_material_colors.get(identity, value)
                            if previous != value:
                                raise ValueError("Material RGB differs between effect clips")
                        except ValueError:
                            if not frozen_material_colors:
                                raise
                            # Declared limitation: the colour does not animate.
                            value = self.constant_material_colors.get(
                                identity, int(255 * min(1.0, max(0.0, title.curve_value(keys, 0.0)))))
                            self.frozen_material_tracks.append(
                                {"clip": len(self.materials), "dobj": "%08x" % dobj.address, "channel": channel})
                        self.constant_material_colors[identity] = value
                    mapping[dobj.address] = ({k:v for k,v in material_tracks.items() if k == 10}, tex_tracks)
                    dobj, anim = dobj.next, anim.next
            self.materials.append(mapping)
            self.image_swaps.append(swaps)
        self.upper, self.lower, self.effector = {}, {}, {}
        for constraint in model.ik_constraints:
            if (constraint.chain_length != 3 or constraint.pole_target_bone or
                    (not constraint.target_bone and not allow_instances)):
                raise ValueError("Unreviewed HSD IK chain")
            end = self.names[constraint.bone_name]
            lower = model.bones[end].parent_index
            upper = model.bones[lower].parent_index
            if constraint.target_bone:
                target = self.names[constraint.target_bone]
            else:
                # jobj_get_effector_checked returns NULL without the
                # effector's REFTYPE_JOBJ RObj: resolveIKJoint1/2 keep the
                # animated FK matrices; only the effector case still places
                # the end joint along joint2's X axis by its IK-hint length.
                reference = self.joints[end].reference
                while reference:
                    if reference.flags & 0x70000000 == 0x10000000:
                        raise ValueError("Targetless HSD IK effector has a JObj reference")
                    reference = reference.next
                target = None
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
            if target is None:
                self.effector[end] = desc
            else:
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
        paths = self.paths[clip] if getattr(self, "paths", None) else {}
        local, accumulated, scales = [], [], []
        for index, (bone, track) in enumerate(zip(bones, tracks)):
            if index in paths:
                position = [v*UNITS for v in path_point(paths[index], title.curve_value(track[4], frame))]
            else:
                position = [title.curve_value(track.get(5+i), frame, bone.position[i]/UNITS)*UNITS for i in range(3)]
            rotation = [title.curve_value(track.get(1+i), frame, bone.rotation[i]) for i in range(3)]
            scale = np.asarray([title.curve_value(track.get(8+i), frame, bone.scale[i]) for i in range(3)])
            parent_scale = accumulated[bone.parent_index] if bone.parent_index is not None else None
            accumulated.append((parent_scale if bone.flags & 8 else scale*parent_scale)
                               if parent_scale is not None else scale)
            scales.append(scale)
            # Compose aligned scale in world space below. The local a_j/a_i
            # correction divides by zero when an authored parent collapses;
            # its inverse scale cancels algebraically against the parent.
            local.append(srt_matrix((1., 1., 1.), rotation, position))
        world, carriers, active = {}, {}, set()
        def matrix(index):
            if index in world:
                return world[index]
            if index in active:
                raise ValueError("Cyclic HSD field constraint")
            active.add(index)
            parent = bones[index].parent_index
            value = local[index].copy()
            if parent is not None:
                parent_world = matrix(parent)
                carrier = carriers[parent] @ local[index][:3, :3]
                value[:3, 3] = (parent_world @ local[index][:, 3])[:3]
                value[:3, :3] = carrier * (accumulated[parent] * scales[index])
            else:
                carrier = local[index][:3, :3].copy()
                value[:3, :3] = carrier * scales[index]
            # CLASSICAL_SCALING excludes this joint's scale from the
            # accumulated correction used by its children, not its world.
            carriers[index] = (carrier * scales[index] if parent is not None and bones[index].flags & 8
                               else carrier)
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
            if index in self.upper or index in self.lower:
                if np.any(accumulated[index] == 0):
                    raise ValueError("Singular HSD IK scale")
                carriers[index] = value[:3, :3] / accumulated[index]
            world[index] = value
            active.remove(index)
            return value
        result = (np.asarray([matrix(i) for i in range(len(bones))]), title.visibility_at(bones, tracks, frame))
        # At most the bounded set of exported frames is evaluated.
        self._cache[key] = result
        return result

    def in_subtree(self, index, root):
        """True for root and its structural descendants, not its siblings."""
        while index is not None:
            if index == root:
                return True
            index = self.model.bones[index].parent_index
        return False

    @staticmethod
    def base_descriptor(descriptor):
        return base_descriptor(descriptor)

    def deform(self, descriptor, world):
        if descriptor[0] == "baked":
            # bake_mesh already applied this sample's transform to vertices.
            return np.eye(4)
        if descriptor[0] == "instance":
            # JObjSetupInstanceMtx: instance.mtx * inverse(child.mtx), then
            # the shared subtree draws with its own joint matrices.
            _, instance, target, base = descriptor
            return world[instance] @ np.linalg.inv(world[target]) @ self.deform(base, world)
        if descriptor[0] == "rigid":
            return world[descriptor[1]]
        _, owner, weights = descriptor
        # The parser already bakes the owner's rest coordinate matrix into
        # each vertex. For non-root owners that constant right-side matrix
        # cancels in _sample_descriptor's D(t) @ inverse(D(rest)). The same
        # cancellation removes the constant inverse bind for a single weight.
        # Retain the blended inverse binds for multi-weight envelopes.
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


def pixel_engine_flags(pe, flags):
    """Admit the original title/first-area PE states without losing Z writes.

    HSD state.c:fn_801B29E4 overrides the render mode with PEDesc. Bit0
    enables color writes, 0x10 depth comparisons, and 0x20 depth writes;
    bit8 only selects early versus late comparison. The supported native
    framebuffer does not carry GX destination alpha.
    """
    state = (pe.flags, pe.type, pe.source_factor, pe.destination_factor, pe.z_comp)
    modes = {(25,1,4,1,3):title.ADDITIVE | title.TRANSLUCENT,
             (25,1,1,1,3):ADDITIVE_ONE | title.TRANSLUCENT,
             (25,3,4,5,3):SUBTRACT | title.TRANSLUCENT,
             (25,1,4,5,3):title.TRANSLUCENT,
             (49,1,4,5,3):title.TRANSLUCENT,
             (57,0,4,5,3):0,
             # ONE/ONE addition with flags 0x39: bit 0x20 keeps depth writes,
             # as the reviewed 0x31 translucent interior state does.
             (57,1,1,1,3):ADDITIVE_ONE | title.TRANSLUCENT}
    if state not in modes:
        raise ValueError("Unreviewed field pixel-engine state: " + str(state))
    flags &= ~(title.ADDITIVE | ADDITIVE_ONE | SUBTRACT | title.TRANSLUCENT | title.NO_DEPTH_WRITE)
    flags |= modes[state]
    if not pe.flags & 0x20:
        flags |= title.NO_DEPTH_WRITE
    return flags


def build_mesh(model, mesh_index, max_envelope_weights=3, source_materials=False):
    if source_materials:
        from prepare_pda_mask_layers import build_mask_mesh
        masked=build_mask_mesh(model,mesh_index,build_mesh,max_envelope_weights)
        if masked is not None:return masked
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
    if source_materials and any(t.flags&(1<<24) for t in raw_textures):
        raise ValueError("Original GX emboss bump mapping requires NBT/light-generated UVs and unclamped signed intermediate support")
    source_reflection_only=(source_materials and len(raw_textures)==1 and raw_textures[0].flags==0x30081 and not raw_textures[0].tev)
    if source_reflection_only:
        reflection_texture=copy.copy(raw_textures[0]);reflection_texture.source=5
        neutral=copy.copy(raw_textures[0]);neutral.flags=0x40010;neutral.blending=1.;neutral.source=4
        neutral.scale=(1.,1.,1.);neutral.rotation=(0.,0.,0.);neutral.translation=(0.,0.,0.)
        raw_textures=[neutral,reflection_texture]
    if len(raw_textures) > 2:
        raise ValueError("Field material has more than two texture layers")
    source_blend = (source_materials and len(raw_textures)==2 and
                    raw_textures[0].flags==0x30010 and raw_textures[0].blending==1 and
                    not raw_textures[0].tev and not raw_textures[1].tev and
                    raw_textures[1].flags==0x30010)
    source_normal_pair=(source_materials and len(raw_textures)==2 and
                        [t.flags for t in raw_textures]==[0x30081,0x670081] and
                        raw_textures[0].tev is None and raw_textures[1].tev is not None and
                        raw_textures[1].tev.active==0 and raw_textures[1].blending==1 and
                        not mesh.uv_layers)
    # M1_out (Phenac City): BLEND factor1 discards the incoming material
    # color, then an inactive-TEV MODULATE layer multiplies texture1. The
    # result is exactly T0*T1 with material alpha; it is serialized as the
    # dual-modulate stage with konst1 and a white diffuse raster.
    blend_modulate = (len(raw_textures) == 2 and
                      [t.flags for t in raw_textures] == [0x30010, 0x40010] and
                      raw_textures[0].blending == 1 and raw_textures[0].tev is None and
                      (raw_textures[1].tev is None or raw_textures[1].tev.active == 0) and
                      [t.source for t in raw_textures] == [4, 5] and len(mesh.uv_layers) == 2 and
                      dobj.mobject.render_mode & 7 == 1)
    dual = len(raw_textures) == 2 and raw_textures[1].flags == 0x40010 and not blend_modulate
    reflection = len(raw_textures) == 2 and raw_textures[1].flags == 0x30081
    if len(raw_textures)==2 and not (dual or reflection or source_blend or source_normal_pair or blend_modulate):
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
        if ((len(mesh.uv_layers)!=1 and not source_reflection_only) or raw_textures[0].flags not in ((0x40010,0x30010) if source_materials else (0x40010,)) or
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
        if source_normal_pair or blend_modulate:
            dobj_copy.mobject.texture.flags=0x40010
        if source_materials and dobj_copy.mobject.texture.wrap_t==2:
            dobj_copy.mobject.texture.wrap_t=0
        if source_materials and raw_textures[0].flags==0x30010 and raw_textures[0].blending==1 and (reflection or source_blend):
            # BLEND with factor1 ignores incoming material diffuse entirely.
            # MODULATE by white is the same expression in the native subset.
            dobj_copy.mobject.texture.flags=0x40010
    pe = dobj.mobject.pixel_engine_data
    dobj_copy.mobject.pixel_engine_data = None
    # C3T already carries the separate specular material. The title itself
    # never uses it, but the arrival's nine metal/gloss meshes do.
    dobj_copy.mobject.render_mode &= ~8
    vertices, batches, textures, _ = title.geometry(ir_copy, [(bone,dobj_copy,pobj)])
    batch = list(batches[0])
    if source_materials and raw_textures and raw_textures[0].wrap_t==2:
        tex0=textures[batch[2]];textures[batch[2]]=(*tex0[:4],2)
    if source_reflection_only:
        textures[batch[2]]=(8,8,b"\xff"*256,0,0)
    if source_materials and raw_textures and raw_textures[0].flags==0x30010 and raw_textures[0].blending==1 and (reflection or source_blend):
        batch[8:11]=[1.,1.,1.]
    if blend_modulate:
        if batch[3] & (title.LIT | title.VERTEX_COLOR | title.TEV_LERP):
            raise ValueError("Blend-modulate material is not unlit material color")
        batch[8:11] = [1., 1., 1.]
    for channel in range(1, 10):
        value = model.constant_material_colors.get((dobj.address, channel))
        if value is not None and blend_modulate:
            raise ValueError("Animated diffuse on a blend-modulate material")
        if value is not None:
            # HSD channels 1..3 ambient, 4..6 diffuse, 7..9 specular.
            index = (11 + channel - 1 if channel <= 3 else
                     8 + channel - 4 if channel <= 6 else 14 + channel - 7)
            batch[index] = value / 255.0
    batch[6] = dobj.mobject.render_mode
    if dobj.mobject.render_mode & 8:
        batch[3] |= title.SPECULAR
    if pe:
        batch[3] = pixel_engine_flags(pe, batch[3])
        batch[20:26] = [pe.reference_0,pe.reference_1,pe.alpha_component_0,
                       pe.alpha_component_1,pe.alpha_op,pe.z_comp]
    if dual or reflection or source_blend or source_normal_pair or blend_modulate:
        textures.append(_texture(raw_textures[1],mesh.material.texture_layers[0 if source_reflection_only else 1].image))
        batch[3] |= DUAL_MODULATE | title.TEV_LERP if blend_modulate else DUAL_MODULATE if dual else REFLECTION_BLEND
        if source_blend:batch[3] |= 16384
        if source_normal_pair:batch[3] |= 65536
        batch[26] = 1
        if reflection or source_blend:
            batch[27] = raw_textures[1].blending
        if source_normal_pair:batch[27]=raw_textures[0].blending
    if source_materials:
        # HSD_TExpSetupTev converts F32 constants to int(255*x), then clamps
        # to u8. Original Slugma's authored factor2 therefore becomes255.
        batch[19]=max(0,min(255,int(255*batch[19])))/255.
        batch[27]=max(0,min(255,int(255*batch[27])))/255.
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
                    if source_materials and weights and weights[0][1]==1.:
                        # HSD's single-weight fast path, also used by the
                        # pinned parser, ignores trailing envelope entries.
                        weights=weights[:1]
                    if (not 1 <= len(weights) <= max_envelope_weights or
                        any(not math.isfinite(w) or w<0 for _,w in weights) or
                        abs(sum(w for _,w in weights)-1)>1e-5):
                        raise ValueError("Invalid source field envelope weights")
                    descriptors.append(("skin",bone,weights))
                else:
                    descriptors.append(("rigid",bone))
                loops.append(offset+corner)
        offset += len(face)
    if len(vertices) != len(descriptors):
        raise ValueError("Triangulated field envelope mapping differs")
    vertices = [tuple(v) + ((mesh.uv_layers[1].uvs[loop][0],1-mesh.uv_layers[1].uvs[loop][1])
                            if dual or source_blend or blend_modulate else (0.,0.)) for v,loop in zip(vertices,loops)]
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


def expand_image_swaps(meshes):
    """Represent an admitted texture-image track with visible mesh variants.

    The original GSmodel links the TIMG track to its skeletal clock. Source
    image/palette bytes are decoded by the pinned parser; only the selected
    variant is visible. This needs no mutable GPU texture or new C3T material.
    """
    from importer.phases.describe.helpers.materials import _build_ir_image
    result=[]
    for mesh in meshes:
        model,dobj=mesh["model"],mesh["dobj"]
        entries=[s.get((dobj.address,0)) for s in model.image_swaps]
        nonempty=[e for e in entries if e is not None]
        if not nonempty:
            result.append(mesh)
            continue
        table=nonempty[0]
        if len(mesh["raw_textures"])!=1:
            raise ValueError("Multiple image tables or layers are unreviewed")
        def decoded(images):
            textures=[]
            for image in images:
                texture=copy.copy(mesh["raw_textures"][0]);texture.image=image
                ir_image=_build_ir_image(texture)
                if ir_image is None:
                    raise ValueError("Source image animation has no decoded pixels")
                textures.append(_texture(texture,ir_image))
            return textures
        textures=decoded(table)
        # Trainer clips duplicate equivalent image tables at different DAT
        # addresses. Compare decoded dimensions/pixels/wrap, not addresses.
        if any(decoded(images)!=textures for images in nonempty[1:]):
            raise ValueError("Multiple distinct image tables are unreviewed")
        for index,texture in enumerate(textures):
            variant=dict(mesh)
            variant["textures"]=[texture]
            variant["image_variant"]=index
            result.append(variant)
    return result


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
        if tex.wrap_t==2:
            # HSD MakeTextureMtx centers mirrored T by one complete tile.
            transform=(*transform[:3],transform[3]+1.)
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
    draw_visible=visible[mesh["bone"]] and visible[mesh.get("instance",mesh["bone"])] and (clip, frame) not in mesh.get("collapsed_samples", set())
    if "image_variant" in mesh:
        image_track=tex_tracks.get(0,{}).get(1)
        if not image_track:
            raise ValueError("Image variant has no source TIMG track")
        draw_visible=draw_visible and int(title.curve_value(image_track,frame))==mesh["image_variant"]
    return (*transforms[0],alpha,float(draw_visible),*tev0,*konst,*transforms[1],*crosses[0],*crosses[1])


def instance_meshes(model, meshes):
    """One additional draw of each shared subtree per HSD instance joint.

    HSD_JObjDispAll draws the instance child and its descendants (not the
    child's siblings) after JObjSetupInstanceMtx. The shared subtree also
    keeps its ordinary draw at its structural position in the tree.
    """
    result = []
    for instance, target in model.instances:
        for mesh in meshes:
            if mesh["model"] is not model or not model.in_subtree(mesh["bone"], target):
                continue
            item = dict(mesh)
            item["descriptors"] = [("instance", instance, target, d) for d in mesh["descriptors"]]
            item["instance"] = instance
            result.append(item)
    return result


def bake_mesh(mesh, clip, frame):
    """Apply one sample's deformation to the vertices; no sampled bone remains.

    Used for meshes proven static by split_static, and for explicitly
    reviewed instance copies drawn at a fixed sample. Returns None when a
    corner's transform is singular (a collapsed part keeps its sampled path).
    """
    model = mesh["model"]
    world, _ = model.world(clip if model.tracks else 0, frame)
    transforms = {}
    for descriptor in dict.fromkeys(mesh["descriptors"]):
        value = model.deform(descriptor, world) @ np.linalg.inv(
            model.deform(base_descriptor(descriptor), model.rest))
        if not np.isfinite(value).all() or abs(np.linalg.det(value[:3, :3])) < 1e-12:
            return None
        transforms[descriptor] = (value, np.linalg.inv(value[:3, :3]).T)
    vertices = []
    for vertex, descriptor in zip(mesh["vertices"], mesh["descriptors"]):
        value, normal_matrix = transforms[descriptor]
        position = value @ np.array((*vertex[:3], 1.))
        normal = normal_matrix @ np.asarray(vertex[3:6])
        normal /= max(float(np.linalg.norm(normal)), 1e-30)
        vertices.append((*(float(x) for x in position[:3]), *(float(x) for x in normal), *vertex[6:]))
    item = dict(mesh)
    item.update(vertices=vertices, descriptors=[("baked",)] * len(vertices), baked_frame=frame)
    return item


def split_static(meshes, clip, frames, tolerance=1e-6):
    """Partition meshes whose sampled transforms and materials never change.

    A static mesh needs one sample instead of the whole clock. Transforms are
    compared after deformation (within `tolerance`), material states exactly.
    """
    static, animated = [], []
    for mesh in meshes:
        model = mesh["model"]
        source_clip = clip if model.tracks else 0
        states = [model.world(source_clip, frame) for frame in frames]
        moving = False
        for descriptor in dict.fromkeys(mesh["descriptors"]):
            first = model.deform(descriptor, states[0][0])
            if any(np.max(np.abs(model.deform(descriptor, world) - first)) > tolerance
                   for world, _ in states[1:]):
                moving = True
                break
        if not moving:
            first = material_state(mesh, source_clip, frames[0], states[0][1])
            moving = any(material_state(mesh, source_clip, frame, visible) != first
                         for frame, (_, visible) in zip(frames[1:], states[1:]))
        (animated if moving else static).append(mesh)
    return static, animated


def surface_collapsed(linear):
    """True where a 3x3 map has rank <= 1: every triangle gets zero area.

    An authored scale of zero on all axes, or on two of them, leaves a point
    or a line, so the part draws nothing at that sample. A rank-2 map (one
    zero axis) can still show a flat surface and is not collapsed.
    """
    values = np.linalg.svd(np.asarray(linear, dtype=float), compute_uv=False)
    return values[..., 1] <= values[..., 0] * 1e-9


def _sample_descriptor(model, descriptor, states, attachment=None):
    rest_inverse = np.linalg.inv(model.deform(base_descriptor(descriptor),model.rest))
    result = np.asarray([model.deform(descriptor,world) @ rest_inverse for world,_ in states])
    if attachment is not None:
        result = attachment @ result
    if not np.isfinite(result).all():
        raise ValueError("Nonfinite sampled field transform")
    # A rigid part authored at exactly zero scale contributes no surface.
    # prepare_pack_set hides its batch at these samples; provide an invertible
    # unused normal matrix instead of serializing a singular GPU transform.
    collapsed = np.all(result[:, :3, :3] == 0, axis=(1, 2))
    if base_descriptor(descriptor)[0] == "rigid":
        # A point or a line (rank <= 1) has no surface either; such samples
        # are hidden like the exactly-zero ones.
        collapsed = collapsed | surface_collapsed(result[:, :3, :3])
        result[collapsed, :3, :3] = np.eye(3)
    else:
        approvals = getattr(model, "_collapsed_world_descriptors", {})
        for index in np.flatnonzero(collapsed):
            if descriptor not in approvals.get(id(states[index][0]), set()):
                raise ValueError("Singular sampled field transform")
    # Exactly collapsed skin tracks stay zero. Their proven no-surface batches
    # are hidden at those samples; the native reader supplies an unused normal
    # while retaining exact interpolation from the neighboring visible tick.
    # A uniformly tiny authored mesh (Espeon's auxiliary rigid part) can
    # have a tiny determinant without being ill-conditioned. Compare its
    # singular values, preserving scale while rejecting collapsed axes.
    singular = np.linalg.svd(result[:,:3,:3], compute_uv=False)
    if np.any((singular[:, -1] <= singular[:, 0] * 1e-9) & ~collapsed):
        raise ValueError("Singular sampled field transform")
    if np.max(np.abs(result-np.eye(4)))<1e-7:
        result[:] = np.eye(4)
    return result[:,:3,:].astype("<f4")


def prepare_pack_set(meshes, schedules, fps, camera_frames, lights, archive_hash, output, prefix,
                     attachment=None, expected_count=None, format_version=2, retained_parts=()):
    """Sample once, compact equal affine tracks, then partition at native bounds.

    schedules is a list of (source_clip, authored_frame_sequence, loop). Every
    model in a map group uses the same clock; independent clocks get a new set.
    retained_parts adds original non-mesh JObj world matrices after render
    tracks, with an explicit source part to sampled bone map in the report.
    """
    if retained_parts and expected_count!=1:
        raise ValueError("Retained source parts require one explicit model pack")
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
            world, visible = states[model][i]
            if (all(descriptor[0] == "rigid" for descriptor in mesh["descriptors"]) and
                (np.all(np.asarray(world[mesh["bone"]])[:3, :3] == 0) or
                 surface_collapsed(np.asarray(world[mesh["bone"]])[:3, :3]))):
                visible = list(visible)
                visible[mesh["bone"]] = False
            material = material_state(mesh,clip,frame,visible)
            if format_version>=3:
                source_joint = states[model][i][0][mesh["bone"]]
                if attachment is not None:
                    source_joint = attachment[i] @ source_joint
                material += (*source_joint[:3,3],0.)
            else:
                if any(abs(value)>1e-7 for value in material[16:]):
                    raise ValueError("Full UV affine rotation requires C3T3")
                material=material[:16]
            material_tracks[id(mesh)].append(material)
    chunks, current, keys, count, images = [], [], set(), 0, set()
    for mesh in meshes:
        used = {signatures[(mesh["model"],d)] for d in mesh["descriptors"]}
        # C3_TITLE_MAX_TEXTURES: distinct (size, pixels, wrap) per pack.
        textures = {(w,h,hashlib.sha256(pixels).digest(),s,t) for w,h,pixels,s,t in mesh["textures"]}
        if len(used)>64:
            raise ValueError("Single field mesh exceeds sampled bone limit")
        if (len(keys|used)>64 or count+len(mesh["vertices"])>32768 or len(current)==64 or
                len(images|textures)>32):
            chunks.append(current)
            current,keys,count,images=[],set(),0,set()
        current.append(mesh)
        keys |= used
        images |= textures
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
        part_records=[]
        seen_parts=set()
        for model,part_index,joint_index in retained_parts:
            if (model not in models or not isinstance(part_index,int) or not 0<=part_index<65536 or
                not isinstance(joint_index,int) or not 0<=joint_index<len(model.joints) or
                part_index in seen_parts):
                raise ValueError("Invalid retained original part mapping")
            seen_parts.add(part_index)
            track=np.asarray([world[joint_index] for world,_ in states[model]])
            if attachment is not None:
                track=attachment @ track
            if (not np.isfinite(track).all() or
                np.min(np.abs(np.linalg.det(track[:,:3,:3])))<1e-9 or len(bone_tracks)>=64):
                raise ValueError("Retained original part exceeds sampled transform limits")
            part_records.append({"source_part":part_index,"source_joint":joint_index,
                "source_joint_address":f"{model.joints[joint_index].address:08x}",
                "sampled_bone":len(bone_tracks),"transform":"model-space-world"})
            bone_tracks.append(track[:,:3,:].astype("<f4"))
        sampled=bytearray()
        for index in range(len(frames)):
            for track in bone_tracks:
                sampled.extend(track[index].tobytes())
            for mesh in chunk:
                sampled.extend(struct.pack("<24f" if format_version>=3 else "<16f",*material_tracks[id(mesh)][index]))
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
                "retained_parts":part_records,
                "source_meshes":[{"hsd_root_address":mesh["model"].raw.root_joint.address,
                                  "mesh_index":mesh["index"],"owner_joint":mesh["bone"],
                                  "texture_index":batches[i][2],
                                  "image_variant":mesh.get("image_variant"),
                                  "source_render_mode":"%08x"%mesh["dobj"].mobject.render_mode}
                                 for i,mesh in enumerate(chunk)],
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


def wes_motion_schedules(model):
    """Keep locomotion at pack 0/1/2; append the original event pose at 3.

    Source motion zero is an authored ten-frame animation, not an idle alias.
    The source general motion native chooses looping again at runtime.
    """
    if model.ends[:4] != [10,30,30,17]:
        raise ValueError("Original Wes motion mapping changed")
    return [(i,sample_sequence(model.ends[i]),True) for i in (1,2,3,0)]


def prepare_wes(archive, output, prefix, camera, lights, format_version=2):
    from prepare_outskirt import PROFILES
    _,raw,scene=load_scene(archive,0x00F70400)
    model=Model(scene.models[0],raw.models[0])
    meshes=[build_mesh(model,i) for i in range(len(model.meshes))]
    schedules=wes_motion_schedules(model)
    samples=sum(len(sequence) for _,sequence,_ in schedules)
    reports=prepare_pack_set(meshes,schedules,60,[camera]*samples,lights,
        PROFILES["field_common.fsys"][1],output,prefix,expected_count=1,
        format_version=format_version)
    return reports,sum(len(mesh["vertices"]) for mesh in meshes)


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
    wes_reports,actor_vertices=prepare_wes(archives["field_common.fsys"],output,
                                         "outskirt_wes",fixed,lights)
    reports+=wes_reports
    reports+=prepare_arrival(archives,output,lights,source_hash)
    return reports,fixed[:11],actor_vertices


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
