# Title screen side characters

This records how the original GC6E01 title screen picks, places and animates the
character artwork that slides in from the screen edges beside the logo, and how
the choice rotates each time the title is entered. Everything below comes from
the pinned decomp
([`gs_title_8001EF78.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_title_8001EF78.c),
[`gs_title.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/gs_title.c),
[`menu_bios_range_8005D7F8.c`](https://github.com/Count-Sheep/pkmn-colosseum/blob/51bfe4314a218dfaf9592fc80723c50498d0ee55/src/game/menu/menu_bios_range_8005D7F8.c))
and from the reviewed revision 0 `sys/main.dol` and `title.fsys`
(SHA-256 `8ae138e5…7b4797a`, the archive `tools/prepare_title.py` already admits).
No disc data is reproduced here: only table indices, IDs, coordinates and sizes.

## They are window sprites, not 3D models

The side characters are not drawn by the HSD model renderer. They are 19 window
sprite items, IDs `0xEFB`–`0xF0D`, in title menu `0x13`. Each item points at one
of 17 sprite records, and each sprite record names one whole RGB5A3 texture in
`title.fsys`. The textures are pre-rendered images, sized exactly to the sprite
(114–257 px wide, 140–236 px tall). Nothing about them is lit, posed or animated
at run time: the only motion is a horizontal slide.

So a faithful port draws these original textures as 2D quads. There is no
original camera, lighting or model animation for this element to port. This
differs from the brief, which assumed 3D models; see "Port decision" below.

## Data path

| Step | Source | Layout |
| --- | --- | --- |
| Menu | menu data `0x802E2DB8 + id*0x1C` | Menu `0x13`: first item `0xEFB`, origin (0, 0). Menu `0x15`: first item `0xEFA` (PRESS START), tick `fn_8001EF78` |
| Item | `menuItemBiosGetPtr`, `0x802EF0A8 + id*0x1C` | `+0` flags, `+2` s16 x, `+4` s16 y, `+6` w, `+8` h, `+0xA` sprite index, `+0x14` draw callback (`0x8001F304`), `+0x18` next item |
| Sprite | `menuSpriteBiosGetPtr`, `0x802E7CE8 + idx*0x18` | `+8` u, `+0xA` v, `+0xC` w, `+0xE` h, `+0x10` texture file ID |
| Texture | `title.fsys` member by file ID | GS texture header, format `0x90` (GX RGB5A3), data at 128; LZSS-compressed member |

Coordinates are GameCube screen pixels (640x480). Items chain `0xEFB → … → 0xF0D`
in ID order, then `0x2C3`; menu `0x15` holds `0xEFA` (PRESS START) and `0x2C4`.

## The four pages

`fn_8001F304` (the item draw callback) shows an item only when the title page
`lbl_80478878` equals the item's page, and places it at its item x plus one of
six animated column offsets. Columns 0–2 slide in from the right, columns 3–5
from the left.

| Page | Item | x, y | w x h | Sprite | Texture | Column |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | `0xEFB` | 34, 249 | 145 x 231 | `0x33C` | `0x100C1200` | 3 (left) |
| 0 | `0xEFC` | 0, 257 | 139 x 223 | `0x33B` | `0x10191200` | 4 (left) |
| 0 | `0xEFD` | 454, 250 | 170 x 230 | `0x33A` | `0x10181200` | 0 (right) |
| 0 | `0xEFE` | 505, 288 | 135 x 192 | `0x339` | `0x10171200` | 1 (right) |
| 1 | `0xEFF` | 0, 220 | 114 x 204 | `0x338` | `0x10161200` | 3 (left) |
| 1 | `0xF00` | 0, 275 | 257 x 205 | `0x337` | `0x10151200` | 4 (left) |
| 1 | `0xF01` | 0, 326 | 151 x 154 | `0x336` | `0x10141200` | 5 (left) |
| 1 | `0xF02` | 505, 220 | 135 x 188 | `0x335` | `0x10131200` | 0 (right) |
| 1 | `0xF03` | 449, 293 | 191 x 187 | `0x334` | `0x10121200` | 1 (right) |
| 1 | `0xF04` | 503, 302 | 137 x 178 | `0x333` | `0x10111200` | 2 (right) |
| 2 | `0xF05` | 0, 277 | 197 x 203 | `0x332` | `0x10101200` | 3 (left) |
| 2 | `0xF06` | 0, 225 | 139 x 140 | `0x331` | `0x100F1200` | 4 (left) |
| 2 | `0xF07` | 475, 187 | 165 x 223 | `0x330` | `0x100E1200` | 0 (right) |
| 2 | `0xF08` | 442, 292 | 198 x 188 | `0x32F` | `0x100D1200` | 1 (right) |
| 2 | `0xF09` | 501, 308 | 139 x 172 | `0x32E` | `0x101A1200` | 2 (right) |
| 3 | `0xF0A` | 441, 262 | 172 x 218 | `0x33E` | `0x101C1200` | 0 (right) |
| 3 | `0xF0B` | 499, 244 | 141 x 236 | `0x33D` | `0x101B1200` | 1 (right) |
| 3 | `0xF0C` | 34, 249 | 145 x 231 | `0x33C` | `0x100C1200` | 3 (left) |
| 3 | `0xF0D` | 0, 257 | 139 x 223 | `0x33B` | `0x10191200` | 4 (left) |

All sprite records use u = v = 0, so each item draws its whole texture. Pages
hold 4, 6, 5 and 4 images. Page 3 reuses page 0's two left-hand images with two
new right-hand ones, so the left pair of the first showing returns on every
fourth visit. The user's description (first showing: the two protagonists on
one side and two Pokémon on the other; second: a different Pokémon group;
third: the legendary group) matches pages 0, 1 and 2 in that order; the
identities above are by texture ID only.

## Cycle rule

- `lbl_80478878` is a RAM word in `.sdata` (`0x80478878`). Its initial value in
  `main.dol` is **3**. It is never read from or written to the memory card.
- `fn_8002049C` (GStitle init) adds 1 and wraps 4 to 0 each time the title floor
  (floor 900) starts. The first title after power-on therefore shows page 0, and
  each later entry shows the next page: 0, 1, 2, 3, 0, …
- Any re-entry of floor 900 advances it: returning from the main menu (Quit
  calls `fn_800205B8(1)` and links floor `0x384`, which also skips the logo
  intro through title state 7), returning from the attract demo (state `0x28`
  links floor `0x39C`), and a soft reset back to the title.
- A power cycle restarts at page 0.

## Slide-in motion

`fn_8002049C` resets the six column offsets and their targets on every title
entry:

| Column | 0 | 1 | 2 | 3 | 4 | 5 |
| --- | --- | --- | --- | --- | --- | --- |
| Start offset (px) | +300 | +400 | +500 | −300 | −400 | −500 |
| Target | 0 | 0 | 0 | 0 | 0 | 0 |

At these starts every item lies fully outside the 640-wide screen.

`fn_8001EF78` (menu `0x15` tick) eases each offset only while the title state
`lbl_8047A31C` is at least `0x1E` and not `0xC8`:

```
step = frameDelta / GSgfxVideoVsyncRate          (seconds this frame)
move = clamp(4 * (target - cur) * step, -6, +6)  (pixels)
cur += move
snap cur = target when |target - cur| <= |move| or |target - cur| < 1
```

So each column moves at a capped 6 px per game frame while far away, then decays
exponentially at rate 4/s, and stops once less than one pixel remains. The
farther-starting columns arrive later, which staggers the images. At 60 game
frames per second, column 0 reaches the target after about 100 frames (35 at the
cap, then about 65 of decay); columns 1 and 2 follow about 17 and 33 frames
later. The 6 px cap is per frame, so the cap phase depends on the title's actual
game frame rate, which should be confirmed against a Dolphin capture.

When the motion starts:

- Normal boot: title state 3 waits for logo clip 1 to finish, state 8 starts
  clip 2 and sets state `0x1E`; the slide starts with clip 2.
- A/B/START during the intro: states 4–7 fade out for 0.5 s, wait, start clip 2,
  fade in for 0.5 s (`fadeCheck(1)` blocks until done) and then set `0x1E`; the
  slide starts after the fade-in.
- Return from the main menu: title mode 1 enters at state 7, so it is the same as
  the skip path.

State `0x1E` moves at once to `0x20`, which opens menu `0x13` (these items) and
waits for PRESS START. After START the title waits 1 s (state `0x1F`) and goes to
state `0xC8`, where the offsets stop easing; the title then links the main-menu
floor `0x3A1`, whose top screen is the `topmenu.fsys` background, not the logo.
The side characters therefore appear only on the PRESS START screen.

Draw order follows the item chain, so later items in the table draw over earlier
ones. The draw callback sets no alpha for these items (only PRESS START blinks).

## Mapping to the port

The port's title (`platform/3ds/boot_scene.c`, `run_title`) keeps the logo on the
top screen while the main menu runs on the bottom, and "return to title" goes
back to `TITLE_WAIT_START` inside the same `run_title` call. To follow the
original:

- Keep a process-lifetime page counter initialised to 3; advance it when
  `run_title` starts and when the menu returns to the title.
- Reset the six offsets to the start values at the same points, and ease them
  while the phase is `TITLE_WAIT_START` or `TITLE_ACCEPTED` (state `0x1E`
  onwards). Use the frame's elapsed seconds as `step`.
- Hide the images during `TITLE_MENU`, since the original's main menu floor does
  not show them.
- On the 400x240 top screen the 640x480 layout scales by 0.5 to 320 px wide. To
  use the widescreen margins, anchor left columns to the left edge
  (`x' = x / 2`) and right columns to the right edge
  (`x' = 400 - (640 - x) / 2`), scaling offsets by 0.5 as well. This is a layout
  choice, not original behavior.

## Port decision

The brief asked for 3D models, animations, cameras and lighting. The original
has none of those for this element: it slides 17 pre-rendered 2D textures.
Porting it faithfully means drawing those textures, which conflicts with the
standing "no 2D stand-ins" rule only in wording; they are the original assets,
not invented ones. That choice belongs to the user.
