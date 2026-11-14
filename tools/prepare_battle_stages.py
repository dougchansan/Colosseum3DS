#!/usr/bin/env python3
"""Prepare private 3D assets for the general story battles (sdmc boot/battle3d/).

One stage per battle floor, one model pack per PKX model id and per trainer
sequence id, and the binary index `index.c3n` the runtime reads. Every source
binding is read from the original tables (common REL encounter, battle floor,
trainer, pokemon and species rows; the DOL PKX and trainer sequence resource
tables; GSfsys.toc). The disc is admitted by its pinned main.dol and the
common REL by its canonical SHA-1; individual archives are not pinned here,
their SHA-256 is recorded in battle3d.json instead.

A floor or model that the reviewed pipeline cannot represent (topology,
section bounds, a split pack, a missing member) is skipped and its reason is
recorded. The index then has no entry for it; nothing is substituted.

    python3 tools/prepare_battle_stages.py --disc <extraction> \
        --common-rel <canonical common_rel.rel> --parser <pinned hsd importer> \
        --output private/battle3d [--floors 5,7] [--species 196,197] [--skip-existing]
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import struct
import traceback
import types

from prepare_battle_encounters import Rel, roster, u16
from prepare_battle_init import COMMON_BYTES, COMMON_SHA1, SOURCE_COMMIT
from prepare_battle_scene import PKX_TABLE, UNSUPPORTED, archive_names, clip_mapping, load_pkx
from prepare_boot_media import verify_disc
from prepare_outskirt import extent, member, rel_data, u32
from prepare_pda_models import omit_collapsed_meshes, split_envelope_positions

TRAINER_TABLE = 0x36D840  # DOL file offset (RAM lbl_80370840), twelve-byte records.
SPECIES_COUNT, TRAINER_COUNT, FLOOR_COUNT = 413, 820, 50
WES_TRAINER, WES_STARTERS = 1, (196, 197)
POKEMON_ENTRIES = (0, 1, 2, 3, 4, 8, 10)
TRAINER_ENTRIES = (0, 5, 10)
ENTRY_COUNT = 17
ARENA_MAX_SAMPLES = 121
ARENA_REDUCED_SAMPLES = 31
MAX_ARENA_RECORDS = 8
MAX_PACK_BONES = 64       # C3_TITLE_MAX_BONES: sampled tracks per pack
MODEL_RAM_BUDGET = 4_000_000  # decompressed bytes per Pokemon/trainer pack, where possible
MAX_MERGE_ERROR = 0.05    # of the model's extent: merged envelope tracks beyond this fail
REFERENCE_FLOOR = 5  # S1_out_bf: its arena lights/camera fill the actor packs' fixed fields.
SCENE_TYPE = 0x02    # FSYS identity byte 1: scene DAT (S1_out_bf 0x0DA80200).


def admit_common(path):
    path = Path(path)
    if path.is_symlink() or not path.is_file() or path.stat().st_size != COMMON_BYTES:
        raise ValueError("Invalid common REL path/size")
    data = path.read_bytes()
    if hashlib.sha1(data).hexdigest() != COMMON_SHA1:
        raise ValueError("common REL differs from the canonical original")
    return data


def read_archive(disc, name, cache):
    if name not in cache:
        path = disc / "files" / name
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 32 * 1024 * 1024:
            raise ValueError("Invalid original archive path/extent: " + name)
        cache[name] = path.read_bytes()
    return cache[name]


def fsys_identities(archive):
    if len(archive) < 64 or archive[:4] != b"FSYS" or u32(archive, 32) != len(archive):
        raise ValueError("Invalid FSYS header")
    count = u32(archive, 12)
    if not 1 <= count <= 4096:
        raise ValueError("Invalid FSYS entry count")
    table = u32(archive, u32(archive, 24))
    return [u32(archive, u32(archive, table + i * 4)) for i in range(count)]


def sources(rel_bytes, dol, toc):
    """Every binding the battles use, straight from the original tables."""
    rel = Rel(rel_bytes)
    common = rel_data(rel_bytes, 125)
    names = archive_names(toc)
    floor_rows = [extent(common, 0x8BCF4 + i * 76, 76) for i in range(196)]
    encounters = []
    for index in range(rel.count("encounter")):
        row = rel.row("encounter", index)
        slots = [(u16(row, 24 + i * 8), u32(row, 28 + i * 8)) for i in range(4)]
        trainers = [t for t, _ in slots[1:] if t]
        party = sorted({u16(p, 10) for t in trainers for p in roster(rel, rel.row("trainer", t))})
        encounters.append({"encounter": index, "battle_floor": u16(row, 4),
                           "trainers": trainers, "species": party})

    def floor(battle_floor):
        row = rel.row("floor", battle_floor)
        field = u16(row, 2)
        matches = [i for i, r in enumerate(floor_rows) if u32(r, 12) == field]
        if len(matches) != 1:
            raise ValueError("Battle floor field id %d is not one common floor row" % field)
        group = u32(floor_rows[matches[0]], 4)
        if group not in names:
            raise ValueError("Floor archive group %#x is not in GSfsys.toc" % group)
        return {"battle_floor": battle_floor, "field": field, "floor_record": matches[0],
                "group": group, "archive": names[group]}

    def species(identity):
        row = rel.row("species", identity)
        pkx = u32(row, 0x2C)
        group, resource, name_id = struct.unpack(">III", extent(dol, PKX_TABLE + pkx * 12, 12))
        return {"species": identity, "pkx": pkx, "group": group, "resource": resource,
                "name_id": name_id, "archive": names.get(group),
                "source_dol_offset": PKX_TABLE + pkx * 12}

    def trainer(identity):
        row = rel.row("trainer", identity)
        sequence = u32(row, 16)
        group, resource, name_id = struct.unpack(">III", extent(dol, TRAINER_TABLE + sequence * 12, 12))
        return {"trainer": identity, "sequence": sequence, "group": group, "resource": resource,
                "name_id": name_id, "archive": names.get(group),
                "source_dol_offset": TRAINER_TABLE + sequence * 12}

    return encounters, floor, species, trainer


def resample(end, samples_per_frame=1):
    """Authored frames at samples_per_frame; a fractional end keeps its span."""
    if not end:
        return [0.], False
    count = end * samples_per_frame
    if abs(count - round(count)) < 1e-5:
        count = int(round(count))
        return [i / samples_per_frame for i in range(count + 1)], False
    count = math.ceil(count)
    return [i * end / count for i in range(count + 1)], True


def arena_frames(end, cap=ARENA_MAX_SAMPLES):
    """1 sample per frame up to cap samples; longer clips are spread over
    their whole duration, never below the pack's 1 sample/second floor."""
    if not end:
        return [0.], 60.
    frames, _ = resample(end)
    if len(frames) <= cap:
        return frames, 60.
    count = max(cap, math.ceil(end / 60) + 1)
    return [i * end / (count - 1) for i in range(count)], 60. * (count - 1) / end


def merge_batches(meshes):
    """Join adjacent batches of one material (one DObj's several PObjs).

    Exact: same model, owner joint, DObj, textures and batch words, so the
    joined batch samples the same material state; draw order is unchanged.
    Used only when a model has more batches than one pack holds.
    """
    geometry_keys = ("vertices", "descriptors", "batch", "index")
    result = []
    for mesh in meshes:
        last = result[-1] if result else None
        if (last is not None and last["dobj"] is mesh["dobj"] and
                list(last["batch"][2:]) == list(mesh["batch"][2:]) and
                set(last) == set(mesh) and
                all(last[k] is mesh[k] or last[k] == mesh[k] for k in mesh if k not in geometry_keys)):
            joined = dict(last)
            joined["vertices"] = last["vertices"] + mesh["vertices"]
            joined["descriptors"] = last["descriptors"] + mesh["descriptors"]
            joined["batch"] = list(last["batch"])
            joined["batch"][1] = len(joined["vertices"])
            result[-1] = joined
        else:
            result.append(mesh)
    return result


def merge_tracks(geometry, meshes, states, approximate):
    """Approximation: fold skin tracks together until a model fits one pack.

    Every sampled track maps rest-pose model-space vertices to the pose, so a
    vertex may follow another envelope's track. Exact-equal tracks are one
    pack bone already; the closest remaining pair (largest displacement of
    the folded vertices' bounding-box corners over the samples, measured
    against each vertex's own original track) is merged first. Tracks with
    collapsed samples keep their own bone.
    """
    import numpy as np
    points = {}
    for mesh in meshes:
        for vertex, descriptor in zip(mesh["vertices"], mesh["descriptors"]):
            points.setdefault((mesh["model"], descriptor), []).append(vertex[:3])
    groups, fixed = {}, []
    for key in points:
        try:
            track = geometry._sample_descriptor(key[0], key[1], states)
        except ValueError:
            fixed.append(key)
            continue
        if np.all(track[:, :3, :3] == 0, axis=(1, 2)).any():
            fixed.append(key)
            continue
        groups.setdefault(track.tobytes(), (track, []))[1].append(key)
    distinct = len(groups) + len(fixed)
    if distinct <= MAX_PACK_BONES or not approximate:
        return None
    if len(fixed) >= MAX_PACK_BONES or len({key[0] for key in points}) != 1:
        raise ValueError("Skin tracks cannot be merged into one pack")
    reps = list(groups.values())
    picks = np.linspace(0, len(states) - 1, min(len(states), 64)).round().astype(int)
    tracks = np.stack([track[picks] for track, _ in reps]).astype(np.float64)   # G,S,3,4
    corners = []
    for _, keys in reps:
        cloud = np.asarray([p for key in keys for p in points[key]], dtype=np.float64)
        low, high = cloud.min(axis=0), cloud.max(axis=0)
        corners.append([(x, y, z, 1.) for x in (low[0], high[0]) for y in (low[1], high[1])
                        for z in (low[2], high[2])])
    corners = np.asarray(corners)                                                # G,8,4
    allpoints = np.asarray([p for cloud in points.values() for p in cloud])
    extent = float(np.linalg.norm(allpoints.max(axis=0) - allpoints.min(axis=0))) or 1.
    count = len(reps)
    cost = np.empty((count, count))
    for a in range(count):
        moved = np.einsum("gsij,pj->gspi", tracks - tracks[a], corners[a])
        cost[a] = np.sqrt((moved ** 2).sum(-1)).max(axis=(1, 2))
    np.fill_diagonal(cost, np.inf)
    alive, owner, worst = count, list(range(count)), 0.
    while alive + len(fixed) > MAX_PACK_BONES:
        a, b = np.unravel_index(np.argmin(cost), cost.shape)
        worst = max(worst, cost[a, b])
        cost[b] = np.maximum(cost[b], cost[a])
        cost[b, b] = np.inf
        cost[a, :] = np.inf
        cost[:, a] = np.inf
        owner = [b if o == a else o for o in owner]
        alive -= 1
    if worst > MAX_MERGE_ERROR * extent:
        raise ValueError("Merging %d skin tracks into %d moves vertices %.3f (%.1f%% of the model)" % (
            distinct, MAX_PACK_BONES, worst, 100 * worst / extent))
    mapping = {}
    for index, (_, keys) in enumerate(reps):
        target = reps[owner[index]][1][0]
        for key in keys:
            if key != target:
                mapping[key] = target[1]
    for mesh in meshes:
        mesh["descriptors"] = [mapping.get((mesh["model"], d), d) for d in mesh["descriptors"]]
    return {"distinct_tracks": distinct, "merged_to": MAX_PACK_BONES,
            "max_displacement": round(worst, 5), "model_extent": round(extent, 5),
            "max_displacement_share": round(worst / extent, 5)}


def mark_collapsed(meshes, states, pairs):
    """omit_collapsed_meshes' per-sample hiding, over every kept clip.

    A no-surface batch is hidden at each (clip, frame) where all its
    envelopes are exactly zero; the zero track is approved only when every
    batch using it is hidden at that sample.
    """
    import numpy as np
    flags, users = [], {}
    for index, mesh in enumerate(meshes):
        descriptors = mesh["descriptors"]
        degenerate = all(len(set(descriptors[i:i + 3])) <= 2 for i in range(0, len(descriptors), 3))
        flags.append([degenerate and all(np.all(mesh["model"].deform(d, world)[:3, :3] == 0)
                                         for d in set(descriptors)) for world, _ in states])
        mesh["collapsed_samples"] = {pair for pair, hidden in zip(pairs, flags[-1]) if hidden}
        for d in set(descriptors):
            users.setdefault((id(mesh["model"]), d), []).append(index)
    for sample, (world, _) in enumerate(states):
        for (model_id, d), owners in users.items():
            if all(flags[i][sample] for i in owners):
                model = meshes[owners[0]]["model"]
                model.__dict__.setdefault("_collapsed_world_descriptors", {}).setdefault(
                    id(world), set()).add(d)


def encode_stage(camera, files):
    """C3BV v1 (prepare_battle_scene.encode_scene layout) with arena records only."""
    if len(camera) != 12 or not all(math.isfinite(x) for x in camera):
        raise ValueError("Invalid stage camera")
    if not 1 <= len(files) <= MAX_ARENA_RECORDS:
        raise ValueError("Stage record bound exceeded")
    output = bytearray(128 + len(files) * 128)
    struct.pack_into("<4s4I", output, 0, b"C3BV", 1, len(output), len(files), 0)
    struct.pack_into("<12f", output, 32, *camera)
    for index, name in enumerate(files):
        if not name.endswith(".c3t") or not 1 <= len(name) <= 47 or not all(
                c.isascii() and (c.isalnum() or c in "_.") for c in name):
            raise ValueError("Invalid stage pack basename")
        at = 128 + index * 128
        output[at:at + len(name)] = name.encode("ascii")
        struct.pack_into("<12I5f", output, at + 48, UNSUPPORTED, UNSUPPORTED, 0,
                         0, *[UNSUPPORTED] * 8, 0., 0., 0., 0., 1.)
    return bytes(output)


def encode_index(species_models, trainer_models, floors, models):
    header = 64
    species_off = header
    trainer_off = (species_off + SPECIES_COUNT * 2 + 3) & ~3
    floor_off = (trainer_off + TRAINER_COUNT * 2 + 3) & ~3
    model_off = (floor_off + FLOOR_COUNT * 32 + 3) & ~3
    output = bytearray(model_off + len(models) * 128)
    struct.pack_into("<4s9I", output, 0, b"C3BN", 1, SPECIES_COUNT, TRAINER_COUNT, FLOOR_COUNT,
                     len(models), species_off, trainer_off, floor_off, model_off)
    for i in range(SPECIES_COUNT):
        struct.pack_into("<H", output, species_off + i * 2, species_models.get(i, 0xFFFF))
    for i in range(TRAINER_COUNT):
        struct.pack_into("<H", output, trainer_off + i * 2, trainer_models.get(i, 0xFFFF))
    for i in range(FLOOR_COUNT):
        name = floors.get(i, "").encode("ascii")
        if len(name) > 31:
            raise ValueError("Floor descriptor name too long")
        output[floor_off + i * 32:floor_off + i * 32 + len(name)] = name
    for i, model in enumerate(models):
        at = model_off + i * 128
        name = model["file"].encode("ascii")
        if len(name) > 47:
            raise ValueError("Model pack name too long")
        output[at:at + len(name)] = name
        struct.pack_into("<17IIf", output, at + 48, *model["entry_clip"], model["kind"], 1.)
    return bytes(output)


class Run:
    def __init__(self, args):
        import prepare_field_geometry as geometry
        import prepare_title as title
        import compress_packs
        self.geometry, self.compress = geometry, compress_packs
        self.disc = verify_disc(args.disc)
        try:
            self.parser = title.verify_parser(args.parser)
        except ValueError as error:
            raise ValueError(str(error) + " (a Windows checkout with core.autocrlf=true has CRLF "
                             "files; export the pinned commit with `git -c core.autocrlf=false archive`)")
        self.common = admit_common(args.common_rel)
        self.output = Path(args.output).resolve()
        self.max_texture = args.max_texture
        self.skip_existing = args.skip_existing
        self.approximate = args.approximate
        self.arena_budget = int(args.arena_budget_mb * 1_000_000)
        root = Path(__file__).resolve().parents[1]
        if (self.output == root or root in self.output.parents) and \
                root / "private" not in self.output.parents:
            raise ValueError("Output inside the repository must be under ignored private/")
        self.previous, previous_archives = {}, {}
        if self.output.exists() and any(self.output.iterdir()):
            if not self.skip_existing:
                raise ValueError("Output directory is not empty; pass --skip-existing to resume")
            report = self.output / "battle3d.json"
            if report.exists():
                old = json.loads(report.read_text())
                previous_archives = old.get("archives", {})
                for kind in ("floors", "models"):
                    for item in old.get(kind, []):
                        self.previous[(kind, item["key"])] = item
        self.output.mkdir(parents=True, exist_ok=True)
        self.dol = (self.disc / "sys/main.dol").read_bytes()
        self.archives = {}
        toc = read_archive(self.disc, "GSfsys.toc", self.archives)
        self.encounters, self.floor, self.species, self.trainer = sources(self.common, self.dol, toc)
        geometry.import_parser(self.parser)
        # Like the field tour's surveyed floors: recorded approximations
        # instead of stopping on unreviewed materials and lights.
        geometry.APPROXIMATE["enabled"] = self.approximate
        title.APPROXIMATE_HOOK = geometry.approximate if self.approximate else None
        self.report = {"schema": 1, "format": "battle3d index v1", "source_commit": SOURCE_COMMIT,
                       "common_sha1": COMMON_SHA1, "max_texture": self.max_texture,
                       "approximate": self.approximate, "arena_budget_bytes": self.arena_budget,
                       "max_arena_records": MAX_ARENA_RECORDS, "model_ram_budget": MODEL_RAM_BUDGET,
                       "sampling": {"actors": "1 sample per authored frame (60 Hz)",
                                    "arena": "1 sample per frame up to %d samples; longer clips "
                                             "spread over their duration (>= 1 sample/s)" % ARENA_MAX_SAMPLES,
                                    "pokemon_entries": list(POKEMON_ENTRIES),
                                    "trainer_entries": list(TRAINER_ENTRIES)},
                       "archives": dict(previous_archives), "floors": [], "models": [], "skipped": []}

    def archive(self, name):
        data = read_archive(self.disc, name, self.archives)
        self.report["archives"].setdefault(name, {"bytes": len(data),
                                                  "sha256": hashlib.sha256(data).hexdigest()})
        return data

    def save(self):
        (self.output / "battle3d.json").write_text(json.dumps(self.report, indent=1) + "\n")

    def finish_pack(self, pack):
        """Compress one written pack in place (C3Z2, screen-sized textures)."""
        path = self.output / pack["file"]
        raw = path.stat().st_size
        self.compress.compress_directory(self.output, pattern=pack["file"], max_texture=self.max_texture)
        stored = path.read_bytes()
        decoded = self.compress.decompress_pack(stored)
        return {"file": pack["file"], "raw_bytes": raw, "repacked_bytes": len(decoded),
                "stored_bytes": len(stored), "sha256": hashlib.sha256(stored).hexdigest(),
                "vertices": pack["vertices"], "batches": pack["batches"],
                "textures": pack["textures"], "sampled_bones": pack["sampled_bones"],
                "sample_fps": pack["sample_fps"],
                "clips": pack["clips"]}

    def reuse(self, kind, key):
        old = self.previous.get((kind, key))
        if old and self.skip_existing and all((self.output / p["file"]).exists() for p in old["packs"]):
            if kind == "floors" and not (self.output / old["descriptor"]).exists():
                return None
            ram = sum(p["repacked_bytes"] for p in old["packs"])
            # Results outside the current limits are regenerated.
            if kind == "floors" and (ram > self.arena_budget or len(old["packs"]) > MAX_ARENA_RECORDS):
                return None
            if kind == "models" and ram > MODEL_RAM_BUDGET:
                return None
            print("Reusing %s %s" % (kind, key), flush=True)
            old.setdefault("approximations", {})
            if kind == "floors":
                old.setdefault("ram_bytes", ram)
            return old
        return None

    def skip(self, kind, key, reason, **extra):
        print("Skipped %s %s: %s" % (kind, key, reason), flush=True)
        self.report["skipped"].append({"kind": kind, "key": key, "reason": reason, **extra})

    # Arena ---------------------------------------------------------------
    def scene_member(self, archive):
        found = [i for i in fsys_identities(archive) if (i >> 8) & 0xFF == SCENE_TYPE]
        if len(found) != 1:
            raise ValueError("Expected one scene DAT member, found %d" % len(found))
        return found[0]

    def reference(self):
        if not hasattr(self, "_reference"):
            source = self.floor(REFERENCE_FLOOR)
            archive = self.archive(source["archive"])
            data, raw, scene = self.geometry.load_scene(archive, self.scene_member(archive))
            self._reference = (self.camera(scene), self.geometry.fixed_lights(raw, data))
        return self._reference

    @staticmethod
    def camera(scene):
        if not scene.cameras:
            raise ValueError("Arena scene has no camera")
        camera = scene.cameras[0]
        return (*camera.position, *camera.target_position, camera.roll, camera.field_of_view,
                camera.near, camera.far, camera.aspect, 0.)

    def model_options(self):
        # Interior-floor subsets (prepare_floor): joint path tracks and
        # animated material colours frozen at their first value.
        return {"allow_paths": True, "frozen_material_colors": True} if self.approximate else {}

    def used(self, since):
        now = self.geometry.APPROXIMATIONS
        return collections.Counter({k: now[k] - since.get(k, 0) for k in now if now[k] != since.get(k, 0)})

    def prepare_arena(self, owner, source):
        geometry = self.geometry
        archive = self.archive(source["archive"])
        resource = self.scene_member(archive)
        payload = member(archive, resource)
        before = collections.Counter(geometry.APPROXIMATIONS)
        data, raw, scene = geometry.load_scene(archive, resource)
        if not scene.models:
            raise ValueError("Arena scene has no model")
        fixed = self.camera(scene)
        lights = geometry.fixed_lights(raw, data)
        digest = self.report["archives"][source["archive"]]["sha256"]
        loaded, notes = [], []
        for index, (model, raw_model) in enumerate(zip(scene.models, raw.models)):
            # HSD instance joints draw their shared subtree again (as on the field floors).
            sampled = geometry.Model(model, raw_model, allow_instances=True, **self.model_options())
            meshes = [geometry.build_mesh(sampled, i) for i in range(len(sampled.meshes))]
            meshes += geometry.instance_meshes(sampled, meshes)
            if len(sampled.ends) > 1:
                notes.append("model %d: %d clips, clip 0 kept" % (index, len(sampled.ends)))
            if getattr(sampled, "frozen_material_tracks", None):
                notes.append("model %d: animated material colours frozen at first value" % index)
            if any(sampled.paths):
                notes.append("model %d: joint path tracks" % index)
            loaded.append((index, sampled, meshes, sampled.ends[0] if sampled.ends else 0))
        approximations = self.used(before)
        levels = {index: "full" for index, _, meshes, end in loaded if meshes and end}
        reductions = []
        while True:
            self.cleanup("floor%02d_arena" % owner)
            since = collections.Counter(geometry.APPROXIMATIONS)
            packs, models = self.build_arena(owner, loaded, levels, fixed, lights, digest)
            ram = sum(p["repacked_bytes"] for p in packs)
            if not packs:
                raise ValueError("Arena produced no packs")
            if ram <= self.arena_budget and len(packs) <= MAX_ARENA_RECORDS:
                approximations += self.used(since)
                break
            animated = {m["model"]: sum(p["repacked_bytes"] for p in packs if m["model"] in p["source_models"]
                                        and p["sampling"] == "animated")
                        for m in models if m.get("animated_meshes") and levels.get(m["model"]) != "frozen"}
            full = [i for i in animated if levels[i] == "full"]
            if full:
                for i in full:
                    levels[i] = "reduced"
                change = "models %s capped at %d samples" % (full, ARENA_REDUCED_SAMPLES)
            elif animated:
                i = max(animated, key=animated.get)
                levels[i] = "frozen"
                change = "model %d frozen at its first sample" % i
            else:
                raise ValueError("Arena exceeds %.1f MB / %d records with every model frozen: "
                                 "%.1f MB, %d packs" % (self.arena_budget / 1e6, MAX_ARENA_RECORDS,
                                                        ram / 1e6, len(packs)))
            reductions.append({"was_bytes": ram, "was_packs": len(packs), "change": change})
            print("floor %d: %.1f MB in %d packs; %s" % (owner, ram / 1e6, len(packs), change), flush=True)
        if reductions:
            approximations["arena_budget_reductions"] += len(reductions)
        return {"archive": source["archive"], "member": "%08x" % resource,
                "member_bytes": len(payload), "member_sha256": hashlib.sha256(payload).hexdigest(),
                "member_choice": "the archive's only scene DAT (identity type byte 0x02)",
                "cameras": len(scene.cameras), "fogs_ignored": len(scene.fogs),
                "lights": len(lights), "camera": list(fixed), "models": models,
                "notes": notes, "approximations": dict(approximations), "budget_reductions": reductions,
                "ram_bytes": ram, "packs": packs}

    def build_arena(self, owner, loaded, levels, fixed, lights, digest):
        geometry = self.geometry
        packs, models, groups, pooled = [], [], [], []
        for index, sampled, meshes, end in loaded:
            level = levels.get(index, "full")
            if level == "frozen" or not end:
                frames, fps = [0.], 60.
            else:
                frames, fps = arena_frames(end, ARENA_MAX_SAMPLES if level == "full" else ARENA_REDUCED_SAMPLES)
            if not meshes:
                models.append({"model": index, "meshes": 0})
                continue
            static, animated = geometry.split_static(meshes, 0, frames) if len(frames) > 1 else (meshes, [])
            pooled += [geometry.bake_mesh(mesh, 0, frames[0]) or mesh for mesh in static]
            if animated:
                groups.append(("animated", [index], animated, frames, fps, end))
            models.append({"model": index, "meshes": len(meshes), "instances": len(sampled.instances),
                           "static_meshes": len(static), "animated_meshes": len(animated),
                           "clip_end": end, "level": level if end else "static",
                           "samples": len(frames), "sample_fps": fps})
        # Static meshes of every model need one sample and share one pack set;
        # each animated model keeps its own clock. Static draws before animated.
        if pooled:
            groups.insert(0, ("static", [m["model"] for m in models if m.get("static_meshes")],
                              pooled, [0.], 60., 0))
        for group, (label, sources, items, frames, fps, end) in enumerate(groups):
            prefix = "floor%02d_arena%d_" % (owner, group)
            written = geometry.prepare_pack_set(items, [(0, frames, True)], fps, [fixed] * len(frames),
                                                lights, digest, self.output, prefix + "t",
                                                format_version=3)
            for number, pack in enumerate(written):
                name = "%s%d.c3t" % (prefix, number)
                (self.output / pack["file"]).replace(self.output / name)
                pack["file"] = name
                entry = self.finish_pack(pack)
                entry.update(group=group, source_models=sources, sampling=label, authored_end=end)
                packs.append(entry)
        return packs, models

    def prepare_floors(self, floor_ids):
        owners = {}
        for battle_floor in floor_ids:
            key = str(battle_floor)
            try:
                source = self.floor(battle_floor)
            except ValueError as error:
                self.skip("floor", key, str(error))
                continue
            descriptor = "floor%02d.c3v" % battle_floor
            shared = owners.get(source["archive"])
            item = None if shared else self.reuse("floors", key)
            if item is None:
                try:
                    if shared is None:
                        arena = self.prepare_arena(battle_floor, source)
                    else:
                        arena = {k: v for k, v in shared.items() if k not in ("key", "battle_floor",
                                 "descriptor", "descriptor_sha256", "shared_with", "field", "floor_record")}
                        arena["shared_with"] = shared["battle_floor"]
                except Exception as error:  # noqa: BLE001 - recorded, never substituted
                    self.cleanup("floor%02d_arena" % battle_floor)
                    self.skip("floor", key, "%s: %s" % (type(error).__name__, error),
                              archive=source["archive"], trace=traceback.format_exc(limit=3))
                    self.save()
                    continue
                stage = encode_stage(arena["camera"], [p["file"] for p in arena["packs"]])
                (self.output / descriptor).write_bytes(stage)
                item = {"key": key, **source, **arena, "descriptor": descriptor,
                        "descriptor_sha256": hashlib.sha256(stage).hexdigest()}
            owners.setdefault(item["archive"], item)
            self.report["floors"].append(item)
            self.save()

    def cleanup(self, prefix):
        for path in self.output.glob(prefix + "*"):
            path.unlink()

    # Models --------------------------------------------------------------
    def prepare_model(self, binding, kind):
        geometry = self.geometry
        if not binding["archive"] or not binding["group"]:
            raise ValueError("No model resource (group %#x)" % binding["group"])
        archive = self.archive(binding["archive"])
        payload = member(archive, binding["resource"])
        before = collections.Counter(geometry.APPROXIMATIONS)
        data, raw, scene, header = load_pkx(archive, binding["archive"])
        # The reviewed PDA PKX path (tools/prepare_pda_models.py): lossless
        # GX position/envelope corners, ten envelope influences, source materials.
        from shared.Constants.gx import GX_VA_POS, GX_VA_PNMTXIDX
        split_positions = sum(split_envelope_positions(pobj, GX_VA_POS, GX_VA_PNMTXIDX)
                              for _, _, pobj in geometry.raw_meshes(raw.models[0]))
        if split_positions:
            from importer.phases.describe.describe import describe_scene
            options = {"filepath": binding["archive"], "game": "COLO_XD", "max_frame": 1000000000,
                       "section_names": [], "ik_hack": False, "pkx_header": header}
            scene = describe_scene([types.SimpleNamespace(root_node=raw, section_name="scene_data")], options)
        model = geometry.Model(scene.models[0], raw.models[0], allow_image_swaps=True,
                               **({"frozen_material_colors": True} if self.approximate else {}))
        entries = clip_mapping(header, model.ends)
        wanted = POKEMON_ENTRIES if kind == 0 else TRAINER_ENTRIES
        idle = entries[0]["clips"][0]
        built = [geometry.build_mesh(model, i, max_envelope_weights=10, source_materials=True)
                 for i in range(len(model.meshes))]
        approximations = self.used(before)
        if getattr(model, "frozen_material_tracks", None):
            approximations["frozen_material_colors"] += 1
        camera, lights = self.reference()
        prefix = ("pkx%03d" % binding["pkx"]) if kind == 0 else ("trn%03d" % binding["sequence"])
        # Smallest change first: 60 Hz with every kept entry, then 30 Hz, then
        # (Pokemon) without the clips only entries 3/4 use (not runtime roles).
        variants = [(1, wanted), (0.5, wanted)]
        if kind == 0:
            variants.append((0.5, tuple(e for e in wanted if e not in (3, 4))))
        if not self.approximate:
            variants = variants[:1]
        reductions = []
        for attempt, (rate, chosen) in enumerate(variants):
            self.cleanup(prefix)
            since = collections.Counter(geometry.APPROXIMATIONS)
            model.__dict__.pop("_collapsed_world_descriptors", None)
            kept = sorted({c for e in chosen if e < len(entries) for c in entries[e]["clips"]} | {idle})
            remap = {source: index for index, source in enumerate(kept)}
            entry_clip = [remap.get(entries[e]["clips"][0], UNSUPPORTED)
                          if e < len(entries) and entries[e]["clips"] else UNSUPPORTED
                          for e in range(ENTRY_COUNT)]
            schedules, resampled = [], []
            for source in kept:
                frames, uneven = resample(model.ends[source], rate)
                if uneven:
                    resampled.append(source)
                schedules.append((source, frames, source == idle))
            count = sum(len(frames) for _, frames, _ in schedules)
            meshes = geometry.expand_image_swaps([dict(mesh) for mesh in built])
            pairs = [(source, frame) for source, frames, _ in schedules for frame in frames]
            states = [model.world(source, frame) for source, frame in pairs]
            meshes, omitted = omit_collapsed_meshes(meshes, states)
            batches = len(meshes)
            if batches > 64:
                meshes = merge_batches(meshes)
            merged = merge_tracks(geometry, meshes, states, self.approximate)
            mark_collapsed(meshes, states, pairs)
            written = geometry.prepare_pack_set(meshes, schedules, 60. * rate, [camera] * count, lights,
                                                self.report["archives"][binding["archive"]]["sha256"],
                                                self.output, prefix, expected_count=1, format_version=3)
            packs = [self.finish_pack(pack) for pack in written]
            ram = packs[0]["repacked_bytes"]
            if ram <= MODEL_RAM_BUDGET or attempt == len(variants) - 1:
                approximations += self.used(since)
                break
            reductions.append({"was_bytes": ram, "next": "%g Hz, entries %s" % (
                60 * variants[attempt + 1][0], list(variants[attempt + 1][1]))})
        if merged:
            approximations["skin_tracks_merged"] += 1
        if rate != 1:
            approximations["actor_sampled_at_30hz"] += 1
        if len(chosen) != len(wanted):
            approximations["actor_entries_3_4_dropped"] += 1
        return {"file": packs[0]["file"], "kind": kind, "entry_clip": entry_clip,
                "kept_source_clips": kept, "source_clip_ends": model.ends,
                "sample_hz": 60 * rate, "kept_entries": list(chosen),
                "multi_clip_entries": [e["entry"] for e in entries if len(e["clips"]) > 1],
                "resampled_fractional_clips": resampled,
                "split_envelope_positions": split_positions,
                "omitted_zero_surface_meshes": omitted,
                "merged_skin_tracks": merged, "size_reductions": reductions,
                "joined_batches": batches - len(meshes),
                "approximations": dict(approximations),
                "member": "%08x" % binding["resource"], "member_bytes": len(payload),
                "member_sha256": hashlib.sha256(payload).hexdigest(),
                "dat_sha256": hashlib.sha256(data).hexdigest(),
                "animation_entries": entries, "particle_events": model.particle_events,
                "fixed_lights_camera": "battle floor %d arena" % REFERENCE_FLOOR,
                "packs": packs}

    def prepare_models(self, species_ids, trainer_ids):
        groups = {}
        for identity in species_ids:
            try:
                binding = self.species(identity)
            except ValueError as error:
                self.skip("species", str(identity), str(error))
                continue
            groups.setdefault(("pokemon", binding["pkx"]), (binding, 0, []))[2].append(identity)
        for identity in trainer_ids:
            try:
                binding = self.trainer(identity)
            except ValueError as error:
                self.skip("trainer", str(identity), str(error))
                continue
            groups.setdefault(("trainer", binding["sequence"]), (binding, 1, []))[2].append(identity)
        for (label, number), (binding, kind, users) in sorted(groups.items()):
            key = "%s%d" % (label, number)
            users_key = "species" if kind == 0 else "trainers"
            item = self.reuse("models", key)
            if item is None:
                try:
                    result = self.prepare_model(binding, kind)
                except Exception as error:  # noqa: BLE001 - recorded, never substituted
                    self.cleanup(("pkx%03d" % number) if kind == 0 else ("trn%03d" % number))
                    self.skip("model", key, "%s: %s" % (type(error).__name__, error),
                              archive=binding["archive"], **{users_key: users},
                              trace=traceback.format_exc(limit=3))
                    self.save()
                    continue
                item = {"key": key, **{k: v for k, v in binding.items() if k not in ("species", "trainer")},
                        **result}
            item[users_key] = sorted(set(users))
            self.report["models"].append(item)
            self.save()

    def write_index(self):
        models = sorted(self.report["models"], key=lambda m: (m["kind"], m["key"]))
        species, trainers = {}, {}
        for index, model in enumerate(models):
            for identity in model.get("species", []):
                species[identity] = index
            for identity in model.get("trainers", []):
                trainers[identity] = index
        floors = {int(f["key"]): f["descriptor"] for f in self.report["floors"]}
        index = encode_index(species, trainers, floors, models)
        (self.output / "index.c3n").write_bytes(index)
        packs = [p for f in self.report["floors"] if "shared_with" not in f for p in f["packs"]] + \
                [p for m in models for p in m["packs"]]
        self.report["index"] = {"file": "index.c3n", "bytes": len(index),
                                "sha256": hashlib.sha256(index).hexdigest(),
                                "model_order": [m["key"] for m in models],
                                "species": {str(k): v for k, v in sorted(species.items())},
                                "trainers": {str(k): v for k, v in sorted(trainers.items())},
                                "floors": {str(k): v for k, v in sorted(floors.items())}}
        self.report["totals"] = {
            "packs": len(packs),
            "raw_bytes": sum(p["raw_bytes"] for p in packs),
            "stored_bytes": sum(p["stored_bytes"] for p in packs),
            "arena_stored_bytes": sum(p["stored_bytes"] for f in self.report["floors"]
                                      if "shared_with" not in f for p in f["packs"]),
            "model_stored_bytes": sum(p["stored_bytes"] for m in models for p in m["packs"])}
        self.report["models"] = models
        self.save()


def id_list(text):
    return sorted({int(x, 0) for x in text.split(",") if x.strip()}) if text else None


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--disc", required=True, type=Path)
    parser.add_argument("--common-rel", required=True, type=Path)
    parser.add_argument("--parser", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--floors", help="comma list of battle-floor ids (default: every one an encounter uses)")
    parser.add_argument("--species", help="comma list of internal species ids "
                        "(default: every encounter trainer party species plus 196,197)")
    parser.add_argument("--all-species", action="store_true", help="every species 1..412")
    parser.add_argument("--trainers", help="comma list of trainer ids (default: every encounter opponent plus 1)")
    parser.add_argument("--max-texture", type=int, default=128, choices=(128, 256, 512, 1024))
    parser.add_argument("--skip-existing", action="store_true", help="resume into an existing output")
    parser.add_argument("--approximate", action="store_true",
                        help="accept and record the field tour's approximations, merged skin tracks, "
                             "30 Hz actors and frozen/capped arena animation")
    parser.add_argument("--arena-budget-mb", type=float, default=10.,
                        help="decompressed arena bytes per floor (default 10 MB)")
    args = parser.parse_args()
    run = Run(args)
    encounters = run.encounters
    floors = id_list(args.floors) or sorted({e["battle_floor"] for e in encounters})
    if args.all_species:
        species = list(range(1, SPECIES_COUNT))
    else:
        species = id_list(args.species) or sorted({s for e in encounters for s in e["species"]} |
                                                  set(WES_STARTERS))
    trainers = id_list(args.trainers) or sorted({t for e in encounters for t in e["trainers"]} |
                                                {WES_TRAINER})
    run.report["request"] = {"floors": floors, "species": species, "trainers": trainers}
    run.prepare_floors(floors)
    run.prepare_models(species, trainers)
    run.write_index()
    totals = run.report["totals"]
    print("battle3d: %d floors, %d models, %d skipped; %d packs %.1f MB raw -> %.1f MB stored" % (
        len(run.report["floors"]), len(run.report["models"]), len(run.report["skipped"]),
        totals["packs"], totals["raw_bytes"] / 1e6, totals["stored_bytes"] / 1e6))


if __name__ == "__main__":
    main()
