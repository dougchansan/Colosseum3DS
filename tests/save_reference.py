#!/usr/bin/env python3
"""Independent byte-wise/hashlib save-codec golden vector, no game content.

The C tests embed these fixed results. This is a format test pattern, not a
trainer record or a playable save. Offsets/sequence come from the pinned
memcard.c functions fn_801CF320, fn_801CBE44, and fn_801CBCDC.
"""
import hashlib
import struct


def fixture():
    body = bytes((i * 37 + (i >> 8) * 11 + 13) & 255 for i in range(0x1DFD0))
    slot = bytearray(0x1E000)
    slot[0:2] = b"\1\1"
    struct.pack_into(">I", slot, 4, 0x01020304)
    slot[8:8 + len(body)] = body
    slot[12:16] = b"\0" * 4
    slot[32:36] = b"\0" * 4
    digest = hashlib.sha1(slot[:0x1DFD8]).digest()
    key = bytes(value ^ 255 for value in digest)
    for offset in range(0x18, 0x1DFD8, 20):
        slot[offset:offset + 20] = bytes(value ^ pad for value, pad
                                        in zip(slot[offset:offset + 20], key))
        key = hashlib.sha1(slot[offset:offset + 20]).digest()
    slot[0x1DFEC:0x1E000] = digest
    checksum = sum(struct.unpack(">8I", slot[:32]))
    struct.pack_into(">I", slot, 12, -checksum & 0xFFFFFFFF)
    slot[0x1DFD8:0x1DFEC] = bytes(range(0xA0, 0xB4))
    return bytes(slot)


def main():
    slot = fixture()
    assert hashlib.sha256(slot).hexdigest() == "4856dfce8a39c6b80a171ba7645a59ac2625f3f338dbacccc2f04edb954cbecc"
    assert hashlib.sha1(slot).hexdigest() == "a2c9db0642fcf4c2a981b504f2e23e38dc32040e"
    assert slot[0x1DFEC:].hex() == "a54a31da326f8cfaeea2d405f243660dcb152641"
    print("Independent Python/hashlib save vector verified")


if __name__ == "__main__":
    main()
