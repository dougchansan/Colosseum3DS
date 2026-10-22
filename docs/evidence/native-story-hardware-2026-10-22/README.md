# PR5 physical first-visit evidence

Exact source `322eb4d3a9eb09d057cdccab83da31885b66b3cb` was built, installed and tested on the New3DS with the regenerated 35-pack bundle. The executable and all assets passed staged/final console hash verification; previous executable and changed Wes pack backups remain local and on SD.

The fresh WES route completed the original opening/arrival, controller movement, departure actors/dialogue, shop load/TV, Willie talk and exterior challenge. Encounter 10 captured the two current player Pokemon and remained pending for the missing combat backend. No battle outcome or victory flag was supplied. Native exit completed without an error, and NTR remained live throughout.

The physical trace reports a 96,043,008-byte normal heap, 33,554,432-byte linear heap, and 14,508,928 linear bytes free at shop readiness. NTR delivery rate is not a gameplay frame-rate measurement.

Explicit shop Save/fresh-process Continue, lid sleep and audible audio were not retested in this physical run. The owner requested the next phase after reaching the battle boundary. Those limits remain separate from PR5's published emulator Save/Continue evidence. No GCI or completed card was installed or written here.

31 of 35 prepared hashes match the published emulator bundle. Four same-length C3T files differ: title, exterior map3 and NPC0/NPC1. Title/map3 reproduce earlier local Windows exports, but the original PR5 reference bytes were unavailable for direct residual comparison. Do not call the four files bit-identical or infer NPC residual sizes. The exact tested identities are in asset-identities.json; prepared source/runtime/story/shop/battle packs match the reference.

See verification.json, build.json and the actual downloaded boot-trace.log. Images are captures of both real console screens, not generated scenes.


The specific Save/fresh-process Continue gap in this historical run was later
closed by the [physical shop persistence check](../shop-save-continue-hardware-2026-10-22/README.md).
The earlier first-visit and encounter-10 acceptance above remains valid.
