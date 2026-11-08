# Particle command admission

The shared Citro3D context allocates a 1 MiB command buffer. This adds 768 KiB
of driver memory over the SDK default; scene-owned vertex/texture statistics
exclude that allocation. All scenes use this same context.

Pinned Citro3D 1.7.1 and libctru 2.7.0 emit 114 command words for each steady
particle draw: six TEV stages (36), effect state (34), two texture bindings and
texture status (22), and DrawArrays (22). Initial context/uniform setup is
additional. The renderer preserves particle order and emission counts.

Before particle GPU state or draw commands, admission requires:

```
current_words + visible_quads * 192 + 8192 <= 262144
```

The live SDK command cursor supplies current_words; GetCmdBufUsage is updated
only at split/end. The 8192-word reserve covers following HUD/fade commands and
finalization. Failure closes the acquired frame and returns an ordinary renderer
error. It does not truncate particles. The full 1536-slot pool is not automatically
admitted; admission uses visible quads and remaining capacity.

A host simulation of the admitted Reflect bank with cosmetic seed123 measured
1133 visible quads at tick80 (215 alpha,918 additive) and at most850 adjacent
identical-state runs. At1133 quads the conservative check leaves36416 words for
preceding scene commands. These are CPU/source measurements, not hardware timing
or visual acceptance. Source-preserving adjacent batching remains a possible
optimization; globally sorting transparent particles is not admitted.

The render statistics expose total command words after FrameEnd, particle-pass
command words, and capacity words. Command-buffer memory is reported separately
through the explicit allocation constant, not added to owned geometry bytes.

Source authority: [Citro3D command/context](https://github.com/devkitPro/citro3d/blob/v1.7.1/source/base.c),
[draw commands](https://github.com/devkitPro/citro3d/blob/v1.7.1/source/drawArrays.c),
[libctru buffer bounds](https://github.com/devkitPro/libctru/blob/v2.7.0/libctru/source/gpu/gpu.c).
Neither DrawArrays nor command append automatically recycles a full buffer;
FrameSplit advances the buffer pointer and reduces remaining capacity.

Up to two distinct borrowed banks may be registered before the first draw.
Their texture indices remain separate; the one live particle system selects its
registered pack through bank.bytes identity. Registering the same pack pointer
again is idempotent; aliasing its bytes through another pack is rejected.
Shutdown clears both bindings only after the normal GPU drain.
