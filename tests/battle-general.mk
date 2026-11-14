# General story battles: every admitted encounter of the private C3B2 pack.
BATTLE_GENERAL_SOURCES = src/battle_general.c src/battle_general_view.c src/battle_presentation.c src/battle_rng.c src/decomp_rng.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c
build/test_battle_general: tests/test_battle_general.c $(BATTLE_GENERAL_SOURCES) include/colosseum3ds/battle_general.h include/colosseum3ds/battle_general_view.h build/battle_rng_source.h $(HEADERS) $(UPSTREAM)
	$(CC) $(CPPFLAGS) -Ibuild -Isrc $(CFLAGS) tests/test_battle_general.c $(BATTLE_GENERAL_SOURCES) $(LDFLAGS) -lm -o $@

all: build/test_battle_general
.PHONY: battle-general-check
battle-general-check: build/test_battle_general build/test_battle3d_index build/test_wzx_player
	./build/test_battle_general
	./build/test_battle3d_index
	./build/test_wzx_player

test: battle-general-check

# The 3D battle asset index (synthetic; a prepared index as argument lists it).
build/test_battle3d_index: tests/test_battle3d_index.c src/battle3d_index.c include/colosseum3ds/battle3d_index.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_battle3d_index.c src/battle3d_index.c $(LDFLAGS) -o $@
all: build/test_battle3d_index

# The general WZX effect player core (synthetic; a battle3d dir as argument plays every pack).
build/test_wzx_player: tests/test_wzx_player.c src/wzx_player.c include/colosseum3ds/wzx_player.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -D_POSIX_C_SOURCE=200809L tests/test_wzx_player.c src/wzx_player.c $(LDFLAGS) -o $@
all: build/test_wzx_player
