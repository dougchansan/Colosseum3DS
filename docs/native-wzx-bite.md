# Native Bite attack slice and immutable arena staging

This update starts executing the original Bite attack's timed effect model and
floor-color entry. It is **not a complete WZX/GPT1 interpreter, complete Bite,
or a retail-matching battle**. The existing preview remains nonpersistent;
native98 is suspended and combat, RNG, reward and save APIs are unchanged.

## Source-derived execution

`tools/prepare_wzx_bite.py` verifies the existing disc/source/importer pins and
Bite attack member `0x034B2000` from `wzx_kamituku_attack.fsys`. Its SHA-256 is
`0cb9912dcbe7c4626d63789a34cfe1a36e869a0878454b8cde878434814216f7`.
`config/wzx-bite-source-files.json` pins the source code used to interpret it.
The admitted four-entry profile consists of floor color, stop control, an
embedded HSD model, and an explicitly unsupported sound entry. Unknown profiles
fail preparation instead of becoming successful no-ops.

The preparer follows `wazaSequenceEntryLink`'s owner/linked-entry time arithmetic,
not an English move-name heuristic. Umbreon's PKX entry 2 resolves to clip 5,
owner timing values 72 / 100 / 160 / 0, and source body-map part 59. The relevant
`GSmodelSetAnimRate` constants in the canonical DOL are both 0.5.

| Original timeline (60 ticks/second) | Admitted behavior |
| --- | --- |
| 0 | Start the authored owner clip at its source-derived rate |
| 72 | Begin the floor RGB transition, lasting 60 ticks |
| 142 | Start embedded HSD model entry 3 and record unsupported sound `0x85` |
| 160 + 5 | Stop the sequence, hide the effect, restore floor state and idle |

The model DAT is at member offset `0x260`, length 29,799. The source one-shot
attachment is baked from part 59 at the start's authored frame (142 * 0.5).
The runtime applies the owning actor's root transform, rather than casting a
PPC address into an ARM pointer. This narrowly admitted fixed-owner attachment
does not implement general dynamic bone links or other actors' variations.

`src/wzx_bite.c` owns a bounded integer clock (milliseconds * 60), crossed-entry
counts and visual state. A long step retains elapsed time and observes each
crossed start once, although intermediate rendered samples may be omitted.
No combat RNG is read or advanced. Stop/cancellation clears the effect/floor
ownership. Other moves retain their previous development presentation pacing;
this is not a global retail timing qualification.

The embedded model's constant HSD RGB material channels are checked and folded
with the original clamped-byte conversion. Dynamic alpha remains sampled.
The shared field preparer retains its stricter default: this RGB admission is
opt-in, rejects nonconstant values/tangents, and does not silently flatten a
dynamic color animation.

Floor modulation uses the source material expression `2 * previous * color`,
with alpha unchanged. The shader stage applies it after the existing diffuse
and specular paths; GPU cache data remains independent of this modulation.
The existing multipass specular saturation limitations still apply. Original
pixel parity, whole-sequence start phase and retail camera/audio timing are
not established by the host tests below.

## Prepare and install private additions

From the repository root, using a **new** output directory:

```sh
python3 tools/prepare_wzx_bite.py --disc /private/extracted-disc \
  --parser vendor/hsd-importer --output /private/bite-addon
```

Install only `bite.c3w` and `bite_attack.c3t` beside the existing `scene.c3v` in
`sdmc:/3ds/Colosseum3DS/boot/battle/`. Preserve the current assets and saves;
use staged FTP transfer and readback verification. Keep the generated `bite.json`
provenance report private. Original/prepared bytes are not committed.

A missing descriptor preserves the accepted actor-only mode. A present but
invalid descriptor, inaccessible file, missing effect pack or hash mismatch
fails explicitly. The descriptor binds both the source profile and exact model
SHA-1. These checks detect corruption, not malicious provenance; local original
verification and the retained preparation report establish input provenance.

The canonical preparation performed for this update produced:

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `bite.c3w` | 256 | `e00ced8474cef7c13b3038137d46344c80c5186c6ac894fc58ec1d19c3bf1643` |
| `bite_attack.c3t` | 189,680 | `1052599180286a00f570e3870c367ccc6f7b81a9a36fe78c56ac82a58c1c3e87` |

The model has 1,668 vertices, four batches, two textures and 69 sampled poses.
The production loader reports 353,696 bytes of resident CPU model storage.
A platform-specific converter result must retain its actual identity rather
than being relabeled with these hashes.

## Rendering-work reduction

The stage can explicitly mark a one-frame arena pack immutable. Its pose/root
transform is sampled once, and the renderer reuses its prepared vertex bytes
without skipping its draw calls. Only `arena1_0` (9,600 vertices) qualifies in
the current canonical scene; the other two arena packs remain animated.
Identity root transforms also avoid unnecessary per-vertex transformation.

Specular/reflection staging keys include the view matrix. Disabling the static
contract, changing relevant view state, television configuration, or destroying
and recreating a scene invalidates the cache. Only dirty model ranges are
flushed. Dynamic actors/effects remain uncached. The caller must disable the
contract **before** changing opted-in geometry, materials, visibility or lights.
Floor modulation is shader-side and does not require CPU restaging.

`battle_render` now includes `vertices_prepared` and `cached_models`, separate
from submitted vertices/draw calls. No hardware speedup or FPS is claimed yet.
The prior 80.532-ms median physical CPU draw measurement belongs to the previous
binary; it is not an after-measurement for these changes.

## Executed validation and its limits

In the materialized full-source sandbox:

- All 23 native suites executed successfully, normally and under ASan/UBSan
  with leak detection enabled.
- The Bite clock passed 66,283 synthetic checks, and 133,339 checks with the
  actual prepared descriptor, including bounded malformed inputs, fractional
  step partitioning, long steps, exactly-once starts and cancellation.
- Production C3T loading sampled the actual effect at 138 half-sample points.
- The optional production-stage test below loaded the complete original scene
  plus effect, executed Bite twice, verified timing, static arena byte stability,
  floor-only modulation and BUSY/idle cleanup. GPU calls are explicit test
  doubles: this is **not** a rendered or physical-console acceptance test.
- Renderer fixtures exercise cached staging, view invalidation, continued draw
  submission, teardown, RGB scale-two setup and alpha preservation.
- Original/prepared files, completed saves and emulator dependencies are excluded
  from the change. Source verification used materialized mode. Two existing
  Python checks require genuine GekkoCTR submodule Git metadata and cannot run
  in this archive checkout; full Git-checkout CI is the integration gate.

```sh
make build/test_wzx_bite build/test_wzx_bite_stage
./build/test_wzx_bite /private/bite-addon/bite.c3w
# Put both addon files alongside the existing private battle scene first.
./build/test_wzx_bite_stage /private/battle-scene
python3 -m unittest discover -s tests -p 'test_prepare_wzx_bite.py' -v
python3 -m unittest discover -s tests -p 'test_wzx_render_modulation.py' -v
```

The new effect is only admitted for the existing Umbreon/Bite preview event.
Other moves keep their accepted actor animation. Bite's damage GPT1 generator,
original sound `0x85`, battle camera sequences, ball effects, remaining moves,
original UI/audio/shadows and authoritative combat/writeback remain unfinished.
The development HUD continues to disclose this. Actual hardware rendering,
source-matched original-game comparison and streaming-off measurements are
required before claiming this slice visually matches retail.
