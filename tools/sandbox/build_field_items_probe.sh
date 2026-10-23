#!/usr/bin/env bash
# Standalone compile-time test only. No test damage code enters the normal app.
set -euo pipefail
cd "$(dirname "$0")/../.."
: "${DEVKITPRO:?Set DEVKITPRO to the established devkitPro toolchain}"
DEVKITARM="${DEVKITARM:-$DEVKITPRO/devkitARM}"
mkdir -p build/field-items-probe
python3 tools/prepare_save_codec.py
python3 tools/prepare_field_items_source.py
build_id="${C3_BUILD_ID:-$(git rev-parse --short=12 HEAD)}"
"$DEVKITARM/bin/arm-none-eabi-gcc" -g -O2 -Wall -Wextra -Werror -std=gnu11 \
  -mword-relocations -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft \
  -D__3DS__ "-DC3_BUILD_ID=\"$build_id\"" \
  -I"$DEVKITPRO/libctru/include" -Iinclude -Icompat/include \
  -Ivendor/colosseum/include -Iplatform/3ds -Isrc -Ibuild \
  tools/sandbox/field_items_probe.c platform/3ds/bag_menu.c \
  src/game_state_items.c src/game_state.c src/decomp_pokemon.c src/save.c build/save_codec.c \
  -L"$DEVKITPRO/libctru/lib" -specs=3dsx.specs -lctru -lm \
  -o build/field-items-probe/field-items.elf
"$DEVKITPRO/tools/bin/3dsxtool" build/field-items-probe/field-items.elf \
  build/field-items-probe/Colosseum3DS-field-items-probe.3dsx
