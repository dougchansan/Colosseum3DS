#!/usr/bin/env python3
"""Extract the pinned game's save codec, with explicit native-endian fixes.

No CARD task code, banner/icon data, or game-state initializer is extracted.
The only semantic portability change is loading SHA-1 message words as BE32.
Workspace becomes local/aligned so concurrent calls do not share scratch data.
"""
import argparse
import hashlib
from pathlib import Path
import re

SOURCE_COMMIT = "51bfe4314a218dfaf9592fc80723c50498d0ee55"
SOURCE_SHA256 = "3b0003c7d7971ba05b8fb1933dd5e687288cef1760ef0b664311264ba3ed3cc1"


def function(source, name):
    match = re.search(r"^(?:static inline void|void|u8) " + name +
                      r"\([^;]*?\)\n\{", source, re.M)
    if not match:
        raise ValueError("Pinned function definition missing: " + name)
    start = match.start()
    opening = source.index("{", start)
    depth = 1
    for end in range(opening + 1, len(source)):
        depth += (source[end] == "{") - (source[end] == "}")
        if depth == 0:
            return source[start:end + 1]
    raise ValueError("Unclosed pinned function: " + name)


def generate(data):
    if hashlib.sha256(data).hexdigest() != SOURCE_SHA256:
        raise ValueError("memcard.c does not match the pinned source SHA-256")
    source = data.decode("utf-8")
    update = function(source, "SHA1Update")
    transform = function(source, "fn_801CC380")
    final = function(source, "fn_801CBF64")
    encrypt = function(source, "fn_801CBE44")
    decrypt = function(source, "fn_801CBCDC")
    digest = function(source, "fn_801CBBAC")

    original_workspace = "    static u8 workspace[64];\n    SHA1Block* block = (SHA1Block*) workspace;"
    if transform.count(original_workspace) != 1:
        raise ValueError("Pinned SHA-1 workspace changed")
    transform = transform.replace(original_workspace,
                                  "    SHA1Block workspace;\n    SHA1Block* block = &workspace;\n    u32 native_i;")
    original_load = "    memcpy(block, input, 64);"
    if transform.count(original_load) != 1:
        raise ValueError("Pinned SHA-1 block load changed")
    transform = transform.replace(original_load,
        "    /* Native delta: the GameCube loads these words big-endian. */\n"
        "    for (native_i = 0; native_i < 16; native_i++) {\n"
        "        block->words[native_i] = c3_codec_be32(input + native_i * 4);\n"
        "    }")
    transform = "static " + transform
    final = "static " + final
    encrypt = encrypt.replace("\n{\n", "\n{\n    u32 sHash[5];\n    u32 sKey[5];\n", 1)
    decrypt = decrypt.replace("\n{\n", "\n{\n    u32 sHash[5];\n    u32 sKey[5];\n    u32 sDigest[5];\n", 1)

    macros = source[source.index("typedef union SHA1Block {"):source.index("void fn_801CC380(u32 state[5]")]
    undef = source[source.index("#undef SHA1_R4"):source.index("void fn_801CBF64(u8 digest[20]")]
    generated = """/* Generated from Count-Sheep/pkmn-colosseum, MIT licensed.
 * Source: src/game/memcard.c
 * Commit: %s
 * SHA-256: %s
 * Generate with tools/prepare_save_codec.py; do not edit generated output.
 * Native deltas: explicit BE32 SHA-1 loads; automatic aligned scratch;
 * local crypto work arrays; fixed-width names and private helper symbols.
 */
#include <stdint.h>
#include <string.h>
typedef struct FieldSha1Context {
    uint32_t state[5];
    uint32_t count[2];
    uint8_t buffer[64];
} FieldSha1Context;
void fn_801CBBAC(uint8_t digest[20], const uint8_t* input, uint32_t length);
static void fn_801CBF64(uint8_t digest[20], FieldSha1Context* context);
static void fn_801CC380(uint32_t state[5], const uint8_t input[64]);
static uint32_t c3_codec_be32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}
""" % (SOURCE_COMMIT, SOURCE_SHA256)
    generated += "\n\n".join((update, macros, transform, undef, final,
                               encrypt, decrypt, digest)) + "\n"
    # Array parameters decay to pointers in C. Spell that explicitly so GCC
    # does not infer an unconditional 64-byte read for the guarded Update
    # callsite while specializing the one-byte SHA-1 padding updates.
    generated = generated.replace("const u8 input[64]", "const u8* input")
    generated = generated.replace("const uint8_t input[64]", "const uint8_t* input")
    substitutions = {
        "u8": "uint8_t", "u32": "uint32_t", "s32": "int32_t",
        "FieldSha1Context": "C3CodecSha1Context",
        "SHA1Update": "c3_codec_sha1_update",
        "fn_801CC380": "c3_codec_sha1_transform",
        "fn_801CBF64": "c3_codec_sha1_final",
        "fn_801CBBAC": "c3_save_codec_sha1",
        "fn_801CBE44": "c3_save_codec_encrypt",
        "fn_801CBCDC": "c3_save_codec_decrypt",
    }
    pattern = re.compile(r"\b(" + "|".join(substitutions) + r")\b")
    return pattern.sub(lambda match: substitutions[match.group()], generated)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=Path("vendor/colosseum/src/game/memcard.c"))
    parser.add_argument("--output", type=Path, default=Path("build/save_codec.c"))
    args = parser.parse_args()
    try:
        output = generate(args.source.read_bytes())
        args.output.parent.mkdir(parents=True, exist_ok=True)
        if args.output.is_file() and args.output.read_text() == output:
            return
        temporary = args.output.with_suffix(args.output.suffix + ".tmp")
        temporary.write_text(output)
        temporary.replace(args.output)
    except (OSError, ValueError) as error:
        parser.exit(1, str(error) + "\n")


if __name__ == "__main__":
    main()
