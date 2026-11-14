# General story battles: every admitted encounter of the private C3B2 pack.
BATTLE_GENERAL_SOURCES = src/battle_general.c src/battle_general_view.c src/battle_presentation.c src/battle_rng.c src/decomp_rng.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c
build/test_battle_general: tests/test_battle_general.c $(BATTLE_GENERAL_SOURCES) include/colosseum3ds/battle_general.h include/colosseum3ds/battle_general_view.h build/battle_rng_source.h $(HEADERS) $(UPSTREAM)
	$(CC) $(CPPFLAGS) -Ibuild -Isrc $(CFLAGS) tests/test_battle_general.c $(BATTLE_GENERAL_SOURCES) $(LDFLAGS) -lm -o $@

all: build/test_battle_general
.PHONY: battle-general-check
battle-general-check: build/test_battle_general
	./build/test_battle_general

test: battle-general-check
