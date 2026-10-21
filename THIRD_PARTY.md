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

## GekkoCTR native graphics integration

`vendor/gekkoctr` pins `dougchansan/GekkoCTR` at
`0f2032c6d641c78ebf4e6a418ecb51e04c0c62d8`, licensed under GNU GPL version 3.
The combined model-preview executable includes GPL-covered graphics code and is
distributed under GPLv3 terms, with corresponding source available in this
repository and its pinned source submodules. The existing MIT notices remain
applicable to their source files; the root MIT notice alone does not describe
the license of the combined executable. Preserve the donor's full `LICENSE`
and this notice with binary distributions. See [GPLv3](https://www.gnu.org/licenses/gpl-3.0.html).

The linked donor translation units are explicitly listed in `source-lock.json`.
The generated CTR backend adapts its frame/texture capacities and resource
cleanup; its provenance and exact transformations remain visible in
`tools/prepare_gekko_backend.py`. No guest execution, memory arena, FIFO parser,
or emulator bridge is linked. The donor's `docs/INDEPENDENCE.md` remains in its
submodule.

`vendor/hsd-importer` pins StarsMmd's Blender GameCube importer at
`dc71de837978a6a2fe8c5082f2840cfafb135235`, also GPLv3. Its parsing, planning
and skeletal bake helpers are used by the optional offline preparation tool,
not linked into the ARM executable. Preserve its upstream license and notices.
The required Blender runtime is an external tool. Neither source license grants
permission to redistribute extracted Pokémon models, textures or animations;
prepared `.c3m` packs remain local/private and are excluded from CI artifacts.
