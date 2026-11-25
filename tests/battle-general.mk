# General story battles: every admitted encounter of the private C3B2 pack.
BATTLE_GENERAL_SOURCES = src/battle_general.c src/battle_general_view.c src/battle_presentation.c src/battle_rng.c src/decomp_rng.c src/game_state.c src/save.c src/decomp_pokemon.c build/save_codec.c
build/test_battle_general: tests/test_battle_general.c $(BATTLE_GENERAL_SOURCES) include/colosseum3ds/battle_general.h include/colosseum3ds/battle_general_view.h build/battle_rng_source.h $(HEADERS) $(UPSTREAM)
	$(CC) $(CPPFLAGS) -Ibuild -Isrc $(CFLAGS) tests/test_battle_general.c $(BATTLE_GENERAL_SOURCES) $(LDFLAGS) -lm -o $@

all: build/test_battle_general

# Original turn rules (synthetic): order, priority, speed items, ties, targeting, damage, end of turn.
build/test_battle_turn_rules: tests/test_battle_turn_rules.c $(BATTLE_GENERAL_SOURCES) include/colosseum3ds/battle_general.h build/battle_rng_source.h $(HEADERS) $(UPSTREAM)
	$(CC) $(CPPFLAGS) -Ibuild -Isrc $(CFLAGS) tests/test_battle_turn_rules.c $(BATTLE_GENERAL_SOURCES) $(LDFLAGS) -lm -o $@
all: build/test_battle_turn_rules

# Original trainer AI (synthetic version-3 pack): scored moves, random rows, switching.
build/test_battle_trainer_ai: tests/test_battle_trainer_ai.c $(BATTLE_GENERAL_SOURCES) include/colosseum3ds/battle_general.h build/battle_rng_source.h $(HEADERS) $(UPSTREAM)
	$(CC) $(CPPFLAGS) -Ibuild -Isrc $(CFLAGS) tests/test_battle_trainer_ai.c $(BATTLE_GENERAL_SOURCES) $(LDFLAGS) -lm -o $@
all: build/test_battle_trainer_ai

# Forget-a-move prompt (synthetic; a prepared pda.c3pd as argument renders the original messages).
build/test_move_forget: tests/test_move_forget.c src/move_forget.c src/pda.c $(BATTLE_GENERAL_SOURCES) include/colosseum3ds/move_forget.h include/colosseum3ds/battle_general.h build/battle_rng_source.h $(HEADERS) $(UPSTREAM)
	$(CC) $(CPPFLAGS) -Ibuild -Isrc $(CFLAGS) tests/test_move_forget.c src/move_forget.c src/pda.c $(BATTLE_GENERAL_SOURCES) $(LDFLAGS) -lm -o $@
all: build/test_move_forget

# Shadow Pokemon in battle (synthetic version-3 pack): Hyper Mode, Call, heart gauge, banking.
build/test_battle_hyper: tests/test_battle_hyper.c $(BATTLE_GENERAL_SOURCES) include/colosseum3ds/battle_general.h build/battle_rng_source.h $(HEADERS) $(UPSTREAM)
	$(CC) $(CPPFLAGS) -Ibuild -Isrc $(CFLAGS) tests/test_battle_hyper.c $(BATTLE_GENERAL_SOURCES) $(LDFLAGS) -lm -o $@
all: build/test_battle_hyper
# One press per battle UI layer: the command pages ignore the press that ended the log (synthetic).
build/test_battle_command_input: tests/test_battle_command_input.c platform/3ds/battle_general_scene.c platform/3ds/battle_general_scene.h src/bottom_ui.c $(BATTLE_GENERAL_SOURCES) include/colosseum3ds/press_gate.h include/colosseum3ds/battle_general.h build/battle_rng_source.h $(HEADERS) $(UPSTREAM)
	$(CC) $(CPPFLAGS) -Iplatform/3ds -Ibuild -Isrc $(CFLAGS) tests/test_battle_command_input.c platform/3ds/battle_general_scene.c src/bottom_ui.c $(BATTLE_GENERAL_SOURCES) $(LDFLAGS) -lm -o $@
all: build/test_battle_command_input
.PHONY: battle-general-check
battle-general-check: build/test_battle_general build/test_battle_turn_rules build/test_battle_trainer_ai build/test_move_forget build/test_battle_hyper build/test_battle3d_index build/test_wzx_player build/test_battle_command_input
	./build/test_battle_general
	./build/test_battle_command_input
	./build/test_battle_turn_rules
	./build/test_battle_trainer_ai
	./build/test_move_forget
	./build/test_battle_hyper
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

# The general WZX battle camera (synthetic; a prepared wzx_camera.c3k as argument is exercised too).
build/test_wzx_camera: tests/test_wzx_camera.c src/wzx_camera.c include/colosseum3ds/wzx_camera.h include/colosseum3ds/title.h
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_wzx_camera.c src/wzx_camera.c $(LDFLAGS) -lm -o $@
all: build/test_wzx_camera
.PHONY: wzx-camera-check
wzx-camera-check: build/test_wzx_camera
	./build/test_wzx_camera
battle-general-check: wzx-camera-check
