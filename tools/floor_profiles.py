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
        "groups": (((0, 1, 2, 3, 4, 5, 7), ("short", 20)),
                   ((6,), ("short", 299)),
                   ((8,), ("sky", 24999))),
    },
}

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
        if row["floor"] in floors or prefix in prefixes:
            raise ValueError("Duplicate floor or prefix in profiles: " + name)
        floors.add(row["floor"])
        prefixes.add(prefix)
    return profiles


validate()
