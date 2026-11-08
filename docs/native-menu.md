# Native Story menu and naming

The main menu and name selection run as a native bottom-screen interface over
the live title scene. The choice order and transitions follow the pinned original
code. The [graphical bottom-screen UI](bottom-screen-ui.md) reflows the original
menu cards, cursor, window frames and proportional font from a private
`ui.c3ui` pack. Colosseum Battle and Battle Now
are visibly unavailable and do not enter placeholder game modes.

## Source decisions

The source pin is
[`51bfe4314a218dfaf9592fc80723c50498d0ee55`](https://github.com/Count-Sheep/pkmn-colosseum/tree/51bfe4314a218dfaf9592fc80723c50498d0ee55).

| Native behavior | Original evidence |
| --- | --- |
| New Game, Continue, Colosseum Battle, Battle Now, Options, Quit | [`gs_title.c`, `fn_800210F0`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_title.c#L2692) dispatches indices 0 through 5 in this order. Quit returns to the title. |
| New Game asks for consent, then names the player before creating state | The same function selects messages `0x3C3A`, `0x3C35` or `0x44C9`, opens mode-0 name entry, then calls `savedataCreate` and links to the opening movie. |
| NEW NAME, WES, SETH, THOMAS; at most seven characters | [`menuNameEntry.c`, `menuNameEntry`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/menuNameEntry.c) consumes the mode-0 record. The admitted DOL record at `0x80266DD8` has maximum length 7 and three preset IDs at `0x80266DC0`: `0x2EFE`, `0x2EFF`, `0x2F00`. Those messages decode from `name_entry_menu.fsys` to the three names above. |
| Confirm the selected/custom name; a blank custom name becomes WES | `menuNameEntry` uses message `0x2EF6` for confirmation and restores the default name for an empty or all-space entry. |
| Continue shows the stored name, location, money and play time before confirmation | `fn_800210F0` reads the save summary and asks message `0x3C22` before resuming. Native Continue is enabled only for a checked original save body and a supported area. |
| Options uses Mono/Stereo and original rumble preference; B cancels edits | [`gs_title.c`, `fn_80020618` and `fn_80020C9C`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_title.c) use audio mode 1 for Mono, 0 for Stereo, and original selector 9 values 0 for rumble on and 1 for off. |

An existing primary or backup card triggers an explicit replacement warning,
with **No selected initially**. Accepting New Game replaces only in-memory
state. The card changes only after the player later selects Save and confirms
it. A pending menu action emits its event once, so holding A cannot create
multiple games or issue repeated loads.

The native keyboard accepts seven printable ASCII characters, with case, space,
backspace and Done controls. The original keyboard also has non-ASCII glyphs and
a different layout; that presentation and input repertoire remain outside this
interface. D-pad and Circle Pad navigation both work. A confirms; B goes back or
deletes a character while editing; R/X changes case; START finishes custom entry.

## Options behavior in this build

END applies the options for the current application session. Returning to the
title or main menu retains that choice. A later explicit in-game Save stores the
original option bytes in the genuine save body. B from Options restores the
values present when the page opened. The menu states this persistence policy;
it does not silently write a card from the Options page.

Mono affects embedded movie audio: each decoded stereo PCM16 pair is averaged
with 32-bit arithmetic, then the result is sent to both NDSP output channels.
Stereo preserves the decoded pair. The selected mix is captured in the
`movie_begin` trace. Audio that has not been connected, such as external title
BGM, is not supplied by the setting. The rumble preference is retained in state;
the UI explains that the 3DS has no Controller Rumble motor.

## Prepare the original menu messages

The original short menu descriptions and name prompts can be read from the
user's admitted GC6E01 disc files. Run from the repository root:

```sh
python3 tools/prepare_game_menu.py --disc /absolute/path/disc --output /absolute/path/private/menu.c3u
```

Copy the resulting `menu.c3u` to `sdmc:/3ds/Colosseum3DS/boot/menu.c3u`.
The converter verifies the known disc revision and exact hashes of
`topmenu.fsys` and `name_entry_menu.fsys`, extracts only 25 selected messages,
and writes provenance alongside the private pack. It refuses to replace an
existing output. Generated text and original archive files stay outside commits.
The native interface has reviewed fallback labels if this optional pack is
missing; the trace records that fallback.

The original GSchar stream is not uniformly UTF-16: a `FFFF` escape is followed
by a one-byte control. The converter handles the reviewed newline, font,
button-wait and name-placeholder controls explicitly, removes styles/waits that
the native interface handles itself, and transliterates e-acute for the console.
Unknown controls and unsupported glyphs fail preparation. The runtime separately
checks pack size, sorted IDs, text extents, termination and allowed ASCII before
replacing an already loaded pack, preventing terminal escapes from being printed.

## Verification and remaining scope

`tests/test_game_menu.c` exercises confirmation/cancellation, existing-card
default No, WES/custom-name selection, Continue, unavailable modes, option
rollback/apply, bounded held-direction repetition, one-time pending actions and
transactional text loading. Its 47 checks passed. Seven synthetic Python tests
cover mixed-width GSchar controls, compressed-data bounds, overlapping LZSS
references, message extents and native text-pack rejection. The movie tests cover
signed PCM conversion and same/opposite-sign extremes in the mono mix.

The menu owns no C3D frame or render target. It emits requests to the boot/field
state controller and draws only to the bottom framebuffer. The live title,
opening movie, field geometry and genuine save service retain their respective
resource ownership. Passing these menu tests does not establish a complete
Story Mode, original UI visual parity, or physical-console performance.
