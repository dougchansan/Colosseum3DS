CC ?= cc
CPPFLAGS += -Iinclude -Icompat/include -Ivendor/colosseum/include
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
CORE = src/core.c src/data.c src/decomp_rng.c src/decomp_collision.c src/decomp_pokemon.c
HEADERS = $(wildcard include/colosseum3ds/*.h) compat/include/dolphin/types.h
HEADERS += vendor/colosseum/include/game/gs_colsys.h vendor/colosseum/include/game/pokemon_evolution.h
UPSTREAM = vendor/colosseum/src/hsd/random.c vendor/colosseum/src/game/GScolsys2Util_exact_8010C77C.c vendor/colosseum/src/game/pokemon_range_8012795C.c

.PHONY: all test clean 3ds verify-source
all: build/test_bottom_ui
all: build/test_core build/test_data build/test_pokemon build/test_model build/test_movie build/test_save build/test_title build/test_field build/test_game_menu build/test_game_state build/test_render_recovery build/test_area build/test_area_camera build/test_area_lights build/test_story build/test_field_story build/test_battle build/test_battle_action build/test_battle_rng

build:
	mkdir -p $@

build/test_core: tests/test_core.c $(CORE) $(HEADERS) $(UPSTREAM) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_core.c $(CORE) $(LDFLAGS) -lm -o $@

build/test_data: tests/test_data.c src/data.c include/colosseum3ds/data.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_data.c src/data.c $(LDFLAGS) -o $@

build/test_pokemon: tests/test_pokemon.c src/decomp_pokemon.c $(HEADERS) $(UPSTREAM) | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_pokemon.c src/decomp_pokemon.c $(LDFLAGS) -o $@

build/test_model: tests/test_model.c src/model.c include/colosseum3ds/model.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_model.c src/model.c $(LDFLAGS) -lm -o $@

build/test_movie: tests/test_movie.c src/movie.c include/colosseum3ds/movie.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_movie.c src/movie.c $(LDFLAGS) -o $@

build/test_title: tests/test_title.c src/title.c include/colosseum3ds/title.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_title.c src/title.c $(LDFLAGS) -lm -o $@

build/test_field: tests/test_field.c src/field.c include/colosseum3ds/field.h include/colosseum3ds/title.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_field.c src/field.c $(LDFLAGS) -lm -o $@

build/test_area: tests/test_area.c src/area.c src/field.c include/colosseum3ds/area.h include/colosseum3ds/field.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_area.c src/area.c src/field.c $(LDFLAGS) -lm -o $@

build/test_area_camera: tests/test_area_camera.c src/area_camera.c include/colosseum3ds/area_camera.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_area_camera.c src/area_camera.c $(LDFLAGS) -lm -o $@

build/test_area_lights: tests/test_area_lights.c src/area_lights.c include/colosseum3ds/area_lights.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_area_lights.c src/area_lights.c $(LDFLAGS) -lm -o $@

build/test_story: tests/test_story.c src/story.c include/colosseum3ds/story.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_story.c src/story.c $(LDFLAGS) -lm -o $@

FIELD_STORY_SOURCES = src/field_story.c src/story.c src/area.c src/area_camera.c src/field.c src/title.c src/game_state.c src/save.c src/decomp_pokemon.c src/battle.c build/save_codec.c
build/test_field_story: tests/test_field_story.c $(FIELD_STORY_SOURCES) $(HEADERS)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_field_story.c $(FIELD_STORY_SOURCES) $(LDFLAGS) -lm -o $@

build/test_battle: tests/test_battle.c src/battle.c include/colosseum3ds/battle.h build/save_codec.c
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_battle.c src/battle.c build/save_codec.c $(LDFLAGS) -o $@

BATTLE_ACTION_UPSTREAM = $(addprefix vendor/colosseum/,$(shell python3 -c "import json; print(' '.join(f['path'] for f in json.load(open('config/battle-action-source-files.json'))['files']))"))
build/battle_action_original.inc: tools/prepare_battle_action.py config/battle-action-source-files.json source-lock.json $(BATTLE_ACTION_UPSTREAM) | build
	python3 tools/prepare_battle_action.py

build/test_battle_action: tests/test_battle_action.c src/battle_action.c include/colosseum3ds/battle_action.h build/battle_action_original.inc
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) tests/test_battle_action.c src/battle_action.c $(LDFLAGS) -o $@

build/battle_rng_source.h: tools/prepare_battle_rng.py config/battle-rng-source-files.json vendor/colosseum/src/hsd/random.c vendor/colosseum/src/game/gs_math_range_800E09E8.c | build
	python3 tools/prepare_battle_rng.py

build/test_battle_rng: tests/test_battle_rng.c src/battle_rng.c src/decomp_rng.c include/colosseum3ds/battle_rng.h build/battle_rng_source.h $(UPSTREAM)
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) tests/test_battle_rng.c src/battle_rng.c src/decomp_rng.c $(LDFLAGS) -o $@

build/test_game_menu: tests/test_game_menu.c platform/3ds/game_menu.c src/bottom_ui.c platform/3ds/game_menu.h platform/3ds/n3ds_platform.h include/colosseum3ds/bottom_ui.h | build
	$(CC) $(CPPFLAGS) -Iplatform/3ds -DC3_MENU_HOST_TEST $(CFLAGS) tests/test_game_menu.c platform/3ds/game_menu.c src/bottom_ui.c $(LDFLAGS) -lm -o $@

UI_VIEW_SOURCES = src/inventory.c src/decomp_pokemon.c platform/3ds/game_menu.c platform/3ds/game_ui_views.c platform/3ds/ingame_menu.c src/bottom_ui.c src/battle_preview.c src/game_state.c src/pda.c build/save_codec.c
UI_VIEW_HEADERS = platform/3ds/game_menu.h platform/3ds/game_ui_views.h platform/3ds/ingame_menu.h include/colosseum3ds/bottom_ui.h include/colosseum3ds/game_state.h include/colosseum3ds/pda.h
build/render_bottom_ui_preview: tools/render_bottom_ui_preview.c $(UI_VIEW_SOURCES) $(UI_VIEW_HEADERS) | build
	$(CC) $(CPPFLAGS) -Iplatform/3ds -Isrc -DC3_MENU_HOST_TEST $(CFLAGS) -ffunction-sections tools/render_bottom_ui_preview.c $(UI_VIEW_SOURCES) $(LDFLAGS) -Wl,--gc-sections -lm -o $@

build/test_bottom_ui: tests/test_bottom_ui.c $(UI_VIEW_SOURCES) $(UI_VIEW_HEADERS) | build
	$(CC) $(CPPFLAGS) -Iplatform/3ds -Isrc -DC3_MENU_HOST_TEST $(CFLAGS) -ffunction-sections tests/test_bottom_ui.c $(UI_VIEW_SOURCES) $(LDFLAGS) -Wl,--gc-sections -lm -o $@

build/pda_fixture.c3pd: tests/pda_fixture.py tools/prepare_pda.py | build
	python3 tests/pda_fixture.py $@

PDA_SOURCES = src/inventory.c platform/3ds/ingame_menu.c src/pda.c src/bottom_ui.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c
build/test_pda: tests/test_pda.c tests/test_game_state.c $(PDA_SOURCES) src/story.c $(HEADERS) $(UI_VIEW_HEADERS) build/pda_fixture.c3pd
	$(CC) $(CPPFLAGS) -Iplatform/3ds -Isrc $(CFLAGS) tests/test_pda.c $(PDA_SOURCES) src/story.c $(LDFLAGS) -lm -o $@

build/render_ingame_menu_preview: tools/render_ingame_menu_preview.c $(PDA_SOURCES) $(HEADERS) $(UI_VIEW_HEADERS)
	$(CC) $(CPPFLAGS) -Iplatform/3ds -Isrc $(CFLAGS) tools/render_ingame_menu_preview.c $(PDA_SOURCES) $(LDFLAGS) -lm -o $@

all: build/test_pda
.PHONY: pda-check
pda-check: build/test_pda
	./build/test_pda
test: pda-check

build/test_game_state: tests/test_game_state.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c $(HEADERS)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_game_state.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c $(LDFLAGS) -lm -o $@

build/save_codec.c: tools/prepare_save_codec.py vendor/colosseum/src/game/memcard.c | build
	python3 tools/prepare_save_codec.py

build/test_save: tests/test_save.c src/save.c src/save_codec.h include/colosseum3ds/save.h build/save_codec.c
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_save.c src/save.c build/save_codec.c $(LDFLAGS) -o $@

build/test_render_recovery: tests/test_render_recovery.c platform/3ds/render_recovery.c platform/3ds/render_recovery.h platform/3ds/n3ds_platform.h | build
	$(CC) $(CPPFLAGS) -Iplatform/3ds -DC3_RECOVERY_HOST_TEST $(CFLAGS) tests/test_render_recovery.c platform/3ds/render_recovery.c $(LDFLAGS) -o $@

test: all
	./build/test_bottom_ui
	./build/test_core
	./build/test_data
	./build/test_pokemon
	./build/test_model
	./build/test_movie
	./build/test_save
	./build/test_title
	./build/test_field
	./build/test_game_menu
	./build/test_game_state
	./build/test_render_recovery
	./build/test_area
	./build/test_area_camera
	./build/test_area_lights
	./build/test_story
	./build/test_field_story
	./build/test_battle
	./build/test_battle_action
	./build/test_battle_rng
	python3 -m unittest discover -s tests -p 'test_*.py'

verify-source:
	python3 tools/verify_source.py

3ds:
	$(MAKE) -f Makefile.3ds

clean:
	rm -rf build build-3ds build-3ds-boot

include tests/battle-init.mk

include tests/battle-preview.mk

build/test_battle_presentation: tests/test_battle_presentation.c src/battle_presentation.c include/colosseum3ds/battle_presentation.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_battle_presentation.c src/battle_presentation.c $(LDFLAGS) -o $@

all: build/test_battle_presentation
test: battle-presentation-check
.PHONY: battle-presentation-check
battle-presentation-check: build/test_battle_presentation
	./build/test_battle_presentation

include tests/wzx-bite.mk

# Source-derived particle execution and WZX defender/camera contracts.
build/test_particle: tests/test_particle.c src/particle.c include/colosseum3ds/particle.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_particle.c src/particle.c $(LDFLAGS) -lm -o $@

build/test_particle_line: tests/test_particle_line.c src/particle_line.c src/particle.c include/colosseum3ds/particle_line.h include/colosseum3ds/particle.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_particle_line.c src/particle_line.c src/particle.c $(LDFLAGS) -lm -o $@

all: build/test_particle_line
test: particle-line-check
.PHONY: particle-line-check
particle-line-check: build/test_particle_line
	./build/test_particle_line

build/test_particle_pack: tests/test_particle_pack.c src/particle_pack.c src/particle.c build/save_codec.c $(HEADERS)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_particle_pack.c src/particle_pack.c src/particle.c build/save_codec.c $(LDFLAGS) -lm -o $@

build/test_wzx_damage: tests/test_wzx_damage.c src/wzx_damage.c build/save_codec.c $(HEADERS)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_wzx_damage.c src/wzx_damage.c build/save_codec.c $(LDFLAGS) -lm -o $@

build/test_battle_camera: tests/test_battle_camera.c src/battle_camera.c build/save_codec.c $(HEADERS)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_battle_camera.c src/battle_camera.c build/save_codec.c $(LDFLAGS) -lm -o $@

all: build/test_particle build/test_particle_pack build/test_wzx_damage build/test_battle_camera
.PHONY: battle-effects-check battle-audio-check
battle-effects-check: build/test_particle build/test_particle_pack build/test_wzx_damage build/test_battle_camera
	./build/test_particle
	./build/test_particle_pack
	./build/test_wzx_damage
	./build/test_battle_camera

# This existing Python suite builds/runs the production decoder and NDSP-player
# C fixtures; ordinary `make test` includes it through full Python discovery.
battle-audio-check:
	python3 -m unittest discover -s tests -p 'test_prepare_battle_audio.py' -v

test: battle-effects-check

# Qualified CPU state/format core; capture and dependent GPU sampling remain required.
build/test_wzx_confusion: tests/test_wzx_confusion.c src/wzx_confusion.c build/save_codec.c include/colosseum3ds/wzx_confusion.h | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_wzx_confusion.c src/wzx_confusion.c build/save_codec.c $(LDFLAGS) -lm -o $@

all: build/test_wzx_confusion
.PHONY: wzx-confusion-check
wzx-confusion-check: build/test_wzx_confusion
	./build/test_wzx_confusion

test: wzx-confusion-check

build/test_confusion_indirect: tests/test_confusion_indirect.c src/confusion_indirect.c src/wzx_confusion.c build/save_codec.c include/colosseum3ds/confusion_indirect.h | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_confusion_indirect.c src/confusion_indirect.c src/wzx_confusion.c build/save_codec.c $(LDFLAGS) -lm -o $@

all: build/test_confusion_indirect

confusion-indirect-check: build/test_confusion_indirect
	./build/test_confusion_indirect

test: confusion-indirect-check

# Original assets remain optional; the core test uses synthetic descriptors.
build/test_wzx_reflect: tests/test_wzx_reflect.c src/wzx_reflect.c $(HEADERS) build/save_codec.c | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_wzx_reflect.c src/wzx_reflect.c build/save_codec.c $(LDFLAGS) -lm -o $@
all: build/test_wzx_reflect
.PHONY: wzx-reflect-check
wzx-reflect-check: build/test_wzx_reflect
	./build/test_wzx_reflect
test: wzx-reflect-check

build/test_wzx_tail_whip: tests/test_wzx_tail_whip.c src/wzx_tail_whip.c build/save_codec.c include/colosseum3ds/wzx_tail_whip.h | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_wzx_tail_whip.c src/wzx_tail_whip.c build/save_codec.c $(LDFLAGS) -lm -o $@
all: build/test_wzx_tail_whip
.PHONY: wzx-tail-whip-check
wzx-tail-whip-check: build/test_wzx_tail_whip
	./build/test_wzx_tail_whip
test: wzx-tail-whip-check

build/test_particle_app_srt: tests/test_particle_app_srt.c src/particle.c src/particle_pack.c build/save_codec.c include/colosseum3ds/particle.h include/colosseum3ds/particle_pack.h | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_particle_app_srt.c src/particle.c src/particle_pack.c build/save_codec.c $(LDFLAGS) -lm -o $@
all: build/test_particle_app_srt
.PHONY: particle-app-srt-check
particle-app-srt-check: build/test_particle_app_srt
	./build/test_particle_app_srt
test: particle-app-srt-check

build/test_particle_green_down: tests/test_particle_green_down.c src/particle.c src/particle_pack.c build/save_codec.c include/colosseum3ds/particle.h include/colosseum3ds/particle_pack.h | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_particle_green_down.c src/particle.c src/particle_pack.c build/save_codec.c $(LDFLAGS) -lm -o $@
all: build/test_particle_green_down
.PHONY: particle-green-down-check
particle-green-down-check: build/test_particle_green_down
	./build/test_particle_green_down
test: particle-green-down-check

build/test_wzx_growl: tests/test_wzx_growl.c src/wzx_growl.c build/save_codec.c include/colosseum3ds/wzx_growl.h | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_wzx_growl.c src/wzx_growl.c build/save_codec.c $(LDFLAGS) -lm -o $@
all: build/test_wzx_growl
.PHONY: wzx-growl-check
wzx-growl-check: build/test_wzx_growl
	./build/test_wzx_growl
test: wzx-growl-check

build/test_wzx_stat_down: tests/test_wzx_stat_down.c src/wzx_stat_down.c build/save_codec.c include/colosseum3ds/wzx_stat_down.h | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_wzx_stat_down.c src/wzx_stat_down.c build/save_codec.c $(LDFLAGS) -lm -o $@
all: build/test_wzx_stat_down
.PHONY: wzx-stat-down-check
wzx-stat-down-check: build/test_wzx_stat_down
	./build/test_wzx_stat_down
test: wzx-stat-down-check

# Focused Tackle math/admission/order fixtures (optional original-bank arguments).
build/test_particle_tackle: tests/test_particle_tackle.c src/particle.c src/particle_line.c src/particle_pack.c build/save_codec.c $(HEADERS) | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_particle_tackle.c src/particle_line.c src/particle_pack.c build/save_codec.c $(LDFLAGS) -lm -o $@

build/test_wzx_tackle: tests/test_wzx_tackle.c src/wzx_tackle.c build/save_codec.c include/colosseum3ds/wzx_tackle.h | build
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_wzx_tackle.c src/wzx_tackle.c build/save_codec.c $(LDFLAGS) -lm -o $@
all: build/test_wzx_tackle
.PHONY: wzx-tackle-check
wzx-tackle-check: build/test_wzx_tackle
	./build/test_wzx_tackle
test: wzx-tackle-check

all: build/test_particle_tackle
.PHONY: particle-tackle-check
particle-tackle-check: build/test_particle_tackle
	./build/test_particle_tackle
test: particle-tackle-check

build/test_inventory: tests/test_inventory.c tests/test_game_state.c src/inventory.c src/pda.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c $(HEADERS)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_inventory.c src/inventory.c src/pda.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c $(LDFLAGS) -lm -o $@
all: build/test_inventory
.PHONY: inventory-check
inventory-check: build/test_inventory
	./build/test_inventory
test: inventory-check

build/test_pda_audio: tests/test_pda_audio.c platform/3ds/pda_audio.c platform/3ds/pda_audio.h platform/3ds/battle_audio_player.h | build
	$(CC) $(CPPFLAGS) -Iplatform/3ds $(CFLAGS) tests/test_pda_audio.c platform/3ds/pda_audio.c $(LDFLAGS) -o $@

all: build/test_pda_audio
.PHONY: pda-audio-check
pda-audio-check: build/test_pda_audio
	./build/test_pda_audio
test: pda-audio-check

build/test_inventory_ui: tests/test_inventory_ui.c tests/test_game_state.c $(PDA_SOURCES) $(HEADERS) $(UI_VIEW_HEADERS) build/pda_fixture.c3pd
	$(CC) $(CPPFLAGS) -Iplatform/3ds -Isrc $(CFLAGS) tests/test_inventory_ui.c $(PDA_SOURCES) $(LDFLAGS) -lm -o $@
all: build/test_inventory_ui
.PHONY: inventory-ui-check
inventory-ui-check: build/test_inventory_ui
	./build/test_inventory_ui
test: inventory-ui-check

# Opt-in private input: ./build/test_inventory_real INPUT.gci GAME.c3g PDA.c3pd
build/test_inventory_real: tests/test_inventory_real.c tests/test_game_state.c src/inventory.c src/pda.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c $(HEADERS)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_inventory_real.c src/inventory.c src/pda.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c $(LDFLAGS) -lm -o $@

# Opt-in original model audit: pass model root followed by additional species IDs.
build/pda_valid_batch.inc: platform/3ds/title_renderer.c | build
	python3 -c 'from pathlib import Path; s=Path("$<").read_text(); i=s.index("static bool valid_batch("); j=s.index("\n}\n",i)+3; Path("$@").write_text(s[i:j])'

build/test_pda_preview: tests/pda_preview_native.c platform/3ds/pda_preview.c platform/3ds/pda_spinda.c src/title.c build/pda_valid_batch.inc $(HEADERS) platform/3ds/pda_preview.h platform/3ds/pda_spinda.h
	$(CC) $(CPPFLAGS) -Iplatform/3ds -Ibuild -DC3_PDA_VALIDATE_BATCH $(CFLAGS) tests/pda_preview_native.c platform/3ds/pda_preview.c platform/3ds/pda_spinda.c src/title.c $(LDFLAGS) -lm -o $@

build/test_bottom_screen: tests/test_bottom_screen.c tests/bottom_screen_stubs/3ds.h platform/3ds/bottom_screen.c platform/3ds/bottom_screen.h src/bottom_ui.c include/colosseum3ds/bottom_ui.h | build
	$(CC) $(CPPFLAGS) -Itests/bottom_screen_stubs -Iplatform/3ds $(CFLAGS) tests/test_bottom_screen.c platform/3ds/bottom_screen.c src/bottom_ui.c $(LDFLAGS) -o $@
all: build/test_bottom_screen
.PHONY: bottom-screen-check
bottom-screen-check: build/test_bottom_screen
	./build/test_bottom_screen
test: bottom-screen-check

BATTLE_RULES_UPSTREAM = $(addprefix vendor/colosseum/,$(shell python3 -c "import json; print(' '.join(f['path'] for f in json.load(open('config/battle-rules-source-files.json'))['files']))"))
build/battle_rules_original.inc: tools/prepare_battle_rules.py config/battle-rules-source-files.json source-lock.json $(BATTLE_RULES_UPSTREAM) | build
	python3 tools/prepare_battle_rules.py

BATTLE_RULES_SOURCES = src/battle_rules.c src/battle_rng.c
build/test_battle_rules: tests/test_battle_rules.c $(BATTLE_RULES_SOURCES) $(HEADERS) build/battle_rules_original.inc build/battle_rng_source.h | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) tests/test_battle_rules.c $(BATTLE_RULES_SOURCES) $(LDFLAGS) -o $@

build/libbattle_rules.so: $(BATTLE_RULES_SOURCES) $(HEADERS) build/battle_rules_original.inc build/battle_rng_source.h | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) -fPIC -shared $(BATTLE_RULES_SOURCES) $(LDFLAGS) -o $@

all: build/test_battle_rules
.PHONY: battle-rules-check battle-rules-original-check
battle-rules-check: build/test_battle_rules
	./build/test_battle_rules
# ctypes can load an ASan-instrumented library only when the sanitizer runtime
# is first in the process; leak reports would otherwise cover Python itself.
SANITIZER_PYTHON_ENV = $(if $(findstring -fsanitize=address,$(CFLAGS) $(LDFLAGS)),LD_PRELOAD="$$($(CC) -print-file-name=libasan.so)" ASAN_OPTIONS=detect_leaks=0)
battle-rules-original-check: build/libbattle_rules.so docs/battle-rules-original-vectors.json
	$(SANITIZER_PYTHON_ENV) python3 tools/qualification/check_battle_rules_oracle.py --library build/libbattle_rules.so
test: battle-rules-check battle-rules-original-check

BATTLE_POKEMON_UPSTREAM = $(addprefix vendor/colosseum/,$(shell python3 -c "import json; print(' '.join(f['path'] for f in json.load(open('config/battle-pokemon-source-files.json'))['files']))"))
build/battle_pokemon_original.inc: tools/prepare_battle_pokemon.py config/battle-pokemon-source-files.json source-lock.json $(BATTLE_POKEMON_UPSTREAM) | build
	python3 tools/prepare_battle_pokemon.py

BATTLE_POKEMON_SOURCES = src/battle_pokemon.c src/battle_rng.c
build/test_battle_pokemon: tests/test_battle_pokemon.c $(BATTLE_POKEMON_SOURCES) $(HEADERS) build/battle_pokemon_original.inc build/battle_rng_source.h | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) tests/test_battle_pokemon.c $(BATTLE_POKEMON_SOURCES) $(LDFLAGS) -o $@

build/libbattle_pokemon.so: $(BATTLE_POKEMON_SOURCES) $(HEADERS) build/battle_pokemon_original.inc build/battle_rng_source.h | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) -fPIC -shared $(BATTLE_POKEMON_SOURCES) $(LDFLAGS) -o $@

all: build/test_battle_pokemon
.PHONY: battle-pokemon-check
battle-pokemon-check: build/test_battle_pokemon
	./build/test_battle_pokemon
test: battle-pokemon-check

BATTLE_TRAINER_UPSTREAM = $(addprefix vendor/colosseum/,$(shell python3 -c "import json; print(' '.join(f['path'] for f in json.load(open('config/battle-trainer-source-files.json'))['files']))"))
build/battle_trainer_original.inc: tools/prepare_battle_trainer.py tools/prepare_battle_pokemon.py config/battle-trainer-source-files.json source-lock.json $(BATTLE_TRAINER_UPSTREAM) | build
	python3 tools/prepare_battle_trainer.py

BATTLE_TRAINER_SOURCES = src/battle_trainer.c $(BATTLE_POKEMON_SOURCES)
BATTLE_TRAINER_GENERATED = build/battle_trainer_original.inc build/battle_pokemon_original.inc build/battle_rng_source.h
build/test_battle_trainer: tests/test_battle_trainer.c $(BATTLE_TRAINER_SOURCES) $(HEADERS) $(BATTLE_TRAINER_GENERATED) | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) tests/test_battle_trainer.c $(BATTLE_TRAINER_SOURCES) $(LDFLAGS) -o $@

build/libbattle_trainer.so: $(BATTLE_TRAINER_SOURCES) $(HEADERS) $(BATTLE_TRAINER_GENERATED) | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) -fPIC -shared $(BATTLE_TRAINER_SOURCES) $(LDFLAGS) -o $@

all: build/test_battle_trainer
.PHONY: battle-trainer-check
battle-trainer-check: build/test_battle_trainer
	./build/test_battle_trainer
test: battle-trainer-check

BATTLE_FIGHT_POKEMON_UPSTREAM = $(addprefix vendor/colosseum/,$(shell python3 -c "import json; print(' '.join(f['path'] for f in json.load(open('config/battle-fight-pokemon-source-files.json'))['files']))"))
build/battle_fight_pokemon_original.inc: tools/prepare_battle_fight_pokemon.py tools/prepare_battle_pokemon.py config/battle-fight-pokemon-source-files.json source-lock.json $(BATTLE_FIGHT_POKEMON_UPSTREAM) | build
	python3 tools/prepare_battle_fight_pokemon.py

BATTLE_FIGHT_POKEMON_SOURCES = src/battle_fight_pokemon.c $(BATTLE_POKEMON_SOURCES)
BATTLE_FIGHT_POKEMON_GENERATED = build/battle_fight_pokemon_original.inc build/battle_pokemon_original.inc build/battle_rng_source.h
build/test_battle_fight_pokemon: tests/test_battle_fight_pokemon.c $(BATTLE_FIGHT_POKEMON_SOURCES) $(HEADERS) $(BATTLE_FIGHT_POKEMON_GENERATED) | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) tests/test_battle_fight_pokemon.c $(BATTLE_FIGHT_POKEMON_SOURCES) $(LDFLAGS) -o $@

build/test_battle_fight_pokemon_failure: tests/test_battle_fight_pokemon.c src/battle_fight_pokemon.c $(HEADERS) build/battle_fight_pokemon_original.inc | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) -DC3_TEST_FIGHT_POKEMON_INIT_FAILURE tests/test_battle_fight_pokemon.c src/battle_fight_pokemon.c $(LDFLAGS) -o $@

build/libbattle_fight_pokemon.so: $(BATTLE_FIGHT_POKEMON_SOURCES) $(HEADERS) $(BATTLE_FIGHT_POKEMON_GENERATED) | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) -fPIC -shared $(BATTLE_FIGHT_POKEMON_SOURCES) $(LDFLAGS) -o $@

all: build/test_battle_fight_pokemon build/test_battle_fight_pokemon_failure
.PHONY: battle-fight-pokemon-check
battle-fight-pokemon-check: build/test_battle_fight_pokemon build/test_battle_fight_pokemon_failure
	./build/test_battle_fight_pokemon
	./build/test_battle_fight_pokemon_failure
test: battle-fight-pokemon-check

BATTLE_PARTY_SORT_UPSTREAM = $(addprefix vendor/colosseum/,$(shell python3 -c "import json; print(' '.join(f['path'] for f in json.load(open('config/battle-party-sort-source-files.json'))['files']))"))
build/battle_party_sort_original.inc: tools/prepare_battle_party_sort.py tools/prepare_battle_pokemon.py config/battle-party-sort-source-files.json source-lock.json $(BATTLE_PARTY_SORT_UPSTREAM) | build
	python3 tools/prepare_battle_party_sort.py

BATTLE_PARTY_SORT_SOURCES = src/battle_party_sort.c $(BATTLE_FIGHT_POKEMON_SOURCES)
BATTLE_PARTY_SORT_GENERATED = build/battle_party_sort_original.inc $(BATTLE_FIGHT_POKEMON_GENERATED)
build/test_battle_party_sort: tests/test_battle_party_sort.c $(BATTLE_PARTY_SORT_SOURCES) $(HEADERS) $(BATTLE_PARTY_SORT_GENERATED) | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) tests/test_battle_party_sort.c $(BATTLE_PARTY_SORT_SOURCES) $(LDFLAGS) -o $@

build/test_battle_party_sort_failure: tests/test_battle_party_sort.c src/battle_party_sort.c src/battle_rng.c $(HEADERS) build/battle_party_sort_original.inc build/battle_rng_source.h | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) -DC3_TEST_PARTY_DEPENDENCY_FAILURE tests/test_battle_party_sort.c src/battle_party_sort.c src/battle_rng.c $(LDFLAGS) -o $@

build/libbattle_party_sort.so: $(BATTLE_PARTY_SORT_SOURCES) $(HEADERS) $(BATTLE_PARTY_SORT_GENERATED) | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) -fPIC -shared $(BATTLE_PARTY_SORT_SOURCES) $(LDFLAGS) -o $@

all: build/test_battle_party_sort build/test_battle_party_sort_failure
.PHONY: battle-party-sort-check
battle-party-sort-check: build/test_battle_party_sort build/test_battle_party_sort_failure
	./build/test_battle_party_sort
	./build/test_battle_party_sort_failure
test: battle-party-sort-check

BATTLE_RUNTIME_TRAINER_UPSTREAM = $(addprefix vendor/colosseum/,$(shell python3 -c "import json; print(' '.join(f['path'] for f in json.load(open('config/battle-runtime-trainer-source-files.json'))['files']))"))
build/battle_runtime_trainer_original.inc: tools/prepare_battle_runtime_trainer.py tools/prepare_battle_pokemon.py config/battle-runtime-trainer-source-files.json source-lock.json $(BATTLE_RUNTIME_TRAINER_UPSTREAM) | build
	python3 tools/prepare_battle_runtime_trainer.py

BATTLE_RUNTIME_TRAINER_SOURCES = src/battle_runtime_trainer.c src/battle_trainer.c $(BATTLE_FIGHT_POKEMON_SOURCES)
BATTLE_RUNTIME_TRAINER_GENERATED = build/battle_runtime_trainer_original.inc $(BATTLE_TRAINER_GENERATED) build/battle_fight_pokemon_original.inc
build/test_battle_runtime_trainer: tests/test_battle_runtime_trainer.c $(BATTLE_RUNTIME_TRAINER_SOURCES) $(HEADERS) $(BATTLE_RUNTIME_TRAINER_GENERATED) | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) tests/test_battle_runtime_trainer.c $(BATTLE_RUNTIME_TRAINER_SOURCES) $(LDFLAGS) -o $@

build/libbattle_runtime_trainer.so: $(BATTLE_RUNTIME_TRAINER_SOURCES) $(HEADERS) $(BATTLE_RUNTIME_TRAINER_GENERATED) | build
	$(CC) $(CPPFLAGS) -Ibuild $(CFLAGS) -fPIC -shared $(BATTLE_RUNTIME_TRAINER_SOURCES) $(LDFLAGS) -o $@

all: build/test_battle_runtime_trainer
.PHONY: battle-runtime-trainer-check
battle-runtime-trainer-check: build/test_battle_runtime_trainer
	./build/test_battle_runtime_trainer

# Trainer-entry session over the real constructors (no spies). It runs the
# private-pack path only when C3_ENTRY_PACK and C3_ENTRY_NEWGAME are set.
BATTLE_ENTRY_SESSION_SOURCES = src/battle_entry_session.c src/battle_entry_pack.c 	src/battle_entry_records.c src/battle_trainer_entry.c src/battle_trainer_entry_native.c 	src/battle_sequence.c src/battle_resume.c src/battle_action.c src/battle_party_sort.c 	src/battle_runtime_trainer.c src/battle_trainer.c $(BATTLE_FIGHT_POKEMON_SOURCES) 	src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c
BATTLE_ENTRY_SESSION_GENERATED = build/battle_action_original.inc build/battle_party_sort_original.inc 	$(BATTLE_RUNTIME_TRAINER_GENERATED)
build/test_battle_entry_session: tests/test_battle_entry_session.c $(BATTLE_ENTRY_SESSION_SOURCES) $(HEADERS) $(BATTLE_ENTRY_SESSION_GENERATED) | build
	$(CC) $(CPPFLAGS) -Isrc -Ibuild $(CFLAGS) tests/test_battle_entry_session.c $(BATTLE_ENTRY_SESSION_SOURCES) $(LDFLAGS) -lm -o $@

all: build/test_battle_entry_session
.PHONY: battle-entry-session-check
battle-entry-session-check: build/test_battle_entry_session
	./build/test_battle_entry_session
test: battle-entry-session-check

# Entry-built FightPokemon starting the preview battle. Private comparison runs
# only with C3_ENTRY_PACK, C3_ENTRY_NEWGAME and C3_PREVIEW_DIR set.
BATTLE_ENTRY_PREVIEW_SOURCES = $(sort src/battle_entry_preview.c $(BATTLE_ENTRY_SESSION_SOURCES) \
	src/battle_preview.c src/battle_presentation.c src/battle_damage.c $(BATTLE_INIT_SOURCES))
build/test_battle_entry_preview: tests/test_battle_entry_preview.c $(BATTLE_ENTRY_PREVIEW_SOURCES) $(HEADERS) \
	$(BATTLE_ENTRY_SESSION_GENERATED) build/battle_damage_original.inc build/battle_rng_source.h | build
	$(CC) $(CPPFLAGS) -Isrc -Ibuild $(CFLAGS) tests/test_battle_entry_preview.c $(BATTLE_ENTRY_PREVIEW_SOURCES) $(LDFLAGS) -lm -o $@

all: build/test_battle_entry_preview
.PHONY: battle-entry-preview-check
battle-entry-preview-check: build/test_battle_entry_preview
	./build/test_battle_entry_preview
test: battle-entry-preview-check

# Original move-script interpreter. The original-pack run needs C3_SCRIPT_PACK.
BATTLE_SCRIPT_SOURCES = src/battle_script.c build/save_codec.c
build/test_battle_script: tests/test_battle_script.c $(BATTLE_SCRIPT_SOURCES) src/battle_script_names.inc include/colosseum3ds/battle_script.h | build
	$(CC) $(CPPFLAGS) -Isrc -Ibuild $(CFLAGS) tests/test_battle_script.c $(BATTLE_SCRIPT_SOURCES) $(LDFLAGS) -o $@

all: build/test_battle_script
.PHONY: battle-script-check
battle-script-check: build/test_battle_script
	./build/test_battle_script
test: battle-script-check

# FightPokemon/FightOutPokemon status accessors (field table generated from source).
build/battle_status_fields.inc: tools/prepare_battle_status_fields.py vendor/colosseum/src/game/pokemon_get_status_exact_8012640C.c vendor/colosseum/src/game/pokemon_range_8011F5FC.c vendor/colosseum/src/game/fight_trainer_db_range_801FBD10.c | build
	python3 tools/prepare_battle_status_fields.py --output $@

build/test_battle_status: tests/test_battle_status.c src/battle_status.c include/colosseum3ds/battle_status.h build/battle_status_fields.inc | build
	$(CC) $(CPPFLAGS) -Isrc -Ibuild $(CFLAGS) tests/test_battle_status.c src/battle_status.c $(LDFLAGS) -o $@

all: build/test_battle_status
.PHONY: battle-status-check
battle-status-check: build/test_battle_status
	./build/test_battle_status
test: battle-status-check

# fightOutPokemonCreate over the arena; private run with C3_ENTRY_PACK/C3_ENTRY_NEWGAME.
BATTLE_FIGHT_OUT_SOURCES = $(sort src/battle_fight_out.c src/battle_status.c $(BATTLE_ENTRY_SESSION_SOURCES))
build/test_battle_fight_out: tests/test_battle_fight_out.c $(BATTLE_FIGHT_OUT_SOURCES) $(HEADERS) build/battle_status_fields.inc $(BATTLE_ENTRY_SESSION_GENERATED) | build
	$(CC) $(CPPFLAGS) -Isrc -Ibuild $(CFLAGS) tests/test_battle_fight_out.c $(BATTLE_FIGHT_OUT_SOURCES) $(LDFLAGS) -lm -o $@

all: build/test_battle_fight_out
.PHONY: battle-fight-out-check
battle-fight-out-check: build/test_battle_fight_out
	./build/test_battle_fight_out
test: battle-fight-out-check
test: battle-runtime-trainer-check
