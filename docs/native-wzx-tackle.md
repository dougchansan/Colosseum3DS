# Original Tackle profile

`tools/prepare_wzx_tackle.py` verifies pinned Colosseum commit
`51bfe4314a218dfaf9592fc80723c50498d0ee55`, the original DOL lifecycle/script
slices, both WZX archives/members, and the original three actor PKX mappings.
It exports private `tackle.c3w` (C3TK v1, 1024 bytes), `tackle.c3p` and unchanged
`tackle.gpt`. It does not regenerate HSD or native actor/model packs.

The descriptor binds the source commit, both WZX member SHA256 identities,
the original GPT1 SHA256, the converted pack SHA1, the caster/target PKX and
HSD SHA1 identities, original node words, and target body-map2 anchors. The
native decoder checks exact admitted values and checksum, reserved bytes and
length before publishing its output. Failed decode/load leaves output intact.

Run preparation with an unused private output directory:

```powershell
python tools/prepare_wzx_tackle.py --disc <original-disc> --parser vendor/hsd-importer --output <private-output>
```

## Source timing and ownership

Attack resource `3A4/072E2000` links control and SE4A2 at source tick130. Its
control0 delay3 stops the whole attack WZX at133. The caster is Zigzagoon
entry2/clip4, raw timing `[0,62,130,240]`, authored clip end120 at rate.5.
Authored240 is metadata, not permission to retain attack motion through240.

For the successful ordinary callback, defender mode2 start calls
`ResetAnimationExcept` (DBCCC → DB864 → DEF0C) before defender motion starts.
The runtime exposes `reset_except_requests`; stage integration must apply the
source owner16 and flags18 bit8 guards to other active model owners. The
shared reset must not be generalized to caster-only moves or misses.

Defender WZX `230/03A42000` starts its particle bank and SEDE at phase tick0.
Common script09 then0A05 waits for normalized WZX type5 gate0 before0B12 HP
application. Damage node flags2 specify one-shot attachment. Its bank payload
flags1 are a distinct field. Root0 reaches all eight original descriptors;
the raw bank is91680 bytes. The explicit `prepare_particle.bank(...,
profile='tackle')` gate admits shape4 only for original generator6 and pins
the complete bank identity; existing generic profiles retain their behavior.

Body-map2 resolves to **original** source parts42 (Umbreon) and62 (Espeon),
sampled from their hit clips at frame0. These are not compact native bone
indices. The descriptor provides native-unit translation anchors (.1 original source unit); integration
must apply the admitted actor world transform, convert the resulting native point back to source units for the particle engine, and apply generator class scale once.
It must not use an idle pose or Bite's linked-model part12 anchor.

## Completion and API

`begin` chooses target0/Umbreon or1/Espeon. `step` advances in milliseconds*60
units (1000 per source tick), preserving crossed attack events. `defend(hit)`
requires attack completion and explicitly hands off; phase events fire at0.
Elapsed beyond133 is retained in `handoff_units` and transferred to the
defender clock at this handoff. Integration must advance the new particle/audio
consumers for that retained elapsed as appropriate; it must not advance the
old attack consumers after their stop event. The stage should hand off at once
when the source hit callback is available. Miss completes only this bounded
profile and emits no bank, DE, HP or reset request; special57 remains separate.

The completion predicate follows `wazaSequenceUpdate` B988:

| Target | Native hit clip | Type1 normal | Type2 minimum | Authored end |
| --- | ---: | ---: | ---: | ---: |
| Umbreon197 | 3 | 170 | 298 | 149 |
| Espeon196 | 2 | 100 | 176 | 88 |

Normal170/100 is also the separate camera duration and is not the normal
sequence terminal. DA9E8 clears owner16/17 on start. With these admitted flags,
`finish(..., particles_done, sound_done, 0, 0)` requires type2 minimum **and**
both consumers complete. Source PS completion includes generators and surviving
particles (EntryUpdate checks both bank predicates). Direct SE statuses2/3
remain pending. Callers must retain actual sound completion state.

The API preserves the source external interruption branch: when owner16 is0
and owner17 is nonzero, strictly `normal < current_time` can stop early before
the resource checks. Passing owner flags without this source meaning changes
the completion semantics. It does not wait for a copied Bite190/380 terminal.

Finish/cancel requests whole owned particle-family teardown and direct SE
keyoff; these are observable monotonic requests the caller consumes once.
Cancel is idempotent. Keyoff does not prove immediate original DSP silence.
Oversized elapsed fails atomically rather than dropping elapsed. Actor sampling
clamps to authored minimum; stage owns the natural idle transition.

## Verification and limits

`make wzx-tackle-check` covers descriptor mutation, source pin/typed table and
reserved-byte rejection, attack130/133 boundaries, successful and missed
handoffs, minimum/consumer completion, strict early-return conditions,
overflow atomicity and cancellation. The executable optionally accepts a
prepared descriptor path to verify actual private preparation output.
`tests/test_prepare_wzx_tackle.py` checks source hashes and preparation refusal
boundaries. Existing particle preparation regression tests remain applicable.

Camera, selected dry audio, original shared RNG, frame-handoff retail parity,
GPU rasterization and hardware presentation are separate qualifications.


## Native stage qualification

The optional `tackle.c3w` activates strict loading of its matching C3P, selected
sounds4A2/DE from the13-cue audio pack, and the explicit Tackle camera. The
bounded successful callback supports Zigzagoon slots2/3 to committed Umbreon/
Espeon slots0/1. Production miss handling never starts the normal Tackle
profile, either cue, its camera, bank or shared reset: it retains the existing
actor-only fallback and reports source special57 as unsupported. The pure
clock `defend(hit=0)` refusal path is not the production miss dispatch.

The stage keeps node flags2 distinct from bank flags1. Anchors are native
translations (.1 original source unit); it transforms the original frame0
bodymap2 point into world space, converts it back by10 for source particles,
and applies generator class scale.75 independently. Radius scaling remains1.
Camera and particle unit APIs consume the20-unit fractional handoff at133
without dropping or double advancing it. The historical motion carrier uses
states3/cast,4/hit and2/idle with authored ends240/298/176.

ResetExcept accepts known live Pokemon motions and idle trainer owners,
preserves existing idle frames, skips only a completed zero-HP hidden faint owner (source mode3 clears its owner pool record), and rejects other hidden/fainted/unknown-owner flags
rather than fabricating their source state. Accepted cue identities survive
partial failures for cleanup. Families and camera are canceled before close;
GPU BUSY retains all backing storage until the renderer drains.

The actual-resource harness covers both casters and targets,130/133 events,
HP0 and faint gating, one-shot source parts distinct from compact bones,
minimum plus particle and natural FINISHED audio completion, pause, miss,
partial cue/status/STOPPED/binder/keyoff errors, reset refusals and BUSY/repeated
close. Partitions1/17/1000 preserve phase completion298040,134 births,8
generators, peak133 and964 cosmetic draws for its fixed event seed. The bank
first finishes naturally at104 source ticks in the separate core test; the
stage may keep advancing its empty particle clock while waiting for the owner
minimum. Display mutates source order, so this is not a whole-pool attribute or
pixel parity claim across display cadences. Actual visible POINT and quad
geometry and texture poses are checked through production geometry kernels.


### Sequential preview lifecycle regression

A completed Bite leaves its committed-hit marker available for matching later
HP events. That marker must not exempt the former damage owner from a later
FAINT animation wait. The generic stage now delegates that owner clock only
while the damage resource is active; after it finishes, the authored faint
clip remains the barrier before the model becomes hidden and its source owner
is treated as released.

The actual-resource regression submits Bite with lethal HP, FAINT, Confusion
against the survivor, and then the surviving Zigzagoon's Tackle in one
continuous committed event batch. It exercises either defeated enemy slot,
checks the real faint clip completed before hiding, and then reaches Tackle
ResetExcept without manually supplying a completed-faint pose. The exact
previous stage implementation fails the new faint-completion assertion; the
corrected stage passes. This keeps the narrow released-owner predicate intact.
