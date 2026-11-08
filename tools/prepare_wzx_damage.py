#!/usr/bin/env python3
"""Prepare admitted original Bite defender model and GPT1 dependencies privately."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from prepare_boot_media import verify_disc
from prepare_model import check_output
from prepare_outskirt import member
from prepare_battle_scene import admit, load_pkx, clip_mapping
import prepare_particle as particle
from prepare_wzx_bite import read32, link_times
from prepare_game_state import Dol

ARCHIVE_SHA256 = "9626a36b5b609302940e8c74a147d0e590fd0b08c06726999a0489ce98dc41ef"
MEMBER_SHA256 = "4ed0fd1739bb6fcb2caac0c49e4a53db1e0d39680cfd1495653e146a8aacd9ed"
BANK_SHA256 = "2021b7c2254479bddba1d1c34b3919af8568c3120a60953fc466badc72209783"
MEMBER = 0x04E32000
BANK_PROFILES = {
    "reflect": {"archive": "wzx_reflector_special.fsys", "archive_bytes": 86016,
        "archive_sha256": "9dee2c130b67525fe0074f1f1cb3c97325a040d45b57e2f7e862b7fc9cabc753",
        "member": 0x07632000, "member_bytes": 121312,
        "member_sha256": "28b205cba86a2ce4b5e81f1a9224f0593ce033220773284f3efabcf885a570ac",
        "offset": 0x120, "bytes": 93864,
        "bank_sha256": "3f77fa09ec566050830845b806b6d23d9ee98e639afa2cb291f36f9f2cdcad08",
        "root": 0, "additional_roots": (), "closure": list(range(10))},
    "confusion-attack": {"archive": "wzx_nenriki_attack.fsys", "archive_bytes": 72832,
        "archive_sha256": "ce7df34845f8cc5520242dec14538ee2c1c662e9b4217cc1002393eff3c0885d",
        "member": 0x07532000, "member_bytes": 103516,
        "member_sha256": "c4c0c0c2a678a891ed71b92aa2212977a1e24adb985bc6a5c155fe2184b9f8bb",
        "offset": 0x19C, "bytes": 86132,
        "bank_sha256": "5a8f38eaf12eff12c78b1e61e91ab2f001cc0f9c0c9e23fe650781368585ced2",
        "root": 1, "additional_roots": (), "closure": [1, 9, 10, 11, 12]},
    "confusion-damage": {"archive": "wzx_nenriki_damage.fsys", "archive_bytes": 72960,
        "archive_sha256": "b4bc2e6b2c5332cf2afc7a8cbfa26d86f9b37b69625be4d0de2143c64b1c3920",
        "member": 0x05292000, "member_bytes": 104184,
        "member_sha256": "a1a0a4114c68a1b4c92d7952fb8bed60503b041f8eee85f033ef32f64d696d04",
        "offset": 0x120, "bytes": 86132,
        "bank_sha256": "5a8f38eaf12eff12c78b1e61e91ab2f001cc0f9c0c9e23fe650781368585ced2",
        "root": 7, "additional_roots": (4, 3, 2), "closure": [2, 3, 4, 7]},
}


def qualify_timeline(dol):
    expected = {0x80372734: 0x801DB668, 0x80372728: 0x801DB678,
                0x80372738: 0x801DB678, 0x801DB668: 0x801E000C,
                0x801DB678: 0x801E0014, 0x801DB82C: 0x7C7F0214}
    observed = {address: struct.unpack(">I", dol.read(address, 4))[0] for address in expected}
    if observed != expected:
        raise ValueError("Original SequenceEntryCopy timing instruction profile changed")
    return {f"0x{address:08X}": f"0x{value:08X}" for address, value in observed.items()}


def prepare_bank_effect(disc, output, name):
    """Same C3PT path, admitting only the qualified sequence's reached roots."""
    disc, output = verify_disc(disc), check_output(output)
    sources, profile = particle.source_manifest(), BANK_PROFILES[name]
    path = disc / "files" / profile["archive"]
    if path.is_symlink() or path.stat().st_size != profile["archive_bytes"]:
        raise ValueError("Invalid particle sequence archive path/size")
    archive = path.read_bytes()
    if hashlib.sha256(archive).hexdigest() != profile["archive_sha256"]:
        raise ValueError("Particle sequence archive differs from admitted original")
    data = member(archive, profile["member"])
    if len(data) != profile["member_bytes"] or hashlib.sha256(data).hexdigest() != profile["member_sha256"]:
        raise ValueError("Particle sequence member differs from admitted original")
    raw = particle.extent(data, profile["offset"], profile["bytes"])
    if hashlib.sha256(raw).hexdigest() != profile["bank_sha256"]:
        raise ValueError("Particle sequence bank differs from admitted original")
    report = particle.bank(raw, profile["root"], profile["additional_roots"])
    if report["closure"] != profile["closure"]:
        raise ValueError("Particle sequence transitive closure changed")
    packed = particle.encode(raw, report, profile["member_sha256"])
    prefix = name.replace("-", "_")
    output.mkdir(parents=True)
    (output / (prefix + ".c3p")).write_bytes(packed)
    (output / (prefix + ".gpt1")).write_bytes(raw)
    public_report = particle.report_json(report)
    public_report.update(source_commit=particle.SOURCE_COMMIT, source_hashes=sources["files"],
        source_handlers=sources["handlers"], source_profile=profile,
        scope="Original particle bank/texture dependencies; not complete WZX/model/camera/audio execution",
        output={"file": prefix + ".c3p", "bytes": len(packed), "sha256": hashlib.sha256(packed).hexdigest()})
    (output / (prefix + ".json")).write_text(json.dumps(public_report, indent=2) + "\n")
    return public_report


def node(data, offset):
    words = struct.unpack(">28i", particle.extent(data, offset, 112))
    if words[26] != 2 or words[1] not in (2, 3, 4, 5):
        raise ValueError("Unsupported Bite damage node profile/version")
    return dict(offset=offset, key=words[0], kind=words[1], attachment=words[2],
                position_type=words[3], link=words[4], source=words[5], target=words[6],
                timing_index=words[7], timing=list(words[8:24]), flags=words[24],
                part=words[25], state=words[27])


def parse(data, owner_times):
    if len(data) != 196392 or read32(data, 104) != 2 or [read32(data, 112 + i * 4) for i in range(6)] != [8, 5, 0x00200848, 2, 5, 0]:
        raise ValueError("Bite defender WZX profile changed")
    nodes = [node(data, offset) for offset in (0xA0, 0x209C0, 0x2FE00, 0x2FEAC)]
    if [(n["key"], n["kind"], n["attachment"], n["position_type"], n["flags"], n["part"]) for n in nodes] != [
        (1, 2, 2, 1, 2, 0), (2, 3, 0, 1, 1, 12), (3, 4, 0, 0, 0, 0), (4, 5, 0, 0, 0, 0)]:
        raise ValueError("Bite damage attachment/entry profile changed")
    model_params = [read32(data, 0x110 + 4 * i) for i in range(8)]
    particle_params = [read32(data, 0x20A30 + 4 * i) for i in range(4)]
    color = [read32(data, 0x2FE70 + 4 * i) for i in range(15)]
    if model_params != [0, 0, 0, 0, 0, 0, 1, 133219] or particle_params != [3, 0x10A1, 62392, 4]:
        raise ValueError("Bite defender embedded dependency profile changed")
    if color != [0, 160, 2, 1, 2, 3, 0, 0x803A3A3A, 0x803A3A3A, 60, 1, 0x803A3A3A, 0x80808080, 100, 1]:
        raise ValueError("Bite defender floor-color program changed")
    if [read32(data, 0x2FF1C + 4 * i) for i in range(3)] != [0x86, 3, 0]:
        raise ValueError("Bite defender sound profile changed")
    shift = link_times(nodes, owner_times)
    if shift != 22 or [n["start"] for n in nodes] != [0, 0, 0, 22]:
        raise ValueError("Bite damage owner/link timeline changed")
    for entry in nodes:
        entry["raw_start"] = entry["start"] - shift
    dat = particle.extent(data, 0x140, 133219)
    bank = particle.extent(data, 0x20A40, 62392)
    if hashlib.sha256(bank).hexdigest() != BANK_SHA256:
        raise ValueError("Bite defender GPT1 differs from admitted bank")
    return {"entries": nodes, "model_dat_offset": 0x140, "model_dat_bytes": len(dat),
            "gpt1_offset": 0x20A40, "gpt1_bytes": len(bank), "particle_flags": 0x10A1,
            "particle_animation_mode": 4, "root_generator": 3, "sound_id": 0x86,
            "floor_program": color, "owner_timing_ticks": owner_times,
            "normalization_shift": shift, "owner_start": shift,
            "owner_end": 358 + shift, "hp_start": shift}, dat, bank


def encode(report, model, particles):
    out = bytearray(256)
    struct.pack_into("<4s15I", out, 0, b"C3WD", 2, 256, 44, 288, report["owner_clip"],
        0, 60, 0, 0, 22, 0, 160, 22, 380, 25)
    struct.pack_into("<5f11I", out, 64, .5, .5, *report["anchor"], 0, 12,
        report["particle_attachment_sampled_bone"], 0x803A3A3A, 0x803A3A3A, 0x80808080,
        60, 100, 0x86, 0x10A1, 3)
    out[128:148] = bytes.fromhex(particle.SOURCE_COMMIT)
    out[148:180] = bytes.fromhex(MEMBER_SHA256)
    out[180:200] = hashlib.sha1(model).digest()
    out[200:220] = hashlib.sha1(particles).digest()
    struct.pack_into("<2I", out, 220, 22, 22)
    out[236:256] = hashlib.sha1(out[:236]).digest()
    return bytes(out)


def prepare(disc, parser, output):
    import prepare_field_geometry as geometry
    import prepare_title as title
    disc, output = verify_disc(disc), check_output(output)
    parser = title.verify_parser(parser)
    sources = particle.source_manifest()
    timeline_words = qualify_timeline(Dol((disc / "sys/main.dol").read_bytes()))
    path = disc / "files/wzx_kamituku_damage.fsys"
    if path.is_symlink() or path.stat().st_size != 117856:
        raise ValueError("Invalid Bite damage archive path/size")
    archive = path.read_bytes()
    if hashlib.sha256(archive).hexdigest() != ARCHIVE_SHA256:
        raise ValueError("Bite damage archive differs from admitted original")
    data = member(archive, MEMBER)
    if hashlib.sha256(data).hexdigest() != MEMBER_SHA256:
        raise ValueError("Bite damage member differs from admitted original")
    geometry.import_parser(parser)
    _dat, owner_raw, owner_scene, header = load_pkx(admit(disc / "files/pkx_ziguzaguma.fsys"), "pkx_ziguzaguma.fsys")
    owner = geometry.Model(owner_scene.models[0], owner_raw.models[0], allow_image_swaps=True)
    mapping = clip_mapping(header, owner.ends)[8]
    importer_times = [round(value * 60) for value in mapping["timing_seconds"]]
    if mapping["clips"] != [2] or importer_times != [190, 358, 0, 0] or mapping["body_map_bones"][0] != 0 or owner.ends[2] != 179:
        raise ValueError("Bite defender owner clip/timing profile changed")
    # SequenceEntryCopy's source table starts at raw entry+0x0C, including an
    # implicit zero before the importer's two exposed type-3 timing values.
    # The prior v1 descriptor incorrectly indexed [190,358,...] directly.
    owner_times = [0, *importer_times[:2]]
    report, dat, raw_bank = parse(data, owner_times)
    bank_report = particle.bank(raw_bank, 3)
    if bank_report["closure"] != [0, 3, 5] or len(bank_report["textures"]) != 6:
        raise ValueError("Bite damage particle transitive closure changed")
    particle_pack = particle.encode(raw_bank, bank_report, MEMBER_SHA256)
    from importer.phases.route.route import route_sections
    from importer.phases.parse.parse import parse_sections
    from importer.phases.describe.describe import describe_scene
    opts = dict(filepath="bite-damage.dat", game="COLO_XD", max_frame=1000000000, section_names=[], ik_hack=False)
    sections = parse_sections(dat, route_sections(dat), opts)
    if len(sections) != 1 or sections[0].section_name != "scene_data":
        raise ValueError("Unexpected Bite damage embedded model root")
    raw, scene = sections[0].root_node, describe_scene(sections, opts)
    if len(scene.models) != 1 or len(scene.cameras) != 1 or len(scene.lights) != 2 or scene.fogs:
        raise ValueError("Unexpected Bite damage HSD topology")
    model = geometry.Model(scene.models[0], raw.models[0], constant_material_colors=True)
    if model.ends != [25.] or len(model.materials) != 1 or len(model.joints) <= 12:
        raise ValueError("Unexpected Bite damage model animation/part topology")
    meshes = [geometry.build_mesh(model, i) for i in range(len(model.meshes))]
    if any(mesh["batch"][3] & (title.LIT | title.SPECULAR) for mesh in meshes):
        raise ValueError("Bite damage requires unqualified runtime lighting")
    frames = geometry.sample_sequence(25.)
    camera = (0., 2., 10., 0., 0., 0., 0., 45., .1, 1000., 4 / 3, 0.)
    output.mkdir(parents=True)
    packs = geometry.prepare_pack_set(meshes, [(0, frames, False)], 120, [camera] * len(frames),
        geometry.fixed_lights(raw, dat), ARCHIVE_SHA256, output, "bite_damage", expected_count=1,
        format_version=3, retained_parts=((model, 12, 12),))
    model_bytes = (output / "bite_damage.c3t").read_bytes()
    report.update(source_commit=particle.SOURCE_COMMIT, source_hashes=sources["files"],
        source_handlers=sources["handlers"], source_semantics=sources["semantics_notes"],
        descriptor_version=2, importer_timing_ticks=importer_times,
        timeline_qualification={"authority": "Pinned DOL instructions and source SequenceEntryCopy/EntryLink",
            "source_owner_table": "raw PKX entry+0x0C, including implicit zero",
            "source_owner_times": [0, 190, 358], "raw_starts": [-22, -22, -22, 0],
            "normalized_starts": [0, 0, 0, 22], "owner_start": 22, "owner_end": 380,
            "hp_gate": 22, "instruction_evidence": [
                "DOL jump table 0x80372720[5] -> 0x801DB668: lwz r0,12(r30)",
                "types2/6 -> 0x801DB678: lwz r0,20(r30)",
                "0x801DB82C adds entry field0x10"], "verified_dol_words": timeline_words},
        archive_sha256=ARCHIVE_SHA256, member_sha256=MEMBER_SHA256,
        model_dat_sha256=hashlib.sha256(dat).hexdigest(), owner_clip=2, owner_rate=.5, model_rate=.5,
        anchor=list(map(float, owner.world(2, 0)[0][0, :3, 3])), source_part=0,
        particle_attachment_source="linked damage model source part12; continuous follow; no synthetic root",
        particle_attachment_frame0=model.world(0, 0)[0][12].tolist(),
        particle_attachment_units="actorRoot * retainedModelPart12 * diag(0.1,0.1,0.1,1); retained translation already converted",
        particle_attachment_sampled_bone=packs[0]["retained_parts"][0]["sampled_bone"],
        particle_bank=particle.report_json(bank_report), packs=packs,
        scope="Bite defender model/GPT1 dependencies; native execution/audio/cameras require separate qualification")
    descriptor = encode(report, model_bytes, particle_pack)
    (output / "bite_damage.c3p").write_bytes(particle_pack)
    (output / "bite_damage.c3w").write_bytes(descriptor)
    report["outputs"] = [{"file": name, "bytes": len(payload), "sha256": hashlib.sha256(payload).hexdigest(),
                          "sha1": hashlib.sha1(payload).hexdigest()} for name, payload in
                         (("bite_damage.c3t", model_bytes), ("bite_damage.c3p", particle_pack), ("bite_damage.c3w", descriptor))]
    (output / "bite_damage.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--disc", type=Path, required=True)
    parser.add_argument("--parser", type=Path)
    parser.add_argument("--effect", choices=("bite", *BANK_PROFILES), default="bite")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if args.effect == "bite":
        if args.parser is None:
            parser.error("--parser is required for the Bite damage HSD model")
        report = prepare(args.disc, args.parser, args.output)
        print(json.dumps(report["outputs"], indent=2))
    else:
        report = prepare_bank_effect(args.disc, args.output, args.effect)
        print(json.dumps(report["output"], indent=2))


if __name__ == "__main__":
    main()
