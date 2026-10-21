# Third-party source and provenance

`vendor/colosseum` is an unmodified git submodule of
[Count-Sheep/pkmn-colosseum](https://github.com/Count-Sheep/pkmn-colosseum)
at `51bfe4314a218dfaf9592fc80723c50498d0ee55`. Its `LICENSE` and `NOTICE` remain
in that tree. The source authors describe the source as MIT-licensed, with
separate exceptions documented in the upstream notice. This build compiles
only the three files listed in `source-lock.json`; it does not link the
upstream GPL semantic-oracle sidecar.

The HSD RNG source in that snapshot records its Melee/sysdolphin reference.
The FSYS/LZSS implementation in this repository is a bounded native adaptation
based on the documented upstream loader and decoder; its exact source links
are in `docs/data-format.md`. Preserve both upstream license and notice with
the compiled application, as the build workflow does.

The platform uses the installed devkitPro toolchain, libctru and citro3d.
Their respective licenses continue to apply. Reference commits used to check
the API and build recipe are listed in `docs/3ds-build.md`.

The private `pkmn-colosseum-build` repository provides input validation and
future local data-extraction references. It is not a source submodule and its
original DOL/REL bytes are not copied into this project or release artifacts.
No source-code license grants rights to game assets, trademarks, original
game binaries, or proprietary compiler distributions.
