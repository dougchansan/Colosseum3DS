#!/usr/bin/env python3
"""Prepare a private C3M1 skeletal preview from a verified extracted GC6E01 disc.

Requires Blender 5.x and the pinned GPLv3 vendor/hsd-importer checkout. Run with
ordinary Python; Blender is launched for the offline parser and pose evaluation.
No game data belongs in the source tree except its ignored private directory.
"""
import argparse
import hashlib
import json
import math
import shutil
import struct
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from verify_inputs import ROOT, file_hash, load_manifest

PARSER_COMMIT = "dc71de837978a6a2fe8c5082f2840cfafb135235"
EEVEE_ARCHIVE_SHA256 = "fe69d7d79f7871610c07ff8875925cdbe4459b4e547f34e253452c3dcad0afef"
MAX_ARCHIVE_BYTES = 1024 * 1024
MAX_BYTES = 16 * 1024 * 1024
LIMITS = (128, 8192, 32, 16, 240)


def peak_working_set_bytes():
    """Process peak including Blender; this is offline host usage, never 3DS RAM."""
    if sys.platform == "win32":
        import ctypes
        from ctypes import wintypes

        class Counters(ctypes.Structure):
            _fields_ = [("cb", wintypes.DWORD), ("faults", wintypes.DWORD)] + [
                (name, ctypes.c_size_t) for name in ("peak", "working", "peak_paged", "paged",
                    "peak_nonpaged", "nonpaged", "pagefile", "peak_pagefile")]

        counters = Counters()
        counters.cb = ctypes.sizeof(counters)
        kernel = ctypes.WinDLL("kernel32", use_last_error=True)
        kernel.GetCurrentProcess.restype = wintypes.HANDLE
        get_memory = ctypes.WinDLL("psapi").GetProcessMemoryInfo
        get_memory.argtypes = [wintypes.HANDLE, ctypes.POINTER(Counters), wintypes.DWORD]
        if get_memory(kernel.GetCurrentProcess(), ctypes.byref(counters), counters.cb):
            return counters.peak
        return None
    import resource
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss * (1 if sys.platform == "darwin" else 1024)


def verify_disc(disc):
    disc = Path(disc).resolve(strict=True)
    for name in ("sys", "sys/boot.bin", "sys/main.dol", "files",
                 "files/pkx_eievui.fsys"):
        if (disc / name).is_symlink():
            raise ValueError("Symbolic links are not accepted in model inputs")
    boot = (disc / "sys/boot.bin").read_bytes()[:8]
    if boot != b"GC6E01\0\0":
        raise ValueError("Expected extracted GC6E01 disc 1 revision 0")
    expected = load_manifest(ROOT / "vendor/colosseum/config/GC6E01/build.sha1")
    actual = file_hash(disc / "sys/main.dol")
    if actual != expected["sys/main.dol"]:
        raise ValueError("main.dol differs from the pinned GC6E01 original")
    return disc, actual


def check_output(output):
    output = Path(output).resolve()
    if output.exists() or output.with_suffix(output.suffix + ".json").exists():
        raise ValueError("Choose a new output file; outputs are never overwritten")
    if output == ROOT or ROOT in output.parents:
        private = ROOT / "private"
        if private not in output.parents:
            raise ValueError("Model output in the repository must be under ignored private/")
    return output


def rgba_top_down(pixels, width, height):
    """IR pixels use Blender's bottom row first; the pack uses its top row first."""
    if width < 1 or height < 1 or len(pixels) != width * height * 4:
        raise ValueError("Malformed RGBA image")
    stride = width * 4
    return b"".join(pixels[y * stride:(y + 1) * stride]
                    for y in range(height - 1, -1, -1))


def resize_nearest(pixels, width, height):
    """Resample whole images, preserving the UV domain and repeat behavior."""
    if width < 1 or height < 1 or width > 1024 or height > 1024:
        raise ValueError("Texture dimensions exceed the supported subset")
    out_width = max(8, 1 << (width - 1).bit_length())
    out_height = max(8, 1 << (height - 1).bit_length())
    if len(pixels) != width * height * 4:
        raise ValueError("Malformed RGBA image")
    if (width, height) == (out_width, out_height):
        return width, height, pixels
    result = bytearray(out_width * out_height * 4)
    for y in range(out_height):
        src_y = min(height - 1, int((y + 0.5) * height / out_height))
        for x in range(out_width):
            src_x = min(width - 1, int((x + 0.5) * width / out_width))
            src = (src_y * width + src_x) * 4
            dst = (y * out_width + x) * 4
            result[dst:dst + 4] = pixels[src:src + 4]
    return out_width, out_height, bytes(result)


def triangulate(faces, uvs):
    """Return source vertex and face-loop UV pairs, retaining source winding."""
    if sum(len(face) for face in faces) != len(uvs):
        raise ValueError("UV loop count does not match geometry")
    offset = 0
    for face in faces:
        if len(face) < 3:
            raise ValueError("Unsupported polygon with fewer than three corners")
        for i in range(1, len(face) - 1):
            for corner in (0, i, i + 1):
                u, v = uvs[offset + corner]
                yield face[corner], (u, 1.0 - v)
        offset += len(face)


def encode_pack(bones, vertices, batches, textures, frames, fps, bounds, archive_sha):
    """Encode the bounded, canonical little-endian C3M1 layout (no alignment gaps)."""
    counts = (bones, len(vertices), len(batches), len(textures), len(frames))
    if any(not 0 < n <= limit for n, limit in zip(counts, LIMITS)):
        raise ValueError("Model exceeds C3M1 limits or contains an empty section")
    if len(vertices) % 3 or not math.isfinite(fps) or not 1 <= fps <= 120:
        raise ValueError("Invalid triangle count or frame rate")
    if len(bounds) != 6 or not all(math.isfinite(x) for x in bounds):
        raise ValueError("Invalid bounds")
    if any(bounds[i] > bounds[i + 3] for i in range(3)):
        raise ValueError("Inverted bounds")
    if len(archive_sha) != 32:
        raise ValueError("Expected raw SHA256 archive digest")
    vertex_bytes = bytearray()
    for position, uv, bone, color in vertices:
        if len(position) != 3 or len(uv) != 2 or not 0 <= bone < bones:
            raise ValueError("Invalid vertex")
        if not all(math.isfinite(x) for x in (*position, *uv)):
            raise ValueError("Non-finite vertex")
        vertex_bytes.extend(struct.pack("<5fI4BI", *position, *uv, bone, *color, 0))
    batch_bytes = bytearray()
    next_vertex = 0
    for first, count, texture, flags in batches:
        if first != next_vertex or count <= 0 or count % 3 or texture >= len(textures) or texture < 0 or flags & ~3:
            raise ValueError("Invalid or noncanonical batch")
        next_vertex += count
        batch_bytes.extend(struct.pack("<4I", first, count, texture, flags))
    if next_vertex != len(vertices):
        raise ValueError("Batches must cover every vertex exactly once")
    matrix_bytes = bytearray()
    for frame in frames:
        if len(frame) != bones:
            raise ValueError("Frame bone count mismatch")
        for matrix in frame:
            if len(matrix) != 12 or not all(math.isfinite(x) for x in matrix):
                raise ValueError("Invalid skin matrix")
            matrix_bytes.extend(struct.pack("<12f", *matrix))
    offsets = [128, 128 + len(vertex_bytes)]
    offsets.append(offsets[-1] + len(batch_bytes))
    offsets.append(offsets[-1] + len(textures) * 24)
    offsets.append(offsets[-1] + len(matrix_bytes))
    descriptors = bytearray()
    pixels = bytearray()
    for width, height, rgba, wrap_s, wrap_t in textures:
        if any(d < 8 or d > 1024 or d & (d - 1) for d in (width, height)):
            raise ValueError("Texture dimensions must be powers of two in 8..1024")
        if len(rgba) != width * height * 4 or wrap_s not in (0, 1) or wrap_t not in (0, 1):
            raise ValueError("Invalid texture data or wrapping")
        descriptors.extend(struct.pack("<6I", width, height, offsets[-1] + len(pixels), len(rgba), wrap_s, wrap_t))
        pixels.extend(rgba)
    total = offsets[-1] + len(pixels)
    if total > MAX_BYTES:
        raise ValueError("Model pack exceeds 16 MiB")
    header = bytearray(128)
    struct.pack_into("<4s7If5I6f", header, 0, b"C3M1", 1, total, *counts, fps, *offsets, *bounds)
    header[80:112] = archive_sha
    return bytes(header + vertex_bytes + batch_bytes + descriptors + matrix_bytes + pixels)


def read_archive(archive):
    """Admit only the reviewed Eevee archive before any container decompression."""
    with Path(archive).open("rb") as stream:
        data = stream.read(MAX_ARCHIVE_BYTES + 1)
    if len(data) > MAX_ARCHIVE_BYTES:
        raise ValueError("Eevee archive exceeds the 1 MiB input limit")
    if hashlib.sha256(data).hexdigest() != EEVEE_ARCHIVE_SHA256:
        raise ValueError("Eevee archive differs from the reviewed GC6E01 revision 0 source")
    return data


def load_scene(parser_root, archive, archive_bytes):
    # The package entry point imports bpy and registration UI. The phases are
    # intentionally independently usable; point their package paths at the pin.
    import types
    sys.path.insert(0, str(parser_root))
    for name in ("importer", "importer.phases"):
        package = types.ModuleType(name)
        package.__path__ = [str(parser_root / name.replace(".", "/"))]
        sys.modules[name] = package
    from importer.phases.extract.extract import extract_dat
    from importer.phases.route.route import route_sections
    from importer.phases.parse.parse import parse_sections
    from importer.phases.describe.describe import describe_scene
    # Preserve source channels; bake() refuses actions beyond the pack limit
    # rather than silently trimming them to the demonstration's frame budget.
    options = {"filepath": archive.name, "game": "COLO_XD", "max_frame": 1_000_000_000,
               "section_names": [], "ik_hack": True}
    entries = extract_dat(archive_bytes, archive.name, options=options)
    if len(entries) != 1:
        raise ValueError("Expected exactly one Eevee DAT member")
    dat, metadata = entries[0]
    options["pkx_header"] = metadata.pkx_header
    scene = describe_scene(parse_sections(dat, route_sections(dat), options), options)
    if len(scene.models) != 1:
        raise ValueError("Expected exactly one skeletal model")
    return scene, options, hashlib.sha256(dat).hexdigest(), metadata.filename


def geometry(model):
    vertices, batches, textures, texture_info = [], [], [], []
    texture_indices = {}
    names = {bone.name: i for i, bone in enumerate(model.bones)}
    for mesh in model.meshes:
        if mesh.is_hidden:
            continue
        material = mesh.material
        if material is None or len(material.texture_layers) != 1:
            raise ValueError("Visible meshes require exactly one texture layer")
        layer = material.texture_layers[0]
        if (layer.coord_type.name != "UV" or layer.uv_index >= len(mesh.uv_layers)
                or layer.is_bump or layer.combiner is not None
                or any(abs(x) > 1e-6 for x in (*layer.rotation, *layer.translation))
                or any(abs(x - 1) > 1e-6 for x in layer.scale)
                or layer.repeat_s != 1 or layer.repeat_t != 1
                or layer.wrap_s.name not in ("CLAMP", "REPEAT")
                or layer.wrap_t.name not in ("CLAMP", "REPEAT")):
            raise ValueError("Unsupported texture coordinates, transform, combiner, or wrapping")
        if mesh.shape_keys or any(abs(channel - 1) > 1e-6 for color_layer in mesh.color_layers
                                  for color in color_layer.colors for channel in color):
            raise ValueError("Morph targets and non-white vertex color layers are outside this subset")
        bw = mesh.bone_weights
        if bw is None or bw.type.name != "WEIGHTED":
            raise ValueError("Visible meshes require explicit weighted vertices")
        weights = {}
        for index, assignments in bw.assignments or []:
            if index in weights or len(assignments) != 1 or abs(assignments[0][1] - 1) > 1e-6:
                raise ValueError("Only one influence of weight 1 per vertex is supported")
            if assignments[0][0] not in names:
                raise ValueError("Weight references an unknown bone")
            weights[index] = names[assignments[0][0]]
        if set(weights) != set(range(len(mesh.vertices))):
            raise ValueError("Every vertex must have one bone assignment")
        image = layer.image
        wrapping = (int(layer.wrap_s.name == "REPEAT"), int(layer.wrap_t.name == "REPEAT"))
        key = (image.image_id, image.palette_id, *wrapping)
        if key not in texture_indices:
            w, h, pixels = resize_nearest(rgba_top_down(image.pixels, image.width, image.height), image.width, image.height)
            texture_indices[key] = len(textures)
            textures.append((w, h, pixels, *wrapping))
            texture_info.append({"name": image.name, "source_size": [image.width, image.height], "size": [w, h],
                                 "resampled_nearest": [w, h] != [image.width, image.height]})
        texture_index = texture_indices[key]
        alpha = any(a != 255 for a in textures[texture_index][2][3::4])
        first = len(vertices)
        for index, uv in triangulate(mesh.faces, mesh.uv_layers[layer.uv_index].uvs):
            if not 0 <= index < len(mesh.vertices):
                raise ValueError("Polygon references an invalid vertex")
            vertices.append((mesh.vertices[index], uv, weights[index], (255, 255, 255, 255)))
        batches.append((first, len(vertices) - first, texture_index, 2 if alpha else 0))
    return vertices, batches, textures, texture_info


def verify_pose_frame(actual, expected, vertices, frame, tolerance=1e-4):
    """Compare Blender skinning with direct GX-world/IR-bind skinning.

    Only source-visible weighted bones affect the pack's geometry. Checking
    both coefficients and transformed vertices catches basis/space mismatches
    as well as a misplaced inverse bind even on this singly-weighted subset.
    """
    used = {bone for _position, _uv, bone, _color in vertices}
    if not used or any(not math.isfinite(value) for bone in used
                       for palette in (actual, expected) for value in palette[bone]):
        raise ValueError(f"GX/Blender skinning fidelity has empty or non-finite data at frame {frame}")
    matrix_error = max(abs(actual[bone][i] - expected[bone][i])
                       for bone in used for i in range(12))
    vertex_error = 0.0
    for position, _uv, bone, _color in vertices:
        difference = []
        for row in range(3):
            start = row * 4
            difference.append(sum((actual[bone][start + i] - expected[bone][start + i])
                                  * position[i] for i in range(3))
                              + actual[bone][start + 3] - expected[bone][start + 3])
        vertex_error = max(vertex_error, math.sqrt(sum(x * x for x in difference)))
    if not all(math.isfinite(x) and x <= tolerance for x in (matrix_error, vertex_error)):
        raise ValueError(f"GX/Blender skinning fidelity failed at frame {frame}: "
                         f"matrix error {matrix_error:.6g}, vertex error {vertex_error:.6g} m; "
                         "check parser bind-space and Blender inheritance compatibility")
    return matrix_error, vertex_error


def bake(scene, options, animation_index, vertices):
    import bpy
    from mathutils import Matrix
    from types import SimpleNamespace
    from importer.phases.plan.plan import plan_scene
    from importer.phases.plan.helpers.animations import compute_bake_plan, bake_frame, _compose_gx, _rest_local_scale
    from importer.phases.build_blender.helpers.skeleton import build_skeleton
    from importer.phases.build_blender.helpers.animations import _insert_raw_keyframes, _fill_missing_channels_with_rest
    model = scene.models[0]
    if not 0 <= animation_index < len(model.bone_animations):
        raise ValueError("Animation index is out of range")
    if any(getattr(model, name) for name in ("ik_constraints", "copy_location_constraints", "track_to_constraints",
                                            "copy_rotation_constraints", "limit_rotation_constraints", "limit_location_constraints")):
        raise ValueError("Constrained skeletons are outside the initial converter subset")
    animation = model.bone_animations[animation_index]
    end = max(track.end_frame for track in animation.tracks)
    if not math.isfinite(end) or end < 1 or end > 239:
        raise ValueError("Animation duration exceeds the initial 240-frame subset")
    # Keep only the selected source action. The donor's plan/build bake handles
    # its scale compensation and NONE/ALIGNED inheritance choices.
    model.bone_animations = [animation]
    planned = plan_scene(scene, options).models[0]
    armature = build_skeleton(planned.armature, bpy.context)
    if any(track.spline_path is not None for track in planned.actions[0].bone_tracks):
        raise ValueError("Spline-constrained animation is outside this subset")
    # Blender 5 removed Action.fcurves. Adapt the donor's raw-keyframe helpers
    # to a real slotted channelbag rather than altering the pinned dependency.
    action = bpy.data.actions.new("C3M1 source channels")
    slot = action.slots.new('OBJECT', armature.name)
    strip = action.layers.new("Source").strips.new(type='KEYFRAME')
    curves = SimpleNamespace(fcurves=strip.channelbag(slot, ensure=True).fcurves)
    raw_by_bone = {}
    for track in planned.actions[0].bone_tracks:
        raw = _insert_raw_keyframes(track, curves)
        _fill_missing_channels_with_rest(track, curves, raw)
        raw_by_bone[track.bone_index] = raw
    skeleton = planned.armature.bake_skeleton
    bake_indices, levels = compute_bake_plan(skeleton, set(raw_by_bone))
    for bone in armature.pose.bones:
        bone.rotation_mode = 'XYZ'
    rest_inverse = [armature.data.bones[bone.name].matrix_local.inverted() for bone in model.bones]
    ir_rest_inverse = [Matrix(bone.world_matrix).inverted_safe() for bone in model.bones]
    frames = []
    max_matrix_error = max_vertex_error = 0.0
    for frame in range(math.ceil(end) + 1):
        bpy.context.scene.frame_set(frame)
        srts = {index: (tuple(raw[i].evaluate(frame) for i in (7, 8, 9)),
                        tuple(raw[i].evaluate(frame) for i in (0, 1, 2)),
                        tuple(raw[i].evaluate(frame) for i in (4, 5, 6)))
                for index, raw in raw_by_bone.items()}
        targets = bake_frame(skeleton, srts, bake_indices)
        for level in levels:
            for index in level:
                armature.pose.bones[skeleton.bones[index].name].matrix = Matrix(targets[index])
            bpy.context.view_layer.update()
        evaluated = armature.evaluated_get(bpy.context.evaluated_depsgraph_get())
        matrices = []
        for bone, inverse in zip(model.bones, rest_inverse):
            # Pose and edit-bone matrices are both armature-local, matching IR
            # geometry. Do not apply the armature object's Y-up/Z-up display turn.
            skin = evaluated.pose.bones[bone.name].matrix @ inverse
            matrices.append([skin[row][col] for row in range(3) for col in range(4)])
        # Independent expected palette: compose source GX worlds directly,
        # then multiply by the *IR* rest inverse. This path never uses edit-bone
        # matrices, normalized targets, pose setters, or evaluated Blender data.
        gx_world, gx_accum, gx_rotation, expected = {}, {}, {}, {}
        for index in skeleton.dfs_order:
            bone = skeleton.bones[index]
            parent = bone.parent_index
            scale, rotation, location = srts.get(index, (
                _rest_local_scale(bone, skeleton.bones[parent] if parent is not None else None),
                bone.rest_rotation, bone.rest_position))
            world, accum, rotation_world = _compose_gx(
                scale, rotation, location, gx_world.get(parent), gx_accum.get(parent),
                gx_rotation.get(parent), bone.classical_scaling)
            gx_world[index], gx_accum[index], gx_rotation[index] = world, accum, rotation_world
            direct_skin = world @ ir_rest_inverse[index]
            expected[index] = [direct_skin[row][column] for row in range(3) for column in range(4)]
        matrix_error, vertex_error = verify_pose_frame(matrices, expected, vertices, frame)
        max_matrix_error = max(max_matrix_error, matrix_error)
        max_vertex_error = max(max_vertex_error, vertex_error)
        frames.append(matrices)
    delta = max(abs(value - frames[0][b][i]) for f in frames[1:] for b, mat in enumerate(f) for i, value in enumerate(mat))
    if delta < 1e-5:
        raise ValueError("Selected source action did not produce nontrivial skeletal motion")
    fidelity = {"method": "All frames: evaluated Blender pose/edit-rest inverse versus direct GX world/IR-rest inverse",
                "frames_checked": len(frames), "visible_weighted_bones_checked": len({v[2] for v in vertices}),
                "tolerance": 1e-4, "max_matrix_coefficient_error": max_matrix_error,
                "max_vertex_position_error_meters": max_vertex_error}
    return frames, animation, delta, bpy.app.version_string, fidelity


def worker(args):
    started = time.perf_counter()
    disc, dol_sha1 = verify_disc(args.disc)
    output = check_output(args.output)
    if disc in output.parents:
        raise ValueError("Model output must not modify the source disc tree")
    parser_root = args.parser.resolve(strict=True)
    commit = subprocess.check_output(["git", "-C", str(parser_root), "rev-parse", "HEAD"], text=True).strip()
    if commit != PARSER_COMMIT:
        raise ValueError("HSD importer checkout is not at the required pin")
    dirty = subprocess.check_output(["git", "-C", str(parser_root), "status", "--porcelain", "--untracked-files=no"], text=True)
    if dirty.strip():
        raise ValueError("HSD importer has tracked modifications")
    archive = disc / "files/pkx_eievui.fsys"
    archive_bytes = read_archive(archive)
    archive_sha = hashlib.sha256(archive_bytes).digest()
    scene, options, dat_sha, member = load_scene(parser_root, archive, archive_bytes)
    model = scene.models[0]
    vertices, batches, textures, texture_info = geometry(model)
    frames, animation, delta, blender_version, fidelity = bake(scene, options, args.animation, vertices)
    mins, maxs = [math.inf] * 3, [-math.inf] * 3
    for frame in frames:
        for position, _uv, bone, _color in vertices:
            mat = frame[bone]
            for axis in range(3):
                value = sum(mat[axis * 4 + i] * position[i] for i in range(3)) + mat[axis * 4 + 3]
                mins[axis] = min(mins[axis], value)
                maxs[axis] = max(maxs[axis], value)
    pack = encode_pack(len(model.bones), vertices, batches, textures, frames, args.fps, mins + maxs, archive_sha)
    manifest = {"schema": 1, "format": "C3M1", "source_game": "GC6E01", "source_revision": 0,
                "source_main_dol_sha1": dol_sha1, "source_archive": archive.name,
                "source_archive_sha256": archive_sha.hex(), "source_member": member, "source_dat_sha256": dat_sha,
                "parser_commit": commit, "parser_license": "GPL-3.0", "blender_version": blender_version,
                "animation": animation.name, "frames": len(frames), "fps": args.fps,
                "preview_loop": True, "source_loop_flag": animation.loop,
                "bones": len(model.bones), "vertices": len(vertices), "triangles": len(vertices) // 3,
                "batches": len(batches), "textures": texture_info,
                "omitted_hidden_meshes": [mesh.name for mesh in model.meshes if mesh.is_hidden],
                "nontrivial_pose_max_delta": delta, "bounds": mins + maxs,
                "bake_fidelity": fidelity,
                "pack_bytes": len(pack), "pack_sha256": hashlib.sha256(pack).hexdigest(),
                "offline_seconds": time.perf_counter() - started,
                "offline_host_peak_working_set_bytes": peak_working_set_bytes(),
                "limitations": ["Preview rate is explicit (default 30 fps); source animation timing is not independently measured.",
                    "Unlit, double-sided, single-texture preview; source lighting, specular, GX combiners and MObj effects are simplified.",
                    "Texture alpha uses alpha testing; translucency and source fragment blend equations are not reproduced.",
                    "Material animation tracks are frozen at the source base texture; skeletal animation is baked.",
                    "Non-power-of-two images are nearest-neighbor resampled to powers of two, preserving their UV domain.",
                    "Skin matrices are sampled at integer source frames; runtime linearly interpolates matrix coefficients."]}
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        stream.write(pack)
    with output.with_suffix(output.suffix + ".json").open("x", encoding="utf8") as stream:
        json.dump(manifest, stream, indent=2)
        stream.write("\n")
    print(f"Prepared {len(pack)} bytes, {len(vertices)//3} triangles, {len(frames)} skeletal frames: {output}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--parser", type=Path, default=ROOT / "vendor/hsd-importer")
    parser.add_argument("--blender", default=shutil.which("blender"))
    parser.add_argument("--animation", type=int, default=0)
    parser.add_argument("--fps", type=float, default=30.0)
    parser.add_argument("--worker", action="store_true", help=argparse.SUPPRESS)
    arguments = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else None
    args = parser.parse_args(arguments)
    try:
        if args.worker:
            worker(args)
            return 0
        verify_disc(args.disc)
        check_output(args.output)
        if not args.blender:
            raise ValueError("Blender 5.x is required; pass --blender PATH")
        command = [args.blender, "--background", "--factory-startup", "--python-exit-code", "1",
                   "--python", str(Path(__file__).resolve()), "--", "--worker", "--disc", str(args.disc.resolve()),
                   "--output", str(args.output.resolve()), "--parser", str(args.parser.resolve()),
                   "--animation", str(args.animation), "--fps", str(args.fps)]
        return subprocess.run(command, check=False).returncode
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Model preparation failed: {error}\n")


if __name__ == "__main__":
    sys.exit(main())
