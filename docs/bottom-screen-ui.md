# Original UI on the bottom screen

The [in-game menu and P★DA](ingame-pda.md) now add save-backed Mail, Strategy
Memo, Snag List, party details, inventory, trainer and Options pages. Install
the new private `pda.c3pd` pack using that guide.

The bottom screen now renders graphical title prompts, main-menu cards, name
entry, Continue, Options, field dialogue and choices, pause/party/save pages,
battle commands, targets, HP and battle messages. Battle text and the old
development HUD no longer duplicate that information on the top scene.

PRESS START uses a larger version of the menu's rounded blue button and glove
cursor. PP and HP values align to the right by measured glyph bounds, including
the battle/field status panels, target selection and party list.

The title heading is centered using measured glyph bounds and drawn at 125%.
Blue EXP bars show saved progress for the currently supported ordinary Umbreon
and Espeon in battle, field and party views. Total EXP comes from save offset
`0x5C`; both species select original growth bank 0, whose levels 1–100 were
checked against the pinned DOL table at `0x8035E940` (level cubed). The interval
matches `pokemonToMenuPokemonStatusSubBar`: current EXP minus the current-level
threshold, divided by the difference to the next-level threshold. Values are
clamped to the bar; level 100 has an empty track and MAX in labelled views.
Other species and shadow records omit the EXP bar until their growth/heart-gauge
metadata is supported. Battle previews display the saved total and do not award
or persist EXP. This needs no replacement of the existing `ui.c3ui` asset pack.

This uses actual GC6E01 Story/Battle/Options/Quit artwork, glove cursor, PRESS
START art, window/HP frames and proportional font, rearranged for 320×240. It
does not port the GS window engine, animated transitions or full glyph/control
repertoire, and does not establish retail pixel parity. Menu events, naming,
save confirmations, legal moves and nonpersistent battle boundaries are retained.

## Prepare the private artwork

No original artwork/font bytes are embedded in the executable or committed.
Use the same GC6E01 revision 0 inputs as the other preparation tools:

```sh
python3 tools/prepare_bottom_ui.py \
  --main-dol /absolute/path/disc/sys/main.dol \
  --files /absolute/path/disc/files \
  --output /absolute/path/private/ui.c3ui
```

Copy `ui.c3ui` to `sd:/3ds/Colosseum3DS/boot/ui.c3ui` alongside the existing
`menu.c3u`, title, field and battle packs. Preparation needs only Python's
standard library. It refuses existing outputs, checks the exact DOL hash and
sizes/SHA256 hashes of `common.fsys`, `fight_common.fsys` and `topmenu.fsys`,
and writes a provenance manifest. The pack is 790,424 bytes, loaded once into
ordinary heap; it adds no linear/GPU allocation.

The font comes from DOL address `0x8027A500`, registered by `main.c` through
`GSmsgFontOpen`. `gs_msg.c` establishes its bank layout, variable advance,
vertical bearing and linear I4 coverage. Preparation preserves the bearing and
averages 2×2 coverage for the smaller display. Printable ASCII is supported;
the source bank lacks curly braces, which display as question marks. Existing
message packs retain their reviewed ASCII conversion. GS formats `0x90` and
`0x45` map to RGB5A3/RGBA8 through `gs_texture.c`; preparation untwiddles the
original 4×4 tiles and crops the selected artwork.

If the optional pack is absent or invalid, the application draws plain graphical
windows with libctru's existing font and text menu labels. The boot trace records
`bottom_ui original_assets=0`. Loading checks every sprite/glyph extent before
exposing the borrowed bytes to drawing code.

## Rendering and input ownership

`src/bottom_ui.c` clips CPU drawing against host RGB8 or rotated BGR8/RGB565.
`bottom_screen.c` selects the actual libctru framebuffer format and borrows the console
font as fallback. It owns no C3D frame, swap, shader, render target or linear
allocation. Existing scenes retain top-screen submission and lifecycle handling.
Console diagnostics remain available for early failures and development probes.

`game_menu.c` draws menu pages; `game_ui_views.c` holds field/battle layouts,
shared unchanged between host previews and runtime. Rendering does not modify
game, story or battle objects. HP uses the presentation event interpolation
previously used by the top development HUD. Field messages paginate locally:
A/B show the next page before reaching the VM; choices become active on the
final page. Battle effect timing remains with the presentation controller.

## Preview and verification

```sh
make build/render_bottom_ui_preview
./build/render_bottom_ui_preview /private/ui.c3ui /private/menu.ppm 0
./build/render_bottom_ui_preview /private/ui.c3ui /private/dialogue.ppm dialogue
./build/render_bottom_ui_preview /private/ui.c3ui /private/battle.ppm battle
```

The tool accepts menu page numbers 0–9 and `title`, `field`, `dialogue`, `choice`,
`save`, `party`, `battle`, `event`, `target`, `result`. Fixture text/values are
synthetic; artwork and raster/layout code are identical to runtime. PPM output
requires no imaging dependency.

On October 25, 2026, `make test` passed 39 C executables and 340 Python tests
(67 environment/private-input skips). UI checks cover bounded pack loading,
transactional rejection, framebuffer orientation/channels, clipping/canaries,
all menu/scene pages, HP width, pagination/choice masking and immutable state.
Six Python tests cover texture formats/alpha, crop order, DOL mapping and pack
bounds. Existing 47 menu behavior checks also pass. The UI target passed ASan,
UBSan and GCC `-fanalyzer`; normal and boot-only ARM targets compile with
`-Werror`. Host visual checks establish source-art reflow and lack of overlap.
On October 26, 2026, the updated build was installed and readback-verified on
the physical New 3DS. P★DA, Pokémon and Items spacing passed using the real
save; the captured save file remained byte-identical after exit. This run
exposed `consoleInit` selecting RGB565: the raster now writes two bytes per
pixel in that mode, with orientation, alpha and buffer-bound regressions.
Completed frames are assembled in a 230,400-byte CPU buffer before publication
to avoid exposing the long raster pass on the live single-buffered screen.
Host publication tests check both formats, canaries, cancellation and format
changes; the full host suite and both ARM targets pass. HOME/sleep acceptance,
frame-time measurements and full model/audio parity remain separate checks.
