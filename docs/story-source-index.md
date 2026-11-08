# Original story dependency discovery

`tools/prepare_story_index.py` creates a private structural index of the admitted
GC6E01 revision 0 sources. It inventories every original floor, event and name
row and every script member in the original archive TOC. It does not generate
playable floor packs or establish the mandatory main-story route.

The existing two-floor converter and runtime are unchanged. No progression
flags, battle results or authored script instructions are replaced.

## Invocation and output contract

```powershell
python tools/prepare_story_index.py `
  --dol <original-disc>/sys/main.dol `
  --common <original-disc>/files/common_rel.rel `
  --files <private-original-FSYS-directory> `
  --output <new-private-output-directory>
```

The directory must not exist. Output inside this checkout is restricted to
ignored `private/`. Source links/junctions, changed pins, malformed extents,
overlapping section/directory/payload/instruction spans, unsafe TOC names,
invalid counts and invalid script control flow are rejected. Generation stages
both JSON files beside the destination and publishes by directory rename.
Existing outputs are never overwritten. Only Python's standard library and
existing repository helpers are used.

`story-source-index.json` contains input and archive hashes, all member IDs and
extents, original table records and offsets, script hashes, all function slots,
per-entry CFG membership, branch/call edges, native sites and conservative
literal argument provenance. `summary.json` contains counts and discovered
flag-0x476 sites. Both files remain private; neither contains message text or
script byte arrays.

The importable interfaces are `build_index(dol, common, files)`,
`write_index(output, report)`, `metadata(rel)`, `bank_index(bank)` and bounded
archive/REL validation helpers. `build_index` returns metadata without writing.

## Source admission and measured inventory

The DOL SHA1 is `870e8b9693ca780782d80f22a6a4572d8ba9458f`; common REL125 SHA256
is `0c89e679305f5e84eff3c774b542120c727ff350a0841cfc516235b3556df69e`.
The existing pinned `GSfsys.toc` determines group/name bindings. Discovery also
pins the complete canonical private-stage archive catalog: SHA256
`92352de701a5f2decf3db2ab835998f07ecf0b6a72ed8f00a39d06fff2a604e9` over compact
JSON rows `[group, filename, bytes, sha256]` sorted by group. This newly measured
catalog admits exactly those stage files; it is not independent original-game
execution evidence. The catalog covers 367,744,672 bytes. Individual archive
hashes remain in the private report for reproducibility.

The canonical inventory run on 2026-10-26 produced:

| Category | Count |
| --- | ---: |
| Lifecycle records | 196 |
| Event records | 594 |
| Name/heard-flag records | 607 |
| TOC archive groups | 1,835 |
| FSYS members | 5,522 |
| Script banks including embedded common bank 0x0596 | 176 |
| Function slots, including 28 original null slots | 2,310 |
| Native sites | 19,086 |
| Unresolved native IDs | 163 |
| Unresolved native argument words | 13,352 |
| Conditional branches with unknown outcome | 5,365 |
| Lifecycle callbacks with relocation records | 83 |
| Unresolved nonzero lifecycle script references | 0 |

These are unfiltered source counts, not implemented content counts or a
main-story coverage denominator. Four admitted archives intentionally have no
members. Duplicate floor IDs, records, function entry offsets and bank IDs are
retained under their original row/archive identity. A bank ID such as 0x0100
is contextual to its owning resource group and cannot be globally deduplicated.
The stage also contains 17 `wzx_*` archives outside the pinned TOC. A separate
directory/type inspection found no type-0x0e script members in those files;
they are not admitted TOC groups, and their presentation/resource dependencies
remain outside this story index.

REL125 section 4 table locations remain 0x8bcf4/76-byte lifecycle rows,
0x146364/28-byte events and 0x1a34/8-byte names, with counts at 0x14a670,
0x14a704 and 0x14a660. Callback fields preserve both raw values and import
module, source section/addend, relocation type, relocation-record file offset
and destination section/offset. A raw zero with a relocation is a callback
reference, not evidence of a null callback.

The two-floor `prepare_story.inspect_bank` remains an additional check where
its assumptions fit. This index generalizes its all-entry CFG traversal locally:
retail null function slots are preserved but not treated as instructions.
Branches into operands, invalid/null function calls and unsupported/truncated
instructions still fail validation. Literal recovery stops at control transfers,
joins and unknown producers; local-slot provenance is retained without assuming
its value. Direct function-call arguments are retained in original stack order.

## Ending-writer findings and remaining gates

Original lifecycle row 90 binds floor 0x76 to group 0x95,
`D4_casino_colo.fsys`, field bank 0x0100, script member 0x02de0e00. Its script
member SHA256 is `f6266072c17db3d4045ef41d0c0e13ae8eadf61202966371e39a6490bfc78323`.
The index finds these original native sites (offsets are uncompressed member
offsets, not compressed-archive addresses):

| Function index | Member offset | Original operation |
| --- | ---: | --- |
| 2 | 598 | native 0x4f clears flag 0x476 |
| 12 | 17,466 | native 0x4e sets flag 0x476 |
| 15 | 19,455 | native 0x4e sets flag 0x476 |
| 4 | 34,266 | native 0x51 reads flag 0x476 |

Functions 12 and 15 call local helper 16 at offsets 17,431 and 19,377 with
literal arguments `[441, 442, 2169]` before those flag writes. Helper 16 reads
local slot 2 as a flag argument and has conditional native-0x98 sites at
34,637 and 34,689 loading local slots 0 and 1 respectively. Thus encounter
441/442 and selector flag 2169 (0x879) are concrete candidates associated with
the ending writers. The index deliberately leaves their contextual values and
branch outcomes unresolved; it does not certify which encounter is taken,
terminal-result handling, or a New Game-to-ending chain.

The separate original `vendor/colosseum/src/game/movie.c` contract identifies
`fn_800361C0` with `movie/staffroll.thp`. `fn_80035F64` checks previous floor
0x76 and flag 0x476 == 1, sets 0x478, offers the original backed-up save
transaction, and returns through `floorLink(2, 1)` and script 0x05960008. These
source findings still need original-execution correlation with the helper,
selected encounter, victory/defeat continuation, movie transition and clear-save
behavior. No adapter may grant those flags to close that evidence gap.

All conditional outcomes, 163 native IDs, 13,352 argument words, native callback
call graphs and any other dynamically produced destination remain explicit
discovery gates. Optional versus mandatory content requires actual route
evidence. Zero unresolved lifecycle script bindings only proves static resource
lookup; it does not certify native implementation, asset preparation or play.

## Verification

`python -m unittest discover -s tests -p test_prepare_story_index.py -v` passes
11 synthetic tests covering provenance, null/duplicate slots, literal versus
dynamic operands, call argument order, context binding, unknown branches,
instruction overlap/truncation, FSYS overlap/count/compression-header rejection,
REL relocations/counts/section overlap/termination, source pins/path names and
atomic new-directory output/no-overwrite behavior.

The full private canonical run produced the counts above without rejected
banks. Its `story-source-index.json` was 18,905,065 bytes with SHA256
`fd69119a61bab4fa07107d3a66857328a551632dbad6c3cd0ab9301836b7f254`.
This is structural source evidence; original PowerPC execution, complete route
closure and native New3DS acceptance are separate required gates.
