# Systems lane: shared systems the story waits on

The systems lane in [story-lanes.md](story-lanes.md) owns the shared systems
the story depends on: message controls, name entry, the partner follower, the
story's menus and the waits the scripts block on. This page records what is
ported, the evidence for it and what is still open. The branch is
`claude/lane-systems-948a00`.

## Messages

`src/story.c` interprets every entry of the original control table
`msgctrlcode` (`data_80363774.c`, 0x5F entries), with the parameter bytes
each callback in `msgctrl.c` consumes:

| Controls | Original | Port |
| --- | --- | --- |
| 00, 01 | `msgctrlCR` | newline |
| 02 | `msgctrlKeyEnd` (stop bit) | ends the message: nothing after it is shown |
| 03 | `msgctrlKeyWait` | page break |
| 04, 05, 06 | ruby start / top / end | base text kept, reading dropped |
| 07, 08, 09, 38, 53 | font, RGBA colour, wait frames, palette colour, alignment | markup for the text box |
| 52, 54, 5B, 5C | shadow, talk sound, line space, baseline bias | parameter consumed, no text |
| 0A-0C, 3A-3C, 3D-40, 5A | no callback, sound on/off, indent off | no text |
| 5D, 5E | `msgctrlSndPlay` / `msgctrlSndWait` | markup |
| every other entry | mode 1 (prints a string) or mode 2 (prints a message ID) | asks the caller's value source; a message ID is printed in place, up to three levels as in `GSmsgDispatchControl` |

- **Rui's question marks.** "Rui became a partner!" (`M1_out` message 0x4F03) ends with control 02 followed by three full-width spaces. Control 02 now ends the message, so the padding is never drawn.
- **Number controls.** Digit, MenuDigit, the U/Z/hex digits, Money, MenuMoney, MenuFullDigit and Time are formatted as `_msgctrlMakeDigit` does (`c3_story_format_number`).
- **Field value source.** It answers every value control a field message uses:
  - hero and partner names;
  - the speaker, or the NPC message 59 names;
  - MsgID and MenuMsgID;
  - item and species names, from the PDA tables through the scene's namer;
  - move names;
  - trainer-class names;
  - string variables natives store.
- **Markup.** Field text is rendered with `C3_STORY_TEXT_MARKUP`, which keeps colour, palette, wait, font, alignment and sound as ESC sequences. The bottom-screen text box applies the colour and palette (the six-colour table `common_rel` registers) and skips the rest.

### Message sweep

```bash
python3 tools/prepare_message_sweep.py --disc EXTRACTED_DISC --output private/sweep/messages.c3ms
```

```bash
make build/test_message_sweep && ./build/test_message_sweep private/sweep/messages.c3ms
```

The pack holds all 162 message groups on the disc: every FSYS type-0A member plus the two `common_rel` groups. The test renders all 41,533 messages twice:

- with a source that answers every value control: **0 messages use a control the decoder can't interpret**;
- for field and menu groups, with only the controls the field source answers: **0 messages need a missing value**.

Battle messages need the battle text's own values (attacker, defender, trainer and similar), which are listed by control. Without the pack the test runs only its synthetic checks.

### Dialogue width

Original lines carry hard breaks sized for the GameCube window. The UI font's advances are half the source glyph width rounded up plus one pixel of spacing, so a line set at the GameCube's proportions runs about one pixel per glyph wider than half its original width. With the old 283 px text area and a glyph cell reserved at the end of each row, 212 of the 13,871 field lines wrapped with a six-letter speaker (TRUDLY). The wrap left one word, such as "with" or "here.", on a row of its own.

The text box now works like this:

- the message window spans the header's width, and its text is `C3_UI_MESSAGE_WIDTH` (294 px);
- a hard line fits by its visible ink, not by a reserved 16 px cell;
- a hard line that is still too wide gives up spacing: two pixels per word space first, then one pixel per glyph, spread evenly;
- only a line too wide even then wraps at spaces, by the same ink rule: a word moves to the next row when its visible glyphs would pass the width. A word is split only when it alone is wider than the row. No text is dropped. (The first version kept the old 16 px end cell for each glyph while measuring words by their advances, so the widened window broke the port's save prompt inside "progress".)

Pass the UI pack as well to check every field line:

```bash
./build/test_message_sweep private/sweep/messages.c3ms boot/ui.c3ui
```

`make test` passes it as `C3_UI_PACK`. The sweep lays out every hard line of every field page at that width, with the speaker control printing FUN OLD LADY (97 px, wider than 95% of the original person names), and requires one row each: 0 of 13,871 lines wrap. With either speaker, no row of any field page may end inside a word (0). The port's own paragraphs (save, return and preview prompts, title options, P*DA notices) are checked at the width and row count each is drawn with. With MYSTERY TROOP, the widest original person name (110 px), 21 lines still wrap; the sweep reports them as information. Five original pages have more than five lines. They are system texts, which the pager splits.

### Original font

`tools/prepare_bottom_ui.py` writes UI pack version 3 (`boot/ui.c3ui`). It appends the main font's 315 non-ASCII glyphs to the ASCII table: accented letters, gender signs, arrows, kana and full-width forms.

- The text box decodes UTF-8 and draws POKéMON's é with its own glyph.
- A code the font lacks advances a blank cell, as `gs_msg.c` does.
- Version 2 packs still load; non-ASCII text then shows blank cells.

On Azahar (save 35188, Phenac POKéMON CENTER) four people's conversations played to their end, and POKéMON is drawn with the original é.

## Name entry (native 16, floor 0x390)

`src/name_entry.c` follows `menuNameEntry` (`menuNameEntry.c`). Its text and keys come from the original message group 02FD0A00 of `name_entry_menu.fsys`, prepared as `boot/name_entry.c3m` by `tools/prepare_menu_messages.py`.

- **Presets first.** The hero and partner first get the preset names of `lbl_80266DD8`: WES/SETH/THOMAS and RUI/ANCA/YUKI, plus NEW NAME. A preset is confirmed with "Is <name> OK?".
- **Keyboard.** NEW NAME or B opens the keyboard:
  - the UPPER and lower pages, four rows of fifteen cells, where spaces are gaps;
  - R switches pages;
  - UPPER, lower, BACK and DONE buttons.
- **Starting name.** The keyboard starts from the first preset (`nameBuf` for modes 0/1), so the partner no longer starts from the N/A placeholder.
- **Length limits.** These are the original ones: 7 for the hero and partner, 10 for a nickname, 8 for a box.
- **Storage.** Names are stored as GSchars and may use the font's symbols (♂ ♀ and others).

On Azahar (probe line `name 1`, save 35188) the list showed RUI/ANCA/YUKI/NEW NAME, the keyboard started from RUI, and the stored name was "RUIAa". The 3D back panel (people model) and the window animation are not drawn. The field scene stops with an error naming `boot/name_entry.c3m` if that file is missing.

## Partner follower (person 0,101)

`src/hero_follow.c` ports `hero_move.c`'s follower:

- `moveLeader` keeps a 20-entry record of the leader's path, adding a point every 12 source units;
- `fn_8012D7F0` steps the follower towards the leader while she is nearer than the newest record, and otherwise towards that record;
- the step stops on the 12-unit spacing circle (`fn_8012D39C`), backs off inside 9 units, and moves at most one source unit per frame;
- `fn_8012DE94` catches her up when she has been more than 100 units away for 300 frames.

The field scene loads `<prefix>_partner.c3t` with the room, and the story attaches it as person (0,101):

- she is shown when the save's partner byte is set, and native 23 places her behind Wes when she joins;
- she is drawn and animated with the other people, and natives that address (0,101) move her;
- source motions map through the pack's clip order.

Evidence:

- **Host:** after Phenac's partner event she closes to 1.2 units (twelve source units) behind Wes as he walks (`test_phenac_story`).
- **Azahar:** save 35188, probe `walk`. She stood behind Wes in the POKéMON CENTER and followed him to the PC, 1.2 units back. On Phenac's exterior she followed him through the door into house C.

Every prepared floor except Outskirt and its shop has a partner pack, Snagem included.

Not ported: neck/head tracking (native 7E), `fn_801C7730`'s step aside (native E8), turning speed limits and walk/run blending (she uses idle, walk and run clips).

## Menu floors (native 62)

Native 62 is `fn_800FF730(floor)`, which pushes a menu floor and yields until it returns.

- A floor the scene enables (`c3_field_story_enable_menu_floor`) waits with `view.menu_floor` set.
- Any other floor returns at once and is recorded as not ported.
- Before this change, using the PC (common script 0596000D) stopped the game with "Unported native 62".

### PC box (floor 0x391)

`src/pc_menu.c` shows the PC with the common group's texts (0x1B59-0x1B95):

- the top menu: POKéMON Storage / Item Storage / Save / Exit;
- the party beside the current BOX, with L/R switching boxes;
- Deposit, with "Deposit in which BOX?", and Withdraw;
- the original refusals: "The BOX is full.", "That's your last POKéMON!" and "Your party's full.".

Save uses the scene's save. The data layer is `pcbox.c` through `game_state`: three boxes of thirty at body 0xB88, store, deposit and withdraw.

On Azahar (save 35188, Phenac POKéMON CENTER PC) the probe opened the PC, deposited a party member into BOX2 (26 → 27), withdrew one (27 → 26) and exited, and the PC script resumed.

Not ported: Item Storage (shown disabled), Move, Mark, Release, Summary, box naming, and the original floor's 3D box view.

### Not yet ported

These menu floors still return at once:

- the bag 0x38F (the existing in-game menu and inventory remain);
- P*DA and mail 0x392;
- evolution 0x386;
- the strategy memo.

## Waits

**Jingles.** Native 82 (`fn_80165668`) and item gifts (native 77, jingle 0x3CA) play the jingle through the audio service and wait until `fn_801666BC` stops reporting it, bounded at ten seconds. The music then resumes.

- The jingles 0x3C9 (3.3 s) and 0x3CA (2.1 s) are one-shot songs. The installed music pack already holds them (`w36_s34.c3m`, `w37_s35.c3m`). `tools/merge_music_index.py` can add other `prepare_music.py --sounds` output to a pack.
- On the host, Rui's partner jingle held her event 3.32 s and the trainer's gift jingle 2.12 s.
- Without the streams, the old fixed waits (2 s, 1 s) remain, recorded.

**Other natives:**

| Native | Function | State |
| --- | --- | --- |
| 7E | `heroMoveSetNeckMode` | returns the original result; head tracking not drawn |
| 83 | `fn_80181EB0`, the "!" emote | recorded, not drawn |
| 89 | `fn_801656F8`, a sound wrapper | recorded |
| DA | `scriptSetPokemonNakigoe`, a Pokémon cry | recorded |

None of the last three blocks a script.

## Probe route lines (`C3_ROUNDTRIP_PROBE`)

Each dialogue page is saved as `probe/msg_<floor>_<id>_<n>.bmp`, with `probe_message ... text="..."` in the trace. New lines in `probe-route.txt`:

- `<floor> p<person>`: stand in front of a person (by area index) and talk.
- `flag <id> <value>`, `partner <0|1>`: story state written before the first floor, in memory only.
- `name <mode>`: open the name entry and play NEW NAME, a key, R, a key and START.
- `walk <floor> <frames> <x> <y>`: hold the Circle Pad (decimal −127..127).
- The PC (`<floor> i1` at Phenac POKéMON CENTER) plays Storage, deposit, withdraw and Exit, with a bottom-screen capture per step.

## SD files

| File | Made by |
| --- | --- |
| `boot/ui.c3ui` | `prepare_bottom_ui.py` (version 3) |
| `boot/name_entry.c3m` | `prepare_menu_messages.py` |

The partner packs (`*_partner.c3t`) and the jingle streams are already on the SD.
