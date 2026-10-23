# Field Bag and restorative items

This module adds a controller-driven native Bag to the existing field pause
menu. The original first four pause choices retain their indices; **BAG** is
appended. The top field renderer and story VM retain their existing ownership.
The Bag uses the bottom text console, not the original graphical Bag UI.

## Supported behavior

All six source inventory pockets can be inspected: Balls (16 slots), Items
(20), Berries (46), TMs (64), Key Items (43), and Colognes (3). Quantities and
slot order come from the original big-endian save body. Berries allow 999 per
stack; the other admitted pocket layouts allow 99. Empty slots are not invented
items, and malformed slots produce an error instead of being repaired silently.

The **17 ordinary restorative item IDs 0x0D..0x1D** are usable outside battle:
Potion, Antidote, Burn Heal, Ice Heal, Awakening, Parlyz Heal, Full Restore,
Max Potion, Hyper Potion, Super Potion, Full Heal, Revive, Max Revive, Fresh
Water, Soda Pop, Lemonade and Moomoo Milk. Names and parameters are privately
prepared from the canonical original tables, not from a replacement database.

Select an item, select a party member, inspect the effect and explicitly choose
YES (default is NO). Cancellation, unsupported actions, wrong targets, stale
selection, full health/no matching condition, and Hyper Mode do not consume an
item. Eggs, invalid/empty party slots, malformed HP and malformed bag data are
rejected. The pending confirmation is rechecked before committing.

HP/condition changes and one-item consumption are staged together and committed
only after an actual effect. Removing the final copy compacts the normal pocket
in source order. Unknown Pokemon bytes, condition padding, other pockets,
other party members, save bookkeeping and RNG are preserved.

These are in-memory changes. The existing explicit **Save** persists them
through the original GCI codec; item use never opens a save file or writes disk.
The item action is permitted only from the normal field pause menu, not while a
script/battle is pending. It does not provide battle inventory, rewards or
result authority.

Other items are inspectable but visibly unavailable. PP items, vitamins, herbs
and friendship effects, berries, held-item exchange, throwing away items, TMs,
evolution, key-item scripts, pickups and shops are not implemented here. Normal
full-story progression and authoritative battle outcomes remain separate work.

## Source authority

`config/field-items-source-files.json` pins every inspected source file.
`prepare_field_items_source.py` verifies those hashes and the existing
source-lock before extracting the original `hpRecover` function from
`people/people_data.c`. Only identifier names and internal linkage change.
It compiles with endian-explicit PokemonBios accessors; the field adapter
always passes a null FightPokemon. Combat-only helper traps are unreachable
under that checked contract, not successful combat stubs.

Other adapters reproduce:
- `heroItemGetItemKindToItemAryPtr` (80129BC8): pocket/capacity/stack limits.
- `hero_bios_exact_8012A450.c`: pocket byte offsets, four-byte item entries.
- `fn_80144574`: field status cures and HP/revive parameter dispatch.
- `fn_80023B9C`: effect-before-consumption and Hyper Mode restriction.
- `fn_80140ACC` / `fn_801425E8`: decrement and stable normal-pocket compaction.
- `fn_8011B788`: seven condition fields cleared while padding survives.

PokemonBios's `tamagoFlag` is at 0xCB, `fuseiFlag` at 0xCD, main condition at
0x64, HP/max HP at 0x8A/0x8C, and the extra status record at 0xE8.
The implementation does not treat these as a GBA status bitmask. Field-only
confusion flags in item parameters do not create/clear nonexistent battle state.

Validation establishes source-derived behavior and compiled-helper parity, not
an independent original-PowerPC execution of the entire item menu. Native
menu layout and printable-name transliteration are deliberate adaptations.

## Private preparation / deployment

From a verified local extraction (the common REL may be extracted from
common.fsys using the existing tools):

```sh
python3 tools/prepare_field_items.py \
  --dol /private/disc/sys/main.dol \
  --common-rel /private/disc/files/common_rel.rel \
  --output /private/boot/items.c3it
```

The converter verifies the canonical DOL/REL hashes and source files, extracts
548 bounded ID-to-record entries, and refuses existing outputs. The C3IT v1 pack
is **70,272 bytes**, including 40-byte original item records, 16-byte parameters,
and bounded display names. It contains original data and must remain private.
Runtime checks source identities, layout, bounds, reserved bytes and payload
SHA-1. This checksum detects corruption, not malicious provenance.

Canonical preparation in the sandbox produced SHA-256:
`7dab02fcb10d3d54b0cd4b8bebba95bbdf54a970276e62472ccfbbde96188064`.

Install this one new file as:
`sdmc:/3ds/Colosseum3DS/boot/items.c3it`.
Missing data only disables Bag access; it does not stop existing boot/field
play. Existing models, movies, battle packs and saves need no conversion.
Use normal staged deployment/readback; retain actual local preparation hashes.

Controls: Left/Right or L/R changes pocket; Up/Down selects; A selects/confirms;
B cancels/goes back; X/START closes the Bag. The existing START+SELECT app exit
remains owned by the platform. Holding A does not repeat an item use.

## Tests and diagnostic

```sh
make build/test_field_items
./build/test_field_items
./build/test_field_items /private/boot/items.c3it /private/boot/newgame.c3g
python3 -m unittest discover -s tests -p 'test_prepare_field_items.py' -v
```

The default C fixture is explicitly synthetic. The optional private run uses
the real prepared item catalog and source-created WES state. It deliberately
sets damaged/status test fixtures in memory; it is not evidence of combat.

Coverage includes each supported item across HP/condition combinations, revive
rounding, immutable no-effect/dry-run/error results, stable bag compaction,
malformed data, every pocket, eggs, Hyper Mode, default-NO/revision/cancellation,
stale confirmation, held-button neutrality, complete-body byte comparison,
production Save/load and a load-only unchanged-GCI check.

`tools/sandbox/field_items_probe.c` is a separate ARM diagnostic, NOT linked into
the normal application. It initializes a fresh private WES fixture with Umbreon
at 5 HP, or loads its own prior test GCI. It runs the production Bag menu and
item service. Y explicitly saves only
`sdmc:/3ds/Colosseum3DS/test-field-items-01/items.gci`.
It never opens the normal `saves/` path or the completed-save original.

Build the diagnostic with `bash tools/sandbox/build_field_items_probe.sh`.
Stage only newgame.c3g and items.c3it into an isolated Azahar state. Test an item,
save with Y, exit, then relaunch in a separate process and compare the GCI.
Screenshots must identify this damaged test fixture, not a gameplay battle.

Physical Bag acceptance remains a local-console task; earlier battle/Save
evidence is not relabeled as this new feature. This branch does not operate the
console while the animation agent owns it.
