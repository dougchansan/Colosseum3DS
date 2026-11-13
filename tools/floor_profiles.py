#!/usr/bin/env python3
"""Exact original floor profiles shared by the field/area/story converters.

One row describes one walkable story floor of the admitted GC6E01 revision 0
disc. Floor, lifecycle row and resource group come from the original REL125
lifecycle table (see docs/phenac-floors.md); member identities from the
archive directory; table offsets from the pinned floor REL source. A row
holds identities, offsets and counts only, never original bytes.

Adding a later floor is one new row here. Keys:

  floor, lifecycle_row, resource_group   original lifecycle identity
  archive, bytes, sha256                 exact admitted archive
  rel, module                            floor REL member and module number
  collision                              CCD member (walk/wall/event tables)
  people_table, people_count             36-byte person rows in REL data
  script, messages                       field GSVM bank and message members
  prefix                                 <prefix>.c3f/.c3a, _map%u, _wes, _npc%u
  story_pack                             C3S1 file name read by the runtime
  event_cameras                          optional camera members played by
                                         the bank; first is <prefix>_camera.c3c

Interior rows ("scene": "interior") add:

  pack_format                            C3T version of map, Wes, partner and
                                         object packs (exterior default 2)
  objects                                map models with several source clips;
                                         each becomes <prefix>_obj<model>.c3t
                                         holding every clip, one-shot
  path_joints                            admit HSD path tracks (cardinal spline)
  material_colors                        "constant": fold constant material RGB
                                         tracks; "frozen": also keep animated
                                         RGB at its first value (a recorded
                                         limitation: the format cannot sample it)
  omit_unsupported_texture_modes         leave out (and list) meshes whose HSD
                                         texture mode the pack runtime rejects

Geometry keys (map, entrance_table, entrance_count, models, groups,
frozen_instance_targets) are consumed by prepare_floor.py. Outskirt Stand's
two floors keep their dedicated geometry converters (prepare_outskirt.py and
prepare_area.py); their rows carry the story/area keys only.
"""

FLOORS = {
    "outskirt": {
        "floor": 0x01,
        "lifecycle_row": 0,
        "resource_group": 0x1D,
        "archive": "S1_out.fsys",
        "bytes": 1468000,
        "sha256": "37900ccab1353ab835c7e4acc40cd5e1349237589a53b441c78ce91a891f8034",
        "collision": 0x00E30600,
        "rel": 0x02571C00,
        "module": 131,
        "people_table": 0x30,
        "people_count": 7,
        "script": 0x01430E00,
        "messages": 0x01420A00,
        "prefix": "outskirt",
        "story_pack": "outskirt.c3s",
    },
    "shop": {
        "floor": 0x02,
        "lifecycle_row": 1,
        "resource_group": 0x1E,
        "archive": "S1_shop_1F.fsys",
        "bytes": 2947200,
        "sha256": "7d74dcb75c7f8aebc7479fee695e87a02ab58112d110dada15e88353d7215329",
        "collision": 0x00E70600,
        "rel": 0x02581C00,
        "module": 5,
        "people_table": 0x180,
        "people_count": 5,
        "script": 0x01460E00,
        "messages": 0x01450A00,
        "prefix": "shop",
        "story_pack": "outskirt_shop.c3s",
    },
    "phenac": {
        "floor": 0x03,
        "lifecycle_row": 2,
        "resource_group": 0x1F,
        "archive": "M1_out.fsys",
        "bytes": 2554304,
        "sha256": "85b1bbd23d9dc32287f90ac19eb8b8bd1b42b83dcfa9c92025490aaf5b2b5987",
        "map": 0x00620200,
        "collision": 0x00640600,
        "rel": 0x06611C00,
        "module": 15,
        # vendor/colosseum/src/rel/M1_out/M1_out.c: lbl_15_data_0, count 10.
        "entrance_table": 0x0,
        "entrance_count": 10,
        # lbl_15_data_A0: 243 words, 27 person rows of 36 bytes.
        "people_table": 0xA0,
        "people_count": 27,
        "script": 0x013E0E00,
        "messages": 0x015A0A00,
        "prefix": "phenac",
        "story_pack": "phenac.c3s",
        # The only camera member the field bank plays (native 2A in fn 28).
        "event_cameras": (0x0C101800,),
        # Original scene layout: nine map models and one camera.
        "models": 9,
        # Pack groups by original clock. Each tuple is (models, schedule).
        # Models 0-5 carry two 20-frame clips; clip0 is played. Model7 is
        # static and shares that clock. Model6 is the town body (299 frames).
        # Model8 is the slow sky (loop 24999 source frames).
        # HSD instances of model6 joint135 redraw an animated skinned
        # subtree with ~60 envelope tracks each; seven animated copies exceed
        # the native per-pack bone bound. Those copies are drawn at the first
        # sample; the structural subtree itself keeps its animation.
        "frozen_instance_targets": {6: (135,)},
        # Models 0-5 are the six doors of the common door table (rows 0-5,
        # open clip 0, close clip 1): object packs that rest until native 60
        # plays a clip. Looping clip0 as map geometry swung them open and shut.
        "objects": (0, 1, 2, 3, 4, 5),
        # Door-table row -> map model, matched by position: each object sits
        # on the touch surface whose link opens that row.
        "door_objects": {0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5},
        "groups": (((7,), ("short", 20)),
                   ((6,), ("short", 299)),
                   ((8,), ("sky", 24999))),
    },
    "phenac_mayor1f": {
        # Mayor's house 1F. Model 0 carries two clips (4 and 40 frames); model 1 only
        # animates textures (299 frames); model 3 is the television screen quad.
        "floor": 0x05,
        "lifecycle_row": 63,
        "resource_group": 0x22,
        "archive": "M1_houseA_1F.fsys",
        "bytes": 2915520,
        "sha256": "ff5a8ad15271ffd0faa3c903367e964fec12e74fcdb91e29526af9b2cf4e32bd",
        "scene": "interior",
        "map": 0x00690200,
        "collision": 0x006A0600,
        "rel": 0x065D1C00,
        "module": 18,
        "entrance_table": 0x28,
        "entrance_count": 2,
        "people_table": 0x48,
        "people_count": 7,
        "script": 0x014E0E00,
        "messages": 0x014F0A00,
        "prefix": "phenac_mayor1f",
        "story_pack": "phenac_mayor1f.c3s",
        "pack_format": 3,
        "models": 4,
        "groups": (((1, 2, 3), ("short", 299)),),
        "objects": (0,),
    },
    "phenac_mayor2f": {
        # Mayor's house 2F: three unanimated models; model 2 is the television screen quad.
        "floor": 0x06,
        "lifecycle_row": 64,
        "resource_group": 0x23,
        "archive": "M1_houseA_2F.fsys",
        "bytes": 2592416,
        "sha256": "fa79e72d721a50a4d4d7ae5a9d17d3c58042ecc815fa1320e3019acd9f2b1440",
        "scene": "interior",
        "map": 0x006B0200,
        "collision": 0x006C0600,
        "rel": 0x065E1C00,
        "module": 19,
        "entrance_table": 0x1A8,
        "entrance_count": 2,
        "people_table": 0x1C8,
        "people_count": 1,
        "script": 0x01510E00,
        "messages": 0x01520A00,
        "prefix": "phenac_mayor2f",
        "story_pack": "phenac_mayor2f.c3s",
        "pack_format": 3,
        "models": 3,
        "groups": (((0, 1, 2), ("static", 0)),),
    },
    "phenac_pc1f": {
        # Pokemon Center 1F. Model 2 moves two joints along cardinal splines (149
        # frames); model 3 carries seven clips. Models 0 and 3 animate material RGB.
        "floor": 0x04,
        "lifecycle_row": 62,
        "resource_group": 0x20,
        "archive": "M1_pc_1F.fsys",
        "bytes": 609504,
        "sha256": "bcb921ad7ed6acc6099e1c9e0b3976189da52ffae421166661be74866ef20974",
        "scene": "interior",
        "map": 0x00650200,
        "collision": 0x00660600,
        "rel": 0x06621C00,
        "module": 16,
        "entrance_table": 0x28,
        "entrance_count": 4,
        "people_table": 0x68,
        "people_count": 4,
        "script": 0x015D0E00,
        "messages": 0x015C0A00,
        "prefix": "phenac_pc1f",
        "story_pack": "phenac_pc1f.c3s",
        "pack_format": 3,
        "models": 4,
        "groups": (((0, 1), ("short", 299)), ((2,), ("short", 149))),
        "objects": (3,),
        "path_joints": True,
        "material_colors": "frozen",
        # Three meshes of model 2 use HSD colour map mode 2.
        "omit_unsupported_texture_modes": True,
    },
    "phenac_pregym1f": {
        # Pre Gym 1F. Model 0 carries four clips (19, 20, 35, 35 frames); model 1
        # animates material RGB.
        "floor": 0x0B,
        "lifecycle_row": 69,
        "resource_group": 0x28,
        "archive": "M1_gym_1F.fsys",
        "bytes": 794048,
        "sha256": "e0bc5af2383da15623f3dc46dffc30e20a7b0766f6c7087ea70edb2a8b2dd268",
        "scene": "interior",
        "map": 0x01C90200,
        "collision": 0x01C80600,
        "rel": 0x065B1C00,
        "module": 24,
        "entrance_table": 0x28,
        "entrance_count": 2,
        "people_table": 0x48,
        "people_count": 10,
        "script": 0x01480E00,
        "messages": 0x01490A00,
        "prefix": "phenac_pregym1f",
        "story_pack": "phenac_pregym1f.c3s",
        "pack_format": 3,
        "models": 3,
        "groups": (((1, 2), ("short", 299)),),
        "objects": (0,),
        "material_colors": "frozen",
    },
    "phenac_stadium1f": {
        # Phenac Stadium 1F. Models 0 and 1 are spline-driven doors (clips of 19, 20,
        # 20 and 1 frames, and 19, 20, 20); model 2 has one spline joint.
        "floor": 0x0D,
        "lifecycle_row": 71,
        "resource_group": 0x2B,
        "archive": "M1_stadium_1F.fsys",
        "bytes": 715200,
        "sha256": "a0f96c3ae3b7988d6f238fa64ce3473d64418c028a4782d59b5ae1d5f389e258",
        "scene": "interior",
        "map": 0x01CE0200,
        "collision": 0x01CD0600,
        "rel": 0x06661C00,
        "module": 26,
        "entrance_table": 0x28,
        "entrance_count": 3,
        "people_table": 0x58,
        "people_count": 3,
        "script": 0x01670E00,
        "messages": 0x01680A00,
        "prefix": "phenac_stadium1f",
        "story_pack": "phenac_stadium1f.c3s",
        "pack_format": 3,
        "models": 4,
        "groups": (((2, 3), ("short", 299)),),
        "objects": (0, 1),
        "path_joints": True,
        "material_colors": "constant",
    },
    "phenac_shop1f": {
        # Mart 1F: material and texture animation only (299 frames).
        "floor": 0x09,
        "lifecycle_row": 67,
        "resource_group": 0x26,
        "archive": "M1_shop_1F.fsys",
        "bytes": 496672,
        "sha256": "5e265300c5d899dc0ea0f78d30e993cc0a0ab3a5049f7d5579107a0cbaaf86ef",
        "scene": "interior",
        "map": 0x00710200,
        "collision": 0x00720600,
        "rel": 0x06641C00,
        "module": 22,
        "entrance_table": 0x28,
        "entrance_count": 2,
        "people_table": 0x48,
        "people_count": 4,
        "script": 0x01610E00,
        "messages": 0x01620A00,
        "prefix": "phenac_shop1f",
        "story_pack": "phenac_shop1f.c3s",
        "pack_format": 3,
        "models": 2,
        "groups": (((0, 1), ("short", 299)),),
        "material_colors": "frozen",
    },
    "phenac_shop2f": {
        # Mart 2F: material and texture animation only (299 frames).
        "floor": 0x0A,
        "lifecycle_row": 68,
        "resource_group": 0x27,
        "archive": "M1_shop_2F.fsys",
        "bytes": 470144,
        "sha256": "0dee1a21a2ce5e366e7fb21f220feb5a7fcc7a1e13c62987796470576cce8876",
        "scene": "interior",
        "map": 0x00730200,
        "collision": 0x00740600,
        "rel": 0x06651C00,
        "module": 23,
        "entrance_table": 0x28,
        "entrance_count": 1,
        "people_table": 0x38,
        "people_count": 3,
        "script": 0x01640E00,
        "messages": 0x01650A00,
        "prefix": "phenac_shop2f",
        "story_pack": "phenac_shop2f.c3s",
        "pack_format": 3,
        "models": 2,
        "groups": (((0, 1), ("short", 299)),),
        "material_colors": "frozen",
    },
    "phenac_houseb1f": {
        # House B 1F: model 0 has one spline joint (299 frames); model 2 is the
        # television screen quad.
        "floor": 0x07,
        "lifecycle_row": 65,
        "resource_group": 0x24,
        "archive": "M1_houseB_1F.fsys",
        "bytes": 2668384,
        "sha256": "66815c3db78f8831ac402490c705d0690f5d49c6e87317a58a0cc69da6638513",
        "scene": "interior",
        "map": 0x006D0200,
        "collision": 0x006E0600,
        "rel": 0x065F1C00,
        "module": 20,
        "entrance_table": 0x1A8,
        "entrance_count": 1,
        "people_table": 0x1B8,
        "people_count": 2,
        "script": 0x01550E00,
        "messages": 0x01540A00,
        "prefix": "phenac_houseb1f",
        "story_pack": "phenac_houseb1f.c3s",
        "pack_format": 3,
        "models": 3,
        "groups": (((0, 1, 2), ("short", 299)),),
        "path_joints": True,
    },
    "phenac_housec1f": {
        # House C 1F: model 0 animates one joint (299 frames); model 2 is the
        # television screen quad.
        "floor": 0x08,
        "lifecycle_row": 66,
        "resource_group": 0x25,
        "archive": "M1_houseC_1F.fsys",
        "bytes": 2661888,
        "sha256": "f87a7d9e7eb6234e0e644eb0a91872929b29a4384cd56150197b99d13732d5aa",
        "scene": "interior",
        "map": 0x006F0200,
        "collision": 0x00700600,
        "rel": 0x06601C00,
        "module": 21,
        "entrance_table": 0x28,
        "entrance_count": 1,
        "people_table": 0x38,
        "people_count": 2,
        "script": 0x01570E00,
        "messages": 0x01580A00,
        "prefix": "phenac_housec1f",
        "story_pack": "phenac_housec1f.c3s",
        "pack_format": 3,
        "models": 3,
        "groups": (((0, 1, 2), ("short", 299)),),
    },
    "phenac_pregymb1": {
        # Pre Gym B1. Model 0 carries four spline-driven clips (19, 20, 35, 35 frames).
        "floor": 0x0C,
        "lifecycle_row": 70,
        "resource_group": 0x29,
        "archive": "M1_gym_B1.fsys",
        "bytes": 1145056,
        "sha256": "ccd4f98c4213265b44a319f176610790568451183f066f58c466c55d605760f1",
        "scene": "interior",
        "map": 0x01CB0200,
        "collision": 0x01CA0600,
        "rel": 0x065C1C00,
        "module": 25,
        "entrance_table": 0x28,
        "entrance_count": 1,
        "people_table": 0x38,
        "people_count": 8,
        "script": 0x014B0E00,
        "messages": 0x014C0A00,
        "prefix": "phenac_pregymb1",
        "story_pack": "phenac_pregymb1.c3s",
        "pack_format": 3,
        "models": 3,
        "groups": (((1, 2), ("short", 299)),),
        "objects": (0,),
        "path_joints": True,
    },
    "phenac_pcb1": {
        # Pokemon Center B1: model 2 moves two joints along cardinal splines.
        "floor": 0x77,
        "lifecycle_row": 115,
        "resource_group": 0x21,
        "archive": "M1_pc_B1.fsys",
        "bytes": 381376,
        "sha256": "f239eb4eaec4b0109c7bd37070223f2cfcfe087065938cde8d68e5abc1eae3fe",
        "scene": "interior",
        "map": 0x00670200,
        "collision": 0x00680600,
        "rel": 0x06631C00,
        "module": 17,
        "entrance_table": 0x28,
        "entrance_count": 3,
        "people_table": 0x58,
        "people_count": 1,
        "script": 0x02090E00,
        "messages": 0x015F0A00,
        "prefix": "phenac_pcb1",
        "story_pack": "phenac_pcb1.c3s",
        "pack_format": 3,
        "models": 3,
        "groups": (((0, 1), ("short", 299)), ((2,), ("short", 149))),
        "path_joints": True,
        "material_colors": "frozen",
        # Three meshes of model 2 use HSD colour map mode 2.
        "omit_unsupported_texture_modes": True,
    },
}

# The party's second member (hero_move.c getResID: group 0, ID 101). Its
# PeopleInfo row is 2; the model sits beside Wes in field_common.fsys. The
# model has more source motions than a pack may hold, so the pack carries a
# fixed list: Wes's convention first (PeopleInfo idle, walk, run at pack clips
# 0, 1, 2 and source motion 0 at 3), then PeopleInfo motion slot 3 and every
# motion a field bank of the admitted disc plays on (0, 0x65) with native 33.
PARTNER = {
    "archive": "field_common.fsys",
    "resource": 0x00F30400,
    "people_info": 2,
    "member_id": 101,
    "locomotion": (1, 2, 3),
    "clips": (1, 2, 3, 0, 4, 7, 14, 21, 22, 23, 27, 28, 29, 42, 43, 44),
}

MATERIAL_COLOR_MODES = (None, "constant", "frozen")

# Floors whose map geometry prepare_floor.py converts from the profile alone.
GEOMETRY_KEYS = ("map", "entrance_table", "entrance_count", "models", "groups")
# Floors whose actors, events and scripts the area/story converters prepare.
STORY_KEYS = ("collision", "rel", "module", "people_table", "people_count",
              "script", "messages", "story_pack")


def geometry_floors():
    return {name: row for name, row in FLOORS.items() if all(key in row for key in GEOMETRY_KEYS)}


def story_floors():
    return {name: row for name, row in FLOORS.items() if all(key in row for key in STORY_KEYS)}


def by_floor(number):
    """The one profile of an original floor number, or None."""
    rows = [(name, row) for name, row in FLOORS.items() if row["floor"] == number]
    if len(rows) > 1:
        raise ValueError("Duplicate original floor profile")
    return rows[0] if rows else None


def area_pack(profile):
    return profile["prefix"] + ".c3a"


def validate(profiles=None):
    """Reject a malformed table before any archive is opened."""
    profiles = FLOORS if profiles is None else profiles
    floors, prefixes, packs = set(), set(), set()
    for name, row in profiles.items():
        for key in ("floor", "lifecycle_row", "resource_group", "bytes"):
            if isinstance(row.get(key), bool) or not isinstance(row.get(key), int) or row[key] < 0:
                raise ValueError("Invalid floor profile field %s: %s" % (key, name))
        if not 1 <= row["floor"] <= 65535 or not 1 <= row["resource_group"] <= 65535:
            raise ValueError("Floor profile identity out of range: " + name)
        digest = row.get("sha256")
        if (not isinstance(digest, str) or len(digest) != 64 or
                any(c not in "0123456789abcdef" for c in digest)):
            raise ValueError("Floor profile needs an exact SHA256: " + name)
        archive, prefix = row.get("archive"), row.get("prefix")
        if not isinstance(archive, str) or not archive.endswith(".fsys") or "/" in archive or "\\" in archive:
            raise ValueError("Invalid floor archive name: " + name)
        if (not isinstance(prefix, str) or not prefix or len(prefix) > 32 or
                not all(c.isascii() and (c.isalnum() or c == "_") for c in prefix)):
            raise ValueError("Invalid floor pack prefix: " + name)
        present = [key in row for key in STORY_KEYS]
        if any(present):
            if not all(present):
                raise ValueError("Incomplete story keys in floor profile: " + name)
            if not 0 <= row["people_count"] <= 32 or row["people_table"] < 0:
                raise ValueError("Floor people table exceeds the area actor bound: " + name)
            story = row["story_pack"]
            if (not isinstance(story, str) or not story.endswith(".c3s") or
                    not all(c.isascii() and (c.isalnum() or c in "_.") for c in story)):
                raise ValueError("Invalid story pack name: " + name)
            if story in packs:
                raise ValueError("Duplicate story pack name: " + name)
            packs.add(story)
        if all(key in row for key in GEOMETRY_KEYS):
            models, objects = row["models"], row.get("objects", ())
            grouped = [index for indices, _ in row["groups"] for index in indices]
            if (isinstance(models, bool) or not isinstance(models, int) or
                    sorted([*grouped, *objects]) != list(range(models))):
                raise ValueError("Floor groups and objects must cover each map model once: " + name)
            doors = row.get("door_objects", {})
            if any(not isinstance(r, int) or r < 0 or model not in objects for r, model in doors.items()):
                raise ValueError("Door objects must name object packs of the floor: " + name)
            if any(kind not in ("short", "sky", "static") for _, (kind, _end) in row["groups"]):
                raise ValueError("Unknown floor group clock: " + name)
            if row.get("pack_format", 2) not in (2, 3):
                raise ValueError("Unsupported floor pack format: " + name)
            if row.get("material_colors") not in MATERIAL_COLOR_MODES:
                raise ValueError("Unknown floor material colour mode: " + name)
            if row.get("scene", "exterior") not in ("exterior", "interior"):
                raise ValueError("Unknown floor scene kind: " + name)
        if row["floor"] in floors or prefix in prefixes:
            raise ValueError("Duplicate floor or prefix in profiles: " + name)
        floors.add(row["floor"])
        prefixes.add(prefix)
    return profiles


def validate_partner(partner=None):
    partner = PARTNER if partner is None else partner
    clips, locomotion = partner["clips"], partner["locomotion"]
    if (len(locomotion) != 3 or tuple(clips[:3]) != tuple(locomotion) or len(set(clips)) != len(clips) or
            not 4 <= len(clips) <= 32 or any(isinstance(c, bool) or not isinstance(c, int) or c < 0 for c in clips)):
        raise ValueError("Partner clip list must start with idle, walk, run and fit one pack")
    return partner


validate()
validate_partner()
