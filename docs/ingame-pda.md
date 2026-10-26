# In-game menu and P★DA

X or START while field control is available opens the graphical bottom-screen
menu. It provides Pokémon summaries/stats/moves, six inventory pockets and item
descriptions, the trainer record, Options, explicit Save, title return and P★DA.
Pokémon model previews use the top display while menu controls remain below.
The original green
P★DA terminal, metal frame/header, envelope indicators and all three mail
pictures are privately prepared from the admitted GC6E01 assets and reflowed
for 320×240. Source font/cursor rendering uses the existing `ui.c3ui` pack.
The ASCII font currently renders the device label as `P*DA`.

## Connected save behavior

| Page | Authoritative data and behavior |
| --- | --- |
| Mail | Received IDs/order and read bits from the live mailbox; source sender, subject, body and picture metadata. Empty saves stay empty. |
| Mail sorting | Newest, oldest, sender and subject. The source sort byte changes in memory and is kept by explicit Save. |
| Alert tone | The ten original named preferences use the actual saved selection byte. Highlighting a choice auditions its original arrangement and instruments; A keeps the preference, B cancels. |
| Reading mail | Applies the original read bit, story flag, tone bookkeeping and any Snag List hints transactionally. Two original common-bank callbacks run through the field VM after closing the menu. Reopening a read letter does not repeat its effects. |
| Strategy Memo | Actual registered entries, national number, species name, seen/caught state and registration marker. Registered details show category, types, US height/weight and abilities. Name/number/height/weight sorting and two-type/color search operate on the save's records. |
| Memo unlock/update | Original flag `0x3F0` gates the page. The `0x8000` entry marker hides unregistered detail/search data. Browsing does not perform the aide's update or invent registrations. |
| Snag List | All 48 original ordered IDs, source species/level, encountered/snagged/purified/new-information flags, saved trainer name and original flag-driven location/reappearance hints. Unseen entries remain anonymous. |
| Party | All six real party slots; nickname, level, current/max HP, stats, EXP, original trainer, selected ability and four moves with PP/PP Ups. |
| Items | Actual stack IDs/counts across all six pockets. Use, give, take, toss, bag order and party order update the live save through one transaction. Medicine, PP recovery/PP Ups, vitamins, Rare Candy, machines, stones, Sacred Ash and cologne use original metadata. Move learning and evolution choices commit together. Cancelled or ineffective medicine/PP/machine actions consume nothing; colognes retain the original valid-target consumption rule. |
| 3D previews | Party and registered Memo entries supply their saved species, personality and trainer IDs. Encountered Snag entries use saved shadow data, preferring an actual matching party record. Original PKX geometry, idle animation and shiny routing are prepared privately. Unknown entries remain hidden. |
| Trainer/Options | Actual name, trainer ID, money, coupons, time, partner name and party count. END applies source sound/rumble preferences; B cancels edits. Movie output picks up the sound preference, and title-menu session preferences stay synchronized. |
| Save/title return | Existing explicit confirmation flows, with No initially selected. Browsing never writes the card file. |

Field-only key-item scripts still require their original world context. Item
changes stay in memory until explicit Save. Current field/Continue admission
rules and the separate battle/reward backend remain unchanged.

## Prepare and install the private pack

Keep original files and generated packs out of commits. No extra Python
packages are required:

```sh
python3 tools/prepare_pda.py \
  --main-dol /absolute/path/disc/sys/main.dol \
  --common-rel /absolute/path/common_rel.rel \
  --files /absolute/path/disc/files \
  --output /absolute/path/private/pda.c3pd
```

The converter verifies the exact pinned DOL and common REL hashes and the
reviewed sizes/SHA256 hashes of `common.fsys`, `pda_menu.fsys`,
`pocket_menu.fsys`, `mail000.fsys`, `mail001.fsys` and `mail002.fsys`. It refuses
existing outputs and writes a provenance manifest beside the pack. The current
complete pack is **2,416,995 bytes / 6,380 entries**, including original item
effect parameters, full learning/evolution data, nature rates and machine IDs.

Install it as `sd:/3ds/Colosseum3DS/boot/pda.c3pd`, alongside the existing
`newgame.c3g` and `ui.c3ui`. The existing UI/font pack does not need replacing.
The menu loads the PDA pack on opening and frees it when field control resumes
or the area exits. The menu state itself is heap allocated; it does not consume
the 3DS thread's stack. A top-screen model preview owns its own GPU resources
after draining the field renderer; returning restores the field renderer and
TV bindings without restarting or ticking the field VM.

If this new pack is missing/incomplete, the basic menu can still display live
party/trainer data, and P★DA gives a preparation notice. It does not show an
invented empty mailbox or claim the original assets were loaded.

`C3PD` version 1 has a 64-byte header, canonical sorted 16-byte directory
records `{kind,key,offset,length}`, the source commit pin and a SHA-1 payload
digest. Runtime checks cover directory/extents/counts, image dimensions,
record sizes, shadow slots/flags and decoding bounds. A digest detects damage;
the private preparer's verified original inputs provide provenance. Source GS
message streams are retained, including mixed-width controls, and receive live
hero/partner/location substitutions at display time. RGB5A3/RGBA8 textures and
the original CMPR mail photos decode offline to bounded RGBA images.

## Source layout and decisions

The source is Count-Sheep/pkmn-colosseum at the repository's existing pin
`51bfe4314a218dfaf9592fc80723c50498d0ee55`.

| Data | Original evidence |
| --- | --- |
| Mailbox base `0x7D20` / memo base `0x82A8` | `pokemon_range_exact_80128CC0.c`, `savedataGetStatus` in `hero_savedata_exact_80129280.c` |
| 512 BE16 IDs; count `+0x400`; MSB-first read flags `+0x402` | `mail.c`: `mailGetMailIDInMailbox`, `mailGetNbMailInMailbox`, `fn_801D1B78`, `fn_801D1C20` |
| Sort `+0x442`, alert selection `+0x443`, receive-tone bookkeeping `+0x444` | `mailGetSortMode`, `fn_801D167C`, `fn_801D1364`; `menu_pda_mail.c` sorting and reader |
| Read effects | `mailMain.c`, `fn_801D228C`: original mail REL records at data `0x138`, sequence table at data `0`; one original letter supplies 29 hints |
| Memo count, 500 twelve-byte entries, packed species at `+4`, trainer at `+8`, personality at `+12` | `memo_r57b_8025FD34_suffix.c`, `memo_exact_8025FEE4.c`, `memoDataSetMemoFlag` |
| Memo visibility/filtering | `pda_range_80037158.c`: `pdaMemoSeen`, `pdaFilterTypes`, `pdaCollectWeights`, `pdaCollectHeights`, `fn_8004B278`; menu item `0x438` gates Strategy Memo |
| National numbers and color | `PokemonData.numPokemon` at `0x12`; source face color at `0x10C` is **1-based** (1 red … 10 pink). Species `numZukan` at `0x14` is a different index. |
| US dimensions | Original Espeon values `211` and `584` mean **2′11″ / 58.4 lb**; the native view preserves the admitted US profile's units. |
| Snag order | DOL `0x80375240`, 48 BE16 IDs; `fn_801EE440` / `fn_801EE468` |
| Shadow definitions and status | Common REL data `0x1439D8`, 56-byte entries: runtime slot, read/unlock/snag/purified/encounter/hint flag IDs; `battle_range_801ED640.c` getters |
| Snag location hints | Original action lists at common REL `0x144F10`; last matching condition wins as in `fn_801EE544`. Saved encounter location uses `fn_801EEF40`; floor label uses `floorDataBiosGetPtr` / `fn_8011396C`. |
| Field callbacks | Original mail 23/24 callbacks `0x05960018` / `0x05960019` set `0x0ADB` / `0x0ADA` via native `0x4E`. Existing field handling supports those calls. |
| Inventory | Original hero pockets: counts 20/43/16/64/46/3; offsets `0x780/0x7D0/0x87C/0x8BC/0x9BC/0xA74` relative to hero `0x70` |

Read actions validate first, then operate on a temporary 122,832-byte body.
Only successful transactions replace the live bytes. Mail callbacks are queued
before the next field tick; if commit fails, the queued task is cancelled.
Opening letters never invokes the disk save service. Original read indicators,
memo flags, registrations and unknown save bytes retain their separate meanings.

## Controls and verification

D-pad/Circle Pad navigates; A opens/confirms; B returns. L/R pages lists,
changes bag pockets, changes Pokémon summary tabs or moves between letters.
Mail X sorts and Y opens alert tones; Y replays the highlighted tone. Memo X sorts and Y opens
two-type/color search; source-unregistered records do not appear in those
filtered/height/weight results. A advances long text; X views an attached photo.
On item details and party summaries, X opens actions. Y on Pokémon, Memo or
Snag details opens/closes the model; B closes the model first. The C-stick
rotates it. Item quantity and move/evolution prompts use D-pad and A/B.

`tests/test_pda.c` uses the existing synthetic card fixture plus a clearly
synthetic PDA pack. It checks list bounds, unlocks, registration preservation,
29-hint read plans, atomic failure, idempotence, sort/tone persistence,
navigation, options rollback/apply, save/exit events, pagination, canaries,
byte-preserving rendering and explicit save/reload. Preparation tests cover the directory/digest, raw message extents/controls and CMPR photo
tile order/transparency. Field-story tests cover queued/cancelled menu callbacks.

Optional original-input checks also decode all 32 sender/subject/body sets,
apply all 32 read plans to a disposable fresh body, resolve all 48 snag hints,
execute both original callbacks and inspect a captured real GCI without writes.
The real GCI check preserved the complete card file and all 122,832 live bytes.
Populated preview screenshots use an explicit disposable **demo fixture**;
fresh-state previews show the actual empty mailbox and source memo lock.

```sh
make build/render_ingame_menu_preview
./build/render_ingame_menu_preview /private/ui.c3ui /private/pda.c3pd \
  /private/newgame.c3g /private/output
# Explicit populated fixture, no GCI loaded or saved:
./build/render_ingame_menu_preview /private/ui.c3ui /private/pda.c3pd \
  /private/newgame.c3g /private/output --demo
```

Host screenshots use the production controller, save accessors and raster
views. Physical-console readability, HOME/sleep behavior and frame-time/memory
measurements with the new PDA pack remain a separate hardware acceptance gate.

## Private model and ringtone preparation

Model preparation reuses the existing pinned HSD importer and NumPy geometry
pipeline. Select original internal species IDs, for example the two starters:

```sh
python3 tools/prepare_pda_models.py --files /private/disc/files \
  --common /private/disc/files/common_rel.rel --dol /private/disc/sys/main.dol \
  --parser vendor/hsd-importer --output /private/stage/pda/models \
  --species 196 197
python3 tools/prepare_ringtones.py --source /private/disc/files \
  --dol /private/disc/sys/main.dol --output /private/stage/ringtones
```

Copy `pda/models/<species>/` to `sd:/3ds/Colosseum3DS/pda/models/<species>/`
and `ringtones/audio.c3u` to `sd:/3ds/Colosseum3DS/ringtones/audio.c3u`.
Keep their provenance manifests with the private prepared assets. Missing
assets produce a recoverable notice and preserve the selected detail page.

Ringtones retain the original MusyX arrangements, instruments, sample loops,
volume/expression automation, source ADSR controllers and release envelopes.
The offline rendition downmixes the original dry stereo buses to the existing
mono PCM format and uses linear resampling; DSP filter/job timing and global
master-fader behavior have not been claimed identical to retail hardware.
Tone playback owns the existing NDSP player while the menu is open and closes
before field/battle/movie audio resumes.

The prepared preview set covers the starters and 47 of the 48 Snag List
species. Metagross (internal species 400) requires GX emboss bump mapping:
light-displaced tangent/binormal UVs and an unclamped intermediate are outside
the current native renderer. Spinda's source PID texture synthesis is
implemented and tested, but its five-stage material chain remains unsupported
by the viewer. Unown's saved-form binding is also guarded. These cases produce
explicit notices; their models/materials are not silently substituted or
stripped. A prepared model is required for each additional Memo/party species.

Inventory tests cover source item metadata, multi-move learning, evolution
decline, Shedinja, held-item transfers, reusable flutes, shadow friendship,
Cologne DP and Hyper Mode. The opt-in `test_inventory_real` harness loads a
captured GCI, modifies only a decoded disposable copy, saves to `/tmp`, reopens
it and compares all unrelated bytes. It also verifies the input card file is
unchanged. Source texture-routing and real-model animation probes cover shiny
selection, source RGBA channels, camera bounds and busy-renderer ownership.

Implementation is concentrated in `src/inventory.c` and the inventory-only
transaction in `src/game_state.c`; `platform/3ds/ingame_menu.c` owns the menu
and prompts. `field_scene.c` coordinates the existing renderer and audio
owners with `pda_preview.c` and `pda_audio.c`. Preparation lives in
`prepare_pda.py`, `prepare_pda_models.py` and `prepare_ringtones.py`. Existing
stat calculation, card saving, GPU ownership and NDSP playback are reused.
