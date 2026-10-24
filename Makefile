CC ?= cc
CPPFLAGS += -Iinclude -Icompat/include -Ivendor/colosseum/include
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -Wpedantic -Werror
CORE = src/core.c src/data.c src/decomp_rng.c src/decomp_collision.c src/decomp_pokemon.c
HEADERS = $(wildcard include/colosseum3ds/*.h) compat/include/dolphin/types.h
HEADERS += vendor/colosseum/include/game/gs_colsys.h vendor/colosseum/include/game/pokemon_evolution.h
UPSTREAM = vendor/colosseum/src/hsd/random.c vendor/colosseum/src/game/GScolsys2Util_exact_8010C77C.c vendor/colosseum/src/game/pokemon_range_8012795C.c

.PHONY: all test clean 3ds verify-source
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

build/test_game_menu: tests/test_game_menu.c platform/3ds/game_menu.c platform/3ds/game_menu.h platform/3ds/n3ds_platform.h | build
	$(CC) $(CPPFLAGS) -Iplatform/3ds -DC3_MENU_HOST_TEST $(CFLAGS) tests/test_game_menu.c platform/3ds/game_menu.c $(LDFLAGS) -lm -o $@

build/test_game_state: tests/test_game_state.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c $(HEADERS)
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_game_state.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c $(LDFLAGS) -lm -o $@

build/save_codec.c: tools/prepare_save_codec.py vendor/colosseum/src/game/memcard.c | build
	python3 tools/prepare_save_codec.py

build/test_save: tests/test_save.c src/save.c src/save_codec.h include/colosseum3ds/save.h build/save_codec.c
	$(CC) $(CPPFLAGS) -Isrc $(CFLAGS) tests/test_save.c src/save.c build/save_codec.c $(LDFLAGS) -o $@

build/test_render_recovery: tests/test_render_recovery.c platform/3ds/render_recovery.c platform/3ds/render_recovery.h platform/3ds/n3ds_platform.h | build
	$(CC) $(CPPFLAGS) -Iplatform/3ds -DC3_RECOVERY_HOST_TEST $(CFLAGS) tests/test_render_recovery.c platform/3ds/render_recovery.c $(LDFLAGS) -o $@

test: all
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
