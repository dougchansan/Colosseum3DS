"""Prepare a private GC6E01 item catalog; no save is modified or created."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

from prepare_game_state import (Dol, SOURCE_COMMIT, COMMON_SHA1, COMMON_BYTES,
                                u16, u32, message)
from prepare_boot_media import MAIN_DOL_SHA1
from prepare_field_items_source import extract
from prepare_model import check_output

COUNT, STRIDE, HEADER = 548, 128, 128
SIZE = HEADER + COUNT * STRIDE
TABLE, INDEX, PARAM = 0x80363CE8, 0x803681E8, 0x80368630


def make_payload(records, mapping, params, name_for_message):
    if len(records) != 397*40 or len(mapping) != COUNT*2 or len(params) != 64*16:
        raise ValueError("Incorrect item table span")
    payload = bytearray(COUNT * STRIDE)
    for ident in range(COUNT):
        row_id = u16(mapping, ident*2)
        if row_id >= 397:
            raise ValueError("Invalid item table lookup")
        row = records[row_id*40:(row_id+1)*40]
        if row[4] >= 64 or row[0] > 6:
            raise ValueError("Invalid item pocket/effect parameter")
        off = ident*STRIDE
        payload[off:off+40] = row
        payload[off+40:off+56] = params[row[4]*16:(row[4]+1)*16]
        key = u32(row, 16)
        if key:
            name = name_for_message(key)
            # Display-only transliteration; original saves/strings stay untouched.
            display = "".join(c if 32 <= ord(c) < 127 else "?" for c in name).encode("ascii")
            if not display or len(display) >= 64:
                raise ValueError("Item name exceeds bounded display record")
            payload[off+56:off+56+len(display)] = display
    return bytes(payload)


def make_pack(dol_path, common_path):
    extract()  # Enforce the source interpretation used by this converter.
    dol_path, common_path = Path(dol_path), Path(common_path)
    if dol_path.is_symlink() or common_path.is_symlink():
        raise ValueError("Original input symlinks are not admitted")
    raw, rel = dol_path.read_bytes(), common_path.read_bytes()
    if len(raw) != 3779808 or hashlib.sha1(raw).hexdigest() != MAIN_DOL_SHA1:
        raise ValueError("Unexpected original main.dol")
    if len(rel) != COMMON_BYTES or hashlib.sha1(rel).hexdigest() != COMMON_SHA1:
        raise ValueError("Unexpected original common REL")
    dol = Dol(raw)
    base = u32(rel, u32(rel, 0x10) + 32)
    if base != 0x184C:
        raise ValueError("Unexpected common REL section")
    records, mapping, params = dol.read(TABLE, 397*40), dol.read(INDEX, COUNT*2), dol.read(PARAM, 64*16)
    payload = make_payload(records, mapping, params,
                           lambda key: message(rel, base, 0x58044, key)[:-2].decode("utf-16-be"))
    header = bytearray(HEADER)
    struct.pack_into(">4s5I", header, 0, b"C3IT", 1, SIZE, COUNT, STRIDE, HEADER)
    header[24:44] = bytes.fromhex(SOURCE_COMMIT)
    header[44:64] = bytes.fromhex(MAIN_DOL_SHA1)
    header[64:84] = bytes.fromhex(COMMON_SHA1)
    header[84:104] = hashlib.sha1(payload).digest()
    result = bytes(header + payload)
    return result, {
        "format": "C3IT", "version": 1, "source_commit": SOURCE_COMMIT,
        "dol_sha1": MAIN_DOL_SHA1, "common_rel_sha1": COMMON_SHA1,
        "bytes": len(result), "sha256": hashlib.sha256(result).hexdigest(),
        "items": COUNT, "record_bytes": STRIDE,
        "tables": {"item_rows_address": TABLE, "id_map_address": INDEX, "params_address": PARAM},
        "scope": "Private original parameters/names; field-use IDs 13..29 only; no save writes",
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dol", required=True, type=Path)
    parser.add_argument("--common-rel", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    output = check_output(args.output)
    report_path = output.with_suffix(output.suffix + ".json")
    if report_path.exists() or report_path.is_symlink():
        raise ValueError("Refusing to overwrite preparation report")
    pack, report = make_pack(args.dol, args.common_rel)
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("xb") as stream:
        stream.write(pack)
    with report_path.open("x", encoding="utf-8") as stream:
        json.dump(report, stream, indent=2)
        stream.write("\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
