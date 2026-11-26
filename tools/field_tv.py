"""Field-camera texture entries of a floor (the original field televisions).

floor.c fn_801129CC calls fn_80117E58 when a floor starts: it selects entry
id 0 of the floor's texture-change table (FloorData +0x48; the floor REL's
prolog publishes the table). An entry is 0x18 bytes:

    u16 width, height   render texture (GStextureCreate, format 0x44)
    u32 id              the id natives AB (fn_80117AE4), B6 and D3 select
    u32 screen          GSresGetResource id of the screen model (map scene
                        member group << 16 | 0x10 << 8 | model index)
    u32 model           the model drawn into the texture (0x10 resource of a
                        type-04 member), opened by floorOpenModel
    u32 clip            GSmodelSetAnimIndex; GSmodelStartAnimation restarts it
    u32 camera          the type-18 camera member it is drawn with

GSmodelSetTextureChange then replaces every material texture of the screen
model with the rendered one (fn_80117D14 draws it each frame). The screen
model's own texture is only a placeholder (8x8 red on 0x1C and 0x13, white
in the Phenac and Under houses, black on 0x2F). The port renders the entry
with the stage renderer's television pass (c3_stage_renderer_set_tv).

The sidecar <prefix>.c3v binds the prepared packs (<prefix>_tv<N>.c3t) to
the screen batch (include/colosseum3ds/field_tv.h, little-endian):

    0  "C3TV"  4 version 1  8 file size  12 screen map pack
    16 screen texture index  20 pack count  24 entry count
    28 entries: u32 id, u32 pack, u32 clip (clip index inside that pack)
"""
import re
import struct

ENTRY = struct.Struct(">HHIIIII")
MAX_PACKS = 5        # C3_FIELD_TV_MAX_PACKS (the house floors' tables name 5 models)
MAX_ENTRIES = 16     # C3_FIELD_TV_MAX_ENTRIES
SIZES = (32, 64, 128, 256, 512)


def _entry(rel, offset, members, map_resource):
    width, height, ident, screen, model, clip, camera = ENTRY.unpack_from(rel, offset)
    if (width not in SIZES or height not in SIZES or ident >= 256 or clip >= 16 or
            (screen >> 8) & 255 != 0x10 or screen >> 16 != map_resource >> 16 or
            (model >> 8) & 255 != 0x10 or (model & 0xFFFF0000) | 0x0400 not in members or
            (camera >> 8) & 255 != 0x18 or camera not in members):
        return None
    return {"offset": offset, "width": width, "height": height, "id": ident, "screen": screen,
            "model": model, "model_member": (model & 0xFFFF0000) | 0x0400, "model_index": model & 255,
            "clip": clip, "camera": camera}


def texture_table(rel, members, map_resource):
    """The floor's texture-change entries in REL order ([] when it has none).

    The table is found by its contents: consecutive 0x18-byte rows whose
    screen is a model of the floor's map scene and whose model and camera
    are members of the floor archive. Exactly one such run may exist."""
    runs, offset = [], 0
    while offset + ENTRY.size <= len(rel):
        row = _entry(rel, offset, members, map_resource)
        if row is None:
            offset += 4
            continue
        run = [row]
        while run[-1]["offset"] + 2 * ENTRY.size <= len(rel):
            following = _entry(rel, run[-1]["offset"] + ENTRY.size, members, map_resource)
            if following is None:
                break
            run.append(following)
        runs.append(run)
        offset = run[-1]["offset"] + ENTRY.size
    if not runs:
        return []
    if len(runs) != 1:
        raise ValueError("Floor REL holds more than one texture-change table")
    rows = runs[0]
    if len({row["screen"] for row in rows}) != 1 or len({row["id"] for row in rows}) != len(rows):
        raise ValueError("Texture-change table names several screens or repeats an id")
    return rows


NATIVE_AB = re.compile(r"; native ab\((0x[0-9a-f]+|[^)]*)\)")
NATIVE_REPORT = re.compile(r"; native (b6|d3)\(")
# fn_801CA358 (native D3) selects the news report of the story so far with
# fn_80117AE4: 0 (A05), 1/2 (ACF; A06 watched), 3/4 (AD0, AD3 or 42C; A07,
# A08 or 42D watched), 11/12 (ADA or ADB; A0F or 483 watched).
REPORT_IDS = frozenset((0, 1, 2, 3, 4, 11, 12))
# fn_801C9F5C (native B6, the news event) also shows 12 and then 15 (ADA)
# or 14 (ADB) during the special report, then fn_801CA358's id.
NEWS_IDS = REPORT_IDS | {12, 14, 15}
# Common bank 0596 function 19 is the television check event: it plays the
# news music and calls native B6. The common event table binds it to the
# floors whose television can be checked (src/field_story.c tv_news).
TV_CHECK_SCRIPT = 0x05960013
EVENTS = (0x146364, 0x14A704, 28)   # common REL125 data: rows, count, stride


def tv_check_floors(common_data):
    """Floors whose event table rows run TV_CHECK_SCRIPT (common REL125 data)."""
    rows, count_at, stride = EVENTS
    count = struct.unpack_from(">I", common_data, count_at)[0]
    if count != 594:
        raise ValueError("Unexpected original event table count")
    floors = set()
    for index in range(count):
        _kind, _pad, floor, _source, script = struct.unpack_from(">BBHII", common_data, rows + index * stride)
        if script == TV_CHECK_SCRIPT:
            floors.add(floor)
    return floors


def script_entry_ids(lines, tv_check=False):
    """Entry ids the floor can show: 0 (floor start), every literal native AB
    argument, every report native D3 (fn_801CA358) can select when the floor
    script calls it and, when the floor script calls B6 or the floor binds
    the common television check event (tv_check), every id of the news event
    fn_801C9F5C."""
    ids = {0}
    for line in lines:
        match = NATIVE_AB.search(line)
        if match and match.group(1).startswith("0x"):
            ids.add(int(match.group(1), 16))
        match = NATIVE_REPORT.search(line)
        if match:
            ids |= NEWS_IDS if match.group(1) == "b6" else REPORT_IDS
    if tv_check:
        ids |= NEWS_IDS
    return ids


def plan(rows, wanted):
    """Group the wanted entries into television packs.

    One pack per (model, camera) pair, entry 0's first, at most MAX_PACKS;
    a pack holds the distinct source clips of its entries in ascending order.
    Returns (packs, entries, omitted): packs as dicts with model_member,
    camera and clips, entries as (id, pack, clip index)."""
    chosen = sorted((row for row in rows if row["id"] in wanted), key=lambda row: (row["id"] != 0, row["offset"]))
    packs, keys, omitted = [], {}, []
    for row in chosen:
        key = (row["model"], row["camera"])
        if key not in keys:
            if len(packs) == MAX_PACKS:
                omitted.append(row["id"])
                continue
            keys[key] = len(packs)
            packs.append({"model": row["model"], "model_member": row["model_member"],
                          "model_index": row["model_index"], "camera": row["camera"], "clips": []})
        clips = packs[keys[key]]["clips"]
        if row["clip"] not in clips:
            clips.append(row["clip"])
    for pack in packs:
        pack["clips"].sort()
    entries = sorted((row["id"], keys[(row["model"], row["camera"])],
                      packs[keys[(row["model"], row["camera"])]]["clips"].index(row["clip"]))
                     for row in chosen if (row["model"], row["camera"]) in keys)
    if len(entries) > MAX_ENTRIES:
        raise ValueError("Television entry bound exceeded")
    return packs, entries, sorted(omitted)


def encode(screen_pack, screen_texture, pack_count, entries):
    if not 1 <= pack_count <= MAX_PACKS or not 1 <= len(entries) <= MAX_ENTRIES:
        raise ValueError("Television binding exceeds its bounds")
    if any(not 0 <= pack < pack_count for _ident, pack, _clip in entries):
        raise ValueError("Television entry names a missing pack")
    size = 28 + 12 * len(entries)
    data = struct.pack("<4s6I", b"C3TV", 1, size, screen_pack, screen_texture, pack_count, len(entries))
    return data + b"".join(struct.pack("<3I", *entry) for entry in entries)


def tv_pack_name(prefix, index):
    return "%s_tv%d.c3t" % (prefix, index)


def sidecar_name(prefix):
    return prefix + ".c3v"
