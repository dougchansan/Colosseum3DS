# Boot and title audio boundary

The two THP files have different audio arrangements. `movie/tpc.thp` contains
32 kHz stereo ADPCM, which the private preparation tool converts to PCM for
NDSP. `movie/gs_logo.thp` contains **no embedded audio**, but the original game
starts a separate MusyX song when it starts that movie. The animated title also
starts an external MusyX song. A silent THP track does not establish that the
original scene was silent.

The native build currently prepares the TPC PCM track. External GS logo and
title music remain unrendered and are not included in its stream assets. Native
audio output was not validated in the supplied emulator environment: its core
log reports a failed open of `/3ds/dspfirm.cdc`, which is absent from the private
SD. Consequently `ndspInit` fails and the player reports audio unavailable. No
DSP firmware is bundled by this repository. Movie pixel captures establish
visual execution only; they do not establish audible playback.

## Exact original mapping

These mappings use Count-Sheep commit
`51bfe4314a218dfaf9592fc80723c50498d0ee55` and were cross-checked against the
user-supplied GC6E01 revision 0 `common_rel.rel` and FSYS entries.

| Scene | Game sound ID | Song table index | MusyX song/setup ID | FSYS member |
| --- | ---: | ---: | ---: | --- |
| Genius Sonority logo | `0x04D1` | 77 | 75 | `bgm_archive.fsys`: `0x119A0800`, `gs_logo_song` |
| Animated title | `0x046A` | 60 | 58 | `bgm_archive.fsys`: `0x0FF00800`, `title_song` |

The call sites are
[`movie.c`, `fn_800364C8`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/movie.c#L463-L485)
and
[`gs_title_8001EF78.c`, `fn_8001FD48`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_title_8001EF78.c#L363-L375).
[`fn_80165A20` / `fn_80165A44`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_range_801653CC_exact_801654E0.c)
look up the sound entry's song-table index and load that resource before playing
it. Both entries use reverb setting 5 and the music sample bank.

The live sound-entry table is published from common REL `.data + 0x13FE08` by
[`common_rel.c` `_prolog`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/rel/common_rel/common_rel.c).
Each
[`GSsndEntry`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/include/game/gs_range_801653CC_shared.h)
is 12 bytes in the GameCube layout: flags at 0, an unnamed byte at 1, reverb at 2,
song-table index at 3, and the big-endian song/setup ID at 4.
[`fn_8016761C`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_dvd_candidate_80167040.c)
forwards that byte (zero in both entries), the song/setup ID, and the loaded song
resource to the MusyX start function `fn_8015A368`. The verified REL's `.data`
file offset is `0x184C`. Thus title entry `0x046A` is at file offset `0x144B4C`
and logo entry `0x04D1` at `0x145020`.

The song names and FSYS IDs are independently declared in
[`snd_song_table.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/rel/common_rel/snd_song_table.c).
The music-bank components come from
[`snd_sample_table.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/rel/common_rel/snd_sample_table.c):

| Private source | FSYS member ID | Decoded bytes | Conventional MusyX filename |
| --- | ---: | ---: | --- |
| `common.fsys`: `snd_music_proj` | `0x01A60000` | 6936 | `snd_music.proj` |
| `common.fsys`: `snd_music_sdir` | `0x01A80000` | 15124 | `snd_music.sdir` |
| `common.fsys`: `snd_music_pool` | `0x01A50000` | 23592 | `snd_music.pool` |
| `sound/snd_music.samp` | Loose file | 3659136 | `snd_music.samp` |
| `bgm_archive.fsys`: `gs_logo_song` | `0x119A0800` | 1240 | `gs_logo.song` |
| `bgm_archive.fsys`: `title_song` | `0x0FF00800` | 20228 | `title.song` |

Only those five archive members were privately extracted for this audit; their
bytes were not added to the repository. The supplied sample bank is already a
loose file. Decoded member SHA-256 values are:

| Member | SHA-256 |
| --- | --- |
| `gs_logo_song` | `a13a61faf858fabe01ebd5122056e10dd41702441445f4b8ae0b70aa38ace213` |
| `title_song` | `a4be38d54fe5fea3a9dc04b0bbe5230b1c75162e2aff35c58d59644146bc01d0` |
| `snd_music_proj` | `c0333bb9a8d9620efc7e2afd62ec16571b844c500338b20dc96123507d4d8e58` |
| `snd_music_sdir` | `a4bcebe11f3e8fdacd2d76558391b9dbc86ba119cb6aabd1a4bd41899ac448a6` |
| `snd_music_pool` | `eb35fecbb53f9fe87790701a7a61fff51abb824c66a3db6859ba1dabe30e4310` |

## Conversion dependency

These songs are sequences using the bank's instruments, macros, sample
directory, and samples. Decoding the ADPCM sample bank alone does not render the
song. The locally installed FFmpeg handles the THP conversion but exposes no
MusyX sequence renderer. `amuserender`, `amuseconv`, and `vgmstream` are not
installed in this workspace.

The upstream
[Amuse release documentation](https://github.com/AxioDL/amuse/releases?after=v1.4)
describes `amuserender` for rendering raw `.proj` / `.pool` / `.sdir` / `.samp`
groups with song data to PCM. That is a candidate offline dependency, not a
tested conversion in this milestone. Availability, a pinned build, correct
setup selection, finite song termination, reverb, and audible fidelity still
need verification before accepting a rendered output. `amuseconv`'s documented
SNG-to-MIDI conversion alone would not preserve the original instruments.

A later private converter can admit the exact members above, render only these
two song/setup IDs under a finite time/output limit, and record source and PCM
hashes. The logo song must begin with its movie. The title's original attract
transition depends on the end of BGM `0x046A`; inventing a fixed timeout before
that song's playback/end behavior is ported would change the game's state flow.
