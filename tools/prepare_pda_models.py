#!/usr/bin/env python3
"""Export private original PKX idle models for the PDA native preview.

Species numbers are original save-table indices, not National Dex numbers.
No assets are distributed. Unsupported source variants fail explicitly.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import types

from prepare_battle_scene import PKX_TABLE, archive_names, bindings, clip_mapping, load_pkx
from prepare_battle_init import COMMON_BYTES, COMMON_SHA1
from prepare_boot_media import MAIN_DOL_SHA1
from prepare_outskirt import extent, rel_data, u32, member


def species_binding(species, common, dol, toc):
    if not 1 <= species <= 411 or species==201:
        raise ValueError("Unsupported species/form: Unown requires source saved-form binding")
    row = extent(rel_data(common, 125), 0x121A04 + species * 284, 284)
    pkx = u32(row, 44)
    group, resource, name = struct.unpack(">III", extent(dol, PKX_TABLE + pkx * 12, 12))
    return {"species": species, "pkx": pkx, "group": group,
            "resource": resource, "name_id": name, "archive": archive_names(toc)[group]}


def descriptor(species, packs, bounds, route=(0, 1, 2), brightness=(127, 127, 127)):
    if len(bounds) != 6 or not all(math.isfinite(x) for x in bounds) or any(bounds[i] > bounds[i+3] for i in range(3)):
        raise ValueError("Invalid PDA bounds")
    if not 1 <= len(packs) <= 16:
        raise ValueError("PDA model exceeds stage part limit")
    result = bytearray(64 + 48 * len(packs))
    if len(route)!=3 or any(not isinstance(x,int) or not 0<=x<=3 for x in route) or len(brightness)!=3 or any(not isinstance(x,int) or not 0<=x<=255 for x in brightness):
        raise ValueError("Invalid source shiny filter")
    struct.pack_into("<4s4I6f", result, 0, b"C3PD", 2, len(result), species, len(packs), *bounds)
    result[44:50] = bytes((*route, *brightness))
    for i, pack in enumerate(packs):
        name = pack["file"]
        if not name.startswith("idle_") or not name.endswith(".c3t") or len(name) > 47 or any(c not in "idle_0123456789.c3t" for c in name):
            raise ValueError("Invalid PDA part basename")
        result[64 + i * 48:64 + i * 48 + len(name)] = name.encode("ascii")
    return bytes(result)


def split_envelope_positions(pobj, position_attribute, matrix_attribute):
    """Make the pinned IR's position-index skin map lossless for GX corners.

    GX indexes position and PNMTXIDX independently. Duplicate only aliased
    source positions; each corner keeps its exact coordinate and envelope.
    Normals/UVs/colors retain their original independent corner indices.
    """
    pos = pobj.find_attribute_index(position_attribute)
    matrix = pobj.find_attribute_index(matrix_attribute)
    if pos is None or matrix is None:
        return 0
    positions, matrices = pobj.face_lists[pos], pobj.face_lists[matrix]
    if len(positions)!=len(matrices):
        raise ValueError("Mismatched source position/matrix faces")
    mapping, first, added = {}, {}, 0
    source = list(pobj.sources[pos])
    faces = []
    for face, indices in zip(positions, matrices):
        if len(face)!=len(indices):
            raise ValueError("Mismatched source position/matrix corners")
        rebuilt = []
        for position, index in zip(face, indices):
            raw = pobj.sources[matrix][index]
            if raw%3 or raw//3>=len(pobj.property):
                raise ValueError("Invalid original envelope index")
            key = (position, raw)
            if key not in mapping:
                if position not in first:
                    first[position]=raw;mapping[key]=position
                else:
                    mapping[key]=len(source);source.append(source[position]);added+=1
            rebuilt.append(mapping[key])
        faces.append(rebuilt)
    pobj.sources[pos]=source
    pobj.face_lists[pos]=faces
    return added


def omit_collapsed_meshes(meshes, states, clip=None, frames=None):
    """Omit only surfaces proven degenerate for the entire sampled idle.

    A zero-linear envelope maps every vertex to its translation. A triangle
    with at most two such envelopes is always a line or point, including
    between samples, so it contributes no rasterized surface or normal.
    """
    import numpy as np
    # Keep authored triangle order while separating surface visibility domains.
    # A single PObj can contain both ordinary body triangles and an independently
    # animated zero-scale accessory; hiding the entire PObj would lose the body.
    split_meshes = []
    for mesh in meshes:
        descriptors = mesh["descriptors"]
        masks = {desc: tuple(np.all(mesh["model"].deform(desc, world)[:3, :3] == 0)
                            for world, _ in states) for desc in set(descriptors)}
        groups = []
        for offset in range(0, len(descriptors), 3):
            triangle = set(descriptors[offset:offset+3])
            mask = tuple(len(triangle) <= 2 and all(masks[d][i] for d in triangle)
                         for i in range(len(states)))
            if not groups or groups[-1][0] != mask:
                groups.append((mask, []))
            groups[-1][1].extend(range(offset, offset+3))
        if len(groups) == 1:
            split_meshes.append(mesh)
        else:
            for _, indices in groups:
                part = dict(mesh)
                part["vertices"] = [mesh["vertices"][i] for i in indices]
                part["descriptors"] = [descriptors[i] for i in indices]
                part["batch"] = list(mesh["batch"])
                part["batch"][1] = len(indices)
                split_meshes.append(part)
    meshes = split_meshes
    retained, omitted = [], []
    candidates = []
    for mesh in meshes:
        descriptors = mesh["descriptors"]
        degenerate = all(len(set(descriptors[i:i+3])) <= 2
                         for i in range(0, len(descriptors), 3))
        candidates.append([degenerate and all(
            np.all(mesh["model"].deform(desc, world)[:3, :3] == 0)
            for desc in set(descriptors)) for world, _ in states])
    for index, mesh in enumerate(meshes):
        if all(candidates[index]):
            omitted.append(index)
        else:
            retained.append(mesh)
            if frames is not None:
                mesh["collapsed_samples"] = {(clip, frame) for frame, collapsed in
                    zip(frames, candidates[index]) if collapsed}
    # A collapsed envelope may occur in several batches. Preserve its exact
    # zero affine only when every consuming batch has no surface at that
    # sample, and will therefore be hidden by material_state.
    if frames is not None:
        for sample, (world, _) in enumerate(states):
            for mesh in meshes:
                model = mesh["model"]
                approved = model.__dict__.setdefault("_collapsed_world_descriptors", {}).setdefault(id(world), set())
                for desc in set(mesh["descriptors"]):
                    users = [i for i, other in enumerate(meshes) if desc in other["descriptors"]]
                    if all(candidates[i][sample] for i in users):
                        approved.add(desc)
    return retained, omitted


def prepare(args):
    import prepare_title as title
    import prepare_field_geometry as geometry
    import numpy as np
    parser = title.verify_parser(args.parser)
    geometry.import_parser(parser)
    common = Path(args.common).read_bytes()
    if len(common) != COMMON_BYTES or hashlib.sha1(common).hexdigest() != COMMON_SHA1:
        raise ValueError("common REL differs from canonical original")
    dol = Path(args.dol).read_bytes()
    if hashlib.sha1(dol).hexdigest() != MAIN_DOL_SHA1:
        raise ValueError("DOL differs from canonical original")
    source = Path(args.files)
    toc = (source / "GSfsys.toc").read_bytes()
    arena = bindings(common, dol, toc)["arena"]
    light_data, light_root, _ = geometry.load_scene((source / arena["archive"]).read_bytes(), arena["resource"])
    lights = geometry.fixed_lights(light_root, light_data)
    successes, failures = [], []
    for species in args.species:
        try:
            binding = species_binding(species, common, dol, toc)
            path = source / binding["archive"]
            if path.is_symlink() or not 0 < path.stat().st_size <= 16 * 1024 * 1024:
                raise ValueError("Invalid PKX source extent")
            archive = path.read_bytes()
            data, raw, scene, header = load_pkx(archive, path.name)
            from shared.Constants.gx import GX_VA_POS, GX_VA_PNMTXIDX
            split_positions = sum(split_envelope_positions(pobj,GX_VA_POS,GX_VA_PNMTXIDX)
                                  for _,_,pobj in geometry.raw_meshes(raw.models[0]))
            if split_positions:
                from importer.phases.describe.describe import describe_scene
                options = {"filepath": path.name, "game": "COLO_XD", "max_frame": 1000000000,
                           "section_names": [], "ik_hack": False, "pkx_header": header}
                scene = describe_scene([types.SimpleNamespace(root_node=raw,section_name="scene_data")],options)
            model = geometry.Model(scene.models[0], raw.models[0], allow_image_swaps=True)
            entries = clip_mapping(header, model.ends)
            if len(entries[0]["clips"]) != 1:
                raise ValueError("Ambiguous authored idle")
            idle = entries[0]["clips"][0]
            frames = geometry.sample_sequence(model.ends[idle])
            # HSD permits ten envelope influences. They become one sampled affine
            # track, so the native GPU bone limit does not bound influence count.
            meshes = geometry.expand_image_swaps([geometry.build_mesh(model, i, max_envelope_weights=10,source_materials=True)
                                                  for i in range(len(model.meshes))])
            states = [model.world(idle, frame) for frame in frames]
            meshes, collapsed_meshes = omit_collapsed_meshes(meshes, states, idle, frames)
            # Each envelope is shared by many vertices. Bound every authored idle
            # sample using the same affine deformation as the runtime pack.
            tracks = {}
            low, high = np.full(3, np.inf), np.full(3, -np.inf)
            for mesh in meshes:
                for vertex, desc in zip(mesh["vertices"], mesh["descriptors"]):
                    if desc not in tracks:
                        rest_inverse = np.linalg.inv(model.deform(desc, model.rest))
                        tracks[desc] = np.asarray([(model.deform(desc, world) @ rest_inverse)[:3]
                                                   for world, _ in states])
                    track = tracks[desc]
                    positions = track @ np.asarray((*vertex[:3], 1.))
                    low, high = np.minimum(low, positions.min(axis=0)), np.maximum(high, positions.max(axis=0))
            bounds = [*map(float, low), *map(float, high)]
            output = Path(args.output) / str(species)
            output.mkdir(parents=True, exist_ok=True)
            if species==308:
                from prepare_spinda import export_spinda
                binding["spinda"]=export_spinda(archive,dol,output)
            camera = (0., 2., 6., 0., 1., 0., 0., 40., .01, 1000., 400/240, 0.)
            packs = geometry.prepare_pack_set(meshes, [(idle, frames, True)], 120,
                [camera] * len(frames), lights, hashlib.sha256(archive).hexdigest(), output, "idle_", format_version=3)
            payload = descriptor(species, packs, bounds, header.shiny_route[:3], header.shiny_brightness[:3])
            (output / "preview.c3p").write_bytes(payload)
            binding.update({"schema": 1, "packs": packs, "bounds": bounds, "idle_source_clip": idle, "lighting_source": arena,
                "split_envelope_positions": split_positions,
                "omitted_zero_surface_meshes": collapsed_meshes,
                "archive_sha256": hashlib.sha256(archive).hexdigest(),
                "member_sha256": hashlib.sha256(member(archive, binding["resource"])).hexdigest(),
                "shiny_route": list(header.shiny_route), "shiny_brightness": list(header.shiny_brightness),
                "unsupported": ["Unown saved forms"]})
            (output / "manifest.json").write_text(json.dumps(binding, indent=2) + "\n")
            successes.append(species)
        except (ValueError, KeyError, OSError) as error:
            if not getattr(args, "snag_list", False):
                raise
            failures.append({"species": species, "error": str(error)})
            print("Unsupported species%d: %s" % (species, error), flush=True)
    report = {"prepared": successes, "unsupported": failures}
    (Path(args.output) / "preparation.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


if __name__ == "__main__":
    p = argparse.ArgumentParser(description=__doc__)
    for name in ("files", "common", "dol", "parser", "output"):
        p.add_argument("--" + name, required=True)
    selection = p.add_mutually_exclusive_group(required=True)
    selection.add_argument("--species", nargs="+", type=int)
    selection.add_argument("--snag-list", action="store_true", help="Prepare all48 canonical Snag List models; report unsupported entries and return nonzero")
    args = p.parse_args()
    if args.snag_list:
        from prepare_game_state import Dol
        dol_bytes = Path(args.dol).read_bytes()
        if hashlib.sha1(dol_bytes).hexdigest()!=MAIN_DOL_SHA1:
            raise ValueError("DOL differs from canonical original")
        common_bytes=Path(args.common).read_bytes()
        if hashlib.sha1(common_bytes).hexdigest()!=COMMON_SHA1:
            raise ValueError("common REL differs from canonical original")
        source=rel_data(common_bytes,125)
        shadow_ids=struct.unpack(">48H", Dol(dol_bytes).read(0x80375240,96))
        args.species=[struct.unpack_from(">H",source,0x1439D8+identity*56+2)[0] for identity in shadow_ids]
    report = prepare(args)
    raise SystemExit(1 if report["unsupported"] else 0)
