# The playable preview is nonpersistent and leaves PR6's foundation unchanged.
BATTLE_PREVIEW_SOURCES = src/battle_preview.c src/battle_presentation.c src/battle_damage.c src/battle_preview_menu.c src/game_state.c src/save.c $(BATTLE_INIT_SOURCES)
build/battle_damage_original.inc: tools/prepare_battle_damage.py vendor/colosseum/src/game/fight_range_80211A00.c | build
	python3 tools/prepare_battle_damage.py

build/test_battle_preview: tests/test_battle_preview.c $(BATTLE_PREVIEW_SOURCES) $(BATTLE_INIT_HEADERS) include/colosseum3ds/battle_preview.h include/colosseum3ds/battle_preview_menu.h build/battle_damage_original.inc build/battle_rng_source.h $(HEADERS) $(UPSTREAM)
	$(CC) $(CPPFLAGS) -Ibuild -Isrc $(CFLAGS) tests/test_battle_preview.c $(BATTLE_PREVIEW_SOURCES) $(LDFLAGS) -lm -o $@

all: build/test_battle_preview
.PHONY: battle-preview-check
battle-preview-check: build/test_battle_preview
	./build/test_battle_preview

test: battle-preview-check
