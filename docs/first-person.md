# First-person field view (optional)

A port of the first-person mode of the PC recomp's camera mod
(`pkmn-colo-recomp/mods/freecam_mod.c`, documented in `mods/FREECAM.md`
there). It is not original game behaviour and is off by default.

| Control | Action |
| --- | --- |
| Hold ZL or ZR (or R on an original 3DS) + press X | Toggle first-person view |
| C-stick (New 3DS) | Look |
| Drag on the bottom screen (any 3DS) | Look |
| Circle Pad / D-pad | Walk in the direction you are looking |

X alone still opens the menu. The chord is checked first and its buttons are
consumed, so holding ZL/ZR/R before X never opens the menu.

## Behaviour, and how it matches the mod

- Eye at the player's ground transform, 14 original units up and 2 forward
  (the mod's `FIRST_PERSON_EYE_HEIGHT` and forward offset), scaled by the
  field pack's unit scale.
- Look speed 2 rad/s at full stick, stick deadzone 15/127 (the mod's 12 on
  the GameCube's ±100 range), pitch limited short of vertical. Touch drag is a
  3DS addition for consoles without a C-stick: 0.012 rad per pixel.
- Entering starts level and facing the player's direction. A new floor
  restarts level and facing the spawn direction (the mod's arrival rule). The
  mode itself survives doors and floor loads.
- Scripted cameras, arrivals, dialogue and menus suspend the view. The game's
  camera runs unchanged and the look resumes when free control returns.
- The near plane is capped at one original unit so the ground just ahead is
  not clipped.
- Difference: the mod collapses only Wes's head joint so the body stays
  visible looking down. Here the whole Wes model is hidden while the view is
  active. Per-joint hiding needs a renderer hook that does not exist yet.
- No camera collision, as in the mod: tight rooms can clip through walls.

The logic is in `src/first_person.c` with host tests in
`tests/test_first_person.c`; `platform/3ds/field_scene.c` wires the input,
movement yaw and camera.
