# Physical fresh-WES shop Save / fresh-process Continue

This closes the specific persistence gap after the already accepted first-visit hardware route. It does not invalidate or claim to repeat the earlier [opening/shop/Willie/encounter-10 acceptance](../native-story-hardware-2026-10-22/README.md).

## Tested installation

Deployed source: `d04e7758220567c2fc21f5799f4870412cc8cbd9`, normal full native PR6 foundation, no probes. Executable: 381732 bytes, SHA-256 `34e9995d5e97d9981bdbd6e152584b53c1a05367982d31e13cc5b9b5229c1069`. The staged/final executable readback matched. Later qualification/evidence commits are not relabeled as that binary.

The existing 35-pack / 428164858-byte asset set was not regenerated or retransferred. All current sizes and 11 metadata hashes were rechecked. Other identities retain the earlier full console readback authority, not a new full hash claim. [installed-assets.json](installed-assets.json) records each file and its verification method.

The four same-length graphics hash differences from the emulator bundle remain disclosed: title, exterior map3, NPC0 and NPC1. They are neither called corrupt nor claimed identical. The earlier [exact asset identities](../native-story-hardware-2026-10-22/asset-identities.json) and converter provenance remain applicable.

## Backup and save policy

The initial successful FTP listing showed no active primary, backup or temporary save files. No completed-save original was loaded, installed or modified. A new WES session was needed to reach a supported shop checkpoint because no fresh save existed. This setup stopped after the TV report; it did not repeat the previously accepted Willie/challenge/battle-request route.

There was exactly one explicit Save. The 393280-byte primary was downloaded before Continue; FTP SIZE, received size and local readback SHA-256 agreed. Its slot header/checksums and encrypted payload SHA-1 were independently checked. Backup and temporary files were absent after this first Save and after the load-only run. Local verified copies of the primary from both intervals remain private.

| Check | Actual result |
| --- | --- |
| Trainer / ID | WES / `04c15ef9` |
| Save count / outer generation | 1 / 1 |
| Checkpoint | floor 2, previous 1, spawn 0 |
| Party | Umbreon level26 HP89/89; Espeon level25 HP69/69 |
| Party bytes / money | All six original party records match the independent fresh-seed calculation; money10000 unchanged |
| Completed prerequisite flags | `0AF8=1`, `03DA=1`, `03DB=1`, `0A05=1`, `0ACE=1` |
| Later story / combat flags | `03DC=0`, `03DD=0`, `03DE=0` |
| Fresh-process Continue | Loaded the valid primary directly, not backup; returned to shop entrance |
| Intro replay | Opening/arrival and shop TV report did not replay |
| Load-only writes | No Save call; GCI byte-identical afterward |
| Exit / transport | Both processes exited cleanly; NTR stayed live without reboot or reload |

The saved checkpoint is before Willie talk in this session (`03DC=0`). That is a supported shop checkpoint, not a claim to reproduce the emulator's post-Willie save flags. The earlier hardware Willie and encounter-10 acceptance remains a separate recorded run. No party healing, experience, reward, victory result or combat continuation was supplied here.

Before and after the load-only run, the complete GCI SHA-256 is:

`7e1cb3159756293bf938fd922fe45e82448d1b667f6833a48da351af3cbd0f23`

The [saved](saved-state.json) and [continued](continued-state.json) decoded reports are identical. See [save-trace.log](save-trace.log) for the single successful Save and [continue-trace.log](continue-trace.log) for primary load, shop startup, no Save/TV report, and clean exit. Original GCI bytes and the completed-save card are not published.

Reproduce the read-only decoded checks against a private copy:

```sh
python tools/sandbox/verify_shop_checkpoint.py --gci /private/pokemon_colosseum.gci --pack /private/newgame.c3g --seed 0x40ef40d5 --out /private/checkpoint.json
```

The verifier reuses the independent slot decoder and source initializer. It checks every party byte, identity, checkpoint and selected packed flags; it does not claim an independent complete original-game execution. Its output cannot replace either input or a hardlink alias. No card or party is changed.

## Actual recordings

- [Explicit Save](explicit-save.mp4): confirmation followed by save number1.
- [Fresh-process Continue](fresh-process-continue.mp4): confirmation, area load and restored shop control.

These short H264 files encode actual NTR combined-screen PNG samples in capture-time order, with no generated scenes or audio. Sample timestamp durations are retained; the final sample is repeated only to preserve its duration. [recordings.json](recordings.json) contains hashes, dimensions, frame counts and durations. The files were locally probed; repository retrieval is verified after publication. NTR delivery rate and recording sampling rate are not gameplay FPS.

Lid sleep and audible audio remain outside this persistence check. No complete battle or result/writeback acceptance is claimed. The console is left in ftpd with the fresh WES regression save retained separately from any completed-save testing.

Text evidence uses canonical LF repository bytes; scoped attributes retain those line endings. Raw device traces and binary recordings are preserved byte-for-byte. The file hashes in verification.json bind those repository artifacts, excluding the manifest itself.
