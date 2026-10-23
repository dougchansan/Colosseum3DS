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
