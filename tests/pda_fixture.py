"""Explicitly synthetic PDA fixture; never installed as original game data."""
from pathlib import Path
import struct
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from prepare_pda import encode


def fixture():
    entries = {}
    def message(key, value):
        entries[1, key] = value.encode("utf-16-be") + b"\0\0"
    message(100, "UMBREON")
    message(101, "ESPEON")
    message(102, "MAKUHITA")
    message(110, "MOONLIGHT POKEMON")
    message(111, "A recorded Pokemon description.")
    message(120, "POTION")
    message(121, "Restores HP.")
    message(130, "BITE")
    for species, key, dex in ((197, 100, 197), (196, 101, 196), (335, 102, 296)):
        d = bytearray(64)
        struct.pack_into(">HH", d, 10, 10, 270)
        struct.pack_into(">H", d, 18, dex)
        struct.pack_into(">5I", d, 24, key, 110, 111, 2200 + species, 2600 + species)
        d[48:50] = bytes((17, 17))
        for i in range(6):
            struct.pack_into(">H", d, 52 + i * 2, 60 + i)
        entries[2, species] = d + bytes((5,))
    for i in range(32):
        message(200 + i, "SENDER %02d" % (31 - i))
        message(300 + i, "SUBJECT %02d" % i)
        # Exercise the real mixed-width hero-name substitution and long pages.
        entries[1, 400 + i] = (b"\xff\xff\x2b" + (", " + "A saved letter with several lines of text. " * 20).encode("utf-16-be") + b"\0\0")
        d = bytearray(44)
        struct.pack_into(">H", d, 8, 0xFFFF)
        struct.pack_into(">3I", d, 12, 200 + i, 300 + i, 400 + i)
        struct.pack_into(">I", d, 32, 2000 + i)
        if i == 0:
            struct.pack_into(">H", d, 2, 1)
            struct.pack_into(">I", d, 24, 27)
        if i == 4:
            struct.pack_into(">H", d, 2, 10)
        entries[3, i] = d
    nodes = bytearray(312)
    nodes[4] = 3
    struct.pack_into(">H", nodes, 6, 1)
    for i in range(29):
        nodes[(10+i)*4] = 2
        struct.pack_into(">H", nodes, (10+i)*4+2, i+1)
    entries[8, 0] = nodes
    for i in range(1, 97):
        d = bytearray(56)
        struct.pack_into(">H", d, 2, 335)
        struct.pack_into(">H", d, 8, 30)
        struct.pack_into(">H", d, 10, i)
        for offset, base in ((12, 1000), (14, 1100), (18, 1200), (20, 1300), (22, 1400), (28, 1500)):
            struct.pack_into(">H", d, offset, base + i)
        entries[4, i] = d
    entries[5, 0] = struct.pack(">48H", *range(1, 49))
    for i in (13, 22):
        d = bytearray(40)
        struct.pack_into(">II", d, 16, 120, 121)
        entries[7, i] = d
    d = bytearray(56)
    d[1] = 25
    struct.pack_into(">I", d, 32, 130)
    entries[10, 44] = d
    entries[9, 0] = b"".join(struct.pack(">I", n**3) for _ in range(6) for n in range(101))
    entries[6, 127] = struct.pack("<II", 8, 8) + bytes((20, 100, 40, 255)) * 64
    return encode(entries)


if __name__ == "__main__":
    Path(sys.argv[1]).write_bytes(fixture())
