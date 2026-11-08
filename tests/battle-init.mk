# Kept separate from PR6's queue/RNG targets for parallel development.
BATTLE_INIT_SOURCES = src/battle_init.c src/battle.c src/battle_rng.c src/decomp_pokemon.c build/save_codec.c
BATTLE_INIT_HEADERS = include/colosseum3ds/battle_init.h include/colosseum3ds/battle.h include/colosseum3ds/battle_rng.h include/colosseum3ds/pokemon.h src/save_codec.h
build/battle_init_reference.inc: tools/prepare_battle_init_reference.py vendor/colosseum/src/game/pokemon_range_8011F5FC.c | build
	python3 tools/prepare_battle_init_reference.py

build/test_battle_init: tests/test_battle_init.c $(BATTLE_INIT_SOURCES) $(BATTLE_INIT_HEADERS) build/battle_rng_source.h build/battle_init_reference.inc $(HEADERS) $(UPSTREAM)
	$(CC) $(CPPFLAGS) -Ibuild -Isrc $(CFLAGS) tests/test_battle_init.c $(BATTLE_INIT_SOURCES) $(LDFLAGS) -lm -o $@

all: build/test_battle_init
.PHONY: battle-init-check
battle-init-check: build/test_battle_init
	./build/test_battle_init

test: battle-init-check
