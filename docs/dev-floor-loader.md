# Developer floor loader (Phenac City)

**This is a developer path, not story progress.** It shows an original floor
that the native story route does not reach yet: Phenac City's exterior
(floor `0x03`, `M1_out.fsys`) with Wes, the original map, the source camera
defaults and the original CCD collision. See
[phenac-floors.md](phenac-floors.md) for how the floor was identified.

It is off by default. Normal and `C3_BOOT_ONLY` builds contain no loader code
path. It is compiled only with `C3_DEV_FLOOR=1`, which builds into its own
object directory and names the homebrew entry `Colosseum3DS DEV FLOOR`.

## What it guarantees

- `c3_dev_floor_scene_run(trace, error, capacity)` takes no game state. It
  cannot write saves, set story flags, call GSVM natives, touch native 98 or
  produce battle results. The boot loop calls it without passing `state` and
  then returns to the title menu.
- The trace records `dev_floor_begin ... game_state=none save_writes=0
  story_flags=0` and the same counters at `dev_floor_end`.
- Packs live in their own SD directory, `/3ds/Colosseum3DS/dev-floor/`, away
  from the story `boot/` assets. A missing or mismatched pack is an explicit
  error, never a silent fallback.
- `c3_dev_floor_validate` accepts only the profile's floor and map resource,
  a separate Wes pack and zero arrival packs.

## Supported and not supported

| Supported | Not supported |
| --- | --- |
| Original Phenac exterior map: all nine models, five lights, map animation clocks, slow sky | NPCs (the 27 person records), dialogue, field scripts (bank `013E0E00`) |
| Wes idle/walk/run with the original motion, distances and radius | Doorways, floor exits and interiors |
| Source follow camera from the map camera (eye offset (0, 14, 11.89), FOV 30°) | Event cameras, the world-map step |
| Original CCD walls (914) and walk surfaces (683) across 46 objects | Moving CCD objects, NPC bodies, original spatial index |
| All ten original entrances, cycled with L/R | Which entrance the world-map arrival uses |
| | Particles, BGM/SFX, battles, menus, save |

Map fidelity limits (all recorded in the private manifest):

- Models 0–5 carry two authored 20-frame clips; clip 0 is played.
- HSD instance joints are drawn: model 7's four copies and the seven copies of
  model 6's joint-161 subtree animate. The seven copies of model 6's joint-135
  subtree (a skinned, IK-animated subtree with ~60 envelope tracks per copy)
  are drawn frozen at their first sample, because animated copies would need
  one 64-bone pack each. The structural joint-135 subtree itself animates.
- Static meshes are baked into one-sample packs, which changes draw order
  only between the static and animated halves of a clock group.
- GX materials go through the native PICA subset, as for Outskirt Stand.

## Prepare the private packs

```sh
python3 tools/prepare_floor.py --disc /path/to/GC6E01 --output private/phenac
```

The archive is admitted by exact size and SHA256 before decompression.
Output must be a new directory, inside ignored `private/` or outside the
repository. The run writes `phenac.c3f`, `phenac_map0.c3t`–`phenac_map7.c3t`,
`phenac_wes.c3t` and `phenac-manifest.json` (34,482,792 bytes of packs plus
the 89,928-byte C3F on the 2026-11-09 run). Copy every `phenac*.c3f/.c3t`
file to `/3ds/Colosseum3DS/dev-floor/`. Keep the manifest private.

| Pack | Contents | Vertices | Sampling |
| --- | --- | ---: | --- |
| `phenac_map0` | Models 0–5, 7 static parts | 13,110 | 1 sample |
| `phenac_map1` | Models 0–5, 7 animated parts | 504 | 41 samples (20 frames) |
| `phenac_map2`–`4` | Model 6 static parts | 48,294 | 1 sample |
| `phenac_map5`–`6` | Model 6 animated parts | 9,990 | 599 samples (299 frames) |
| `phenac_map7` | Model 8 sky | 468 | 1,001 samples over 24,999 frames |
| `phenac_wes` | Wes (`field_common.fsys` `00F70400`) | 2,445 | idle/walk/run/motion0 |

The C3F uses the existing C3F1 layout with floor 3, map resource
`00620200`, ten entrances and eight map packs. The loader's map-pack bound is
now 47 (`C3_FIELD_MAX_MODEL_PACKS`: 48 stage models minus Wes;
`tools/prepare_floor.py` still writes at most 15); the story field scene still
checks its complete model count.

## Converter changes

`tools/prepare_floor.py` is a profile-driven generalisation of
`prepare_outskirt.py`. `tools/prepare_field_geometry.py` gained, all opt-in or
inert for floors without these features:

- `joint_nodes`: the HSD joint order that does not descend into an instance
  joint's child (`HSD_JObjResolveRefsAll`, matching the pinned importer).
- Instance admission (`allow_instances=True`) and `instance_meshes`: one extra
  draw of the instance child's subtree with
  `instance.mtx × inverse(child.mtx)` (`JObjSetupInstanceMtx`, `HSD_JObjDispAll`).
- Targetless IK chains: without the effector's JObj RObj,
  `jobj_get_effector_checked` returns NULL, so joint1/joint2 keep FK and only
  the effector is placed along joint2's X axis.
- The `BLEND(1)` + inactive-TEV `MODULATE` two-texture material, serialized
  exactly as the dual-modulate stage with konst 1 and a white raster.
- `split_static` and `bake_mesh`, and a 32-texture bound in pack partitioning.

Regenerating the Outskirt exterior (8 files) and shop/NPC area (14 files)
with the modified tools gives byte-identical output to unmodified HEAD tools.

## Build and use

```sh
make -f Makefile.3ds -j4 C3_DEV_FLOOR=1
```

The output is `build-3ds-devfloor/Colosseum3DS.3dsx`. At PRESS START the
bottom screen reads `DEVELOPER BUILD`; press **Y** for the loader
(A/START still starts the normal game). The bottom screen shows
`DEVELOPER FLOOR LOADER`, the floor, the notice
`Not story progress. Nothing is saved.`, the entrance and Wes's position.

- Circle Pad / D-pad: move (run by stick magnitude, as in the field).
- L / R: previous / next original entrance.
- START: leave to the title menu.

## Validation

- `build/test_dev_floor`: synthetic profile, path, validation, entrance and
  actor-placement checks; with `C3_DEV_FLOOR_PACK=<private>/phenac.c3f` it also
  loads the real pack, spawns at all ten entrances (all grounded) and moves.
- `tests/test_field.c`: the new map-pack bound.
- `tests/test_prepare_floor.py`: profile, sky schedule, instance walk/matrix
  and baking.
- Host strict and ASan/UBSan suites, and default plus `C3_DEV_FLOOR=1` ARM
  builds. The default ELF contains no `dev_floor` symbols at all (the
  stub and core are discarded by `--gc-sections`); the developer ELF has the
  scene and core. 2026-11-10 hashes: default `.3dsx` `bbe2857a…`,
  developer `.3dsx` `ee0b9169…` (build ID `5ad8c1e…-wip`, before commit).

Physical New 3DS acceptance has not been run for this loader.
