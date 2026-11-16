# First-person field view (optional)

A port of the first-person mode of the PC recomp's camera mod
(`pkmn-colo-recomp/mods/freecam_mod.c`, documented in `mods/FREECAM.md`
there). It is not original game behaviour and is off by default.

| Control | Action |
| --- | --- |
| Hold ZL or ZR (or R on an original 3DS) + press X | Toggle first-person view |
| C-stick (New 3DS) | Look |
| Drag on the bottom screen (any 3DS) | Look |
| Circle Pad | Walk in the direction you are looking |
| D-pad Up / Down | Narrow / widen the field of view (30-100 degrees) |
| D-pad Left / Right | Lower / raise the eye (4-40 original units) |
| Hold L + D-pad Up / Down | Shorten / lengthen the view distance (0.25x-4x the floor's far clip) |
| Hold ZL or ZR (or R) + D-pad Down | Recenter; also resets field of view, eye height and distance |

X alone still opens the menu. The chord is checked first and its buttons are
consumed, so holding ZL/ZR/R before X never opens the menu.

## Behaviour, and how it matches the mod

- Eye at the player's ground transform, 14 original units up and 2 forward
  (the mod's `FIRST_PERSON_EYE_HEIGHT` and forward offset), scaled by the
  field pack's unit scale.
- Look speed 3.5 rad/s at full stick (raised from the mod's 2 after hardware testing), stick deadzone 15/127 (the mod's 12 on
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

## Tuned view (port addition)

Field of view, eye height and view distance can be changed while the view
is on (table above). They start from the floor camera's field of view, the
mod's 14-unit eye and the floor's far plane, change smoothly while the
button is held, are kept across doors and floors, and reset with the
recenter chord. In first person the D-pad adjusts the view instead of
walking; walk with the Circle Pad. The debug overlay's second scene line
shows `fp fov <degrees> eye <units> dist <factor>`.

A depth-of-field blur is not provided: the stage renderer has no blur
pass, and one would need an off-screen render and a filtered composite each
frame.
