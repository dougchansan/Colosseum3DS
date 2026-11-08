#!/usr/bin/env python3
"""Extract a test-only personality-filter reference from pinned upstream C.

The only semantic lowering is making Rand32's call order explicit. Canonical
PPC disassembly at 80124168/170 and 8012444c/454 puts the FIRST draw in HIGH16.
This is source-parity testing, not execution of the original PowerPC body.
"""
import argparse
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = "src/game/pokemon_range_8011F5FC.c"
BLOB = "eeec67fe8096f20bb0a725788377a0c84f10f93c"
FUNCTIONS = ("pokemonGetSexKotei", "pokemonGetSexFromRnd",
             "pokemonCreateRndFitGetSex", "pokemonCreateRndFitIsRare",
             "pokemonCreateRndFit")


def extract_function(source, name):
    match = re.search(r"^(?:static inline )?\w+ " + re.escape(name) +
                      r"\([^;{}]*\)\s*\{", source, re.M)
    if not match:
        raise ValueError("Pinned initializer reference function missing: " + name)
    depth, end = 1, match.end()
    while depth and end < len(source):
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    if depth:
        raise ValueError("Unbalanced pinned reference function")
    return source[match.start():end]


def extract_reference(source):
    # Extract the original helper as a signature/binding check before lowering
    # its unspecified C evaluation order to the observed PPC order.
    helper = extract_function(source, "pokemonCreateRndFitRand32")
    if helper.count("fn_800E0C54()") != 2:
        raise ValueError("Original personality draw binding changed")
    preamble = """/* Generated test-only source reference; no gameplay authority. */
static inline u32 pokemonCreateRndFitRand32(void) {
    u32 high = fn_800E0C54();
    return (high << 16) | (fn_800E0C54() & 0xffffu);
}
"""
    return preamble + "\n\n".join(extract_function(source, name) for name in FUNCTIONS) + "\n"


def prepare(source_root, output):
    raw = (Path(source_root) / SOURCE).read_bytes().replace(b"\r\n", b"\n")
    digest = hashlib.sha1(b"blob " + str(len(raw)).encode() + b"\0" + raw).hexdigest()
    if digest != BLOB:
        raise ValueError("Battle initializer reference source drift")
    text = extract_reference(raw.decode("utf8"))
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf8", newline="\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "vendor/colosseum")
    parser.add_argument("--output", type=Path, default=ROOT / "build/battle_init_reference.inc")
    args = parser.parse_args()
    try:
        prepare(args.source, args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")
