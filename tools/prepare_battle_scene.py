#!/usr/bin/env python3
"""Prepare private encounter-10 scene assets through the existing HSD pipeline.

Exact source admission precedes parsing. This exports geometry and authored
clips, not combat, a save, or an assertion of complete presentation support.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct

from prepare_boot_media import verify_disc
from prepare_battle_init import COMMON_BYTES, COMMON_SHA1, SOURCE_COMMIT
from prepare_model import check_output
from prepare_outskirt import extent, member, rel_data, u32

PROFILES = {
    "GSfsys.toc": (288704, "ec9b61b55df903c39c8aa1a69035776cd7d4d1a33c8078aced9991d63030cc4e"),
    "S1_out_bf.fsys": (763328, "84517ddbc9052d2e94a56811e58ff087cfa019a8ed780f5fcc7882329f77b0f7"),
    "pkx_blacky.fsys": (157664, "09b6ef44ca716b8e7ef22095ab9fa3e031ffece461addf61e9fb16047a9cf156"),
    "pkx_eifie.fsys": (146336, "06a655259f37aa8d8f8c9b3beec901047777d70d6416ec28fde7a888bb11a968"),
    "pkx_ziguzaguma.fsys": (213024, "24ccb04ff36ff68690b313245a39008326590631f55f2ca41c29cb1707e41bbf"),
    "pkx_ken_a1.fsys": (172800, "fc49f3711bcf218e6ff2b3fa855af79603e90a36b81c6c1dfda63fc8f7bdb7a8"),
    "pkx_rider_m_a1.fsys": (101536, "f2dda1ba513913247e5c101281de67ebea97b5fd4f59ecea53cd8c18a939c6a5"),
}
PKX_TABLE = 0x36DBD0  # DOL .data: 0x80370BD0, twelve-byte resource records.
UNSUPPORTED = 0xFFFFFFFF
MOVE_ENTRIES = {44: 2, 290: 1, 269: 0, 289: 1, 93: 1, 216: 2,
                115: 0, 270: 1, 33: 2, 45: 1, 39: 0, 165: 2}


def entry_clip(actor, entry):
    clips = actor["animation_entries"][entry]["clips"]
    if len(clips) != 1:
        raise ValueError("Presentation role requires one source joint clip")
    return clips[0]


def move_metadata(disc, common, dol, toc):
    names = archive_names(toc)
    source = rel_data(common, 125)
    manifest = Path(__file__).resolve().parents[1] / "docs/battle-scene-manifest.json"
    pinned = json.loads(manifest.read_text())
    root_profiles = {root["archive"]: root for move in pinned["moves"] for root in move["default_roots"]}
    result = []
    for move, entry in MOVE_ENTRIES.items():
        row = extent(source, 0x11C7C4 + move * 56, 56)
        wzx = u32(row, 48)
        table = extent(dol, 0x36B150 + wzx * 28, 28)
        roots = []
        for kind in (1, 2, 3):
            group, resource = struct.unpack_from(">II", table, 4 + (kind - 1) * 8)
            if not group and not resource:
                continue
            path = disc / "files" / names[group]
            if path.is_symlink() or not path.is_file() or path.stat().st_size > 16 * 1024 * 1024:
                raise ValueError("Invalid original WZX archive path/extent")
            archive = path.read_bytes()
            data = member(archive, resource)
            profile = root_profiles.get(path.name)
            if (profile is None or profile["resource"] != resource or
                hashlib.sha256(archive).hexdigest() != profile["archive_sha256"] or
                hashlib.sha256(data).hexdigest() != profile["member_sha256"]):
                raise ValueError("Original WZX root differs from the reviewed source manifest")
            if u32(data, 0x68) != 2:
                raise ValueError("Unreviewed WZX sequence version")
            roots.append({"kind": kind, "group": group, "resource": resource,
                          "archive": path.name, "archive_bytes": len(archive),
                          "archive_sha256": hashlib.sha256(archive).hexdigest(),
                          "member_bytes": len(data), "member_sha256": hashlib.sha256(data).hexdigest(),
                          "sequence_motion_entry": u32(data, 0x70)})
        result.append({"move": move, "wzx_id": wzx, "attack_entry": entry,
                       "hit_entry": 8 if move in (44, 290, 93, 216, 33, 165) else None,
                       "default_roots": roots, "effects_playback": "unsupported"})
    return result


def encode_scene(camera, records):
    """Bounded C3BV descriptor; callers supply qualified source role/placement data."""
    if len(camera) != 12 or not all(math.isfinite(x) for x in camera):
        raise ValueError("Invalid scene camera")
    if not 1 <= len(records) <= 16:
        raise ValueError("Scene model record bound exceeded")
    slots = [r["actor_slot"] for r in records if r["actor_slot"] != UNSUPPORTED]
    if sorted(slots) != list(range(6)):
        raise ValueError("Scene must contain all six independent actor slots")
    output = bytearray(128 + len(records) * 128)
    struct.pack_into("<4s4I", output, 0, b"C3BV", 1, len(output), len(records), 6)
    struct.pack_into("<12f", output, 32, *camera)
    for index, record in enumerate(records):
        name = record["file"]
        if (not isinstance(name, str) or not name.endswith(".c3t") or
            any(c not in "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_." for c in name) or
            ".." in name or not 1 <= len(name) <= 47):
            raise ValueError("Invalid scene pack basename")
        shared, idle, roles = record.get("shared_from", UNSUPPORTED), record["idle_clip"], record["roles"]
        if shared != UNSUPPORTED and not 0 <= shared < index:
            raise ValueError("Shared model must reference an earlier record")
        if not 0 <= idle < 32 or len(roles) != 9 or roles[0] != idle or any(
                role != UNSUPPORTED and not 0 <= role < 32 for role in roles):
            raise ValueError("Invalid scene animation roles")
        transform = record["transform"]
        if len(transform) != 5 or not all(math.isfinite(x) for x in transform) or transform[4] <= 0:
            raise ValueError("Invalid scene actor transform")
        at = 128 + index * 128
        output[at:at + len(name)] = name.encode("ascii")
        struct.pack_into("<12I5f", output, at + 48,
                         record["actor_slot"], shared, idle, *roles, *transform)
    return bytes(output)


def admit(path):
    path = Path(path)
    size, digest = PROFILES[path.name]
    if path.is_symlink() or not path.is_file() or path.stat().st_size != size:
        raise ValueError("Unexpected original source path/size: " + path.name)
    data = path.read_bytes()
    if hashlib.sha256(data).hexdigest() != digest:
        raise ValueError("Original source hash differs: " + path.name)
    return data


def archive_names(toc):
    if extent(toc, 0, 4) != b"GLLA":
        raise ValueError("Invalid original archive TOC")
    count, table = u32(toc, 8), u32(toc, 16)
    if not 1 <= count <= 4096:
        raise ValueError("Archive TOC count exceeds bound")
    rows = extent(toc, table, count * 8)
    result = {}
    for index in range(count):
        identity, offset = struct.unpack_from(">II", rows, index * 8)
        name = extent(toc, offset, min(128, len(toc) - offset)).split(b"\0", 1)[0]
        if not name or len(name) == 128 or any(c not in b"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_" for c in name):
            raise ValueError("Unsafe or unterminated archive name")
        if identity in result:
            raise ValueError("Duplicate archive identity")
        result[identity] = name.decode("ascii") + ".fsys"
    return result


def bindings(common, dol, toc):
    """Resolve table references; never substitute national numbers or filenames."""
    names = archive_names(toc)
    source = rel_data(common, 125)
    if u32(source, 0x14A670) != 196 or u32(source, 0x14A6E0) != 413:
        raise ValueError("Original floor/species table counts changed")
    encounter = extent(source, 0x109FBC + 10 * 56, 56)
    floor_id = struct.unpack_from(">H", encounter, 4)[0]
    if floor_id != 5:
        raise ValueError("Encounter 10 battle-floor binding changed")
    floor = extent(source, 0x8F974 + floor_id * 24, 24)
    field_id = struct.unpack_from(">H", floor, 2)[0]
    floors = [extent(source, 0x8BCF4 + i * 76, 76) for i in range(196)]
    matches = [(i, row) for i, row in enumerate(floors) if u32(row, 12) == field_id]
    if len(matches) != 1:
        raise ValueError("Ambiguous battle floor")
    index, row = matches[0]
    arena = {"battle_floor": floor_id, "floor": field_id, "floor_record": index,
             "group": u32(row, 4), "archive": names[u32(row, 4)],
             "resource": 0x0DA80200,
             "bgm_sound_id": u32(floor, 12), "environment_sound_id": u32(floor, 16),
             "introduction_wzx_id": u32(floor, 8)}
    actors = []
    for species in (197, 196, 288):
        species_row = extent(source, 0x121A04 + species * 284, 284)
        pkx = u32(species_row, 44)
        group, resource, name_id = struct.unpack(">III", extent(dol, PKX_TABLE + pkx * 12, 12))
        actors.append({"species": species, "pkx": pkx, "group": group,
                       "archive": names[group], "resource": resource, "name_id": name_id,
                       "voice": struct.unpack_from(">H", species_row, 14)[0],
                       "source_dol_offset": PKX_TABLE + pkx * 12})
    trainers = []
    for trainer_id in (1, 10):
        row = extent(source, 0x91684 + trainer_id * 52, 52)
        sequence = u32(row, 16)
        group, resource, name_id = struct.unpack(">III", extent(dol, 0x36D840 + sequence * 12, 12))
        trainers.append({"trainer": trainer_id, "sequence": sequence, "group": group,
                         "resource": resource, "name_id": name_id, "archive": names[group],
                         "source_dol_offset": 0x36D840 + sequence * 12})
    return {"encounter": 10, "arena": arena, "pokemon": actors,
            "trainers": trainers,
            "audio": {"encounter_bgm_sound_id": u32(encounter, 12),
                      "effective_bgm_sound_id": u32(encounter, 12),
                      "bgm_resolution": "nonzero encounter override (fightEncountGetBgmSndDataId)",
                      "wipe_id": u32(encounter, 16), "introduction_wzx_id": u32(encounter, 20)},
            "instances": [{"slot": i, "species": species, "asset": asset}
                          for i, (species, asset) in enumerate(((197, 0), (196, 1), (288, 2), (288, 2)))]}


def load_pkx(archive, name):
    from importer.phases.extract.extract import extract_dat
    from importer.phases.route.route import route_sections
    from importer.phases.parse.parse import parse_sections
    from importer.phases.describe.describe import describe_scene
    options = {"filepath": name, "game": "COLO_XD", "max_frame": 1000000000,
               "section_names": [], "ik_hack": False}
    entries = extract_dat(archive, name, options)
    if len(entries) != 1:
        raise ValueError("Expected one PKX DAT")
    data, metadata = entries[0]
    if metadata.pkx_header is None:
        raise ValueError("Required PKX animation mapping is missing")
    options["pkx_header"] = metadata.pkx_header
    sections = parse_sections(data, route_sections(data), options)
    if len(sections) != 1 or sections[0].section_name != "scene_data":
        raise ValueError("Expected one PKX scene_data")
    scene = describe_scene(sections, options)
    if len(scene.models) != 1:
        raise ValueError("Expected one PKX model")
    return data, sections[0].root_node, scene, metadata.pkx_header


def clip_mapping(header, ends):
    result = []
    for index, entry in enumerate(header.anim_entries):
        clips = []
        for animation in entry.sub_anims:
            if animation.motion_type == 0:
                if not 0 <= animation.anim_index < len(ends):
                    raise ValueError("PKX animation references a missing authored clip")
                clips.append(animation.anim_index)
        result.append({"entry": index, "type": entry.anim_type,
                       "clips": clips, "timing_seconds": list(entry.timing),
                       "damage_flags": entry.damage_flags,
                       "body_map_bones": list(entry.body_map_bones)})
    if not result or not result[0]["clips"]:
        raise ValueError("PKX idle binding is missing")
    return result


def scene_records(report):
    """battleGridUpdate's encounter-10 one-trainer double layout (units 0.1)."""
    records = []
    def append(pack, slot, idle, position, yaw=0., shared=UNSUPPORTED, roles=None):
        records.append({"file": pack["file"], "actor_slot": slot,
                        "shared_from": shared, "idle_clip": idle,
                        "roles": roles or [idle] + [UNSUPPORTED] * 8,
                        "transform": [*position, yaw, 1.]})
    arena = [p for p in report["packs"] if p["file"].startswith("arena")]
    for pack in arena:
        append(pack, UNSUPPORTED, 0, [0., 0., 0.])
    positions = [(-3.5, 0., -2.625), (-3.5, 0., 2.625),
                 (3.5, 0., 2.625), (3.5, 0., -2.625)]
    for slot, asset in enumerate((0, 1, 2, 2)):
        actor = report["pokemon"][asset]
        if len(actor["packs"]) != 1:
            raise ValueError("Actor split packs require an explicit runtime binding extension")
        append(actor["packs"][0], slot, actor["animation_entries"][0]["clips"][0],
               positions[slot], math.pi / 2 if slot < 2 else -math.pi / 2,
               len(arena) + 2 if slot == 3 else UNSUPPORTED,
               [entry_clip(actor, e) for e in (0, 2, 1, 8, 10, 0)] + [UNSUPPORTED] * 3)
    for index, trainer in enumerate(report["trainers"]):
        if len(trainer["packs"]) != 1:
            raise ValueError("Trainer split packs require an explicit runtime binding extension")
        append(trainer["packs"][0], 4 + index, trainer["animation_entries"][0]["clips"][0],
               [-5.6875 if index == 0 else 5.6875, 0., 0.],
               math.pi / 2 if index == 0 else -math.pi / 2,
               roles=[entry_clip(trainer, 0)] + [UNSUPPORTED] * 6 +
                     [entry_clip(trainer, 5), entry_clip(trainer, 10)])
    return records


def prepare(disc, common_path, parser, output):
    import prepare_field_geometry as geometry
    import prepare_title as title
    disc = verify_disc(disc)
    parser = title.verify_parser(parser)
    output = check_output(output)
    common_path = Path(common_path)
    if common_path.is_symlink() or common_path.stat().st_size != COMMON_BYTES:
        raise ValueError("Invalid common REL path/size")
    common = common_path.read_bytes()
    if hashlib.sha1(common).hexdigest() != COMMON_SHA1:
        raise ValueError("common REL differs from the canonical original")
    archives = {name: admit(disc / "files" / name) for name in PROFILES}
    report = bindings(common, (disc / "sys/main.dol").read_bytes(), archives["GSfsys.toc"])
    report["moves"] = move_metadata(disc, common, (disc / "sys/main.dol").read_bytes(), archives["GSfsys.toc"])
    report.update({"schema": 1, "source_commit": SOURCE_COMMIT, "common_sha1": COMMON_SHA1,
                   "archives": [{"file": n, "bytes": p[0], "sha256": p[1]} for n, p in PROFILES.items()],
                   "unsupported": ["battle camera sequence", "effects and particles", "UI/font", "audio decoding"],
                   "packs": []})
    geometry.import_parser(parser)
    output.mkdir(parents=True)
    arena = report["arena"]
    arena["member_sha256"] = hashlib.sha256(member(archives[arena["archive"]], arena["resource"])).hexdigest()
    data, raw, scene = geometry.load_scene(archives[arena["archive"]], arena["resource"])
    if len(scene.models) != 3 or len(scene.cameras) != 1 or scene.fogs:
        raise ValueError("Reviewed battle arena topology changed")
    camera = scene.cameras[0]
    fixed = (*camera.position, *camera.target_position, camera.roll, camera.field_of_view,
             camera.near, camera.far, camera.aspect, 0.)
    lights = geometry.fixed_lights(raw, data)
    report["source_camera"] = list(fixed)
    for index, (model, raw_model) in enumerate(zip(scene.models, raw.models)):
        sampled = geometry.Model(model, raw_model)
        meshes = [geometry.build_mesh(sampled, i) for i in range(len(sampled.meshes))]
        end = sampled.ends[0] if sampled.ends else 0
        # The 30000-frame slow sky uses sparse samples, retaining its full duration.
        frames = [i * end / 1000 for i in range(1001)] if end > 1000 else (geometry.sample_sequence(end) if end else [0])
        fps = 60000 / end if end > 1000 else 120
        report["packs"] += geometry.prepare_pack_set(meshes, [(0, frames, True)], fps,
            [fixed] * len(frames), lights, PROFILES[arena["archive"]][1], output,
            "arena" + str(index) + "_", format_version=3)
    for actor in report["pokemon"] + report["trainers"]:
        archive = archives[actor["archive"]]
        payload = member(archive, actor["resource"])
        data, raw, scene, header = load_pkx(archive, actor["archive"])
        model = geometry.Model(scene.models[0], raw.models[0], allow_image_swaps=True)
        actor["member_sha256"] = hashlib.sha256(payload).hexdigest()
        actor["dat_sha256"] = hashlib.sha256(data).hexdigest()
        actor["animation_entries"] = clip_mapping(header, model.ends)
        actor["particle_events"] = model.particle_events
        schedules = [(i, geometry.sample_sequence(end), i == actor["animation_entries"][0]["clips"][0])
                     for i, end in enumerate(model.ends)]
        meshes = geometry.expand_image_swaps([geometry.build_mesh(model, i) for i in range(len(model.meshes))])
        count = sum(len(frames) for _, frames, _ in schedules)
        prefix = ("pokemon" + str(actor["species"]) if "species" in actor else "trainer" + str(actor["trainer"])) + "_"
        actor["packs"] = geometry.prepare_pack_set(meshes, schedules, 120, [fixed] * count, lights,
            PROFILES[actor["archive"]][1], output, prefix, format_version=3)
        report["packs"] += actor["packs"]
    report["runtime_records"] = scene_records(report)
    descriptor = encode_scene(fixed, report["runtime_records"])
    (output / "scene.c3v").write_bytes(descriptor)
    report["descriptor"] = {"file": "scene.c3v", "bytes": len(descriptor),
                            "sha256": hashlib.sha256(descriptor).hexdigest()}
    (output / "scene.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--common-rel", required=True, type=Path)
    parser.add_argument("--parser", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    prepare(args.disc, args.common_rel, args.parser, args.output)


if __name__ == "__main__":
    main()
