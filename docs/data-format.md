# Native disc data reader

The first reader supports the GameCube FSYS container and its LZSS members.
It consumes an extracted file through a seekable callback, so an SD file can
be opened without loading the archive into memory. It does not execute DOL or
REL PowerPC code and does not yet convert model, animation, texture, audio or
save data to native 3DS representations.

## Verified source

The layout and decode semantics follow Count-Sheep's source at
`51bfe4314a218dfaf9592fc80723c50498d0ee55`:

- [FSYS reference parser](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/tools/fsys_extract.py)
- [Game decoder fn_8017F2C4](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_range_8017F2C4.c)

All archive words are big endian; the reader reads bytes explicitly and never
casts serialized fields to native structs or pointers.

| Structure | Offset | Field |
| --- | ---: | --- |
| Archive header | `0x00` | `FSYS` magic |
| Archive header | `0x08` | Archive ID |
| Archive header | `0x0C` | Member count |
| Archive header | `0x18` | Table-of-tables offset |
| Archive header | `0x20` | Archive byte length |
| Table-of-tables | `0x00` | Entry-offset-table offset |
| Member | `0x00` | Member ID (file type in bits 8–15) |
| Member | `0x04` | Data offset |
| Member | `0x08` | Uncompressed byte length |
| Member | `0x0C` | Flags (`0x80000000` means LZSS) |
| Member | `0x14` | Stored byte length |
| Member | `0x20` | Resource group |
| Member | `0x24` | Zero-terminated name offset |

Packed members contain `LZSS`, uncompressed length, compressed length including
the 16-byte header, and one reserved word. The decoder uses a zero-filled
4,096-byte history starting at `0xFEE`. Flags are read least-significant bit
first: one is a literal; zero is a two-byte match with a 12-bit history index
and length of the low nibble plus three. Matches can overlap their output,
including across the history boundary.

## Limits and error handling

The caller supplies the destination and its byte capacity. No allocation or
whole-archive read occurs. Decoder stack use is approximately 4.6 KiB, and
each underlying data read is at most 512 bytes. The caller is responsible for
its own allocation budget and file lifetime. The API supports archives up to
`UINT32_MAX` bytes, at most 65,535 members, and names up to 255 bytes. Longer
names are rejected instead of truncated. Archive and member ranges, arithmetic
overflow, inconsistent compression headers, truncated tokens, and output
overruns are rejected. Directory entries are checked when inspected, not
eagerly at open; a successful open alone does not validate every member.

Padding after the compressed length is allowed inside a member's stored
extent. A malformed token inside that declared compressed length is rejected.
On failure, discard the destination buffer; `written` remains zero.

The host tests use original synthetic data, including overlapping references,
history wrap, empty members, malformed offsets and lengths, truncation,
destination limits, and a member spanning many input cache refills. They prove
the specified parser behavior. Real game asset compatibility still requires
running against the user's extracted disc files.
